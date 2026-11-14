#include "World.h"
#include "ChunkData.h"
#include "Block.h"
#include "Types.h"
#include "CaveGenerator.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMinRenderDistance = 2;
constexpr int kMaxRenderDistance = 16;
constexpr int kUnloadMargin = 2;              // chunks beyond render distance before unloading
constexpr int kMaxFluidUpdatesPerTick = 256;

// Chunk save file format
constexpr uint32_t kChunkMagic   = 0x32435856;  // "VXC2"
constexpr uint16_t kChunkVersion = 1;
constexpr int      kChunkCells   = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH;

// Neighbor slots match ChunkData::neighbors:
//   0 = +X, 1 = -X, 4 = +Z, 5 = -Z
struct NeighborDir {
    int dx, dz;
    int slot;      // slot on this chunk that points at the neighbor
    int opposite;  // slot on the neighbor that points back at this chunk
};

constexpr NeighborDir kNeighborDirs[] = {
    { 1,  0, 0, 1},
    {-1,  0, 1, 0},
    { 0,  1, 4, 5},
    { 0, -1, 5, 4},
};

constexpr int kFluidOffsets[7][3] = {
    {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
};

inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

inline int positiveMod(int a, int b) {
    int m = a % b;
    return m < 0 ? m + b : m;
}

float elapsedMs(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

int chunkDist2(const World::ChunkCoord& a, const World::ChunkCoord& b) {
    const int dx = a.first - b.first;
    const int dz = a.second - b.second;
    return dx * dx + dz * dz;
}

// Little-endian helpers for the chunk file format
void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
uint16_t get16(const std::vector<uint8_t>& d, size_t p) {
    return static_cast<uint16_t>(d[p] | (d[p + 1] << 8));
}
uint32_t get32(const std::vector<uint8_t>& d, size_t p) {
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
}

// Procedural fallback used when the Lua mapgen is missing.
void generateFallbackTerrain(ChunkData& chunk, int chunkX, int chunkZ, float seedPhase) {
    auto& registry = BlockRegistry::getInstance();
    auto idOr = [&registry](const char* name, uint8_t fallback) {
        uint8_t id = registry.getBlockId(name);
        return id != 0 ? id : fallback;
    };
    const uint8_t stoneId = idOr("stone", 2);
    const uint8_t grassId = idOr("grass", 3);
    const uint8_t dirtId  = idOr("dirt", 1);

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int worldX = chunkX * CHUNK_WIDTH + x;
            const int worldZ = chunkZ * CHUNK_WIDTH + z;

            const float h = std::sin(worldX * 0.1f + seedPhase) *
                            std::cos(worldZ * 0.1f + seedPhase) * 5.0f;
            const int surface = 55 + static_cast<int>(h);
            const int top = std::min(surface, CHUNK_HEIGHT - 1);

            // Caves are carved afterwards by CaveGenerator::carveChunk (see loadChunk)
            for (int y = 0; y <= top; ++y) {
                if (y == surface) {
                    chunk.setBlock(x, y, z, grassId);
                } else if (y > surface - 4) {
                    chunk.setBlock(x, y, z, dirtId);
                } else {
                    chunk.setBlock(x, y, z, stoneId);
                }
            }
        }
    }
}

} // namespace

World::World() {
    std::random_device rd;
    m_seed = rd() ^ static_cast<uint32_t>(
                 std::chrono::system_clock::now().time_since_epoch().count());
    std::cout << "[World] Initialized with random seed: " << m_seed << std::endl;
}

World::~World() = default;

// ---------------------------------------------------------------------------
// Lookup and bookkeeping
// ---------------------------------------------------------------------------

