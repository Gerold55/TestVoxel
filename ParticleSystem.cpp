#include "ParticleSystem.h"
#include "Block.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdlib>
#include <vector>

// Particles are drawn with the world's block shader, so each vertex uses the same
// 13-float layout as chunk vertices (see Vertex in Types.h):
//   pos(3) tint(3) uv(2) baseLayer overlayLayer tintBase tintOverlay faceShading
namespace {
constexpr int kFloatsPerVertex   = 13;
constexpr int kFloatsPerParticle = kFloatsPerVertex * 6;
constexpr size_t kMaxParticles   = 500;
constexpr float kPieceSize       = 0.25f;   // each particle shows a quarter-width piece of the texture

float rand01() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }

float fract(float v) { return v - std::floor(v); }
}

ParticleSystem::ParticleSystem() {
    initBuffers();
}

ParticleSystem::~ParticleSystem() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}

void ParticleSystem::initBuffers() {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * kFloatsPerParticle * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);                     // position
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));   // tint colour
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));   // uv
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));   // base layer
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));   // overlay layer (-1 = none)
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)(10 * sizeof(float)));  // tint base
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)(11 * sizeof(float)));  // tint overlay
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));  // face shading
    for (int i = 0; i < 8; ++i) glEnableVertexAttribArray(i);

    glBindVertexArray(0);
}

void ParticleSystem::spawnBreakParticles(const glm::vec3& pos, uint8_t blockId, float texLayer) {
    if (texLayer < 0.0f) return;   // block has no texture

    // Grass, leaves etc. are tinted - use the block's colour so particles match
    glm::vec3 tint(1.0f);
    if (blockId != 0) {
        const auto& block = BlockRegistry::getInstance().getBlock(blockId);
        for (const auto& f : block.faces) {
            if (static_cast<float>(f.baseLayer) == texLayer && f.tintBase) {
                tint = block.defaultTint;
                break;
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        if (particles.size() >= kMaxParticles) break;

        Particle p;
        p.position = pos + glm::vec3(rand01() * 0.8f + 0.1f,
                                     rand01() * 0.8f + 0.1f,
                                     rand01() * 0.8f + 0.1f);
        p.velocity = glm::vec3((rand01() - 0.5f) * 3.0f,
                               rand01() * 4.0f + 1.0f,
                               (rand01() - 0.5f) * 3.0f);
        p.color = tint;
        p.lifetime = 0.0f;
        p.maxLifetime = 0.4f + rand01() * 0.3f;   // also seeds which piece of the texture it shows
        p.size = 0.12f;
        p.textureLayer = texLayer;

        particles.push_back(p);
    }
}

void ParticleSystem::update(float deltaTime) {
    for (auto it = particles.begin(); it != particles.end();) {
        it->lifetime += deltaTime;
        if (it->lifetime >= it->maxLifetime) {
            it = particles.erase(it);
        } else {
            it->velocity.y -= 16.0f * deltaTime;
            it->position += it->velocity * deltaTime;
            ++it;
        }
    }
}

void ParticleSystem::render(const glm::mat4& view, const glm::mat4& proj, GLuint shaderProgram, GLuint textureArrayID) {
    if (particles.empty()) return;

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

    // Particles are already in world space. Reset the per-chunk uniforms left over
    // from drawing the world (otherwise particles get the last chunk's offset/fade).
    const glm::mat4 identity(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniform1f(glGetUniformLocation(shaderProgram, "u_ChunkFade"), 1.0f);
    glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

    // Camera axes from the view matrix (same for every particle)
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    std::vector<float> vertexData;
    vertexData.reserve(particles.size() * kFloatsPerParticle);

    for (const auto& p : particles) {
        // Shrink a little as the particle dies
        const float life = 1.0f - 0.5f * (p.lifetime / p.maxLifetime);
        const float hs = p.size * 0.5f * life;

        const glm::vec3 c0 = p.position + (-camRight - camUp) * hs;   // bottom-left
        const glm::vec3 c1 = p.position + ( camRight - camUp) * hs;   // bottom-right
        const glm::vec3 c2 = p.position + ( camRight + camUp) * hs;   // top-right
        const glm::vec3 c3 = p.position + (-camRight + camUp) * hs;   // top-left

        // A random quarter-sized piece of the block texture (fixed per particle).
        // Textures are stored top row first, so v grows downwards.
        const float u0 = fract(p.maxLifetime * 37.13f) * (1.0f - kPieceSize);
        const float v0 = fract(p.maxLifetime * 91.71f) * (1.0f - kPieceSize);
        const float u1 = u0 + kPieceSize;
        const float v1 = v0 + kPieceSize;

        auto vert = [&](const glm::vec3& v, float u, float vv) {
            const float data[kFloatsPerVertex] = {
                v.x, v.y, v.z,
                p.color.r, p.color.g, p.color.b,
                u, vv,
                p.textureLayer,
                -1.0f,          // no overlay
                1.0f,           // apply tint (white for untinted blocks)
                0.0f,
                1.0f            // full brightness before sunlight
            };
            vertexData.insert(vertexData.end(), data, data + kFloatsPerVertex);
        };

        vert(c0, u0, v1); vert(c1, u1, v1); vert(c2, u1, v0);
        vert(c0, u0, v1); vert(c2, u1, v0); vert(c3, u0, v0);
    }

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_DYNAMIC_DRAW);

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(particles.size() * 6));

    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}
