#ifndef TYPES_H
#define TYPES_H

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

const int CHUNK_WIDTH = 16;
const int CHUNK_HEIGHT = 256;
const int CHUNK_SIZE = CHUNK_WIDTH;

// --- Vertex Structure for GPU Chunk Meshing ---
struct Vertex {
    float posX, posY, posZ;
    float tintR, tintG, tintB;
    float texCoordX, texCoordY;
    float baseLayer;
    float overlayLayer;
    float tintBase;
    float tintOverlay;
    float faceShading;
};

struct FaceDefinition {
    int baseLayer = -1;
    int overlayLayer = -1;
    bool tintBase = false;
    bool tintOverlay = true;
};

// Aliased as BlockProperty or BlockType for compatibility
struct BlockProperty {
    uint8_t id = 0;
    std::string name;
    glm::vec3 defaultTint = glm::vec3(1.0f);
    FaceDefinition faces[6]; // 0: +X, 1: -X, 2: +Y (Top), 3: -Y (Bottom), 4: +Z, 5: -Z
    bool isTransparent = false;
    bool isWalkable = true;
    bool isSelectable = true;
    bool hasGravity = false; // Enabled for falling blocks like sand and gravel
    bool in_creative = true; // Controls whether the block appears in the creative inventory menu
    std::string pattern = "cube"; // Controls rendering style (e.g. "cube", "plantlike")
};

// Keep an alias if other files expect BlockType
using BlockType = BlockProperty;

#endif // TYPES_H