#include "ParticleSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>

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
    // Dynamic quad vertex buffer for particles
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 18 * 1000, nullptr, GL_DYNAMIC_DRAW);

    // Position attrib
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Color attrib
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void ParticleSystem::spawnBreakParticles(const glm::vec3& pos, uint8_t blockId) {
    // Spawn 15-20 small debris particles around the broken block
    for (int i = 0; i < 18; ++i) {
        if (particles.size() >= 500) break; // Cap max active particles

        Particle p;
        p.position = pos + glm::vec3(
            (static_cast<float>(rand()) / RAND_MAX) * 0.8f + 0.1f,
            (static_cast<float>(rand()) / RAND_MAX) * 0.8f + 0.1f,
            (static_cast<float>(rand()) / RAND_MAX) * 0.8f + 0.1f
        );
        p.velocity = glm::vec3(
            ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 3.0f,
            (static_cast<float>(rand()) / RAND_MAX) * 4.0f + 1.0f,
            ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 3.0f
        );
        p.color = glm::vec3(0.5f, 0.5f, 0.5f); // Gray stone dust / debris default
        p.lifetime = 0.0f;
        p.maxLifetime = 0.6f + (static_cast<float>(rand()) / RAND_MAX) * 0.4f;
        p.size = 0.15f;

        particles.push_back(p);
    }
}

void ParticleSystem::update(float deltaTime) {
    for (auto it = particles.begin(); it != particles.end();) {
        it->lifetime += deltaTime;
        if (it->lifetime >= it->maxLifetime) {
            it = particles.erase(it);
        } else {
            it->velocity.y -= 12.0f * deltaTime; // Gravity on particles
            it->position += it->velocity * deltaTime;
            ++it;
        }
    }
}

void ParticleSystem::render(const glm::mat4& view, const glm::mat4& proj, GLuint shaderProgram) {
    if (particles.empty()) return;

    // Render logic using billboarding or point/cube quads matching your block aesthetic
}