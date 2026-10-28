#include "MainMenu.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stb_image.h>

namespace fs = std::filesystem;

// Helper to load 2D UI image textures directly for subgame headers
static GLuint loadSubgameMenuTexture(const std::string& path) {
    if (!fs::exists(path)) return 0;

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
    if (!data) return 0;

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    return texID;
}

MainMenu::MainMenu() {
    scanSubgames();

    buttons.push_back({ "play", "Play Selected Game", -0.25f, -0.05f, 0.50f, 0.12f });
    buttons.push_back({ "cycle_game", "Select Subgame", -0.25f, -0.20f, 0.50f, 0.12f });
    buttons.push_back({ "quit", "Quit to Desktop", -0.25f, -0.35f, 0.50f, 0.12f });
}

MainMenu::~MainMenu() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (headerVAO != 0) glDeleteVertexArrays(1, &headerVAO);
    if (headerVBO != 0) glDeleteBuffers(1, &headerVBO);

    for (auto& game : availableGames) {
        if (game.headerTextureID != 0) glDeleteTextures(1, &game.headerTextureID);
    }
}

void MainMenu::scanSubgames() {
    availableGames.clear();
    std::string gamesPath = "games";
    if (!fs::exists(gamesPath) && fs::exists("../../games")) {
        gamesPath = "../../games";
    }

    if (fs::exists(gamesPath)) {
        for (const auto& entry : fs::directory_iterator(gamesPath)) {
            if (entry.is_directory()) {
                SubgameInfo info;
                info.folderName = entry.path().filename().string();
                info.title = info.folderName;

                // 1. Read game.conf or mod.conf metadata if available
                std::string confPath = entry.path().string() + "/game.conf";
                if (!fs::exists(confPath)) {
                    confPath = entry.path().string() + "/mod.conf";
                }

                if (fs::exists(confPath)) {
                    std::ifstream file(confPath);
                    std::string line;
                    while (std::getline(file, line)) {
                        size_t eqPos = line.find('=');
                        if (eqPos != std::string::npos) {
                            std::string key = line.substr(0, eqPos);
                            std::string value = line.substr(eqPos + 1);

                            // Trim whitespace
                            key.erase(0, key.find_first_not_of(" \t\r\n"));
                            key.erase(key.find_last_not_of(" \t\r\n") + 1);
                            value.erase(0, value.find_first_not_of(" \t\r\n"));
                            value.erase(value.find_last_not_of(" \t\r\n") + 1);

                            if (key == "title" || key == "name") info.title = value;
                            else if (key == "author") info.author = value;
                            else if (key == "description") info.description = value;
                        }
                    }
                }

                // 2. Load custom menu graphic from menu/header.png
                std::string headerPath = entry.path().string() + "/menu/header.png";
                info.headerTextureID = loadSubgameMenuTexture(headerPath);

                availableGames.push_back(info);
            }
        }
    }

    if (availableGames.empty()) {
        availableGames.push_back({ "craft_and_ruin", "Craft & Ruin", "Developer", "Default Game", 0 });
    }
}

void MainMenu::updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return;

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;
    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    std::vector<float> vertices;

    auto addQuad = [&](float x, float y, float w, float h, glm::vec4 color) {
        float q[] = {
            x,     y,     color.r, color.g, color.b, color.a,
            x + w, y,     color.r, color.g, color.b, color.a,
            x + w, y + h, color.r, color.g, color.b, color.a,

            x,     y,     color.r, color.g, color.b, color.a,
            x + w, y + h, color.r, color.g, color.b, color.a,
            x,     y + h, color.r, color.g, color.b, color.a
        };
        vertices.insert(vertices.end(), std::begin(q), std::end(q));
    };

    // Dark Background Overlay
    addQuad(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));

    if (selectedGameIndex < static_cast<int>(availableGames.size())) {
        buttons[1].label = "Game: " + availableGames[selectedGameIndex].title;
    }

    // Render Buttons
    for (auto& btn : buttons) {
        float actualWidth = btn.w / aspect;
        btn.isHovered = (ndcX >= btn.x && ndcX <= (btn.x + actualWidth) &&
                         ndcY >= btn.y && ndcY <= (btn.y + btn.h));

        float border = 0.005f;
        glm::vec4 bgColor = btn.isHovered ? glm::vec4(0.35f, 0.35f, 0.35f, 0.95f) : glm::vec4(0.20f, 0.20f, 0.20f, 0.90f);
        addQuad(btn.x, btn.y, actualWidth, btn.h, bgColor);

        glm::vec4 borderColor = btn.isHovered ? glm::vec4(1.0f, 0.84f, 0.0f, 1.0f) : glm::vec4(0.5f, 0.5f, 0.5f, 0.8f);
        addQuad(btn.x - border, btn.y - border, actualWidth + (border * 2.0f), btn.h + (border * 2.0f), borderColor);
    }

    if (vao == 0) glGenVertexArrays(1, &vao);
    if (vbo == 0) glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    GLsizei stride = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

