#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>

struct Bone {
    std::string name;
    glm::vec3 pivot;          // Rotation pivot point relative to parent
    glm::vec3 rotation;       // Current local rotation (angles in degrees)
    glm::vec3 position;       // Local position offset
    std::vector<std::shared_ptr<Bone>> children;
    
    // Mesh data specific to this bone box (like a Minecraft cube)
    Mesh* mesh = nullptr; 

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