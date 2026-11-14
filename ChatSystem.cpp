#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "ChatSystem.h"
#include "FontRenderer.h"
#include <algorithm>
#include <cctype>
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
constexpr size_t kMaxSuggestions    = 6;
constexpr double kVisibleSeconds    = 10.0;
constexpr double kFadeSeconds       = 1.0;

// Pixels reserved at the bottom of the screen for the hotbar (bar height plus its
// bottom margin and any posY offset). The chat sits above this line.
constexpr float kHotbarReserve = 115.0f;

constexpr float kInputHeight    = 30.0f;
constexpr float kLinePixelH     = 24.0f;
constexpr float kTextInsetX     = 8.0f;
constexpr float kLineTextDropY  = 20.0f;    // text offset from the top of each log line
constexpr float kInputTextDropY = 25.0f;   // text offset from the top of the input bar

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

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimLeft(const std::string& s) {
    const size_t p = s.find_first_not_of(' ');
    return p == std::string::npos ? std::string() : s.substr(p);
}

// "/name usage" with no trailing space when usage is empty.
std::string commandLabel(const std::string& name, const std::string& usage) {
    return "/" + name + (usage.empty() ? std::string() : " " + usage);
}

} // namespace

ChatSystem::ChatSystem() {
    registerCommand("help", "[command]", "List commands, or show details for one.");
    registerCommand("clear", "", "Clear the chat log.");
}

ChatSystem::~ChatSystem() {
    if (glfwGetCurrentContext() == nullptr) return;
    if (m_rectVBO) glDeleteBuffers(1, &m_rectVBO);
    if (m_rectVAO) glDeleteVertexArrays(1, &m_rectVAO);
    if (m_rectProgram) glDeleteProgram(m_rectProgram);
}

void ChatSystem::registerCommand(const std::string& name, const std::string& usage,
                                 const std::string& description) {
    std::string key = toLower(name);
    if (!key.empty() && key[0] == '/') key.erase(0, 1);
    if (key.empty()) return;

    for (auto& c : m_commands) {
        if (c.name == key) {
            c.usage = usage;
            c.description = description;
            return;
        }
    }
    m_commands.push_back(CommandInfo{key, usage, description});
}

std::vector<const ChatSystem::CommandInfo*> ChatSystem::matchingCommands() const {
    std::vector<const CommandInfo*> out;
    if (!isOpen || currentInput.empty() || currentInput[0] != '/') return out;

    const std::string typed = toLower(currentInput.substr(1));
    if (typed.find(' ') != std::string::npos) return out;   // already past the command name

    for (const auto& c : m_commands) {
        if (c.name.compare(0, typed.size(), typed) == 0) out.push_back(&c);
    }
    return out;
}

void ChatSystem::acceptSuggestion() {
    const auto matches = matchingCommands();
    if (matches.empty()) return;

    const int n = static_cast<int>(matches.size());
    const int idx = std::clamp(m_suggestSel, 0, n - 1);
    currentInput = "/" + matches[static_cast<size_t>(idx)]->name + " ";
    m_cursor = currentInput.size();
    m_suggestSel = 0;
}