std::string MainMenu::handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return "";

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;
    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    for (auto& btn : buttons) {
        float actualWidth = btn.w / aspect;
        if (ndcX >= btn.x && ndcX <= (btn.x + actualWidth) &&
            ndcY >= btn.y && ndcY <= (btn.y + btn.h)) {
            
            if (btn.id == "play") {
                return "play";
            } else if (btn.id == "cycle_game") {
                if (!availableGames.empty()) {
                    selectedGameIndex = (selectedGameIndex + 1) % availableGames.size();
                }
                return "cycle";
            } else if (btn.id == "quit") {
                return "quit";
            }
        }
    }
    return "";
}

void MainMenu::render(GLuint uiShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight) {
    if (vao == 0) return;

    float aspect = (screenHeight > 0) ? static_cast<float>(screenWidth) / static_cast<float>(screenHeight) : 1.0f;

    // 1. Draw Quads and Background
    glDisable(GL_DEPTH_TEST);
    glUseProgram(uiShader);
    glBindVertexArray(vao);

    GLsizei totalVertices = 6 + static_cast<GLsizei>(buttons.size() * 12);
    glDrawArrays(GL_TRIANGLES, 0, totalVertices);
    glBindVertexArray(0);

    // 2. Draw Title Header (Image or Fallback Text)
    if (selectedGameIndex < static_cast<int>(availableGames.size())) {
        const auto& game = availableGames[selectedGameIndex];

        if (game.headerTextureID != 0) {
            // Draw Subgame Header Graphic
            float headerW = 0.8f / aspect;
            float headerH = 0.25f;
            float headerX = -headerW / 2.0f;
            float headerY = 0.25f;

            float quadVerts[] = {
                headerX,           headerY,           0.0f, 0.0f,
                headerX + headerW, headerY,           1.0f, 0.0f,
                headerX + headerW, headerY + headerH, 1.0f, 1.0f,

                headerX,           headerY,           0.0f, 0.0f,
                headerX + headerW, headerY + headerH, 1.0f, 1.0f,
                headerX,           headerY + headerH, 0.0f, 1.0f
            };

            if (headerVAO == 0) glGenVertexArrays(1, &headerVAO);
            if (headerVBO == 0) glGenBuffers(1, &headerVBO);

            glBindVertexArray(headerVAO);
            glBindBuffer(GL_ARRAY_BUFFER, headerVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);

            GLsizei stride = 4 * sizeof(float);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

            glUseProgram(fontShader);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, game.headerTextureID);
            glUniform1i(glGetUniformLocation(fontShader, "fontTexture"), 0);
            glUniform4f(glGetUniformLocation(fontShader, "textColor"), 1.0f, 1.0f, 1.0f, 1.0f);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        } else {
            // Text Fallback Header
            std::string titleText = game.title;
            float titleScaleX = 0.060f;
            float titleScaleY = 0.080f;
            float titleWidth = titleText.length() * (titleScaleX / aspect);
            fontRenderer.renderText(fontShader, titleText, -titleWidth / 2.0f, 0.35f, titleScaleX, titleScaleY, glm::vec4(1.0f, 0.84f, 0.0f, 1.0f), aspect);
        }
    }

    // 3. Render Button Labels
    for (const auto& btn : buttons) {
        if (!btn.label.empty()) {
            float textScaleX = 0.035f;
            float textScaleY = 0.05f;

            float actualWidth = btn.w / aspect;
            float textWidth = btn.label.length() * (textScaleX / aspect);

            float textX = btn.x + (actualWidth - textWidth) / 2.0f;
            float textY = btn.y + (btn.h - textScaleY) / 2.0f;

            glm::vec4 textColor = btn.isHovered ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            fontRenderer.renderText(fontShader, btn.label, textX, textY, textScaleX, textScaleY, textColor, aspect);
        }
    }

    glEnable(GL_DEPTH_TEST);
}