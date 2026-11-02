#include "Hotbar.h"
#include "Block.h"
#include "FontRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

extern FontRenderer* g_FontRenderer;

Hotbar::Hotbar() {
    resize(9);
}

Hotbar::~Hotbar() {
    if (slotVAO != 0) glDeleteVertexArrays(1, &slotVAO);
    if (slotVBO != 0) glDeleteBuffers(1, &slotVBO);
    if (iconVAO != 0) glDeleteVertexArrays(1, &iconVAO);
    if (iconVBO != 0) glDeleteBuffers(1, &iconVBO);
}

void Hotbar::resize(int count) {
    if (count <= 0) count = 1;
    slotCount = count;
    slots.resize(slotCount);
    if (activeSlot >= slotCount) activeSlot = slotCount - 1;
}

void Hotbar::setSlot(int index, uint8_t blockId, int count) {
    if (index >= 0 && index < slotCount) {
        slots[index].blockId = blockId;
        slots[index].count = count;
    }
}

HotbarSlot Hotbar::getSlot(int index) const {
    if (index >= 0 && index < slotCount) {
        return slots[index];
    }
    return {0, 0};
}

void Hotbar::nextSlot() { 
    if (isCreativeOpen) return;
    activeSlot = (activeSlot + 1) % slotCount; 
}

void Hotbar::previousSlot() { 
    if (isCreativeOpen) return;
    activeSlot = (activeSlot - 1 + slotCount) % slotCount; 
}

uint8_t Hotbar::getActiveBlockId() const {
    if (activeSlot >= 0 && activeSlot < static_cast<int>(slots.size())) {
        return slots[activeSlot].blockId;
    }
    return 0;
}

void Hotbar::toggleCreativeInventory() {
    isCreativeOpen = !isCreativeOpen;
    std::cout << "[Creative Inventory] " << (isCreativeOpen ? "Opened" : "Closed") << std::endl;
}

static bool matchesCreativeTab(const BlockProperty& prop, int tab) {
    if (prop.name.empty() || prop.name == "air" || prop.name == "unknown") return false;
    if (!prop.in_creative) return false;

    bool isNature = (prop.pattern == "plantlike" || 
                     prop.name.find("leaves") != std::string::npos ||
                     prop.name.find("water") != std::string::npos ||
                     prop.name.find("sand") != std::string::npos ||
                     prop.name.find("reed") != std::string::npos);

    bool isBuilding = !isNature && (prop.name.find("stone") != std::string::npos ||
                                   prop.name.find("dirt") != std::string::npos ||
                                   prop.name.find("log") != std::string::npos ||
                                   prop.name.find("gravel") != std::string::npos ||
                                   prop.name.find("bedrock") != std::string::npos);

    if (tab == 0) return true;       
    if (tab == 1) return isNature;      
    if (tab == 2) return isBuilding;    
    
    return false;
}

void Hotbar::handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (!isCreativeOpen) return;

    float actualSlotSize = slotSize <= 1.5f ? slotSize * screenHeight * 0.55f : slotSize;
    if (actualSlotSize > 60.0f) actualSlotSize = 60.0f;

    float tabW = 100.0f;
    float tabH = 24.0f;
    float tabStartY = creativeY - tabH;
    for (int t = 0; t < 3; ++t) {
        float tx = creativeX + 16.0f + (t * (tabW + 6.0f));
        if (mouseX >= tx && mouseX <= tx + tabW && mouseY >= tabStartY && mouseY <= tabStartY + tabH) {
            creativeTab = t;
            std::cout << "[Creative] Switched to tab: " << creativeTab << std::endl;
            return;
        }
    }

    if (mouseX >= creativeX && mouseX <= (creativeX + creativeW) &&
        mouseY >= creativeY && mouseY <= (creativeY + creativeH)) {
        
        float gridStartX = creativeX + 16.0f;
        float gridStartY = creativeY + 45.0f;
        float itemBoxSize = actualSlotSize;
        float spacing = 6.0f;
        int cols = 9;

        int displayIndex = 0;
        for (int i = 1; i <= 255; ++i) {
            try {
                const auto& prop = BlockRegistry::getInstance().getBlock(static_cast<uint8_t>(i));
                if (prop.name.empty() || prop.name == "air") continue;

                if (matchesCreativeTab(prop, creativeTab)) {
                    int col = displayIndex % cols;
                    int row = displayIndex / cols;
                    float bx = std::round(gridStartX + col * (itemBoxSize + spacing));
                    float by = std::round(gridStartY + row * (itemBoxSize + spacing));

                    if (mouseX >= bx && mouseX <= (bx + itemBoxSize) &&
                        mouseY >= by && mouseY <= (by + itemBoxSize)) {
                        
                        setSlot(activeSlot, static_cast<uint8_t>(i), 64);
                        std::cout << "[Creative] Selected Block ID: " << i << std::endl;
                        break;
                    }
                    displayIndex++;
                }
            } catch (...) {
                break;
            }
        }
    }
}