void ChatSystem::openChat(GLFWwindow* window, int triggerKey) {
    if (isOpen) return;
    isOpen = true;
    currentInput.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    m_suggestSel = 0;
    suppressNextChar = (triggerKey == GLFW_KEY_T);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void ChatSystem::closeChat(GLFWwindow* window) {
    isOpen = false;
    suppressNextChar = false;
    currentInput.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    m_suggestSel = 0;
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
    m_suggestSel = 0;
}

void ChatSystem::deleteBack() {
    if (m_cursor == 0) return;
    currentInput.erase(m_cursor - 1, 1);
    --m_cursor;
    m_suggestSel = 0;
}

void ChatSystem::deleteForward() {
    if (m_cursor >= currentInput.size()) return;
    currentInput.erase(m_cursor, 1);
    m_suggestSel = 0;
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

    case GLFW_KEY_TAB:
        acceptSuggestion();
        break;

    case GLFW_KEY_UP:
    case GLFW_KEY_DOWN: {
        // While suggestions are showing, Up/Down move the highlight. Otherwise they browse history.
        const auto matches = matchingCommands();
        if (!matches.empty()) {
            const int n = static_cast<int>(matches.size());
            const int step = (key == GLFW_KEY_UP) ? -1 : 1;
            const int cur = std::clamp(m_suggestSel, 0, n - 1);
            m_suggestSel = (cur + step + n) % n;
        } else {
            recallHistory(key == GLFW_KEY_UP ? +1 : -1);
        }
        break;
    }

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
    m_suggestSel = 0;
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
        } else if (m_messageSender && m_messageSender(text)) {
            // Online: the server sends it to everyone (including us), so nothing to add here
        } else {
            const std::string name = m_playerName.empty() ? std::string("Player") : m_playerName;
            addMessage("<" + name + "> " + text, kPlayerColor);
        }
    }

    closeChat(window);
}

// Lua gets the first look. Built-ins are the fallback.
void ChatSystem::executeCommand(const std::string& cmd) {
    if (m_commandHandler && m_commandHandler(cmd)) return;

    const size_t sp = cmd.find(' ');
    const std::string name = toLower(cmd.substr(0, sp));
    const std::string args = (sp == std::string::npos) ? std::string() : trimLeft(cmd.substr(sp + 1));

    if (name.empty()) return;

    if (name == "help") {
        showHelp(args);
        return;
    }
    if (name == "clear") {
        m_lines.clear();
        return;
    }

    addMessage("Unknown command: /" + name + " (type /help for a list)", kErrorColor);
}

