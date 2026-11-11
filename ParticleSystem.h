#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float lifetime = 0.0f;
    float maxLifetime = 0.5f;
    float size = 0.12f;
    float textureLayer = -1.0f; // Texture array layer index for the block
};

class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    void spawnBreakParticles(const glm::vec3& pos, uint8_t blockId, float texLayer);
    void update(float deltaTime);
    void render(const glm::mat4& view, const glm::mat4& proj, GLuint shaderProgram, GLuint textureArrayID);

private:
    void initBuffers();

    GLuint VAO = 0;
    GLuint VBO = 0;
    std::vector<Particle> particles;
};

#endif // PARTICLESYSTEM_H