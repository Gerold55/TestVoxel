#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

struct ModelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
};

class Mesh {
public:
    GLuint VAO = 0, VBO = 0;
    int vertexCount = 0;

    Mesh(const std::vector<ModelVertex>& vertices);
    void draw() const;
};

struct Bone {
    std::string name;
    glm::vec3 pivot = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 position = glm::vec3(0.0f);
    std::vector<std::shared_ptr<Bone>> children;
    std::shared_ptr<Mesh> mesh = nullptr;

    glm::mat4 getLocalTransform() const {
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, pivot + position);
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::translate(transform, -pivot);
        return transform;
    }
};

class AnimatedModel {
public:
    std::vector<std::shared_ptr<Bone>> rootBones;
    std::unordered_map<std::string, std::shared_ptr<Bone>> boneMap;

    void loadFromFile(const std::string& filepath);
    void loadFromJson(const std::string& filepath);
    void loadFromB3D(const std::string& filepath);
    
    std::shared_ptr<Bone> getBone(const std::string& name);
    void draw(GLuint shaderProgram, const glm::mat4& worldTransform) const;
};