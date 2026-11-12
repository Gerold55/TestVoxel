#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <sol/sol.hpp>
#include "ChunkMesh.h"
#include "Types.h"

struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const noexcept {
        return static_cast<size_t>(v.x) * 73856093u
             ^ static_cast<size_t>(v.y) * 19349663u
             ^ static_cast<size_t>(v.z) * 83492791u;
    }
};

class World {
public:
    using ChunkCoord = std::pair<int, int>;

    World();
    ~World();

    // Per frame: queues and loads chunks, unloads distant ones, rebuilds meshes.
    void update(const glm::vec3& playerPos, sol::state& lua);
    void updateFluids();
    void loadChunk(int chunkX, int chunkZ, sol::state& lua);

    // Draws visible chunks. viewProj = projection * view, eye = camera position.
    // Chunks outside the view frustum are skipped.
    void render(GLuint shaderProgram, const glm::mat4& viewProj, const glm::vec3& eye) const;

    // Legacy overload: draws every loaded chunk without culling.
    void render(GLuint shaderProgram) const;

    void upload() const;
    void renderBlockHighlight(const glm::ivec3& blockPos, const glm::mat4& view, const glm::mat4& proj) const;

    uint8_t getBlockAt(const glm::ivec3& loc) const;
    void setBlockAt(const glm::ivec3& loc, uint8_t blockId);
    uint8_t getMetadataAt(const glm::ivec3& loc) const;
    void setMetadataAt(const glm::ivec3& loc, uint8_t meta);

    // Render distance in chunks (radius), clamped to [2, 16].
    void setRenderDistance(int chunks);
    int renderDistance() const { return m_renderDistance; }
    float renderDistanceBlocks() const { return m_renderDistance * 16.0f; }

    // Per-frame time budgets in milliseconds. Each stage always finishes at least one item.
    void setLoadBudgetMs(float ms)   { m_loadBudgetMs = ms; }
    void setMeshBudgetMs(float ms)   { m_meshBudgetMs = ms; }
    void setUploadBudgetMs(float ms) { m_uploadBudgetMs = ms; }

    size_t loadedChunkCount() const { return chunks.size(); }
    size_t pendingLoadCount() const { return m_loadQueue.size(); }
    size_t pendingMeshCount() const { return m_meshQueue.size(); }

    // Fraction (0..1) of chunks within `radius` chunks of pos that are loaded,
    // meshed, and uploaded. 1.0 means the area is ready to play in.
    float areaLoadProgress(const glm::vec3& pos, int radius) const;

    // Y coordinate of the first air block above the highest walkable block in
    // this column. Returns -1 if the column has no loaded ground.
    int groundHeightAt(int x, int z) const;

    // Kept public for compatibility with existing code.
    std::unordered_map<ChunkCoord, std::shared_ptr<ChunkMesh>, PairHash> chunks;

private:
    struct Visible {
        ChunkMesh* mesh;
        glm::vec3 origin;
        float dist2;
        float fade;     // 0..1 fade-in progress for this chunk
    };

    ChunkMesh* chunkAt(const glm::ivec3& loc) const;
    void markMeshDirty(int cx, int cz);
    void markBlockChanged(const glm::ivec3& loc);
    void linkChunk(int cx, int cz);
    void unlinkChunk(int cx, int cz);

    void rebuildLoadQueue();
    void loadQueuedChunks(sol::state& lua);
    void processMeshQueue();
    void unloadDistantChunks(int playerChunkX, int playerChunkZ);
    void ensureLuaSeeded(sol::state& lua);
    void drawList(GLuint shaderProgram, std::vector<Visible>& list) const;
    float chunkFade(const ChunkCoord& c) const;

    void resolveFluidIds();
    void noteFluidChange(const glm::ivec3& loc);

    uint32_t m_seed = 0;
    bool m_luaSeeded = false;

    int m_renderDistance = 10;
    bool m_queueDirty = true;
    ChunkCoord m_playerChunk{0, 0};

    float m_loadBudgetMs = 4.0f;
    float m_meshBudgetMs = 4.0f;
    float m_uploadBudgetMs = 3.0f;

    std::vector<ChunkCoord> m_loadQueue;
    std::unordered_set<ChunkCoord, PairHash> m_meshQueue;

    // When each chunk first reached the GPU. Drives the fade-in.
    mutable std::unordered_map<ChunkCoord, std::chrono::steady_clock::time_point, PairHash> m_firstUpload;

    uint8_t m_waterId = 0;
    uint8_t m_flowingWaterId = 0;
    std::unordered_set<glm::ivec3, IVec3Hash> m_activeFluids;
};