#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "FontRenderer.h"

struct SubgameInfo {
    std::string folderName;
    std::string title;
    std::string author;
    std::string description;
    GLuint headerTextureID = 0;
    GLuint iconTextureID = 0;
};

struct MenuButton {
    std::string id;
    std::string label;
    float x, y, w, h;
    bool isHovered = false;
};

class MainMenu {
public:
    MainMenu();
    ~MainMenu();

    void scanSubgames();
    void updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight);
    std::string handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight);
    void render(GLuint uiShader, GLuint texturedUIShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight);

    std::vector<SubgameInfo> availableGames;
    int selectedGameIndex = 0;

private:
    std::vector<MenuButton> centerButtons;
    std::vector<MenuButton> topRightButtons;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint headerVAO = 0;
    GLuint headerVBO = 0;
    GLuint iconVAO = 0;
    GLuint iconVBO = 0;
};