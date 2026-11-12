#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declarations
class FontRenderer;
struct Dimensions2D;

struct ButtonInfo {
    std::string id;
    std::string label;
    float x, y, w, h;
    bool isHovered = false;
};

struct SubgameInfo {
    std::string folderName;
    std::string title;
    std::string author;
    std::string description;
    GLuint headerTextureID = 0;
    GLuint iconTextureID = 0;
};

class MainMenu {
public:
    MainMenu();
    ~MainMenu();

    void scanSubgames();
    void updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight);
    std::string handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight);
    void render(GLuint uiShader, GLuint texturedUIShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight);

    // Getters and setters for subgame integration & Lua bindings
    const std::vector<SubgameInfo>& getAvailableGames() const { return availableGames; }
    int getSelectedGameIndex() const { return selectedGameIndex; }
    void setSelectedGameIndex(int index) { selectedGameIndex = index; }

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint headerVAO = 0;
    GLuint headerVBO = 0;
    GLuint iconVAO = 0;
    GLuint iconVBO = 0;

    std::vector<ButtonInfo> centerButtons;
    std::vector<ButtonInfo> topRightButtons;
    std::vector<SubgameInfo> availableGames;
    int selectedGameIndex = 0;
};

#endif // MAIN_MENU_H