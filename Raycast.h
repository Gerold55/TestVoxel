#ifndef RAYCAST_H
#define RAYCAST_H

#include <glm/glm.hpp>
#include <cmath>
#include "World.h"

struct RaycastResult {
    bool hit = false;
    glm::ivec3 blockPos{0};
    glm::ivec3 faceNormal{0}; // Direction adjacent for block placement
};

inline RaycastResult raycastBlock(const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, const World& world) {
    RaycastResult result;

    int x = static_cast<int>(std::floor(rayOrigin.x));
    int y = static_cast<int>(std::floor(rayOrigin.y));
    int z = static_cast<int>(std::floor(rayOrigin.z));

    int stepX = (rayDir.x >= 0) ? 1 : -1;
    int stepY = (rayDir.y >= 0) ? 1 : -1;
    int stepZ = (rayDir.z >= 0) ? 1 : -1;

    float tMaxX = (stepX > 0) ? (std::floor(rayOrigin.x) + 1.0f - rayOrigin.x) / rayDir.x : (rayOrigin.x - std::floor(rayOrigin.x)) / -rayDir.x;
    float tMaxY = (stepY > 0) ? (std::floor(rayOrigin.y) + 1.0f - rayOrigin.y) / rayDir.y : (rayOrigin.y - std::floor(rayOrigin.y)) / -rayDir.y;
    float tMaxZ = (stepZ > 0) ? (std::floor(rayOrigin.z) + 1.0f - rayOrigin.z) / rayDir.z : (rayOrigin.z - std::floor(rayOrigin.z)) / -rayDir.z;

    float tDeltaX = std::abs(1.0f / rayDir.x);
    float tDeltaY = std::abs(1.0f / rayDir.y);
    float tDeltaZ = std::abs(1.0f / rayDir.z);

    float distance = 0.0f;
    glm::ivec3 normal(0);

    while (distance <= maxDistance) {
        uint8_t blockId = world.getBlockAt(x, y, z);
        if (blockId != 0) { // Hit solid block
            result.hit = true;
            result.blockPos = glm::ivec3(x, y, z);
            result.faceNormal = normal;
            return result;
        }

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                x += stepX;
                distance = tMaxX;
                tMaxX += tDeltaX;
                normal = glm::ivec3(-stepX, 0, 0);
            } else {
                z += stepZ;
                distance = tMaxZ;
                tMaxZ += tDeltaZ;
                normal = glm::ivec3(0, 0, -stepZ);
            }
        } else {
            if (tMaxY < tMaxZ) {
                y += stepY;
                distance = tMaxY;
                tMaxY += tDeltaY;
                normal = glm::ivec3(0, -stepY, 0);
            } else {
                z += stepZ;
                distance = tMaxZ;
                tMaxZ += tDeltaZ;
                normal = glm::ivec3(0, 0, -stepZ);
            }
        }
    }

    return result;
}

#endif // RAYCAST_H