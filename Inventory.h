#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <sol/sol.hpp>

enum class InventoryTab {
    BLOCKS = 1,
    NATURE = 2,
    ITEMS = 3
};

class InventorySystem {
public:
    bool creativeOpen = false;
    InventoryTab currentTab = InventoryTab::BLOCKS;

    void toggleCreative();
    void setCreativeOpen(bool open);
    bool isCreativeOpen() const;

    void renderCreativeInventory(GLFWwindow* window, sol::state& lua, float width, float height, GLuint uiProgram, GLuint texturedUIProgram);
    void handleCreativeClick(double mx, double my, float width, float height);

private:
    std::vector<std::string> getCurrentItems();
};