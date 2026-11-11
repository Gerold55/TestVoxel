#ifndef TYPES_H
#define TYPES_H

#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include <memory>

// Forward declaration for custom 3D model support
class AnimatedModel;

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

// --- Bounding Box Structure for Collisions and Selection ---
struct BoundingBox {
    glm::vec3 min = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 max = glm::vec3(1.0f, 1.0f, 1.0f);
};

// --- Door Group and State Properties ---
struct DoorProperty {
    bool isDoor = false;
    bool isTopHalf = false;
    bool isRightHinge = false; // For double door symmetry (left vs right swing)
};

// Aliased as BlockProperty or BlockType for compatibility
struct BlockProperty {
    uint8_t id = 0;
    std::string name;
    glm::vec3 defaultTint = glm::vec3(1.0f);
    FaceDefinition faces[6]; // 0: +X, 1: -X, 2: +Y (Top), 3: -Y (Bottom), 4: +Z, 5: -Z
    bool isTransparent = false;
    bool isCutout = false;   // Alpha-tested (leaves, plants): opaque pass, shader discards transparent texels
    bool isWalkable = true;
    bool isSelectable = true;
    bool hasGravity = false; // Enabled for falling blocks like sand and gravel
    bool in_creative = true; // Controls whether the block appears in the creative inventory menu
    std::string pattern = "cube"; // Controls rendering style (e.g. "cube", "plantlike", "door", "custom_model")

    // --- Custom 3D Model Support (.b3d files) ---
    std::string modelPath = "";
    std::shared_ptr<AnimatedModel> cachedModel = nullptr;

    // Node Sizing & Bounding Boxes
    BoundingBox collisionBox;
    BoundingBox selectionBox;

    // Door specific extension struct
    DoorProperty doorProps;
};

// Keep an alias if other files expect BlockType
using BlockType = BlockProperty;

#endif // TYPES_H