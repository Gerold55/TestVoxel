#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "ChatSystem.h"
#include "FontRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxInputLength    = 200;
constexpr size_t kWrapChars         = 56;
constexpr size_t kInputVisibleChars = 56;
constexpr size_t kSentHistoryMax    = 50;
constexpr size_t kLinesClosed       = 10;
constexpr size_t kLinesOpen         = 14;
constexpr double kVisibleSeconds    = 10.0;
constexpr double kFadeSeconds       = 1.0;

const glm::vec3 kPlayerColor(1.0f, 1.0f, 1.0f);
const glm::vec3 kSystemColor(1.0f, 1.0f, 0.6f);
const glm::vec3 kErrorColor(1.0f, 0.55f, 0.55f);

const char* kRectVS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kRectFS = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

GLuint compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Chat] Shader error: " << log << std::endl;
    }
    return s;
}

std::vector<std::string> wrapText(const std::string& text, size_t maxChars) {
    std::vector<std::string> out;
    std::string rest = text;

    while (rest.size() > maxChars) {
        size_t cut = rest.rfind(' ', maxChars);
        if (cut == std::string::npos || cut == 0) cut = maxChars;
        out.push_back(rest.substr(0, cut));
        rest = rest.substr(cut);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }

    if (!rest.empty() || out.empty()) out.push_back(rest);
    return out;
}

} // namespace

ChatSystem::~ChatSystem() {
    if (glfwGetCurrentContext() == nullptr) return;
    if (m_rectVBO) glDeleteBuffers(1, &m_rectVBO);
    if (m_rectVAO) glDeleteVertexArrays(1, &m_rectVAO);
    if (m_rectProgram) glDeleteProgram(m_rectProgram);
}

