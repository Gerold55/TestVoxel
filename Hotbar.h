#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <string>

struct HotbarSlot {
    uint8_t blockId = 0;
    int count = 0;
};

class Hotbar {
public:
    Hotbar();
    ~Hotbar();

    void resize(int count);
    void setSlot(int index, uint8_t blockId, int count);
    HotbarSlot getSlot(int index) const;
    void nextSlot();
    void previousSlot();
    uint8_t getActiveBlockId() const;

    bool isCreativeOpen = false;
    int creativeTab = 0; // 0: All Items, 1: Nature & Flora, 2: Building & Blocks
    void toggleCreativeInventory();
    void handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight);

    void updateBuffers(int screenWidth, int screenHeight);
    void renderSlots(GLuint uiShader);
    void render3DIcons(GLuint voxelShader, GLuint textureArrayID);

    int slotCount = 9;
    int activeSlot = 0;
    float slotSize = 64.0f;
    float padding = 8.0f;
    float posX = 0.0f;
    float posY = 0.0f;

    std::vector<HotbarSlot> slots;

private:
    GLuint slotVAO = 0, slotVBO = 0;
    GLuint iconVAO = 0, iconVBO = 0;
    unsigned int iconVertexCount = 0;

    float creativeX = 0, creativeY = 0, creativeW = 0, creativeH = 0;
};