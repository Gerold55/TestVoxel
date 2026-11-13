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

    // Registers the ui.* and hotbar.* Lua functions. Call once, before loading inventory.lua.
    void registerLuaBindings(sol::state& lua);

    // Drawing and input are forwarded to inventory.lua
    void renderCreativeInventory(GLFWwindow* window, sol::state& lua, float width, float height, GLuint uiProgram, GLuint texturedUIProgram);
    void handleCreativeClick(double mx, double my, float width, float height);
    void handleCreativeScroll(double yoffset);
    bool handleCreativeKey(int key, int action, int mods);
    bool handleCreativeChar(unsigned int codepoint);

    // Draws any Lua-driven screen (e.g. the pause menu) using the ui.* functions
    void renderLuaScreen(GLFWwindow* window, sol::state& lua, const char* luaFunction,
                         float width, float height, GLuint uiProgram, GLuint texturedUIProgram);

private:
    std::vector<std::string> getCurrentItems();
};