ChunkData* World::chunkAt(const glm::ivec3& loc) const {
    const auto it = chunks.find(ChunkCoord{floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
    return it != chunks.end() ? it->second.get() : nullptr;
}

// Records that a chunk needs redrawing (picked up by WorldRenderer via takeDirtyChunks).
void World::markDirty(int cx, int cz) {
    const ChunkCoord c{cx, cz};
    if (chunks.count(c)) {
        m_dirty.insert(c);
    }
}

// Marks the chunk containing loc, plus any neighbor whose boundary faces depend on it.
void World::markBlockChanged(const glm::ivec3& loc) {
    const int cx = floorDiv(loc.x, CHUNK_WIDTH);
    const int cz = floorDiv(loc.z, CHUNK_WIDTH);
    const int lx = positiveMod(loc.x, CHUNK_WIDTH);
    const int lz = positiveMod(loc.z, CHUNK_WIDTH);

    markDirty(cx, cz);
    if (lx == 0)               markDirty(cx - 1, cz);
    if (lx == CHUNK_WIDTH - 1) markDirty(cx + 1, cz);
    if (lz == 0)               markDirty(cx, cz - 1);
    if (lz == CHUNK_WIDTH - 1) markDirty(cx, cz + 1);
}

// Remembers that this chunk differs from its saved/generated state.
void World::markModified(const glm::ivec3& loc) {
    m_modified.insert(ChunkCoord{floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
}

void World::linkChunk(int cx, int cz) {
    auto selfIt = chunks.find(ChunkCoord{cx, cz});
    if (selfIt == chunks.end()) return;
    ChunkData* self = selfIt->second.get();

    for (const auto& d : kNeighborDirs) {
        auto nIt = chunks.find(ChunkCoord{cx + d.dx, cz + d.dz});
        ChunkData* n = (nIt != chunks.end()) ? nIt->second.get() : nullptr;
        self->neighbors[d.slot] = n;
        if (n) n->neighbors[d.opposite] = self;
    }
}

void World::unlinkChunk(int cx, int cz) {
    for (const auto& d : kNeighborDirs) {
        const int nx = cx + d.dx;
        const int nz = cz + d.dz;
        auto nIt = chunks.find(ChunkCoord{nx, nz});
        if (nIt != chunks.end()) {
            nIt->second->neighbors[d.opposite] = nullptr;
            markDirty(nx, nz);
        }
    }
}

void World::setRenderDistance(int chunks) {
    const int clamped = std::clamp(chunks, kMinRenderDistance, kMaxRenderDistance);
    if (clamped == m_renderDistance) return;
    m_renderDistance = clamped;
    m_queueDirty = true;
}

// ---------------------------------------------------------------------------
// Per-frame update: queue, load, unload, mesh
// ---------------------------------------------------------------------------

void World::ensureLuaSeeded(sol::state& lua) {
    if (m_luaSeeded) return;
    CaveGenerator::setSeed(m_seed);
    sol::protected_function setSeed = lua["set_world_seed"];
    if (setSeed.valid()) {
        auto result = setSeed(m_seed);
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "[Mapgen Error]: " << err.what() << std::endl;
        }
    }
    m_luaSeeded = true;
}

// Missing chunks inside the render radius of the player (and of other players when
// hosting), nearest to any of them first.
void World::rebuildLoadQueue() {
    m_loadQueue.clear();

    std::vector<CenterChunk> centers{{m_playerChunk, m_renderDistance}};
    centers.insert(centers.end(), m_extraCenters.begin(), m_extraCenters.end());

    std::unordered_set<ChunkCoord, PairHash> seen;
    for (const auto& center : centers) {
        const int r = center.radius;
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                const ChunkCoord c{center.chunk.first + dx, center.chunk.second + dz};
                if (chunks.find(c) == chunks.end() && seen.insert(c).second) {
                    m_loadQueue.push_back(c);
                }
            }
        }
    }

    auto nearest = [&centers](const ChunkCoord& c) {
        int best = INT_MAX;
        for (const auto& center : centers) best = std::min(best, chunkDist2(c, center.chunk));
        return best;
    };
    std::sort(m_loadQueue.begin(), m_loadQueue.end(),
              [&nearest](const ChunkCoord& a, const ChunkCoord& b) { return nearest(a) < nearest(b); });
}

void World::setExtraLoadCenters(const std::vector<LoadCenter>& centers) {
    std::vector<CenterChunk> next;
    next.reserve(centers.size());
    for (const auto& c : centers) {
        next.push_back({ChunkCoord{floorDiv(static_cast<int>(std::floor(c.pos.x)), CHUNK_WIDTH),
                                   floorDiv(static_cast<int>(std::floor(c.pos.z)), CHUNK_WIDTH)},
                        std::clamp(c.radius, kMinRenderDistance, kMaxRenderDistance)});
    }
    const bool same = next.size() == m_extraCenters.size() &&
        std::equal(next.begin(), next.end(), m_extraCenters.begin(),
                   [](const CenterChunk& a, const CenterChunk& b) {
                       return a.chunk == b.chunk && a.radius == b.radius;
                   });
    if (!same) {
        m_extraCenters = std::move(next);
        m_queueDirty = true;
    }
}

void World::loadQueuedChunks(sol::state& lua) {
    const auto start = Clock::now();
    size_t consumed = 0;

    while (consumed < m_loadQueue.size()) {
        const ChunkCoord c = m_loadQueue[consumed++];
        if (chunks.find(c) == chunks.end()) {
            loadChunk(c.first, c.second, lua);
            if (elapsedMs(start) >= m_loadBudgetMs) break;
        }
    }

    m_loadQueue.erase(m_loadQueue.begin(),
                      m_loadQueue.begin() + static_cast<std::ptrdiff_t>(consumed));
}

void World::update(const glm::vec3& playerPos, sol::state& lua) {
    if (m_remote) return;   // the server decides which chunks we have
    ensureLuaSeeded(lua);

    const int pcx = floorDiv(static_cast<int>(std::floor(playerPos.x)), CHUNK_WIDTH);
    const int pcz = floorDiv(static_cast<int>(std::floor(playerPos.z)), CHUNK_WIDTH);
    const ChunkCoord playerChunk{pcx, pcz};

    if (m_queueDirty || playerChunk != m_playerChunk) {
        m_playerChunk = playerChunk;
        m_queueDirty = false;
        rebuildLoadQueue();
    }

    loadQueuedChunks(lua);
    unloadDistantChunks();
}

// Unloads chunks that are too far from the player and from every other load center.
void World::unloadDistantChunks() {
    auto wanted = [this](const ChunkCoord& c) {
        auto isNear = [&c](const ChunkCoord& center, int radius) {
            const int dx = c.first - center.first;
            const int dz = c.second - center.second;
            return std::max(std::abs(dx), std::abs(dz)) <= radius + kUnloadMargin;
        };
        if (isNear(m_playerChunk, m_renderDistance)) return true;
        for (const auto& center : m_extraCenters) {
            if (isNear(center.chunk, center.radius)) return true;
        }
        return false;
    };

    for (auto it = chunks.begin(); it != chunks.end();) {
        const int cx = it->first.first;
        const int cz = it->first.second;

        if (!wanted(it->first)) {
            // Keep player changes: write the chunk out before throwing it away
            if (m_modified.count(it->first)) {
                saveChunkToDisk(it->first, *it->second);
                m_modified.erase(it->first);
            }

            unlinkChunk(cx, cz);                 // clear neighbor pointers before destroying
            m_dirty.erase(it->first);
            m_unloaded.push_back(it->first);
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
}

void World::loadChunk(int chunkX, int chunkZ, sol::state& lua) {
    auto chunk = std::make_shared<ChunkData>();

    // 1. Saved chunk on disk? Use it instead of generating.
    bool fromDisk = loadChunkFromDisk(chunkX, chunkZ, *chunk);

    // 2. Otherwise generate it (Lua mapgen, or the C++ fallback).
    if (!fromDisk) {
        chunk = std::make_shared<ChunkData>();   // fresh, in case a broken file half-filled it

        sol::protected_function genFunc = lua["generate_chunk"];
        if (genFunc.valid()) {
            auto result = genFunc(chunk.get(), chunkX, chunkZ);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Mapgen Error]: " << err.what() << std::endl;
            }
        } else {
            generateFallbackTerrain(*chunk, chunkX, chunkZ,
                                    static_cast<float>(m_seed % 100000) * 0.001f);
        }

        // 3. Carve caves into the new terrain (seeded, so the same seed gives the same caves).
        //    Saved chunks skip this - their caves are already in the file.
        CaveGenerator::carveChunk(*chunk, chunkX, chunkZ);
    }

    chunks[ChunkCoord{chunkX, chunkZ}] = chunk;
    linkChunk(chunkX, chunkZ);

    // The new chunk and its neighbors need (re)meshing so border faces cull correctly.
    markDirty(chunkX, chunkZ);
    markDirty(chunkX - 1, chunkZ);
    markDirty(chunkX + 1, chunkZ);
    markDirty(chunkX, chunkZ - 1);
    markDirty(chunkX, chunkZ + 1);
}

// ---------------------------------------------------------------------------
// Saving / loading
// ---------------------------------------------------------------------------

void World::reset() {
    chunks.clear();
    m_loadQueue.clear();
    m_dirty.clear();
    m_unloaded.clear();
    ++m_resetCount;
    m_remote = false;
    m_recordChanges = false;
    m_changes.clear();
    m_extraCenters.clear();
    m_activeFluids.clear();
    m_modified.clear();
    m_queueDirty = true;
    m_luaSeeded = false;
    m_playerChunk = ChunkCoord{0, 0};
}

int World::saveAll() {
    if (m_saveDir.empty()) return 0;

    int written = 0;
    for (auto it = m_modified.begin(); it != m_modified.end();) {
        auto cIt = chunks.find(*it);
        if (cIt != chunks.end() && saveChunkToDisk(*it, *cIt->second)) {
            ++written;
            it = m_modified.erase(it);
        } else if (cIt == chunks.end()) {
            it = m_modified.erase(it);   // already unloaded (and saved then)
        } else {
            ++it;                        // write failed - keep it for the next try
        }
    }
    return written;
}

std::string World::chunkPath(int cx, int cz) const {
    const std::string name = "c." + std::to_string(cx) + "." + std::to_string(cz) + ".bin";
    return (std::filesystem::path(m_saveDir) / "chunks" / name).string();
}

// File layout (little-endian):
//   u32 magic, u16 version, u16 width, u16 height
//   u16 paletteCount, then per entry: u8 id, u8 nameLength, name bytes
//   runs until the chunk is full: u16 count, u8 blockId, u8 metadata
// Cells are stored y-major, then z, then x.
// The palette stores block NAMES, so saves still load correctly if mods
// are added/removed and block ids shift.
std::vector<uint8_t> World::encodeChunk(const ChunkData& chunk) {
    auto& registry = BlockRegistry::getInstance();

    // Run-length encode (id, meta) pairs
    std::vector<uint8_t> runs;
    runs.reserve(4096);
    bool used[256] = {};

    uint16_t runLen = 0;
    uint8_t runId = 0, runMeta = 0;
    auto flush = [&]() {
        if (runLen == 0) return;
        put16(runs, runLen);
        runs.push_back(runId);
        runs.push_back(runMeta);
    };

    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            for (int x = 0; x < CHUNK_WIDTH; ++x) {
                const uint8_t id = chunk.getLocalBlock(x, y, z);
                const uint8_t meta = chunk.getLocalMetadata(x, y, z);
                used[id] = true;
                if (runLen > 0 && id == runId && meta == runMeta && runLen < 0xFFFF) {
                    ++runLen;
                } else {
                    flush();
                    runId = id;
                    runMeta = meta;
                    runLen = 1;
                }
            }
        }
    }
    flush();

    // Header + palette
    std::vector<uint8_t> out;
    out.reserve(runs.size() + 512);
    put32(out, kChunkMagic);
    put16(out, kChunkVersion);
    put16(out, static_cast<uint16_t>(CHUNK_WIDTH));
    put16(out, static_cast<uint16_t>(CHUNK_HEIGHT));

    std::vector<uint8_t> paletteIds;
    for (int id = 1; id < 256; ++id) if (used[id]) paletteIds.push_back(static_cast<uint8_t>(id));
    put16(out, static_cast<uint16_t>(paletteIds.size()));
    for (uint8_t id : paletteIds) {
        std::string name = registry.getBlock(id).name;
        if (name.size() > 255) name.resize(255);
        out.push_back(id);
        out.push_back(static_cast<uint8_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
    }
    out.insert(out.end(), runs.begin(), runs.end());
    return out;
}

