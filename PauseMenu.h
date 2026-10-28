#ifndef PAUSEMENU_H
#define PAUSEMENU_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <sol/sol.hpp>
#include <vector>
#include <string>
#include "FontRenderer.h"

struct LuaMenuElement {
    std::string id;
    std::string type;
    std::string label;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool isHovered = false;
    sol::protected_function action;
};

class PauseMenu {
public:
    std::vector<LuaMenuElement> activeElements;

    GLuint vao = 0;
    GLuint vbo = 0;

    PauseMenu();
    ~PauseMenu();

    void updateAndBuildBuffers(sol::state& lua, double mouseX, double mouseY, int screenWidth, int screenHeight);
    void handleMouseClick(sol::state& lua, double mouseX, double mouseY, int screenWidth, int screenHeight);
    void render(GLuint uiShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight);
};

#endif // PAUSEMENU_H