void Hotbar::updateBuffers(int screenWidth, int screenHeight) {
    std::vector<float> slotVertices;
    std::vector<float> iconVertices;

    float actualSlotSize = slotSize;
    if (slotSize <= 1.5f) {
        actualSlotSize = slotSize * screenHeight * 0.55f;
    }
    if (actualSlotSize > 60.0f) actualSlotSize = 60.0f;

    float actualPadding = padding;

    float pixelStartX = std::round((posX + 1.0f) * 0.5f * screenWidth);
    float pixelStartY = std::round(screenHeight - actualSlotSize - posY - 20.0f);

    float totalWidth = slotCount * (actualSlotSize + actualPadding);
    if (pixelStartX + totalWidth > screenWidth || pixelStartX < 0) {
        pixelStartX = std::round((screenWidth - totalWidth) * 0.5f);
    }

    auto addPixelQuad = [&](float px, float py, float pw, float ph, glm::vec4 color) {
        float x0 = (px / screenWidth) * 2.0f - 1.0f;
        float y0 = 1.0f - (py / screenHeight) * 2.0f;
        float x1 = ((px + pw) / screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - ((py + ph) / screenHeight) * 2.0f;

        float q[] = {
            x0, y1, color.r, color.g, color.b, color.a,
            x1, y1, color.r, color.g, color.b, color.a,
            x1, y0, color.r, color.g, color.b, color.a,

            x0, y1, color.r, color.g, color.b, color.a,
            x1, y0, color.r, color.g, color.b, color.a,
            x0, y0, color.r, color.g, color.b, color.a
        };
        slotVertices.insert(slotVertices.end(), std::begin(q), std::end(q));
    };

    if (customTextureID == 0) {
        for (int i = 0; i < slotCount; ++i) {
            float sx = std::round(pixelStartX + i * (actualSlotSize + actualPadding));
            float sy = std::round(pixelStartY);
            float border = 3.0f;

            glm::vec4 borderColor = (i == activeSlot) ? glm::vec4(1.0f, 0.85f, 0.1f, 1.0f) : glm::vec4(0.25f, 0.25f, 0.25f, 0.8f);
            addPixelQuad(sx - border, sy - border, actualSlotSize + (border * 2.0f), actualSlotSize + (border * 2.0f), borderColor);
            addPixelQuad(sx, sy, actualSlotSize, actualSlotSize, glm::vec4(0.12f, 0.12f, 0.12f, 0.75f));
        }
    }

    int filteredCount = 0;
    for (int i = 1; i <= 255; ++i) {
        try {
            const auto& prop = BlockRegistry::getInstance().getBlock(static_cast<uint8_t>(i));
            if (prop.name.empty() || prop.name == "air") continue;
            if (matchesCreativeTab(prop, creativeTab)) {
                filteredCount++;
            }
        } catch (...) {
            break;
        }
    }

    if (isCreativeOpen) {
        int cols = 9;
        int rows = (filteredCount + cols - 1) / cols;
        if (rows < 3) rows = 3;

        float itemBoxSize = actualSlotSize;
        float spacing = 6.0f;

        creativeW = (cols * itemBoxSize) + ((cols + 1) * spacing) + 20.0f;
        creativeH = 60.0f + (rows * (itemBoxSize + spacing));
        creativeX = std::round((screenWidth - creativeW) * 0.5f);
        creativeY = std::round((screenHeight - creativeH) * 0.5f);

        float tabW = 100.0f;
        float tabH = 24.0f;
        float tabStartY = creativeY - tabH;
        for (int t = 0; t < 3; ++t) {
            float tx = creativeX + 16.0f + (t * (tabW + 6.0f));
            glm::vec4 tabColor = (creativeTab == t) ? glm::vec4(0.75f, 0.75f, 0.75f, 0.98f) : glm::vec4(0.5f, 0.5f, 0.5f, 0.85f);
            addPixelQuad(tx, tabStartY, tabW, tabH, tabColor);
            addPixelQuad(tx - 1.0f, tabStartY - 1.0f, tabW + 2.0f, tabH + 2.0f, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
        }

        addPixelQuad(creativeX, creativeY, creativeW, creativeH, glm::vec4(0.75f, 0.75f, 0.75f, 0.98f));
        addPixelQuad(creativeX - 2.0f, creativeY - 2.0f, creativeW + 4.0f, creativeH + 4.0f, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));

        float gridStartX = creativeX + 16.0f;
        float gridStartY = creativeY + 45.0f;

        int displayIndex = 0;
        for (int i = 1; i <= 255; ++i) {
            try {
                const auto& prop = BlockRegistry::getInstance().getBlock(static_cast<uint8_t>(i));
                if (prop.name.empty() || prop.name == "air") continue;

                if (matchesCreativeTab(prop, creativeTab)) {
                    int col = displayIndex % cols;
                    int row = displayIndex / cols;
                    float bx = std::round(gridStartX + col * (itemBoxSize + spacing));
                    float by = std::round(gridStartY + row * (itemBoxSize + spacing));

                    addPixelQuad(bx, by, itemBoxSize, itemBoxSize, glm::vec4(0.52f, 0.52f, 0.52f, 1.0f));
                    addPixelQuad(bx - 1.0f, by - 1.0f, itemBoxSize + 2.0f, itemBoxSize + 2.0f, glm::vec4(0.35f, 0.35f, 0.35f, 1.0f));
                    displayIndex++;
                }
            } catch (...) {
                break;
            }
        }
    }

    if (slotVAO == 0) glGenVertexArrays(1, &slotVAO);
    if (slotVBO == 0) glGenBuffers(1, &slotVBO);

    glBindVertexArray(slotVAO);
    glBindBuffer(GL_ARRAY_BUFFER, slotVBO);
    glBufferData(GL_ARRAY_BUFFER, slotVertices.size() * sizeof(float), slotVertices.data(), GL_DYNAMIC_DRAW);
    GLsizei stride = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    const float faceVertices[6][18] = {
        { 0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f,  -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f },
        {-0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,   0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f },
        {-0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,   0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f },
        {-0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,   0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f },
        {-0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f },
        { 0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,   0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f }
    };

    const float texCoords[12] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        1.0f, 1.0f,  0.0f, 1.0f,  0.0f, 0.0f
    };
    const float faceShading[6] = { 0.7f, 0.7f, 1.0f, 0.5f, 0.8f, 0.8f };

    auto addIcon = [&](uint8_t blockId, float centerX, float centerY, float boxSize) {
        if (blockId == 0) return;
        const BlockProperty& blockProp = BlockRegistry::getInstance().getBlock(blockId);
        if (blockProp.name.empty() || blockProp.name == "air") return;

        float scale = boxSize * 0.70f;

        if (blockProp.pattern == "plantlike") {
            float halfSize = scale * 0.5f;
            float qX0 = centerX - halfSize;
            float qX1 = centerX + halfSize;
            float qY0 = centerY - halfSize;
            float qY1 = centerY + halfSize;

            int texLayer = blockProp.faces[0].baseLayer >= 0 ? blockProp.faces[0].baseLayer : blockProp.faces[2].baseLayer;
            int rawOverlay = blockProp.faces[0].overlayLayer >= 0 ? blockProp.faces[0].overlayLayer : -1;
            float overlayLayer = (rawOverlay >= 0) ? static_cast<float>(rawOverlay) : -1.0f;
            float tintBase = blockProp.faces[0].tintBase ? 1.0f : 0.0f;
            float tintOverlay = blockProp.faces[0].tintOverlay ? 1.0f : 0.0f;

            glm::vec3 tint = blockProp.defaultTint;

            float plantVerts[78] = {
                qX0, qY1, -5.0f, tint.r, tint.g, tint.b,  0.0f, 1.0f, (float)texLayer, overlayLayer, tintBase, tintOverlay, 1.0f,
                qX1, qY1, -5.0f, tint.r, tint.g, tint.b,  1.0f, 1.0f, (float)texLayer, overlayLayer, tintBase, tintOverlay, 1.0f,
                qX1, qY0, -5.0f, tint.r, tint.g, tint.b,  1.0f, 0.0f, (float)texLayer, overlayLayer, tintBase, tintOverlay, 1.0f,

                qX0, qY1, -5.0f, tint.r, tint.g, tint.b,  0.0f, 1.0f, (float)texLayer, overlayLayer, tintBase, tintOverlay, 1.0f,
                qX1, qY0, -5.0f, tint.r, tint.g, tint.b,  1.0f, 0.0f, (float)texLayer, overlayLayer, tintBase, tintOverlay, 1.0f,
                qX0, qY0, -5.0f, tint.r, tint.g, tint.b,  0.0f, 0.0f, (float)texLayer, overlayLayer, tintBase, tintOverlay, 1.0f
            };

            for (float f : plantVerts) {
                iconVertices.push_back(f);
            }
            return;
        }

        glm::mat4 iconTransform = glm::rotate(glm::mat4(1.0f), glm::radians(35.264f), glm::vec3(1, 0, 0));
        iconTransform = glm::rotate(iconTransform, glm::radians(45.0f), glm::vec3(0, 1, 0));
        iconTransform = glm::scale(iconTransform, glm::vec3(scale));

        for (int face = 0; face < 6; ++face) {
            int targetFaceIdx = face;
            if (face == 2 && blockProp.faces[3].baseLayer >= 0) targetFaceIdx = 3;
            else if (face == 3 && blockProp.faces[2].baseLayer >= 0) targetFaceIdx = 2;

            const FaceDefinition& faceDef = blockProp.faces[targetFaceIdx];
            int activeLayer = faceDef.baseLayer;

            int rawOverlay = faceDef.overlayLayer >= 0 ? faceDef.overlayLayer : -1;
            float overlayLayer = (rawOverlay >= 0) ? static_cast<float>(rawOverlay) : -1.0f;

            for (int v = 0; v < 6; ++v) {
                glm::vec3 localPos(faceVertices[face][v * 3 + 0], faceVertices[face][v * 3 + 1], faceVertices[face][v * 3 + 2]);
                glm::vec4 rotPos = iconTransform * glm::vec4(localPos, 1.0f);

                float screenX = centerX + rotPos.x;
                float screenY = centerY + rotPos.y;

                iconVertices.push_back(screenX);
                iconVertices.push_back(screenY);
                iconVertices.push_back(-5.0f + (rotPos.z * 0.01f));

                glm::vec3 tint = blockProp.defaultTint;
                iconVertices.push_back(tint.r);
                iconVertices.push_back(tint.g);
                iconVertices.push_back(tint.b);

                iconVertices.push_back(texCoords[v * 2 + 0]);
                iconVertices.push_back(texCoords[v * 2 + 1]);

                iconVertices.push_back(static_cast<float>(activeLayer));
                iconVertices.push_back(overlayLayer);
                iconVertices.push_back(faceDef.tintBase ? 1.0f : 0.0f);
                iconVertices.push_back(faceDef.tintOverlay ? 1.0f : 0.0f);
                iconVertices.push_back(faceShading[face]);
            }
        }
    };

    for (int i = 0; i < slotCount; ++i) {
        float sx = std::round(pixelStartX + i * (actualSlotSize + actualPadding));
        float sy = std::round(pixelStartY);
        addIcon(slots[i].blockId, sx + (actualSlotSize * 0.5f), sy + (actualSlotSize * 0.5f), actualSlotSize);
    }

    if (isCreativeOpen) {
        float gridStartX = creativeX + 16.0f;
        float gridStartY = creativeY + 45.0f;
        float itemBoxSize = actualSlotSize;
        float spacing = 6.0f;
        int cols = 9;

        int displayIndex = 0;
        for (int i = 1; i <= 255; ++i) {
            uint8_t candidateId = static_cast<uint8_t>(i);
            const auto& prop = BlockRegistry::getInstance().getBlock(candidateId);
            if (prop.name.empty() || prop.name == "air") continue;

            if (matchesCreativeTab(prop, creativeTab)) {
                int col = displayIndex % cols;
                int row = displayIndex / cols;
                float bx = std::round(gridStartX + col * (itemBoxSize + spacing));
                float by = std::round(gridStartY + row * (itemBoxSize + spacing));

                addIcon(candidateId, bx + (itemBoxSize * 0.5f), by + (itemBoxSize * 0.5f), itemBoxSize);
                displayIndex++;
            }
        }
    }

    iconVertexCount = static_cast<unsigned int>(iconVertices.size() / 13);

    if (iconVAO == 0) glGenVertexArrays(1, &iconVAO);
    if (iconVBO == 0) glGenBuffers(1, &iconVBO);

    if (iconVertexCount > 0) {
        glBindVertexArray(iconVAO);
        glBindBuffer(GL_ARRAY_BUFFER, iconVBO);
        glBufferData(GL_ARRAY_BUFFER, iconVertices.size() * sizeof(float), iconVertices.data(), GL_DYNAMIC_DRAW);

        GLsizei vStride = 13 * sizeof(float);
        for (int j = 0; j < 8; ++j) glEnableVertexAttribArray(j);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vStride, (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vStride, (void*)(3 * sizeof(float)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vStride, (void*)(6 * sizeof(float)));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, vStride, (void*)(8 * sizeof(float)));
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, vStride, (void*)(9 * sizeof(float)));
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, vStride, (void*)(10 * sizeof(float)));
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, vStride, (void*)(11 * sizeof(float)));
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, vStride, (void*)(12 * sizeof(float)));

        glBindVertexArray(0);
    }
}