bool World::saveChunkToDisk(const ChunkCoord& c, const ChunkData& chunk) const {
    if (m_saveDir.empty()) return false;
    const std::vector<uint8_t> out = encodeChunk(chunk);

    // Write to a temp file, then swap it in, so a crash mid-write can't corrupt the save
    namespace fs = std::filesystem;
    const std::string finalPath = chunkPath(c.first, c.second);
    const std::string tmpPath = finalPath + ".tmp";
    std::error_code ec;
    fs::create_directories(fs::path(finalPath).parent_path(), ec);

    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) {
            std::cerr << "[Save] Could not write " << tmpPath << std::endl;
            return false;
        }
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!f) {
            std::cerr << "[Save] Write failed for " << tmpPath << std::endl;
            return false;
        }
    }

    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        fs::remove(finalPath, ec);
        fs::rename(tmpPath, finalPath, ec);
        if (ec) {
            std::cerr << "[Save] Could not replace " << finalPath << ": " << ec.message() << std::endl;
            return false;
        }
    }
    return true;
}

bool World::loadChunkFromDisk(int cx, int cz, ChunkData& chunk) const {
    if (m_saveDir.empty()) return false;

    std::ifstream f(chunkPath(cx, cz), std::ios::binary);
    if (!f) return false;   // never saved - generate it instead

    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!decodeChunk(d, chunk)) {
        std::cerr << "[Load] Chunk " << cx << "," << cz << " is damaged - regenerating it" << std::endl;
        return false;
    }
    return true;
}

