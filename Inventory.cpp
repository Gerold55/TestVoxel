#include "Inventory.h"
#include "Block.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include <iostream>
#include <algorithm>

extern Hotbar* g_Hotbar;
extern FontRenderer* g_FontRenderer;
extern GLuint g_TextureArrayID;

void InventorySystem::toggleCreative() {
    creativeOpen = !creativeOpen;
    std::cout << "[InventoryDebug] Creative Inventory Toggled -> " << (creativeOpen ? "OPEN" : "CLOSED") << std::endl;
}

void InventorySystem::setCreativeOpen(bool open) {
    creativeOpen = open;
    std::cout << "[InventoryDebug] Creative Inventory Set -> " << (creativeOpen ? "OPEN" : "CLOSED") << std::endl;
}

bool InventorySystem::isCreativeOpen() const {
    if (g_Hotbar && g_Hotbar->isCreativeOpen) {
        return true;
    }
    return creativeOpen;
}

std::vector<std::string> InventorySystem::getCurrentItems() {
    std::vector<std::string> allItems;
    for (uint8_t id = 1; id < 255; ++id) {
        std::string name = BlockRegistry::getInstance().getBlock(id).name;
        if (!name.empty() && name != "air") {
            allItems.push_back(name);
        }
    }

    if (allItems.empty()) {
        allItems = { "dirt", "stone", "oak_planks", "cobblestone", "sand", "gravel", "bricks", "obsidian", "oak_log", "short_grass", "flower_red", "hoe", "scythe", "stick" };
    }

    std::vector<std::string> filtered;
    for (const auto& name : allItems) {
        if (currentTab == InventoryTab::BLOCKS) {
            if (name != "water" && name != "flowing_water" && name != "short_grass" && 
                name != "flower_red" && name != "hoe" && name != "scythe" && 
                name != "stick" && name != "leaves" && name != "birch_leaves" && name != "sapling") {
                filtered.push_back(name);
            }
        } else if (currentTab == InventoryTab::NATURE) {
            if (name == "short_grass" || name == "flower_red" || name == "leaves" || name == "birch_leaves" || name == "sapling") {
                filtered.push_back(name);
            }
        } else if (currentTab == InventoryTab::ITEMS) {
            if (name == "hoe" || name == "scythe" || name == "stick") {
                filtered.push_back(name);
            }
        }
    }
    return filtered;
}