void Hotbar::renderSlots(GLuint uiShader, GLuint texturedUIShader) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float screenWidth = static_cast<float>(viewport[2]);
    float screenHeight = static_cast<float>(viewport[3]);

    float actualSlotSize = slotSize <= 1.5f ? slotSize * screenHeight * 0.55f : slotSize;
    if (actualSlotSize > 60.0f) actualSlotSize = 60.0f;
    float totalWidth = slotCount * (actualSlotSize + padding);
    float px = std::round((screenWidth - totalWidth) * 0.5f);
    float py = std::round(screenHeight - actualSlotSize - posY - 20.0f);

    static GLuint texQuadVAO = 0;
    static GLuint texQuadVBO = 0;
    if (texQuadVAO == 0) {
        glGenVertexArrays(1, &texQuadVAO);
        glGenBuffers(1, &texQuadVBO);
    }

    if (customTextureID != 0) {
        glUseProgram(texturedUIShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, customTextureID);
        glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);

        float x0 = (px / screenWidth) * 2.0f - 1.0f;
        float y0 = 1.0f - ((py + actualSlotSize) / screenHeight) * 2.0f;
        float x1 = ((px + totalWidth) / screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - (py / screenHeight) * 2.0f;

        float quadVerts[] = {
            x0, y1,  0.0f, 1.0f,
            x1, y1,  1.0f, 1.0f,
            x1, y0,  1.0f, 0.0f,

            x0, y1,  0.0f, 1.0f,
            x1, y0,  1.0f, 0.0f,
            x0, y0,  0.0f, 0.0f
        };

        glBindVertexArray(texQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, texQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    if (selectedTextureID != 0) {
        glUseProgram(texturedUIShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, selectedTextureID);
        glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);

        float sx = std::round(px + activeSlot * (actualSlotSize + padding));
        float sy = std::round(py);
        float border = 4.0f; 

        float x0 = ((sx - border) / screenWidth) * 2.0f - 1.0f;
        float y0 = 1.0f - ((sy + actualSlotSize + border) / screenHeight) * 2.0f;
        float x1 = ((sx + actualSlotSize + border) / screenWidth) * 2.0f - 1.0f;
        float y1 = 1.0f - ((sy - border) / screenHeight) * 2.0f;

        float highlightVerts[] = {
            x0, y1,  0.0f, 1.0f,
            x1, y1,  1.0f, 1.0f,
            x1, y0,  1.0f, 0.0f,

            x0, y1,  0.0f, 1.0f,
            x1, y0,  1.0f, 0.0f,
            x0, y0,  0.0f, 0.0f
        };

        glBindVertexArray(texQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, texQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(highlightVerts), highlightVerts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    if (slotVAO != 0) {
        glUseProgram(uiShader);
        glBindVertexArray(slotVAO);

        int filteredCount = 0;
        for (int i = 1; i <= 255; ++i) {
            try {
                const auto& prop = BlockRegistry::getInstance().getBlock(static_cast<uint8_t>(i));
                if (prop.name.empty() || prop.name == "air") continue;
                if (matchesCreativeTab(prop, creativeTab)) filteredCount++;
            } catch (...) {
                break;
            }
        }

        int hotbarFallbackQuads = (customTextureID == 0) ? (slotCount * 2) : 0;
        int creativePanelQuads = isCreativeOpen ? (2 + 3) : 0;
        int creativeSlotQuads = isCreativeOpen ? filteredCount * 2 : 0;
        int totalQuads = hotbarFallbackQuads + creativePanelQuads + creativeSlotQuads;

        if (totalQuads > 0) {
            glDrawArrays(GL_TRIANGLES, 0, totalQuads * 6);
        }
        glBindVertexArray(0);
    }

    glEnable(GL_DEPTH_TEST);
}

void Hotbar::render3DIcons(GLuint voxelShader, GLuint textureArrayID) {
    if (iconVAO == 0 || iconVertexCount == 0) return;

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float width = static_cast<float>(viewport[2]);
    float height = static_cast<float>(viewport[3]);

    glm::mat4 orthoProj = glm::ortho(0.0f, width, height, 0.0f, -100.0f, 100.0f);
    glm::mat4 identity(1.0f);

    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    glUseProgram(voxelShader);

    GLint isGuiLoc = glGetUniformLocation(voxelShader, "u_IsGUI");
    if (isGuiLoc != -1) {
        glUniform1i(isGuiLoc, 1);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    glUniform1i(glGetUniformLocation(voxelShader, "textureArray"), 0);

    glUniformMatrix4fv(glGetUniformLocation(voxelShader, "model"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(glGetUniformLocation(voxelShader, "view"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(glGetUniformLocation(voxelShader, "projection"), 1, GL_FALSE, glm::value_ptr(orthoProj));

    glBindVertexArray(iconVAO);
    glDrawArrays(GL_TRIANGLES, 0, iconVertexCount);
    glBindVertexArray(0);

    if (isGuiLoc != -1) {
        glUniform1i(isGuiLoc, 0);
    }
}