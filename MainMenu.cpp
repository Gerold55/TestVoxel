#include "MainMenu.h"
#include "FontRenderer.h"
#include "Types.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stb_image.h>

namespace fs = std::filesystem;

// External reference to the active game folder managed by the engine/Lua
extern std::string g_ActiveGameFolder;

static GLuint loadMenuTexture(const std::string& path) {
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

    // Center vertical button stack (Minecraft style)
    centerButtons.push_back({ "singleplayer", "Singleplayer", -0.30f,  0.08f, 0.60f, 0.08f });
    centerButtons.push_back({ "multiplayer",  "Multiplayer",  -0.30f, -0.02f, 0.60f, 0.08f });
    centerButtons.push_back({ "settings",     "Settings",     -0.30f, -0.12f, 0.60f, 0.08f });
    centerButtons.push_back({ "credits",      "Credits",      -0.30f, -0.22f, 0.60f, 0.08f });
    centerButtons.push_back({ "quit",         "Exit Game",    -0.30f, -0.32f, 0.60f, 0.08f });

    // Top-right header buttons
    topRightButtons.push_back({ "texturepacks", "Textures", 0.53f, 0.86f, 0.20f, 0.08f });
    topRightButtons.push_back({ "mods",         "Mods",     0.75f, 0.86f, 0.20f, 0.08f });
}

MainMenu::~MainMenu() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (headerVAO != 0) glDeleteVertexArrays(1, &headerVAO);
    if (headerVBO != 0) glDeleteBuffers(1, &headerVBO);
    if (iconVAO != 0) glDeleteVertexArrays(1, &iconVAO);
    if (iconVBO != 0) glDeleteBuffers(1, &iconVBO);

    for (auto& game : availableGames) {
        if (game.headerTextureID != 0) glDeleteTextures(1, &game.headerTextureID);
        if (game.iconTextureID != 0) glDeleteTextures(1, &game.iconTextureID);
    }
}

void MainMenu::scanSubgames() {
    availableGames.clear();
    std::string gamesPath = "games";
    if (!fs::exists(gamesPath) && fs::exists("../../games")) {
        gamesPath = "../../games";
    }

    std::cout << "[Menu] Scanning games path: " << fs::absolute(gamesPath) << std::endl;

    if (fs::exists(gamesPath)) {
        for (const auto& entry : fs::directory_iterator(gamesPath)) {
            if (entry.is_directory()) {
                SubgameInfo info;
                info.folderName = entry.path().filename().string();
                info.title = info.folderName;

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

                std::string headerPath = entry.path().string() + "/menu/header.png";

                std::string iconPath = entry.path().string() + "/menu/icon.png";
                if (!fs::exists(iconPath)) {
                    iconPath = entry.path().string() + "/icon.png";
                }

                info.headerTextureID = loadMenuTexture(headerPath);
                info.iconTextureID = loadMenuTexture(iconPath);

                availableGames.push_back(info);
            }
        }
    }

    if (availableGames.empty()) {
        availableGames.push_back({ "craft_and_ruin", "Craft & Ruin", "Developer", "Default Game", 0, 0 });
    }

    // Highlight the game that is actually loaded, not just the first folder found
    selectedGameIndex = 0;
    for (size_t i = 0; i < availableGames.size(); ++i) {
        if (availableGames[i].folderName == g_ActiveGameFolder) {
            selectedGameIndex = static_cast<int>(i);
            break;
        }
    }
}

static int g_TotalUIVertices = 0;