bool World::decodeChunk(const std::vector<uint8_t>& d, ChunkData& chunk) {
    if (d.size() < 12) return false;
    if (get32(d, 0) != kChunkMagic || get16(d, 4) != kChunkVersion) return false;
    if (get16(d, 6) != CHUNK_WIDTH || get16(d, 8) != CHUNK_HEIGHT) return false;

    // Palette: saved id -> current id (looked up by name)
    auto& registry = BlockRegistry::getInstance();
    uint8_t remap[256] = {};   // unknown blocks become air
    size_t p = 10;
    const uint16_t paletteCount = get16(d, p);
    p += 2;
    for (uint16_t i = 0; i < paletteCount; ++i) {
        if (p + 2 > d.size()) return false;
        const uint8_t savedId = d[p];
        const uint8_t len = d[p + 1];
        p += 2;
        if (p + len > d.size()) return false;
        const std::string name(d.begin() + p, d.begin() + p + len);
        p += len;
        remap[savedId] = registry.getBlockId(name);
        if (remap[savedId] == 0) {
            std::cerr << "[Load] Unknown block '" << name << "' - replaced with air" << std::endl;
        }
    }

    // Runs
    int cell = 0;
    while (cell < kChunkCells && p + 4 <= d.size()) {
        const uint16_t count = get16(d, p);
        const uint8_t id = remap[d[p + 2]];
        const uint8_t meta = d[p + 3];
        p += 4;

        for (uint16_t i = 0; i < count && cell < kChunkCells; ++i, ++cell) {
            if (id == 0 && meta == 0) continue;   // new chunks start as air
            const int x = cell % CHUNK_WIDTH;
            const int z = (cell / CHUNK_WIDTH) % CHUNK_WIDTH;
            const int y = cell / (CHUNK_WIDTH * CHUNK_WIDTH);
            chunk.setBlockWithMeta(x, y, z, id, meta);
        }
    }
    return cell == kChunkCells;
}

