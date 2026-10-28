#include "PauseMenu.h"
#include <iostream>

PauseMenu::PauseMenu() {}

PauseMenu::~PauseMenu() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
}

void PauseMenu::updateAndBuildBuffers(sol::state& lua, double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return;

    activeElements.clear();

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;
    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    sol::protected_function layoutFunc = lua["get_pause_menu_layout"];
    if (!layoutFunc.valid()) return;

    sol::protected_function_result result = layoutFunc(mouseX, mouseY, screenWidth, screenHeight);
    if (!result.valid()) return;

    sol::table elementsTable = result;
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

    for (auto& pair : elementsTable) {
        if (!pair.second.is<sol::table>()) continue;
        sol::table el = pair.second.as<sol::table>();

        std::string type = el.get_or("type", std::string("backdrop"));

        if (type == "backdrop") {
            sol::table col = el.get_or("color", sol::table());
            float r = col.valid() ? col.get_or(1, 0.0f) : 0.0f;
            float g = col.valid() ? col.get_or(2, 0.0f) : 0.0f;
            float b = col.valid() ? col.get_or(3, 0.0f) : 0.0f;
            float a = col.valid() ? col.get_or(4, 0.6f) : 0.6f;
            addQuad(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(r, g, b, a));
        } else if (type == "button" || type == "label") {
            LuaMenuElement elem;
            elem.id = el.get_or("id", std::string(""));
            elem.type = type;
            elem.label = el.get_or("label", el.get_or("text", std::string("")));
            elem.x = el.get_or("x", 0.0f);
            elem.y = el.get_or("y", 0.0f);
            elem.w = el.get_or("w", 0.4f);
            elem.h = el.get_or("h", 0.1f);
            elem.action = el["action"];

            float actualWidth = elem.w / aspect;
            elem.isHovered = (type == "button") && 
                             (ndcX >= elem.x && ndcX <= (elem.x + actualWidth) &&
                              ndcY >= elem.y && ndcY <= (elem.y + elem.h));

            activeElements.push_back(elem);

            if (type == "button") {
                float border = 0.005f;
                glm::vec4 bgColor = elem.isHovered ? glm::vec4(0.35f, 0.35f, 0.35f, 0.9f) : glm::vec4(0.18f, 0.18f, 0.18f, 0.85f);
                addQuad(elem.x, elem.y, actualWidth, elem.h, bgColor);

                glm::vec4 borderColor = elem.isHovered ? glm::vec4(1.0f, 0.84f, 0.0f, 0.95f) : glm::vec4(0.5f, 0.5f, 0.5f, 0.7f);
                addQuad(elem.x - border, elem.y - border, actualWidth + (border * 2.0f), elem.h + (border * 2.0f), borderColor);
            }
        }
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

void PauseMenu::handleMouseClick(sol::state& lua, double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return;

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;
    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    for (auto& el : activeElements) {
        if (el.type != "button") continue;

        float actualWidth = el.w / aspect;
        if (ndcX >= el.x && ndcX <= (el.x + actualWidth) &&
            ndcY >= el.y && ndcY <= (el.y + el.h)) {
            if (el.action.valid()) {
                el.action();
            }
        }
    }
}

void PauseMenu::render(GLuint uiShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight) {
    if (vao == 0) return;

    float aspect = (screenHeight > 0) ? static_cast<float>(screenWidth) / static_cast<float>(screenHeight) : 1.0f;

    // 1. Draw Quads and Dark Backdrop
    glDisable(GL_DEPTH_TEST);
    glUseProgram(uiShader);
    glBindVertexArray(vao);

    GLsizei totalVertices = 6;
    for (const auto& el : activeElements) {
        if (el.type == "button") totalVertices += 12;
    }

    glDrawArrays(GL_TRIANGLES, 0, totalVertices);
    glBindVertexArray(0);

    // 2. Render Text Glyphs for Labels and Buttons
    for (const auto& el : activeElements) {
        if (!el.label.empty()) {
            float textScaleX = (el.type == "label") ? 0.045f : 0.035f;
            float textScaleY = (el.type == "label") ? 0.06f : 0.05f;

            float actualWidth = el.w / aspect;
            float textWidth = el.label.length() * (textScaleX / aspect);

            float textX = el.x + (actualWidth - textWidth) / 2.0f;
            float textY = el.y + (el.h - textScaleY) / 2.0f;

            glm::vec4 textColor = (el.type == "label") ? glm::vec4(1.0f, 0.84f, 0.0f, 1.0f) :
                                  (el.isHovered ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));

            fontRenderer.renderText(fontShader, el.label, textX, textY, textScaleX, textScaleY, textColor, aspect);
        }
    }

    glEnable(GL_DEPTH_TEST);
}