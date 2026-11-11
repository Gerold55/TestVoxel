#include "ParticleSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>
#include <vector>

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
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 54 * 1000, nullptr, GL_DYNAMIC_DRAW);

    // Position attrib (layout 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Color attrib (layout 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // TexCoord attrib (layout 2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // Texture Layer attrib (layout 3)
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
}

void ParticleSystem::spawnBreakParticles(const glm::vec3& pos, uint8_t blockId, float texLayer) {
    for (int i = 0; i < 16; ++i) {
        if (particles.size() >= 500) break;

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
        p.color = glm::vec3(1.0f);
        p.lifetime = 0.0f;
        p.maxLifetime = 0.4f + (static_cast<float>(rand()) / RAND_MAX) * 0.3f;
        p.size = 0.10f;
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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

    std::vector<float> vertexData;
    vertexData.reserve(particles.size() * 54);

    float hs = 0.06f; // Half-size of particle quad

    for (const auto& p : particles) {
        // Pass quad corner offsets (-hs to +hs) directly as vertex attributes or position offsets
        // Format: worldPosX, worldPosY, worldPosZ, r, g, b, u, v, layer, offsetX, offsetY
        // To keep the layout clean with your existing VBO structure, we can pass corner offsets 
        // or let the vertex shader bill-board them using quad corner indices.
        
        // Simpler CPU-side billboarding approach: 
        // We calculate camera right and up vectors from the view matrix.
        glm::vec3 camRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
        glm::vec3 camUp    = glm::vec3(view[0][1], view[1][1], view[2][1]);

        glm::vec3 p0 = p.position + (-camRight - camUp) * hs;
        glm::vec3 p1 = p.position + ( camRight - camUp) * hs;
        glm::vec3 p2 = p.position + ( camRight + camUp) * hs;
        glm::vec3 p3 = p.position + (-camRight + camUp) * hs;

        float vertices[54] = {
            // Triangle 1
            p0.x, p0.y, p0.z, p.color.r, p.color.g, p.color.b, 0.0f, 0.0f, p.textureLayer,
            p1.x, p1.y, p1.z, p.color.r, p.color.g, p.color.b, 1.0f, 0.0f, p.textureLayer,
            p2.x, p2.y, p2.z, p.color.r, p.color.g, p.color.b, 1.0f, 1.0f, p.textureLayer,
            // Triangle 2
            p0.x, p0.y, p0.z, p.color.r, p.color.g, p.color.b, 0.0f, 0.0f, p.textureLayer,
            p2.x, p2.y, p2.z, p.color.r, p.color.g, p.color.b, 1.0f, 1.0f, p.textureLayer,
            p3.x, p3.y, p3.z, p.color.r, p.color.g, p.color.b, 0.0f, 1.0f, p.textureLayer
        };

        for (int i = 0; i < 54; ++i) {
            vertexData.push_back(vertices[i]);
        }
    }

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_DYNAMIC_DRAW);

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(particles.size() * 6));

    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}