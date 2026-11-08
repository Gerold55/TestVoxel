#include "World.h"
#include "ChunkMesh.h"
#include "Block.h"
#include "Types.h"
#include "CaveGenerator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
<<<<<<< Updated upstream
#include <iostream>
#include <cmath>
#include <vector>
=======
#include <algorithm>
>>>>>>> Stashed changes
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace {

constexpr int kMaxChunkLoadsPerFrame   = 2;
constexpr int kMaxMeshRebuildsPerFrame = 2;
constexpr int kMaxFluidUpdatesPerTick  = 100;
constexpr double kMaxUploadMs          = 5.0;

// Neighbor slots match ChunkMesh::getBlock():
//   0 = +X, 1 = -X, 4 = +Z, 5 = -Z
struct NeighborDir {
    int dx, dz;
    int slot;      // slot on this chunk that points at the neighbor
    int opposite;  // slot on the neighbor that points back at this chunk
};

constexpr NeighborDir kNeighborDirs[] = {
    { 1,  0, 0, 1},   // +X
    {-1,  0, 1, 0},   // -X
    { 0,  1, 4, 5},   // +Z
    { 0, -1, 5, 4},   // -Z
};

// Floor division and modulo that behave correctly for negative coordinates.
inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

inline int positiveMod(int a, int b) {
    int m = a % b;
    return m < 0 ? m + b : m;
}

// Procedural fallback used when the Lua mapgen is missing.
void generateFallbackTerrain(ChunkMesh& chunk, int chunkX, int chunkZ, float seedPhase) {
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

            for (int y = 0; y <= top; ++y) {
                if (CaveGenerator::isCarvedOut(worldX, y, worldZ)) {
                    chunk.setBlock(x, y, z, 0);
                } else if (y == surface) {
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
// Lookup and bookkeeping helpers
// ---------------------------------------------------------------------------

ChunkMesh* World::chunkAt(const glm::ivec3& loc) const {
    const auto it = chunks.find({floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
    return it != chunks.end() ? it->second.get() : nullptr;
}

void World::markMeshDirty(int cx, int cz) {
    if (chunks.count({cx, cz})) {
        m_meshQueue.insert({cx, cz});
    }
}

// Marks the chunk containing loc, plus any neighbor whose boundary faces
// depend on this block.
void World::markBlockChanged(const glm::ivec3& loc) {
    const int cx = floorDiv(loc.x, CHUNK_WIDTH);
    const int cz = floorDiv(loc.z, CHUNK_WIDTH);
    const int lx = positiveMod(loc.x, CHUNK_WIDTH);
    const int lz = positiveMod(loc.z, CHUNK_WIDTH);

    markMeshDirty(cx, cz);
    if (lx == 0)               markMeshDirty(cx - 1, cz);
    if (lx == CHUNK_WIDTH - 1) markMeshDirty(cx + 1, cz);
    if (lz == 0)               markMeshDirty(cx, cz - 1);
    if (lz == CHUNK_WIDTH - 1) markMeshDirty(cx, cz + 1);
}

// Connects a chunk to whichever neighbors are loaded, in both directions.
void World::linkChunk(int cx, int cz) {
    auto selfIt = chunks.find({cx, cz});
    if (selfIt == chunks.end()) return;
    ChunkMesh* self = selfIt->second.get();

    for (const auto& d : kNeighborDirs) {
        auto nIt = chunks.find({cx + d.dx, cz + d.dz});
        ChunkMesh* n = (nIt != chunks.end()) ? nIt->second.get() : nullptr;
        self->neighbors[d.slot] = n;
        if (n) n->neighbors[d.opposite] = self;
    }
}

// Disconnects a chunk from its neighbors before it is destroyed, and
// remeshes neighbors whose border faces now need to be drawn.
void World::unlinkChunk(int cx, int cz) {
    for (const auto& d : kNeighborDirs) {
        const int nx = cx + d.dx;
        const int nz = cz + d.dz;
        auto nIt = chunks.find({nx, nz});
        if (nIt != chunks.end()) {
            nIt->second->neighbors[d.opposite] = nullptr;
            markMeshDirty(nx, nz);
        }
    }
}

// ---------------------------------------------------------------------------
// Per-frame update: load, unload, remesh
// ---------------------------------------------------------------------------

void World::ensureLuaSeeded(sol::state& lua) {
    if (m_luaSeeded) return;
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

void World::update(const glm::vec3& playerPos, sol::state& lua) {
    ensureLuaSeeded(lua);

    const int pcx = floorDiv(static_cast<int>(std::floor(playerPos.x)), CHUNK_WIDTH);
    const int pcz = floorDiv(static_cast<int>(std::floor(playerPos.z)), CHUNK_WIDTH);

    // Collect missing chunks in the render radius, nearest first.
    std::vector<std::pair<int, int>> missing;
    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            const std::pair<int, int> coord{pcx + dx, pcz + dz};
            if (chunks.find(coord) == chunks.end()) {
                missing.push_back(coord);
            }
        }
    }
    auto distSq = [pcx, pcz](const std::pair<int, int>& c) {
        const int dx = c.first - pcx;
        const int dz = c.second - pcz;
        return dx * dx + dz * dz;
    };
    std::sort(missing.begin(), missing.end(),
              [&](const auto& a, const auto& b) { return distSq(a) < distSq(b); });

    const int toLoad = std::min<int>(kMaxChunkLoadsPerFrame, static_cast<int>(missing.size()));
    for (int i = 0; i < toLoad; ++i) {
        loadChunk(missing[i].first, missing[i].second, lua);
    }

    unloadDistantChunks(pcx, pcz);
    processMeshQueue();
}

void World::unloadDistantChunks(int playerChunkX, int playerChunkZ) {
    // Small hysteresis band so chunks at the edge don't thrash in and out.
    const int limit = m_renderDistance + 2;

    for (auto it = chunks.begin(); it != chunks.end();) {
        const int cx = it->first.first;
        const int cz = it->first.second;
        const int dx = cx - playerChunkX;
        const int dz = cz - playerChunkZ;

        if (std::max(std::abs(dx), std::abs(dz)) > limit) {
            unlinkChunk(cx, cz);          // clear neighbor pointers first
            m_meshQueue.erase({cx, cz});
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
}

<<<<<<< Updated upstream
void World::updateFluids() {
    struct FluidChange {
        int x, y, z;
        uint8_t id;
        uint8_t meta;
    };
    std::vector<FluidChange> pendingChanges;
    int maxUpdates = 100;

    uint8_t waterSourceId = BlockRegistry::getInstance().getBlockId("water");
    uint8_t flowingWaterId = BlockRegistry::getInstance().getBlockId("flowing_water");
    if (flowingWaterId == 0) flowingWaterId = waterSourceId;

    for (const auto& pair : chunks) {
        if (pendingChanges.size() >= static_cast<size_t>(maxUpdates)) break;
        int cx = pair.first.first;
        int cz = pair.first.second;
        auto& chunk = pair.second;

        for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
            for (int lz = 0; lz < CHUNK_WIDTH; ++lz) {
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    uint8_t currentId = chunk->getBlock(lx, y, lz);
                    if (currentId == 0) continue;

                    const auto& prop = BlockRegistry::getInstance().getBlock(currentId);
                    bool isWater = (prop.name == "water" || prop.name == "flowing_water");

                    if (isWater) {
                        int wx = (cx * CHUNK_WIDTH) + lx;
                        int wz = (cz * CHUNK_WIDTH) + lz;

                        if (y > 0 && getBlockAt({wx, y - 1, wz}) == 0) {
                            pendingChanges.push_back({wx, y - 1, wz, flowingWaterId, 0});
                            continue;
                        }
                    }
                }
            }
        }
    }

    if (!pendingChanges.empty()) {
        for (const auto& change : pendingChanges) {
            int cx = static_cast<int>(std::floor(static_cast<float>(change.x) / CHUNK_WIDTH));
            int cz = static_cast<int>(std::floor(static_cast<float>(change.z) / CHUNK_WIDTH));
            auto it = chunks.find({cx, cz});
            if (it != chunks.end()) {
                int lx = change.x - (cx * CHUNK_WIDTH);
                int lz = change.z - (cz * CHUNK_WIDTH);
                if (lx < 0) lx += CHUNK_WIDTH;
                if (lz < 0) lz += CHUNK_WIDTH;

                it->second->setBlockWithMeta(lx, change.y, lz, change.id, change.meta);
                std::async(std::launch::async, [chunk = it->second]() {
                    chunk->generateMeshData();
                });
            }
=======
void World::processMeshQueue() {
    int budget = kMaxMeshRebuildsPerFrame;
    auto it = m_meshQueue.begin();
    while (it != m_meshQueue.end() && budget > 0) {
        auto chunkIt = chunks.find(*it);
        if (chunkIt != chunks.end()) {
            chunkIt->second->generateMeshData();
            --budget;
>>>>>>> Stashed changes
        }
        it = m_meshQueue.erase(it);
    }
}

void World::loadChunk(int chunkX, int chunkZ, sol::state& lua) {
    auto chunk = std::make_shared<ChunkMesh>();

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

    chunks[{chunkX, chunkZ}] = chunk;

    // Connect to loaded neighbors so border faces are culled against real blocks.
    linkChunk(chunkX, chunkZ);

    // The new chunk and its neighbors need (re)meshing.
    markMeshDirty(chunkX, chunkZ);
    markMeshDirty(chunkX - 1, chunkZ);
    markMeshDirty(chunkX + 1, chunkZ);
    markMeshDirty(chunkX, chunkZ - 1);
    markMeshDirty(chunkX, chunkZ + 1);
}

// ---------------------------------------------------------------------------
// Fluids
// ---------------------------------------------------------------------------

void World::updateFluids() {
    if (m_waterId == 0) {
        auto& registry = BlockRegistry::getInstance();
        m_waterId = registry.getBlockId("water");
        m_flowingWaterId = registry.getBlockId("flowing_water");
        if (m_flowingWaterId == 0) m_flowingWaterId = m_waterId;
    }
    if (m_waterId == 0) return;

    struct FluidChange {
        glm::ivec3 pos;
        uint8_t id;
    };
    std::vector<FluidChange> pending;
    pending.reserve(kMaxFluidUpdatesPerTick);

    // Collect first, apply second, so spreading this tick doesn't cascade.
    for (const auto& [coord, chunk] : chunks) {
        const int baseX = coord.first * CHUNK_WIDTH;
        const int baseZ = coord.second * CHUNK_WIDTH;

        for (int lx = 0; lx < CHUNK_WIDTH && pending.size() < kMaxFluidUpdatesPerTick; ++lx)
        for (int lz = 0; lz < CHUNK_WIDTH && pending.size() < kMaxFluidUpdatesPerTick; ++lz)
        for (int y = 1; y < CHUNK_HEIGHT && pending.size() < kMaxFluidUpdatesPerTick; ++y) {
            const uint8_t id = chunk->getBlock(lx, y, lz);
            if (id != m_waterId && id != m_flowingWaterId) continue;

            const glm::ivec3 below{baseX + lx, y - 1, baseZ + lz};
            // Only flow into air in a loaded chunk; unloaded space is not air.
            if (chunkAt(below) && getBlockAt(below) == 0) {
                pending.push_back({below, m_flowingWaterId});
            }
        }
    }

    for (const auto& change : pending) {
        ChunkMesh* chunk = chunkAt(change.pos);
        if (!chunk) continue;
        chunk->setBlockWithMeta(positiveMod(change.pos.x, CHUNK_WIDTH), change.pos.y,
                                positiveMod(change.pos.z, CHUNK_WIDTH), change.id, 0);
        markBlockChanged(change.pos);
    }
}

// ---------------------------------------------------------------------------
// Upload and draw
// ---------------------------------------------------------------------------

void World::render(GLuint shaderProgram) const {
    this->upload();
    this->draw(shaderProgram, glm::vec3(0.0f, 0.0f, 0.0f));
}

void World::render(GLuint shaderProgram) const {
    this->upload();
    this->draw(shaderProgram);
}

void World::upload() const {
    const auto start = std::chrono::steady_clock::now();

    for (const auto& [coord, chunk] : chunks) {
        if (!chunk->meshReadyForUpload || !chunk->isDirty) continue;

        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsedMs >= kMaxUploadMs) break;

        chunk->uploadMesh();
    }
}

<<<<<<< Updated upstream
void World::draw(GLuint shaderProgram) const
{
    for (const auto& [loc, chunk] : this->chunks) {

=======
void World::draw(GLuint shaderProgram, glm::vec3 viewPos) const {
    // Sorted far to near.
    const auto sortedLocs = getDrawOrderChunkLocations(viewPos);
    const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");

    auto setModel = [modelLoc](const std::pair<int, int>& loc) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
            loc.first * CHUNK_WIDTH, 0.0f, loc.second * CHUNK_WIDTH));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    };

    // PASS 1: OPAQUE TERRAIN (includes alpha-cutout plants), near to far.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    for (auto it = sortedLocs.rbegin(); it != sortedLocs.rend(); ++it) {
        const auto& chunk = chunks.at(*it);
>>>>>>> Stashed changes
        if (chunk->opaqueVertexCount > 0 && chunk->opaqueVAO != 0) {
            setModel(*it);
            chunk->drawOpaque();
        }
    }

    // PASS 2: TRANSPARENT (water, glass), far to near. Depth writes off.
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const auto& loc : sortedLocs) {
        const auto& chunk = chunks.at(loc);
        if (chunk->transparentVertexCount > 0 && chunk->transparentVAO != 0) {
<<<<<<< Updated upstream
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
                loc.first * CHUNK_WIDTH,
                0.0f,
                loc.second * CHUNK_WIDTH
            ));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
=======
            setModel(loc);
>>>>>>> Stashed changes
            chunk->drawTransparent();
        }
    }

    glDepthMask(GL_TRUE);
}

<<<<<<< Updated upstream
void World::renderBlockHighlight(const glm::ivec3& blockPos, const glm::mat4& view, const glm::mat4& proj) const {
}

uint8_t World::getBlockAt(WorldLoc loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;

    int cx = static_cast<int>(std::floor(static_cast<float>(loc.x)
            / CHUNK_WIDTH));
    int cz = static_cast<int>(std::floor(static_cast<float>(loc.z)
            / CHUNK_WIDTH));

    auto& mutableChunks = const_cast<std::unordered_map<std::pair<int, int>, std::shared_ptr<ChunkMesh>, PairHash>&>(chunks);

    auto it = mutableChunks.find({cx, cz});
    if (it == mutableChunks.end()) {
        return 0;
    }

    int lx = loc.x - cx * CHUNK_WIDTH;
    int lz = loc.z - cz * CHUNK_WIDTH;
    if (lx < 0) lx += CHUNK_WIDTH;
    if (lz < 0) lz += CHUNK_WIDTH;
    if (lx >= CHUNK_WIDTH) lx -= CHUNK_WIDTH;
    if (lz >= CHUNK_WIDTH) lz -= CHUNK_WIDTH;

    return it->second->getBlock(lx, loc.y, lz);
}

void World::setBlockAt(WorldLoc loc, uint8_t blockId) {
    int cx = static_cast<int>(std::floor(static_cast<float>(loc.x)
            / CHUNK_WIDTH));
    int cz = static_cast<int>(std::floor(static_cast<float>(loc.z)
            / CHUNK_WIDTH));

    auto it = chunks.find({cx, cz});
    if (it != chunks.end()) {
        int lx = loc.x - (cx * CHUNK_WIDTH);
        int lz = loc.z - (cz * CHUNK_WIDTH);
        if (lx < 0) lx += CHUNK_WIDTH;
        if (lz < 0) lz += CHUNK_WIDTH;

        it->second->setBlock(lx, loc.y, lz, blockId);
        std::async(std::launch::async, [chunk = it->second]() {
            chunk->generateMeshData();
        });
    }
}

uint8_t World::getMetadataAt(WorldLoc loc) const {
    int cx = static_cast<int>(std::floor(static_cast<float>(loc.x)
            / CHUNK_WIDTH));
    int cz = static_cast<int>(std::floor(static_cast<float>(loc.z)
            / CHUNK_WIDTH));

    auto it = chunks.find({cx, cz});
    if (it != chunks.end()) {
        int lx = loc.x - (cx * CHUNK_WIDTH);
        int lz = loc.z - (cz * CHUNK_WIDTH);
        if (lx < 0) lx += CHUNK_WIDTH;
        if (lz < 0) lz += CHUNK_WIDTH;
        return it->second->getMetadata(lx, loc.y, lz);
    }
    return 0;
}

void World::setMetadataAt(WorldLoc loc, uint8_t meta) {
    int cx = static_cast<int>(std::floor(static_cast<float>(loc.x)
            / CHUNK_WIDTH));
    int cz = static_cast<int>(std::floor(static_cast<float>(loc.z)
            / CHUNK_WIDTH));

    auto it = chunks.find({cx, cz});
    if (it != chunks.end()) {
        int lx = loc.x - (cx * CHUNK_WIDTH);
        int lz = loc.z - (cz * CHUNK_WIDTH);
        if (lx < 0) lx += CHUNK_WIDTH;
        if (lz < 0) lz += CHUNK_WIDTH;

        it->second->setMetadata(lx, loc.y, lz, meta);
        std::async(std::launch::async, [chunk = it->second]() {
            chunk->generateMeshData();
        });
    }
}
=======
std::vector<std::pair<int, int>> World::getDrawOrderChunkLocations(glm::vec3 viewPos) const {
    using Coord = std::pair<int, int>;

    std::vector<Coord> locs;
    locs.reserve(chunks.size());
    for (const auto& [coord, _] : chunks) {
        locs.push_back(coord);
    }

    const float half = CHUNK_WIDTH * 0.5f;
    auto distSq = [&viewPos, half](const Coord& c) {
        const float dx = c.first * CHUNK_WIDTH + half - viewPos.x;
        const float dz = c.second * CHUNK_WIDTH + half - viewPos.z;
        return dx * dx + dz * dz;
    };

    std::sort(locs.begin(), locs.end(),
              [&distSq](const Coord& a, const Coord& b) { return distSq(a) > distSq(b); });
    return locs;
}

// Stub: block highlighting is not implemented yet.
void World::renderBlockHighlight(const glm::ivec3& /*blockPos*/,
                                 const glm::mat4& /*view*/,
                                 const glm::mat4& /*proj*/) const {}

// ---------------------------------------------------------------------------
// Block and metadata access
// ---------------------------------------------------------------------------

uint8_t World::getBlockAt(const glm::ivec3& loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;
    const ChunkMesh* chunk = chunkAt(loc);
    if (!chunk) return 0;
    return chunk->getBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                           positiveMod(loc.z, CHUNK_WIDTH));
}

void World::setBlockAt(const glm::ivec3& loc, uint8_t blockId) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkMesh* chunk = chunkAt(loc);
    if (!chunk) return;

    chunk->setBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                    positiveMod(loc.z, CHUNK_WIDTH), blockId);
    markBlockChanged(loc);
}

uint8_t World::getMetadataAt(const glm::ivec3& loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;
    const ChunkMesh* chunk = chunkAt(loc);
    if (!chunk) return 0;
    return chunk->getMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                              positiveMod(loc.z, CHUNK_WIDTH));
}

void World::setMetadataAt(const glm::ivec3& loc, uint8_t meta) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkMesh* chunk = chunkAt(loc);
    if (!chunk) return;

    chunk->setMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                       positiveMod(loc.z, CHUNK_WIDTH), meta);
    markBlockChanged(loc);
}
>>>>>>> Stashed changes