const ChunkData* World::chunkData(const ChunkCoord& c) const {
    const auto it = chunks.find(c);
    return it != chunks.end() ? it->second.get() : nullptr;
}

std::vector<World::ChunkCoord> World::takeDirtyChunks() {
    std::vector<ChunkCoord> out(m_dirty.begin(), m_dirty.end());
    m_dirty.clear();
    return out;
}

std::vector<World::ChunkCoord> World::takeUnloadedChunks() {
    std::vector<ChunkCoord> out;
    out.swap(m_unloaded);
    return out;
}

// ---------------------------------------------------------------------------
// Fluids: only water near a change is simulated
// ---------------------------------------------------------------------------

void World::resolveFluidIds() {
    if (m_waterId != 0) return;
    auto& registry = BlockRegistry::getInstance();
    m_waterId = registry.getBlockId("water");
    m_flowingWaterId = registry.getBlockId("flowing_water");
    if (m_flowingWaterId == 0) m_flowingWaterId = m_waterId;
}

// Water in the changed cell or its neighbors may now flow, so add it to the active set.
void World::noteFluidChange(const glm::ivec3& loc) {
    resolveFluidIds();
    if (m_waterId == 0) return;

    for (const auto& o : kFluidOffsets) {
        const glm::ivec3 p(loc.x + o[0], loc.y + o[1], loc.z + o[2]);
        const uint8_t id = getBlockAt(p);
        if (id != 0 && (id == m_waterId || id == m_flowingWaterId)) {
            m_activeFluids.insert(p);
        }
    }
}

void World::updateFluids() {
    if (m_remote) return;   // the server runs water and sends the results
    resolveFluidIds();
    if (m_waterId == 0 || m_activeFluids.empty()) return;

    std::vector<glm::ivec3> toFill;
    int budget = kMaxFluidUpdatesPerTick;
    auto it = m_activeFluids.begin();

    while (it != m_activeFluids.end() && budget > 0) {
        const glm::ivec3 p = *it;
        it = m_activeFluids.erase(it);
        --budget;

        const uint8_t id = getBlockAt(p);
        if (id != m_waterId && id != m_flowingWaterId) continue;
        if (p.y <= 0) continue;

        const glm::ivec3 below(p.x, p.y - 1, p.z);
        if (chunkAt(below) && getBlockAt(below) == 0) {
            toFill.push_back(below);
        }
    }

    // Applied after the scan so one tick doesn't cascade down the whole column.
    for (const glm::ivec3& b : toFill) {
        setBlockAt(b, m_flowingWaterId);
    }
}

