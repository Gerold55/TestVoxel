#include "Player.h"
#include "World.h"
#include "Block.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

Player::Player() {
    position = glm::vec3(8.0f, 75.0f, 8.0f);
    velocity = glm::vec3(0.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    lastX = 640.0f;
    lastY = 360.0f;
    firstMouse = true;
    isGrounded = false;
}

void Player::handleMouseLook(double xpos, double ypos) {
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

void Player::update(float deltaTime, const World& world, GLFWwindow* window, sol::state& lua) {
    if (deltaTime > 0.1f) deltaTime = 0.1f;

    // --- 1. CHECK IF PLAYER IS SUBMERGED IN WATER ---
    bool inWater = false;
    int headX = static_cast<int>(std::floor(position.x));
    int headY = static_cast<int>(std::floor(position.y + 1.0f));
    int headZ = static_cast<int>(std::floor(position.z));
    uint8_t blockAtHead = world.getBlockAt(headX, headY, headZ);
    if (blockAtHead != 0) {
        const auto& prop = BlockRegistry::getInstance().getBlock(blockAtHead);
        if (prop.name == "water" || prop.name == "flowing_water") {
            inWater = true;
        }
    }

    // --- 2. HANDLE INPUT & MOVEMENT DIRECTION ---
    glm::vec3 forward(
        cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
        sin(glm::radians(pitch)),
        sin(glm::radians(yaw)) * cos(glm::radians(pitch))
    );
    forward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 moveDir(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;

    float speed = inWater ? 2.2f : 4.3f;
    if (!inWater && glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
        speed = 7.0f;
    }

    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
    }

    velocity.x = moveDir.x * speed;
    velocity.z = moveDir.z * speed;

    // --- 3. GRAVITY, BUOYANCY & JUMPING/SWIMMING ---
    if (inWater) {
        float waterGravity = -4.0f;
        velocity.y += waterGravity * deltaTime;
        velocity.y *= 0.85f;

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            velocity.y = 4.0f;
        }
    } else {
        float gravity = -22.0f;
        if (!isGrounded) {
            velocity.y += gravity * deltaTime;
        }

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isGrounded) {
            velocity.y = 8.0f;
            isGrounded = false;
        }
    }

    // --- 4. AXIS-SEPARATED AABB COLLISION RESOLUTION ---
    float playerWidth = 0.54f; // Slightly narrower than 0.6f to prevent 1x1 shaft wedging
    float playerHeight = 1.8f;
    float halfWidth = playerWidth / 2.0f;
    float skin = 0.04f;        // Skin width offset to prevent edge snagging

    // --- X Axis Collision & Move ---
    position.x += velocity.x * deltaTime;
    {
        int minX = static_cast<int>(std::floor(position.x - halfWidth + skin));
        int maxX = static_cast<int>(std::floor(position.x + halfWidth - skin));
        int minY = static_cast<int>(std::floor(position.y + skin));
        int maxY = static_cast<int>(std::floor(position.y + playerHeight - skin));
        int minZ = static_cast<int>(std::floor(position.z - halfWidth + skin));
        int maxZ = static_cast<int>(std::floor(position.z + halfWidth - skin));

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    uint8_t blockId = world.getBlockAt(x, y, z);
                    if (blockId != 0) {
                        const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
                        if (prop.name == "water" || prop.name == "flowing_water" || !prop.isWalkable) continue;

                        if (velocity.x > 0.0f) {
                            position.x = static_cast<float>(x) - halfWidth - 0.001f;
                            velocity.x = 0.0f;
                        } else if (velocity.x < 0.0f) {
                            position.x = static_cast<float>(x) + 1.0f + halfWidth + 0.001f;
                            velocity.x = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // --- Z Axis Collision & Move ---
    position.z += velocity.z * deltaTime;
    {
        int minX = static_cast<int>(std::floor(position.x - halfWidth + skin));
        int maxX = static_cast<int>(std::floor(position.x + halfWidth - skin));
        int minY = static_cast<int>(std::floor(position.y + skin));
        int maxY = static_cast<int>(std::floor(position.y + playerHeight - skin));
        int minZ = static_cast<int>(std::floor(position.z - halfWidth + skin));
        int maxZ = static_cast<int>(std::floor(position.z + halfWidth - skin));

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    uint8_t blockId = world.getBlockAt(x, y, z);
                    if (blockId != 0) {
                        const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
                        if (prop.name == "water" || prop.name == "flowing_water" || !prop.isWalkable) continue;

                        if (velocity.z > 0.0f) {
                            position.z = static_cast<float>(z) - halfWidth - 0.001f;
                            velocity.z = 0.0f;
                        } else if (velocity.z < 0.0f) {
                            position.z = static_cast<float>(z) + 1.0f + halfWidth + 0.001f;
                            velocity.z = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // --- Y Axis Collision & Move ---
    position.y += velocity.y * deltaTime;
    isGrounded = false;
    {
        int minX = static_cast<int>(std::floor(position.x - halfWidth + skin));
        int maxX = static_cast<int>(std::floor(position.x + halfWidth - skin));
        int minY = static_cast<int>(std::floor(position.y));
        int maxY = static_cast<int>(std::floor(position.y + playerHeight - skin));
        int minZ = static_cast<int>(std::floor(position.z - halfWidth + skin));
        int maxZ = static_cast<int>(std::floor(position.z + halfWidth - skin));

        bool hitSolid = false;
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    uint8_t blockId = world.getBlockAt(x, y, z);
                    if (blockId != 0) {
                        const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
                        if (prop.name == "water" || prop.name == "flowing_water" || !prop.isWalkable) continue;

                        if (velocity.y < 0.0f) {
                            position.y = static_cast<float>(y) + 1.0f;
                            velocity.y = 0.0f;
                            isGrounded = true;
                            hitSolid = true;
                        } else if (velocity.y > 0.0f) {
                            position.y = static_cast<float>(y) - playerHeight;
                            velocity.y = 0.0f;
                        }
                    }
                }
            }
        }

        // --- SAFETY FALLBACK FOR PENDING CHUNK LOADS ---
        if (!hitSolid && velocity.y < 0.0f) {
            int feetY = static_cast<int>(std::floor(position.y));
            if (feetY <= 61) {
                position.y = 61.0f;
                velocity.y = 0.0f;
                isGrounded = true;
            }
        }
    }

    // --- ULTIMATE SAFETY NET ---
    if (position.y < -10.0f) {
        position = glm::vec3(8.0f, 75.0f, 8.0f);
        velocity = glm::vec3(0.0f);
        isGrounded = true;
    }
}

glm::mat4 Player::getViewMatrix() const {
    glm::vec3 cameraPos = position + glm::vec3(0.0f, 1.62f, 0.0f);
    glm::vec3 front(
        cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
        sin(glm::radians(pitch)),
        sin(glm::radians(yaw)) * cos(glm::radians(pitch))
    );
    return glm::lookAt(cameraPos, cameraPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
}