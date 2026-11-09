#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <utility>
#include <sol/sol.hpp>
#include "ChunkMesh.h"
#include "Types.h"

struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

class World {
public:
    World();
    ~World();

    // Per-frame: loads nearby chunks, unloads distant ones, rebuilds queued meshes.
    void update(const glm::vec3& playerPos, sol::state& lua);
    void updateFluids();
    void loadChunk(int chunkX, int chunkZ, sol::state& lua);

    // render(shader) sorts from the world origin; prefer the viewPos overload.
    void render(GLuint shaderProgram) const;
    void render(GLuint shaderProgram, glm::vec3 viewPos) const;

    void upload() const;
    void draw(GLuint shaderProgram, glm::vec3 viewPos) const;
    std::vector<std::pair<int, int>> getDrawOrderChunkLocations(glm::vec3 viewPos) const;

    void renderBlockHighlight(const glm::ivec3& blockPos, const glm::mat4& view, const glm::mat4& proj) const;

    uint8_t getBlockAt(const glm::ivec3& loc) const;
    void setBlockAt(const glm::ivec3& loc, uint8_t blockId);
    uint8_t getMetadataAt(const glm::ivec3& loc) const;
    void setMetadataAt(const glm::ivec3& loc, uint8_t meta);

    // Kept public for compatibility with existing callers.
    std::unordered_map<std::pair<int, int>, std::shared_ptr<ChunkMesh>, PairHash> chunks;

private:
    // Lookup and bookkeeping helpers
    ChunkMesh* chunkAt(const glm::ivec3& loc) const;
    void markMeshDirty(int cx, int cz);
    void markBlockChanged(const glm::ivec3& loc);
    void linkChunk(int cx, int cz);
    void unlinkChunk(int cx, int cz);
    void processMeshQueue();
    void unloadDistantChunks(int playerChunkX, int playerChunkZ);
    void ensureLuaSeeded(sol::state& lua);

    // World state
    uint32_t m_seed = 0;
    bool     m_luaSeeded = false;
    int      m_renderDistance = 3;

    // Cached block IDs for fluids (0 means not yet resolved)
    uint8_t  m_waterId = 0;
    uint8_t  m_flowingWaterId = 0;

    // Chunks waiting for a mesh rebuild on the main thread
    std::unordered_set<std::pair<int, int>, PairHash> m_meshQueue;
};
