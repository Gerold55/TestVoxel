#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float lifetime;
    float maxLifetime;
    float size;
};

class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    void spawnBreakParticles(const glm::vec3& pos, uint8_t blockId);
    void update(float deltaTime);
    void render(const glm::mat4& view, const glm::mat4& proj, GLuint shaderProgram);

private:
    std::vector<Particle> particles;
    GLuint VAO, VBO;
    void initBuffers();
};