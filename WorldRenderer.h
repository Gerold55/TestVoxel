#pragma once
// Client-side drawing of a World: keeps one ChunkMesh per loaded chunk, rebuilds meshes
// when the world reports a chunk changed, uploads them, fades new chunks in and draws
// what's inside the view. The World itself knows nothing about this.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "World.h"

class ChunkMesh;

class WorldRenderer {
public:
    using ChunkCoord = World::ChunkCoord;

    WorldRenderer();
    ~WorldRenderer();

    // Call once per frame after world.update(): picks up changes, rebuilds and uploads meshes.
    void update(World& world, const glm::vec3& playerPos);

    // Draws all visible chunk meshes (opaque then transparent).
    void render(GLuint shaderProgram, const glm::mat4& viewProj, const glm::vec3& eye);

    // Fraction (0..1) of chunks within `radius` of pos that are loaded, meshed and on the GPU.
    float areaReadyProgress(const World& world, const glm::vec3& pos, int radius) const;

    // Time per frame spent building meshes / uploading them, in milliseconds.
    void setMeshBudgetMs(float ms) { m_meshBudgetMs = ms; }
    void setUploadBudgetMs(float ms) { m_uploadBudgetMs = ms; }

    // Throws away every mesh (e.g. after textures change size or when leaving a world).
    void clear();

    size_t meshCount() const { return m_meshes.size(); }

private:
    using Clock = std::chrono::steady_clock;

    struct Visible {
        ChunkMesh* mesh;
        glm::vec3 origin;
        float dist2;
        float fade;
    };

    void rebuildMeshes(const World& world, const glm::vec3& playerPos);
    void uploadMeshes();
    float chunkFade(const ChunkCoord& c) const;
    void drawList(GLuint shaderProgram, std::vector<Visible>& list) const;

    std::unordered_map<ChunkCoord, std::unique_ptr<ChunkMesh>, PairHash> m_meshes;
    std::unordered_set<ChunkCoord, PairHash> m_needsMesh;     // waiting to be (re)built
    std::unordered_set<ChunkCoord, PairHash> m_needsUpload;   // built, waiting for the GPU
    std::unordered_map<ChunkCoord, Clock::time_point, PairHash> m_firstUpload;

    unsigned m_seenResetCount = 0;
    float m_meshBudgetMs = 4.0f;
    float m_uploadBudgetMs = 3.0f;
};