void ChatSystem::openChat(GLFWwindow* window, int triggerKey) {
    if (isOpen) return;
    isOpen = true;
    currentInput.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    suppressNextChar = (triggerKey == GLFW_KEY_T);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void ChatSystem::closeChat(GLFWwindow* window) {
    isOpen = false;
    suppressNextChar = false;
    currentInput.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void ChatSystem::toggleChat(GLFWwindow* window) {
    if (isOpen) closeChat(window);
    else openChat(window, 0);
}

void ChatSystem::addMessage(const std::string& message) {
    addMessage(message, kSystemColor);
}

void ChatSystem::addMessage(const std::string& message, const glm::vec3& color) {
    for (const auto& piece : wrapText(message, kWrapChars)) {
        pushLine(piece, color);
    }
}

void ChatSystem::pushLine(const std::string& text, const glm::vec3& color) {
    if (text.empty()) return;
    m_lines.push_back(ChatLine{text, color, glfwGetTime()});
    while (m_lines.size() > maxHistorySize) m_lines.pop_front();
}

void ChatSystem::handleCharInput(unsigned int codepoint) {
    if (!isOpen) return;

    if (suppressNextChar) {
        suppressNextChar = false;
        return;
    }

    if (codepoint >= 32 && codepoint < 127) {
        insertText(std::string(1, static_cast<char>(codepoint)));
    }
}

void ChatSystem::insertText(const std::string& text) {
    for (char ch : text) {
        if (currentInput.size() >= kMaxInputLength) break;
        currentInput.insert(currentInput.begin() + static_cast<std::ptrdiff_t>(m_cursor), ch);
        ++m_cursor;
    }
}

void ChatSystem::deleteBack() {
    if (m_cursor == 0) return;
    currentInput.erase(m_cursor - 1, 1);
    --m_cursor;
}

void ChatSystem::deleteForward() {
    if (m_cursor >= currentInput.size()) return;
    currentInput.erase(m_cursor, 1);
}

void ChatSystem::handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window) {
    if (!isOpen) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    suppressNextChar = false;
    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    switch (key) {
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        sendCurrentInput(window);
        break;
    case GLFW_KEY_ESCAPE:
        closeChat(window);
        break;
    case GLFW_KEY_BACKSPACE:
        deleteBack();
        break;
    case GLFW_KEY_DELETE:
        deleteForward();
        break;
    case GLFW_KEY_LEFT:
        if (m_cursor > 0) --m_cursor;
        break;
    case GLFW_KEY_RIGHT:
        if (m_cursor < currentInput.size()) ++m_cursor;
        break;
    case GLFW_KEY_HOME:
        m_cursor = 0;
        break;
    case GLFW_KEY_END:
        m_cursor = currentInput.size();
        break;
    case GLFW_KEY_UP:
        recallHistory(+1);
        break;
    case GLFW_KEY_DOWN:
        recallHistory(-1);
        break;
    case GLFW_KEY_V:
        if (ctrl) {
            if (const char* clip = glfwGetClipboardString(window)) {
                std::string pasted;
                for (const char* p = clip; *p; ++p) {
                    if (*p >= 32 && *p < 127) pasted += *p;
                }
                insertText(pasted);
            }
        }
        break;
    default:
        break;
    }
}

void ChatSystem::recallHistory(int direction) {
    if (m_sentHistory.empty()) return;

    const int last = static_cast<int>(m_sentHistory.size()) - 1;
    m_historyIndex = std::clamp(m_historyIndex + direction, -1, last);

    if (m_historyIndex < 0) {
        currentInput.clear();
    } else {
        currentInput = m_sentHistory[m_sentHistory.size() - 1 - static_cast<size_t>(m_historyIndex)];
    }
    m_cursor = currentInput.size();
}

void ChatSystem::sendCurrentInput(GLFWwindow* window) {
    const std::string text = currentInput;

    if (!text.empty()) {
        if (m_sentHistory.empty() || m_sentHistory.back() != text) {
            m_sentHistory.push_back(text);
            while (m_sentHistory.size() > kSentHistoryMax) m_sentHistory.pop_front();
        }

        if (text[0] == '/') {
            executeCommand(text.substr(1));
        } else {
            addMessage("<Player> " + text, kPlayerColor);
        }
    }

    closeChat(window);
}

void ChatSystem::executeCommand(const std::string& cmd) {
    if (m_commandHandler && m_commandHandler(cmd)) return;

    const std::string name = cmd.substr(0, cmd.find(' '));

    if (name == "clear") {
        m_lines.clear();
        return;
    }
    if (name == "help") {
        addMessage("Built-in: /clear, /help. Mods add more through on_chat_command.", kSystemColor);
        return;
    }

    addMessage("Unknown command: /" + name, kErrorColor);
}

void ChatSystem::ensureRectGpu() {
    if (m_gpuReady) return;
    m_gpuReady = true;

    GLuint vs = compileStage(GL_VERTEX_SHADER, kRectVS);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, kRectFS);
    m_rectProgram = glCreateProgram();
    glAttachShader(m_rectProgram, vs);
    glAttachShader(m_rectProgram, fs);
    glLinkProgram(m_rectProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &m_rectVAO);
    glGenBuffers(1, &m_rectVBO);
    glBindVertexArray(m_rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_rectVBO);
    glBufferData(GL_ARRAY_BUFFER, 6 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ChatSystem::drawRect(float x0, float y0, float x1, float y1, const glm::vec4& color) {
    const float v[36] = {
        x0, y0, color.r, color.g, color.b, color.a,
        x1, y0, color.r, color.g, color.b, color.a,
        x1, y1, color.r, color.g, color.b, color.a,
        x0, y0, color.r, color.g, color.b, color.a,
        x1, y1, color.r, color.g, color.b, color.a,
        x0, y1, color.r, color.g, color.b, color.a,
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_rectProgram);
    glBindVertexArray(m_rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_rectVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void ChatSystem::renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight) {
    if (!fontRenderer || fontShader == 0) return;
    ensureRectGpu();

    const double now = glfwGetTime();
    const Dimensions2D screen{screenWidth, screenHeight};

    float boxLeft = static_cast<float>(screenWidth) * 0.05f;
    float boxWidth = static_cast<float>(screenWidth) * 0.9f;
    float boxHeight = 32.0f;
    float inputBottomY = static_cast<float>(screenHeight) - 100.0f;

    float linePixelHeight = 24.0f;
    float linesBaseY = inputBottomY;

    if (isOpen) {
        float ndcX0 = (boxLeft / screenWidth) * 2.0f - 1.0f;
        float ndcX1 = ((boxLeft + boxWidth) / screenWidth) * 2.0f - 1.0f;
        float ndcY0 = 1.0f - ((inputBottomY + boxHeight) / screenHeight) * 2.0f;
        float ndcY1 = 1.0f - (inputBottomY / screenHeight) * 2.0f;

        drawRect(ndcX0, ndcY0, ndcX1, ndcY1, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

        const size_t start = m_cursor > kInputVisibleChars ? m_cursor - kInputVisibleChars : 0;
        std::string display = currentInput.substr(start, kInputVisibleChars);
        const size_t caretPos = std::min(m_cursor - start, display.size());

        const bool caretOn = std::fmod(now, 1.0) < 0.5;
        display.insert(caretPos, caretOn ? "_" : " ");

        // Pass scale = 1.0f so font size maps cleanly to pixel units, and place Y near the vertical center of the input box
        fontRenderer->renderText(fontShader, display, boxLeft + 8.0f, inputBottomY + 22.0f,
                                 1.0f, 1.0f, glm::vec4(1.0f), screen);

        linesBaseY = inputBottomY - 4.0f;
    }

    const size_t maxLines = isOpen ? kLinesOpen : kLinesClosed;
    size_t row = 0;

    for (auto it = m_lines.rbegin(); it != m_lines.rend() && row < maxLines; ++it, ++row) {
        const double age = now - it->timeAdded;

        float alpha = 1.0f;
        if (!isOpen) {
            if (age >= kVisibleSeconds) break;
            if (age > kVisibleSeconds - kFadeSeconds) {
                alpha = static_cast<float>((kVisibleSeconds - age) / kFadeSeconds);
            }
        }

        float currentLineTop = linesBaseY - static_cast<float>(row + 1) * linePixelHeight;
        float currentLineBottom = currentLineTop + linePixelHeight - 2.0f;

        float ndcX0 = (boxLeft / screenWidth) * 2.0f - 1.0f;
        float ndcX1 = ((boxLeft + boxWidth) / screenWidth) * 2.0f - 1.0f;
        float ndcY0 = 1.0f - (currentLineBottom / screenHeight) * 2.0f;
        float ndcY1 = 1.0f - (currentLineTop / screenHeight) * 2.0f;

        const float bgAlpha = (isOpen ? 0.5f : 0.35f) * alpha;
        drawRect(ndcX0, ndcY0, ndcX1, ndcY1, glm::vec4(0.0f, 0.0f, 0.0f, bgAlpha));

        fontRenderer->renderText(fontShader, it->text, boxLeft + 8.0f, currentLineTop + 18.0f,
                                 1.0f, 1.0f, glm::vec4(it->color, alpha), screen);
    }
}