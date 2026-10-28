#ifndef PLAYER_H
#define PLAYER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <sol/sol.hpp>

class World; // Forward declaration

struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    bool intersects(const AABB& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }
};

class Player {
public:
    glm::vec3 position{8.0f, 20.0f, 8.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 dimensions{0.6f, 1.8f, 0.6f};

    float yaw = -90.0f;
    float pitch = 0.0f;

    float mouseSensitivity = 0.1f;
    float movementSpeed = 5.0f;
    float gravity = 25.0f;
    float jumpForce = 8.5f;

    bool isGrounded = false;
    bool isPaused = false;
    bool escapePressedLastFrame = false;
    bool firstMouse = true;

    double lastX = 640.0;
    double lastY = 360.0;

    Player();

    glm::mat4 getViewMatrix() const;
    AABB getAABB() const;
    void handleMouseLook(double xpos, double ypos);
    void update(float deltaTime, const World& world, GLFWwindow* window, sol::state& lua);

private:
    void resolveCollisionAxis(int axis, float delta, const World& world);
};

#endif // PLAYER_H