void MainMenu::updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return;

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;

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

    auto addBorderBox = [&](float x, float y, float w, float h, float thickness, glm::vec4 color) {
        addQuad(x, y + h - thickness, w, thickness, color);
        addQuad(x, y, w, thickness, color);
        addQuad(x, y, thickness, h, color);
        addQuad(x + w - thickness, y, thickness, h, color);
    };

    // 1. Dark background base color
    addQuad(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));

    // 2. Top bar background strip
    addQuad(-1.0f, 0.80f, 2.0f, 0.20f, glm::vec4(0.16f, 0.16f, 0.16f, 0.95f));

    // 3. Center buttons
    for (auto& btn : centerButtons) {
        btn.isHovered = (ndcX >= btn.x && ndcX <= (btn.x + btn.w) &&
                         ndcY >= btn.y && ndcY <= (btn.y + btn.h));

        glm::vec4 bgColor = btn.isHovered ? glm::vec4(0.35f, 0.35f, 0.45f, 0.95f) : glm::vec4(0.20f, 0.20f, 0.20f, 0.90f);

        float border = 0.003f;
        addQuad(btn.x - border, btn.y - border, btn.w + (border * 2.0f), btn.h + (border * 2.0f), glm::vec4(0.08f, 0.08f, 0.08f, 1.0f));
        addQuad(btn.x, btn.y, btn.w, btn.h, bgColor);
    }

    // 4. Top-right header buttons
    for (auto& btn : topRightButtons) {
        btn.isHovered = (ndcX >= btn.x && ndcX <= (btn.x + btn.w) &&
                         ndcY >= btn.y && ndcY <= (btn.y + btn.h));

        glm::vec4 bgColor = btn.isHovered ? glm::vec4(0.35f, 0.35f, 0.45f, 0.95f) : glm::vec4(0.20f, 0.20f, 0.20f, 0.90f);

        float border = 0.003f;
        addQuad(btn.x - border, btn.y - border, btn.w + (border * 2.0f), btn.h + (border * 2.0f), glm::vec4(0.08f, 0.08f, 0.08f, 1.0f));
        addQuad(btn.x, btn.y, btn.w, btn.h, bgColor);
    }

    // 5. Active subgame selection highlight
    float iconStartX = -0.92f;
    float iconBaseSize = 0.08f;
    float aspect = static_cast<float>(screenHeight) / static_cast<float>(screenWidth);
    float iconW = iconBaseSize * aspect;
    float iconH = iconBaseSize;
    float iconY = 0.80f + (0.20f - iconH) * 0.5f;

    if (selectedGameIndex >= 0 && selectedGameIndex < static_cast<int>(availableGames.size())) {
        float ix = iconStartX + static_cast<float>(selectedGameIndex) * (iconW + 0.04f);
        float pad = 0.005f;
        addBorderBox(ix - pad, iconY - pad, iconW + (pad * 2.0f), iconH + (pad * 2.0f), 0.003f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    g_TotalUIVertices = static_cast<int>(vertices.size() / 6);

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

    // 1. Subgame icon selection
    float iconStartX = -0.92f;
    float iconBaseSize = 0.08f;
    float aspect = static_cast<float>(screenHeight) / static_cast<float>(screenWidth);
    float iconW = iconBaseSize * aspect;
    float iconH = iconBaseSize;
    float iconY = 0.80f + (0.20f - iconH) * 0.5f;

    for (size_t i = 0; i < availableGames.size(); ++i) {
        float ix = iconStartX + static_cast<float>(i) * (iconW + 0.04f);
        if (ndcX >= ix && ndcX <= (ix + iconW) && ndcY >= iconY && ndcY <= (iconY + iconH)) {
            selectedGameIndex = static_cast<int>(i);
            g_ActiveGameFolder = availableGames[selectedGameIndex].folderName;
            std::cout << "[Menu] Switched active subgame to: " << g_ActiveGameFolder << std::endl;
            return "select_game";
        }
    }

    // 2. Center buttons
    for (auto& btn : centerButtons) {
        if (ndcX >= btn.x && ndcX <= (btn.x + btn.w) && ndcY >= btn.y && ndcY <= (btn.y + btn.h)) {
            if (btn.id == "singleplayer") return "play";
            if (btn.id == "multiplayer") return "multiplayer";
            if (btn.id == "settings") return "settings";
            if (btn.id == "credits") return "credits";
            if (btn.id == "quit") return "quit";
        }
    }

    // 3. Top-right buttons
    for (auto& btn : topRightButtons) {
        if (ndcX >= btn.x && ndcX <= (btn.x + btn.w) && ndcY >= btn.y && ndcY <= (btn.y + btn.h)) {
            if (btn.id == "mods") return "mods";
            if (btn.id == "texturepacks") return "texturepacks";
        }
    }

    return "";
}

void MainMenu::render(GLuint uiShader, GLuint texturedUIShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight) {
    if (vao == 0) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1. Background quads and buttons
    glUseProgram(uiShader);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, g_TotalUIVertices);
    glBindVertexArray(0);

    // 2. Subgame icons
    glUseProgram(texturedUIShader);
    float iconStartX = -0.92f;
    float iconBaseSize = 0.08f;
    float aspect = static_cast<float>(screenHeight) / static_cast<float>(screenWidth);
    float iconW = iconBaseSize * aspect;
    float iconH = iconBaseSize;
    float iconY = 0.80f + (0.20f - iconH) * 0.5f;

    for (size_t i = 0; i < availableGames.size(); ++i) {
        const auto& game = availableGames[i];
        if (game.iconTextureID != 0) {
            float ix = iconStartX + static_cast<float>(i) * (iconW + 0.04f);
            float quadVerts[] = {
                ix,         iconY,         0.0f, 0.0f,
                ix + iconW, iconY,         1.0f, 0.0f,
                ix + iconW, iconY + iconH, 1.0f, 1.0f,

                ix,         iconY,         0.0f, 0.0f,
                ix + iconW, iconY + iconH, 1.0f, 1.0f,
                ix,         iconY + iconH, 0.0f, 1.0f
            };

            if (iconVAO == 0) glGenVertexArrays(1, &iconVAO);
            if (iconVBO == 0) glGenBuffers(1, &iconVBO);

            glBindVertexArray(iconVAO);
            glBindBuffer(GL_ARRAY_BUFFER, iconVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);

            GLsizei stride = 4 * sizeof(float);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, game.iconTextureID);
            glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
    }

    // 3. Title header (image, or text fallback)
    if (selectedGameIndex < static_cast<int>(availableGames.size())) {
        const auto& game = availableGames[selectedGameIndex];

        if (game.headerTextureID != 0) {
            float headerW = 0.65f;
            float headerH = 0.18f;
            float headerX = -headerW / 2.0f;
            float headerY = 0.40f;

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

            glUseProgram(texturedUIShader);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, game.headerTextureID);
            glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        } else {
            std::string titleText = game.title;
            float pixelX = ((-0.3f + 1.0f) * 0.5f) * screenWidth;
            float pixelY = ((1.0f - 0.45f) * 0.5f) * screenHeight;
            fontRenderer.renderText(fontShader, titleText, pixelX, pixelY, 1.2f, 1.2f, glm::vec4(1.0f, 0.84f, 0.0f, 1.0f), Dimensions2D{screenWidth, screenHeight});
        }
    }

    // Tuning values for button labels, in pixels. Change these to nudge the text.
    constexpr float kLabelCharWidth = 14.0f;   // approximate advance per character at scale 1.0
    constexpr float kLabelNudgeX    = 0.0f;    // positive moves right
    constexpr float kLabelNudgeY    = 0.0f;    // positive moves down

    // Centered button labels. NDC spans 2 units across the screen, so sizes convert with * 0.5 * screen size.
    auto renderCenteredButtonText = [&](const ButtonInfo& btn, float fontScale) {
        if (btn.label.empty()) return;

        const float textWidth  = static_cast<float>(btn.label.length()) * kLabelCharWidth * fontScale;
        const float textHeight = 16.0f * fontScale;

        const float btnX = ((btn.x + 1.0f) * 0.5f) * screenWidth;
        const float btnY = ((1.0f - (btn.y + btn.h)) * 0.5f) * screenHeight;
        const float btnW = btn.w * 0.5f * screenWidth;
        const float btnH = btn.h * 0.5f * screenHeight;

        // Center the text box inside the button. pixelY is treated as the baseline.
        const float pixelX = btnX + (btnW - textWidth) * 0.5f + kLabelNudgeX;
        const float pixelY = btnY + (btnH - textHeight) * 0.5f + textHeight + kLabelNudgeY;

        glm::vec4 textColor = btn.isHovered ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f)
                                            : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        fontRenderer.renderText(fontShader, btn.label, pixelX, pixelY,
                                fontScale, fontScale, textColor,
                                Dimensions2D{screenWidth, screenHeight});
    };

    // 4. Center button labels
    for (const auto& btn : centerButtons) {
        renderCenteredButtonText(btn, 0.9f);
    }

    // 5. Top-right button labels
    for (const auto& btn : topRightButtons) {
        renderCenteredButtonText(btn, 0.85f);
    }

    // 6. Version and copyright
    fontRenderer.renderText(fontShader, "Craft & Ruin 5.17.0-dev", 15.0f, static_cast<float>(screenHeight) - 25.0f, 0.5f, 0.5f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), Dimensions2D{screenWidth, screenHeight});
    fontRenderer.renderText(fontShader, "(c) 2010-2026 Developer Studio", static_cast<float>(screenWidth) - 260.0f, static_cast<float>(screenHeight) - 25.0f, 0.5f, 0.5f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), Dimensions2D{screenWidth, screenHeight});

    glEnable(GL_DEPTH_TEST);
}