void ChatSystem::showHelp(const std::string& topicRaw) {
    const std::string topic = toLower(topicRaw);

    if (topic.empty()) {
        addMessage("Type / to see suggestions. Press Tab to complete, Up/Down to choose.", kSystemColor);

        std::vector<const CommandInfo*> sorted;
        for (const auto& c : m_commands) sorted.push_back(&c);
        std::sort(sorted.begin(), sorted.end(),
                  [](const CommandInfo* a, const CommandInfo* b) { return a->name < b->name; });

        for (const CommandInfo* c : sorted) {
            addMessage(commandLabel(c->name, c->usage) + "  -  " + c->description, kSystemColor);
        }
        addMessage("Use /help <command> for details.", kSystemColor);
        return;
    }

    std::string key = topic;
    if (!key.empty() && key[0] == '/') key.erase(0, 1);

    for (const auto& c : m_commands) {
        if (c.name == key) {
            addMessage(commandLabel(c.name, c.usage), kSystemColor);
            addMessage(c.description, kSystemColor);
            return;
        }
    }
    addMessage("No command named '" + key + "'.", kErrorColor);
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
    glBufferData(GL_ARRAY_BUFFER, 36 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ChatSystem::renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight) {
    if (!fontRenderer || fontShader == 0) return;
    ensureRectGpu();

    // Use the framebuffer size for text, since it may differ from the logical size.
    GLFWwindow* currentWindow = glfwGetCurrentContext();
    int fbWidth = screenWidth, fbHeight = screenHeight;
    if (currentWindow) {
        glfwGetFramebufferSize(currentWindow, &fbWidth, &fbHeight);
    }
    const Dimensions2D screen{fbWidth, fbHeight};

    const float W = static_cast<float>(screenWidth);
    const float H = static_cast<float>(screenHeight);

    const float boxLeft  = W * 0.05f;
    const float boxWidth = W * 0.90f;

    // The chat's bottom edge sits just above the hotbar.
    const float chatBottom = H - kHotbarReserve;
    const float inputTopY  = chatBottom - kInputHeight;

    struct RectTask { float x0, y0, x1, y1; glm::vec4 color; };
    struct TextTask { std::string text; float x, y; glm::vec4 color; };

    std::vector<RectTask> rectTasks;
    std::vector<TextTask> textTasks;

    // Pixel rectangle (top-left origin) converted to NDC.
    auto addRectTask = [&](float px, float py, float pw, float ph, glm::vec4 col) {
        const float x0 = (px / W) * 2.0f - 1.0f;
        const float x1 = ((px + pw) / W) * 2.0f - 1.0f;
        const float y0 = 1.0f - ((py + ph) / H) * 2.0f;
        const float y1 = 1.0f - (py / H) * 2.0f;
        rectTasks.push_back({x0, y0, x1, y1, col});
    };

    const double now = glfwGetTime();

    // Input bar and caret.
    if (isOpen) {
        addRectTask(boxLeft, inputTopY, boxWidth, kInputHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

        const size_t start = m_cursor > kInputVisibleChars ? m_cursor - kInputVisibleChars : 0;
        std::string display = currentInput.substr(start, kInputVisibleChars);
        const size_t caretPos = std::min(m_cursor - start, display.size());

        const bool caretOn = std::fmod(now, 1.0) < 0.5;
        display.insert(caretPos, caretOn ? "_" : " ");

        textTasks.push_back({display, boxLeft + kTextInsetX, inputTopY + kInputTextDropY, glm::vec4(1.0f)});
    }

    // Suggestion list, stacked directly above the input bar.
    const auto matches = matchingCommands();
    const int shown = static_cast<int>(std::min(matches.size(), kMaxSuggestions));
    if (shown > 0) {
        m_suggestSel = std::clamp(m_suggestSel, 0, shown - 1);
    }
    const float suggestTop = inputTopY - static_cast<float>(shown) * kLinePixelH;

    for (int i = 0; i < shown; ++i) {
        const CommandInfo* c = matches[static_cast<size_t>(i)];
        const bool selected = (i == m_suggestSel);
        const float y = suggestTop + static_cast<float>(i) * kLinePixelH;

        addRectTask(boxLeft, y, boxWidth, kLinePixelH,
                    selected ? glm::vec4(0.25f, 0.25f, 0.35f, 0.9f) : glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

        const std::string label = commandLabel(c->name, c->usage) + "  -  " + c->description;
        textTasks.push_back({label, boxLeft + kTextInsetX, y + kLineTextDropY,
                             selected ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f)});
    }

    // Log lines stack upward from the suggestions when open, or from the hotbar line when closed.
    const float baseY = isOpen ? suggestTop : chatBottom;
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

        const float lineTop = baseY - static_cast<float>(row + 1) * kLinePixelH;
        const float bgAlpha = (isOpen ? 0.5f : 0.35f) * alpha;

        addRectTask(boxLeft, lineTop, boxWidth, kLinePixelH, glm::vec4(0.0f, 0.0f, 0.0f, bgAlpha));
        textTasks.push_back({it->text, boxLeft + kTextInsetX, lineTop + kLineTextDropY,
                             glm::vec4(it->color, alpha)});
    }

    // Save state we change so the rest of the frame is unaffected.
    const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWasOn  = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Phase 1: background rectangles.
    glUseProgram(m_rectProgram);
    glBindVertexArray(m_rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_rectVBO);

    for (const auto& rect : rectTasks) {
        const float v[36] = {
            rect.x0, rect.y0, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
            rect.x1, rect.y0, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
            rect.x1, rect.y1, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
            rect.x0, rect.y0, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
            rect.x1, rect.y1, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
            rect.x0, rect.y1, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);

    // Phase 2: text. FontRenderer takes pixel coordinates, top-left origin.
    glUseProgram(fontShader);

    const float scaleX = (W > 0.0f) ? static_cast<float>(fbWidth) / W : 1.0f;
    const float scaleY = (H > 0.0f) ? static_cast<float>(fbHeight) / H : 1.0f;

    for (const auto& task : textTasks) {
        fontRenderer->renderText(fontShader, task.text,
                                 task.x * scaleX, task.y * scaleY,
                                 1.0f, 1.0f, task.color, screen);
    }

    if (cullWasOn) glEnable(GL_CULL_FACE);
    if (depthWasOn) glEnable(GL_DEPTH_TEST);
}
