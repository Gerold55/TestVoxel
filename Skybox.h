#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class Skybox {
public:
    Skybox();
    ~Skybox();

    void init();
    void render(const glm::mat4& view, const glm::mat4& projection, float sunPosY);

    GLuint cubemapTexture = 0;

private:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
};