#include "World.h"
#include "ChunkMesh.h"
#include "Block.h"
#include "Types.h"
#include "CaveGenerator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMinRenderDistance = 2;
constexpr int kMaxRenderDistance = 16;
constexpr int kUnloadMargin = 2;              // chunks beyond render distance before unloading
constexpr int kMaxFluidUpdatesPerTick = 256;
constexpr float kChunkFadeSeconds = 0.6f;     // how long a new chunk takes to fully appear

// Neighbor slots match ChunkMesh::getBlock():
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

glm::vec3 originOf(const World::ChunkCoord& c) {
    return glm::vec3(static_cast<float>(c.first * CHUNK_WIDTH),
                     0.0f,
                     static_cast<float>(c.second * CHUNK_WIDTH));
}

// Gribb-Hartmann frustum extraction from a combined projection * view matrix.
struct Frustum {
    glm::vec4 planes[6];
};

Frustum makeFrustum(const glm::mat4& m) {
    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

    Frustum f;
    f.planes[0] = row3 + row0;  // left
    f.planes[1] = row3 - row0;  // right
    f.planes[2] = row3 + row1;  // bottom
    f.planes[3] = row3 - row1;  // top
    f.planes[4] = row3 + row2;  // near
    f.planes[5] = row3 - row2;  // far

    for (auto& p : f.planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
    return f;
}

// True if the axis-aligned box [boxMin, boxMax] is at least partly inside the frustum.
bool boxInFrustum(const Frustum& f, const glm::vec3& boxMin, const glm::vec3& boxMax) {
    for (const auto& p : f.planes) {
        const glm::vec3 n(p);
        const glm::vec3 positive(n.x >= 0.0f ? boxMax.x : boxMin.x,
                                 n.y >= 0.0f ? boxMax.y : boxMin.y,
                                 n.z >= 0.0f ? boxMax.z : boxMin.z);
        if (glm::dot(n, positive) + p.w < 0.0f) return false;
    }
    return true;
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
// Lookup and bookkeeping
// ---------------------------------------------------------------------------

ChunkMesh* World::chunkAt(const glm::ivec3& loc) const {
    const auto it = chunks.find(ChunkCoord{floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
    return it != chunks.end() ? it->second.get() : nullptr;
}

void World::markMeshDirty(int cx, int cz) {
    const ChunkCoord c{cx, cz};
    if (chunks.count(c)) {
        m_meshQueue.insert(c);
    }
}

// Marks the chunk containing loc, plus any neighbor whose boundary faces depend on it.
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

void World::linkChunk(int cx, int cz) {
    auto selfIt = chunks.find(ChunkCoord{cx, cz});
    if (selfIt == chunks.end()) return;
    ChunkMesh* self = selfIt->second.get();

    for (const auto& d : kNeighborDirs) {
        auto nIt = chunks.find(ChunkCoord{cx + d.dx, cz + d.dz});
        ChunkMesh* n = (nIt != chunks.end()) ? nIt->second.get() : nullptr;
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
            markMeshDirty(nx, nz);
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

// Missing chunks inside the render radius, nearest to the player first.
void World::rebuildLoadQueue() {
    m_loadQueue.clear();
    const int r = m_renderDistance;
    for (int dx = -r; dx <= r; ++dx) {
        for (int dz = -r; dz <= r; ++dz) {
            const ChunkCoord c{m_playerChunk.first + dx, m_playerChunk.second + dz};
            if (chunks.find(c) == chunks.end()) {
                m_loadQueue.push_back(c);
            }
        }
    }

    const ChunkCoord p = m_playerChunk;
    std::sort(m_loadQueue.begin(), m_loadQueue.end(),
              [p](const ChunkCoord& a, const ChunkCoord& b) {
                  return chunkDist2(a, p) < chunkDist2(b, p);
              });
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
    unloadDistantChunks(pcx, pcz);
    processMeshQueue();
}

void World::unloadDistantChunks(int playerChunkX, int playerChunkZ) {
    const int limit = m_renderDistance + kUnloadMargin;

    for (auto it = chunks.begin(); it != chunks.end();) {
        const int cx = it->first.first;
        const int cz = it->first.second;
        const int dx = cx - playerChunkX;
        const int dz = cz - playerChunkZ;

        if (std::max(std::abs(dx), std::abs(dz)) > limit) {
            unlinkChunk(cx, cz);                 // clear neighbor pointers before destroying
            m_meshQueue.erase(ChunkCoord{cx, cz});
            m_firstUpload.erase(ChunkCoord{cx, cz});
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
}

// Rebuilds meshes nearest-first until the mesh budget runs out.
void World::processMeshQueue() {
    if (m_meshQueue.empty()) return;

    std::vector<ChunkCoord> order(m_meshQueue.begin(), m_meshQueue.end());
    const ChunkCoord p = m_playerChunk;
    std::sort(order.begin(), order.end(),
              [p](const ChunkCoord& a, const ChunkCoord& b) {
                  return chunkDist2(a, p) < chunkDist2(b, p);
              });

    const auto start = Clock::now();
    for (const ChunkCoord& c : order) {
        m_meshQueue.erase(c);
        auto it = chunks.find(c);
        if (it != chunks.end()) {
            it->second->generateMeshData();
        }
        if (elapsedMs(start) >= m_meshBudgetMs) break;
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

    chunks[ChunkCoord{chunkX, chunkZ}] = chunk;
    linkChunk(chunkX, chunkZ);

    // The new chunk and its neighbors need (re)meshing so border faces cull correctly.
    markMeshDirty(chunkX, chunkZ);
    markMeshDirty(chunkX - 1, chunkZ);
    markMeshDirty(chunkX + 1, chunkZ);
    markMeshDirty(chunkX, chunkZ - 1);
    markMeshDirty(chunkX, chunkZ + 1);
}

// ---------------------------------------------------------------------------
// Upload, fade, and draw
// ---------------------------------------------------------------------------

void World::upload() const {
    const auto start = Clock::now();

    for (const auto& entry : chunks) {
        const auto& chunk = entry.second;
        if (!chunk->meshReadyForUpload || !chunk->isDirty) continue;
        if (elapsedMs(start) >= m_uploadBudgetMs) break;

        chunk->uploadMesh();

        // Start the fade-in the first time this chunk's mesh reaches the GPU.
        if (m_firstUpload.find(entry.first) == m_firstUpload.end()) {
            m_firstUpload.emplace(entry.first, Clock::now());
        }
    }
}

// 0 at the moment a chunk first appears, rising to 1 over kChunkFadeSeconds.
float World::chunkFade(const ChunkCoord& c) const {
    const auto it = m_firstUpload.find(c);
    if (it == m_firstUpload.end()) return 1.0f;
    const float t = elapsedMs(it->second) / (kChunkFadeSeconds * 1000.0f);
    return std::clamp(t, 0.0f, 1.0f);
}

// Opaque pass nearest-first (early depth rejection). Transparent pass farthest-first.
void World::drawList(GLuint shaderProgram, std::vector<Visible>& list) const {
    std::sort(list.begin(), list.end(),
              [](const Visible& a, const Visible& b) { return a.dist2 < b.dist2; });

    const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    const GLint fadeLoc  = glGetUniformLocation(shaderProgram, "u_ChunkFade");

    auto setChunkUniforms = [modelLoc, fadeLoc](const Visible& v) {
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), v.origin);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1f(fadeLoc, v.fade);
    };

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    for (const Visible& v : list) {
        if (v.mesh->opaqueVertexCount > 0 && v.mesh->opaqueVAO != 0) {
            setChunkUniforms(v);
            v.mesh->drawOpaque();
        }
    }

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (it->mesh->transparentVertexCount > 0 && it->mesh->transparentVAO != 0) {
            setChunkUniforms(*it);
            it->mesh->drawTransparent();
        }
    }
    glDepthMask(GL_TRUE);
}

void World::render(GLuint shaderProgram, const glm::mat4& viewProj, const glm::vec3& eye) const {
    upload();

    const Frustum frustum = makeFrustum(viewProj);
    std::vector<Visible> list;
    list.reserve(chunks.size());

    for (const auto& entry : chunks) {
        ChunkMesh* mesh = entry.second.get();
        if (mesh->opaqueVertexCount == 0 && mesh->transparentVertexCount == 0) continue;

        const glm::vec3 boxMin = originOf(entry.first);
        const glm::vec3 boxMax = boxMin + glm::vec3(static_cast<float>(CHUNK_WIDTH),
                                                    static_cast<float>(CHUNK_HEIGHT),
                                                    static_cast<float>(CHUNK_WIDTH));
        if (!boxInFrustum(frustum, boxMin, boxMax)) continue;

        const glm::vec3 center = (boxMin + boxMax) * 0.5f;
        const glm::vec3 d = center - eye;
        list.push_back(Visible{mesh, boxMin, glm::dot(d, d), chunkFade(entry.first)});
    }

    drawList(shaderProgram, list);
}

void World::render(GLuint shaderProgram) const {
    upload();

    std::vector<Visible> list;
    list.reserve(chunks.size());
    for (const auto& entry : chunks) {
        ChunkMesh* mesh = entry.second.get();
        if (mesh->opaqueVertexCount == 0 && mesh->transparentVertexCount == 0) continue;
        list.push_back(Visible{mesh, originOf(entry.first), 0.0f, chunkFade(entry.first)});
    }

    drawList(shaderProgram, list);
}

void World::renderBlockHighlight(const glm::ivec3& /*blockPos*/,
                                 const glm::mat4& /*view*/,
                                 const glm::mat4& /*proj*/) const {}

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
    noteFluidChange(loc);
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