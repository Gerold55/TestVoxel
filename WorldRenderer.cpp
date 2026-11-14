#include "WorldRenderer.h"
#include "ChunkMesh.h"
#include "ChunkData.h"
#include "Types.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace {

using Clock = std::chrono::steady_clock;

constexpr float kChunkFadeSeconds = 0.6f;   // how long a new chunk takes to fully appear

float elapsedMs(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int chunkDist2(const World::ChunkCoord& a, const World::ChunkCoord& b) {
    const int dx = a.first - b.first;
    const int dz = a.second - b.second;
    return dx * dx + dz * dz;
}

glm::vec3 originOf(const World::ChunkCoord& c) {
    return glm::vec3(static_cast<float>(c.first * CHUNK_WIDTH), 0.0f,
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

} // namespace

WorldRenderer::WorldRenderer() = default;
WorldRenderer::~WorldRenderer() = default;

void WorldRenderer::clear() {
    m_meshes.clear();
    m_needsMesh.clear();
    m_needsUpload.clear();
    m_firstUpload.clear();
}

void WorldRenderer::update(World& world, const glm::vec3& playerPos) {
    // World was reset (new world / quit to menu): drop everything we had.
    if (world.resetCount() != m_seenResetCount) {
        m_seenResetCount = world.resetCount();
        clear();
    }

    for (const ChunkCoord& c : world.takeUnloadedChunks()) {
        m_meshes.erase(c);
        m_needsMesh.erase(c);
        m_needsUpload.erase(c);
        m_firstUpload.erase(c);
    }
    for (const ChunkCoord& c : world.takeDirtyChunks()) {
        m_needsMesh.insert(c);
    }

    rebuildMeshes(world, playerPos);
    uploadMeshes();
}

// Rebuilds meshes nearest-first until the mesh budget runs out.
void WorldRenderer::rebuildMeshes(const World& world, const glm::vec3& playerPos) {
    if (m_needsMesh.empty()) return;

    const ChunkCoord p{floorDiv(static_cast<int>(std::floor(playerPos.x)), CHUNK_WIDTH),
                       floorDiv(static_cast<int>(std::floor(playerPos.z)), CHUNK_WIDTH)};
    std::vector<ChunkCoord> order(m_needsMesh.begin(), m_needsMesh.end());
    std::sort(order.begin(), order.end(), [p](const ChunkCoord& a, const ChunkCoord& b) {
        return chunkDist2(a, p) < chunkDist2(b, p);
    });

    const auto start = Clock::now();
    for (const ChunkCoord& c : order) {
        m_needsMesh.erase(c);
        const ChunkData* data = world.chunkData(c);
        if (!data) {                       // unloaded meanwhile
            m_meshes.erase(c);
            continue;
        }
        auto& mesh = m_meshes[c];
        if (!mesh) mesh = std::make_unique<ChunkMesh>();
        mesh->generateMeshData(*data);
        m_needsUpload.insert(c);
        if (elapsedMs(start) >= m_meshBudgetMs) break;
    }
}

void WorldRenderer::uploadMeshes() {
    const auto start = Clock::now();
    for (auto it = m_needsUpload.begin(); it != m_needsUpload.end();) {
        if (elapsedMs(start) >= m_uploadBudgetMs) break;
        auto mIt = m_meshes.find(*it);
        if (mIt != m_meshes.end() && mIt->second->meshReadyForUpload) {
            mIt->second->uploadMesh();
            // Start the fade-in the first time this chunk's mesh reaches the GPU.
            m_firstUpload.emplace(*it, Clock::now());
        }
        it = m_needsUpload.erase(it);
    }
}

// 0 at the moment a chunk first appears, rising to 1 over kChunkFadeSeconds.
float WorldRenderer::chunkFade(const ChunkCoord& c) const {
    const auto it = m_firstUpload.find(c);
    if (it == m_firstUpload.end()) return 1.0f;
    return std::clamp(elapsedMs(it->second) / (kChunkFadeSeconds * 1000.0f), 0.0f, 1.0f);
}

// Opaque pass nearest-first (early depth rejection). Transparent pass farthest-first.
void WorldRenderer::drawList(GLuint shaderProgram, std::vector<Visible>& list) const {
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

void WorldRenderer::render(GLuint shaderProgram, const glm::mat4& viewProj, const glm::vec3& eye) {
    const Frustum frustum = makeFrustum(viewProj);
    std::vector<Visible> list;
    list.reserve(m_meshes.size());

    for (const auto& entry : m_meshes) {
        ChunkMesh* mesh = entry.second.get();
        if (mesh->opaqueVertexCount == 0 && mesh->transparentVertexCount == 0) continue;

        const glm::vec3 boxMin = originOf(entry.first);
        const glm::vec3 boxMax = boxMin + glm::vec3(static_cast<float>(CHUNK_WIDTH),
                                                    static_cast<float>(CHUNK_HEIGHT),
                                                    static_cast<float>(CHUNK_WIDTH));
        if (!boxInFrustum(frustum, boxMin, boxMax)) continue;

        const glm::vec3 d = (boxMin + boxMax) * 0.5f - eye;
        list.push_back(Visible{mesh, boxMin, glm::dot(d, d), chunkFade(entry.first)});
    }
    drawList(shaderProgram, list);
}

float WorldRenderer::areaReadyProgress(const World& world, const glm::vec3& pos, int radius) const {
    const int pcx = floorDiv(static_cast<int>(std::floor(pos.x)), CHUNK_WIDTH);
    const int pcz = floorDiv(static_cast<int>(std::floor(pos.z)), CHUNK_WIDTH);

    int total = 0, done = 0;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            ++total;
            const ChunkCoord c{pcx + dx, pcz + dz};
            if (!world.chunkData(c)) continue;                    // not generated yet
            if (m_needsMesh.count(c) || m_needsUpload.count(c)) continue;
            const auto it = m_meshes.find(c);
            if (it == m_meshes.end() || !it->second->everUploaded) continue;
            ++done;
        }
    }
    return total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 1.0f;
}