// ---------------------------------------------------------------------------
// Block and metadata access
// ---------------------------------------------------------------------------

uint8_t World::getBlockAt(const glm::ivec3& loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;
    const ChunkData* chunk = chunkAt(loc);
    if (!chunk) return 0;
    return chunk->getBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                           positiveMod(loc.z, CHUNK_WIDTH));
}

void World::setBlockAt(const glm::ivec3& loc, uint8_t blockId) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;

    chunk->setBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                    positiveMod(loc.z, CHUNK_WIDTH), blockId);
    markBlockChanged(loc);
    markModified(loc);
    noteFluidChange(loc);
    recordChange(loc);
}

uint8_t World::getMetadataAt(const glm::ivec3& loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;
    const ChunkData* chunk = chunkAt(loc);
    if (!chunk) return 0;
    return chunk->getMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                              positiveMod(loc.z, CHUNK_WIDTH));
}

void World::setMetadataAt(const glm::ivec3& loc, uint8_t meta) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;

    chunk->setMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                       positiveMod(loc.z, CHUNK_WIDTH), meta);
    markBlockChanged(loc);
    markModified(loc);
    recordChange(loc);
}

// ---------------------------------------------------------------------------
// Multiplayer
// ---------------------------------------------------------------------------

void World::recordChange(const glm::ivec3& loc) {
    if (!m_recordChanges && !m_remote) return;
    m_changes.push_back(BlockChange{loc, getBlockAt(loc), getMetadataAt(loc)});
}

std::vector<World::BlockChange> World::takeBlockChanges() {
    std::vector<BlockChange> out;
    out.swap(m_changes);
    return out;
}

void World::insertChunk(int cx, int cz, std::shared_ptr<ChunkData> chunk) {
    if (!chunk) return;
    const ChunkCoord c{cx, cz};
    if (chunks.count(c)) unlinkChunk(cx, cz);
    chunk->neighbors[0] = chunk->neighbors[1] = chunk->neighbors[4] = chunk->neighbors[5] = nullptr;
    chunks[c] = std::move(chunk);
    linkChunk(cx, cz);
    markDirty(cx, cz);
    markDirty(cx - 1, cz);
    markDirty(cx + 1, cz);
    markDirty(cx, cz - 1);
    markDirty(cx, cz + 1);
}

void World::removeChunk(int cx, int cz) {
    const ChunkCoord c{cx, cz};
    auto it = chunks.find(c);
    if (it == chunks.end()) return;
    unlinkChunk(cx, cz);
    m_dirty.erase(c);
    m_unloaded.push_back(c);
    chunks.erase(it);
}

void World::applyNetworkBlock(const glm::ivec3& loc, uint8_t id, uint8_t meta) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;
    const int lx = positiveMod(loc.x, CHUNK_WIDTH);
    const int lz = positiveMod(loc.z, CHUNK_WIDTH);
    if (chunk->getLocalBlock(lx, loc.y, lz) == id && chunk->getLocalMetadata(lx, loc.y, lz) == meta) return;
    chunk->setBlockWithMeta(lx, loc.y, lz, id, meta);
    markBlockChanged(loc);
}

// ---------------------------------------------------------------------------
// Loading readiness
// ---------------------------------------------------------------------------

// Fraction of chunks within `radius` of pos that are loaded (WorldRenderer checks meshes).
float World::areaLoadProgress(const glm::vec3& pos, int radius) const {
    const int pcx = floorDiv(static_cast<int>(std::floor(pos.x)), CHUNK_WIDTH);
    const int pcz = floorDiv(static_cast<int>(std::floor(pos.z)), CHUNK_WIDTH);

    int total = 0;
    int done = 0;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            ++total;
            const ChunkCoord c{pcx + dx, pcz + dz};
            if (chunks.count(c)) ++done;
        }
    }
    return total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 1.0f;
}

// First air block above the highest walkable block in this column, or -1 if none is loaded.
int World::groundHeightAt(int x, int z) const {
    for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
        const uint8_t id = getBlockAt(glm::ivec3(x, y, z));
        if (id == 0) continue;
        if (BlockRegistry::getInstance().getBlock(id).isWalkable) {
            return y + 1;
        }
    }
    return -1;
}