void InventorySystem::renderCreativeInventory(GLFWwindow* window, sol::state& lua, float width, float height, GLuint uiProgram, GLuint texturedUIProgram) {
    if (!isCreativeOpen()) return;

    // CRITICAL: Ensure depth test and culling are disabled for 2D UI rendering
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Helper lambda to draw 2D colored rectangles safely
    auto drawRect = [uiProgram](float x, float y, float w, float h, float sw, float sh, float r, float g, float b, float a) {
        if (sw == 0 || sh == 0) return;
        float x0 = (x / sw) * 2.0f - 1.0f;
        float y1 = 1.0f - (y / sh) * 2.0f;
        float x1 = ((x + w) / sw) * 2.0f - 1.0f;
        float y0 = 1.0f - ((y + h) / sh) * 2.0f;

        float vertices[36] = {
            x0, y0, r, g, b, a,   x1, y0, r, g, b, a,   x1, y1, r, g, b, a,
            x0, y0, r, g, b, a,   x1, y1, r, g, b, a,   x0, y1, r, g, b, a
        };

        static GLuint vao = 0, vbo = 0;
        if (vao == 0) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glUseProgram(uiProgram);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    };

    int sw = static_cast<int>(width), sh = static_cast<int>(height);

    // 1. Dim background overlay
    drawRect(0, 0, width, height, width, height, 0.0f, 0.0f, 0.0f, 0.5f);

    // 2. Window Panel Dimensions
    float win_w = 520.0f;
    float win_h = 380.0f;
    float win_x = (width - win_w) / 2.0f;
    float win_y = (height - win_h) / 2.0f;

    // Main Panel Background (Dark Gray)
    drawRect(win_x, win_y, win_w, win_h, width, height, 0.15f, 0.15f, 0.15f, 0.98f);

    // Title text
    if (g_FontRenderer) {
        Dimensions2D winSize{sw, sh};
        g_FontRenderer->renderText(uiProgram, "Creative Inventory [DEBUG MODE]", win_x + 16, win_y + 14, 1.0f, 1.0f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), winSize);
    }

    // 3. Render Tabs
    std::vector<std::string> tabNames = { "Blocks", "Nature", "Items" };
    float tab_w = 90.0f;
    float tab_h = 26.0f;
    float tab_y = win_y - tab_h + 2.0f;
    for (size_t i = 0; i < tabNames.size(); ++i) {
        float tab_x = win_x + 16.0f + static_cast<float>(i) * (tab_w + 6.0f);
        bool isActive = (static_cast<int>(currentTab) == static_cast<int>(i + 1));
        float bg = isActive ? 0.35f : 0.12f;

        drawRect(tab_x, tab_y, tab_w, tab_h, width, height, bg, bg, bg, 0.95f);

        if (g_FontRenderer) {
            Dimensions2D winSize{sw, sh};
            g_FontRenderer->renderText(uiProgram, tabNames[i], tab_x + 15, tab_y + 6, 0.9f, 0.9f, glm::vec4(1.0f), winSize);
        }
    }

    // 4. Render Item Slots & Icons
    float start_x = win_x + 24.0f;
    float start_y = win_y + 65.0f;
    float slot_size = 44.0f;
    float padding = 8.0f;
    int cols = 8;

    auto items = getCurrentItems();

    // DEBUG LOG: Print render pass confirmation once per frame
    static int frameCounter = 0;
    if (++frameCounter % 120 == 0) {
        std::cout << "[InventoryDebug] Rendering " << items.size() << " items in current tab." << std::endl;
    }

    // PASS 1: Draw all slot background boxes first (Gray background slots)
    for (size_t i = 0; i < items.size(); ++i) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        float sx = start_x + static_cast<float>(col) * (slot_size + padding);
        float sy = start_y + static_cast<float>(row) * (slot_size + padding);

        // Slot Box Background (Dark Gray)
        drawRect(sx, sy, slot_size, slot_size, width, height, 0.25f, 0.25f, 0.25f, 1.0f);
    }

    // PASS 2: Draw all item graphics/icons second (High-contrast MAGENTA foreground preview so it's obvious)
    for (size_t i = 0; i < items.size(); ++i) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        float sx = start_x + static_cast<float>(col) * (slot_size + padding);
        float sy = start_y + static_cast<float>(row) * (slot_size + padding);

        std::string blockName = items[i];

        // DEBUG COLORS: Make items bright Magenta (1.0, 0.0, 1.0) so they stand out clearly against gray slots
        float tr = 1.0f, tg = 0.0f, tb = 1.0f; 

        // Draw inner item preview box cleanly over the background slot
        drawRect(sx + 6.0f, sy + 6.0f, slot_size - 12.0f, slot_size - 12.0f, width, height, tr, tg, tb, 1.0f);

        // Tiny text abbreviation helper
        if (g_FontRenderer) {
            Dimensions2D winSize{sw, sh};
            std::string abbr = blockName.substr(0, 3);
            g_FontRenderer->renderText(uiProgram, abbr, sx + 4.0f, sy + slot_size - 14.0f, 0.6f, 0.6f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), winSize);
        }
    }
}

void InventorySystem::handleCreativeClick(double mx, double my, float width, float height) {
    if (!isCreativeOpen()) return;

    std::cout << "[InventoryDebug] Mouse Click at (" << mx << ", " << my << ")" << std::endl;

    float win_w = 520.0f;
    float win_h = 380.0f;
    float win_x = (width - win_w) / 2.0f;
    float win_y = (height - win_h) / 2.0f;

    // Check Tabs
    float tab_w = 90.0f;
    float tab_h = 26.0f;
    float tab_y = win_y - tab_h + 2.0f;
    for (int i = 0; i < 3; ++i) {
        float tab_x = win_x + 16.0f + static_cast<float>(i) * (tab_w + 6.0f);
        if (mx >= tab_x && mx <= (tab_x + tab_w) && my >= tab_y && my <= (tab_y + tab_h)) {
            currentTab = static_cast<InventoryTab>(i + 1);
            std::cout << "[InventoryDebug] Switched Tab to index: " << (i + 1) << std::endl;
            return;
        }
    }

    // Check Grid Items
    float start_x = win_x + 24.0f;
    float start_y = win_y + 65.0f;
    float slot_size = 44.0f;
    float padding = 8.0f;
    int cols = 8;

    auto items = getCurrentItems();
    for (size_t i = 0; i < items.size(); ++i) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        float sx = start_x + static_cast<float>(col) * (slot_size + padding);
        float sy = start_y + static_cast<float>(row) * (slot_size + padding);

        if (mx >= sx && mx <= (sx + slot_size) && my >= sy && my <= (sy + slot_size)) {
            std::cout << "[InventoryDebug] Clicked Item: " << items[i] << " at index " << i << std::endl;
            if (g_Hotbar) {
                int activeSlot = g_Hotbar->activeSlot;
                uint8_t blockId = BlockRegistry::getInstance().getBlockId(items[i]);
                g_Hotbar->setSlot(activeSlot, blockId, 64);
                std::cout << "[InventoryDebug] Assigned " << items[i] << " (ID: " << (int)blockId << ") to hotbar slot " << (activeSlot + 1) << std::endl;
            }
            return;
        }
    }
}