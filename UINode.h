#ifndef UINODE_H
#define UINODE_H

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

enum class NodeType { Panel, SlotGrid, Button, Label, Image };

struct UINode {
    std::string id;
    NodeType type = NodeType::Panel;
    
    // Bounds in screen space or relative pixels
    float x = 0.0f, y = 0.0f;
    float width = 0.0f, height = 0.0f;
    
    // Styling & Appearance
    glm::vec4 color{0.2f, 0.2f, 0.2f, 0.9f};
    std::string texturePath = "";
    
    // Inventory Grid Properties (for SlotGrids)
    int columns = 1;
    int rows = 1;
    int slotSize = 32;
    int padding = 4;
    std::string inventorySource = ""; // e.g., "player_inventory", "chest"

    // Hierarchy
    std::vector<UINode> children;
    
    // Interactive Callbacks
    bool isHovered = false;
    std::function<void()> onClick;
};

#endif // UINODE_H