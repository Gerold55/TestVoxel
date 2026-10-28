#ifndef MAINMENU_H
#define MAINMENU_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <sol/sol.hpp>
#include <vector>
#include <string>
#include "FontRenderer.h"

struct SubgameInfo {
    std::string folderName;
    std::string title;
    std::string author;
    std::string description;
    GLuint headerTextureID = 0; // Loaded from games/<folder>/menu/header.png
};

struct MenuButton {
    std::string id;
    std::string label;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool isHovered = false;
};

class MainMenu {
public:
    std::vector<SubgameInfo> availableGames;
    int selectedGameIndex = 0;

    std::vector<MenuButton> buttons;

    GLuint vao = 0, vbo = 0;
    GLuint headerVAO = 0, headerVBO = 0;

    MainMenu();
    ~MainMenu();

    void scanSubgames();
    void updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight);
    std::string handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight);
    void render(GLuint uiShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight);
};

#endif // MAINMENU_H