#pragma once

#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <glad/glad.h>
#include <glm/glm.hpp>

struct GLFWwindow;
class FontRenderer;

class ChatSystem {
public:
    // Returns true if the command was handled (for example, by a Lua mod).
    using CommandHandler = std::function<bool(const std::string& command)>;

    ChatSystem() = default;
    ~ChatSystem();
    ChatSystem(const ChatSystem&) = delete;
    ChatSystem& operator=(const ChatSystem&) = delete;

    bool isOpen = false;
    std::string currentInput;
    size_t maxHistorySize = 100;   // lines kept in the log

    void openChat(GLFWwindow* window, int triggerKey);
    void toggleChat(GLFWwindow* window);

    void addMessage(const std::string& message);
    void addMessage(const std::string& message, const glm::vec3& color);

    void setCommandHandler(CommandHandler handler) { m_commandHandler = std::move(handler); }

    void handleCharInput(unsigned int codepoint);
    void handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window);

    void renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight);

private:
    struct ChatLine {
        std::string text;
        glm::vec3 color;
        double timeAdded;   // glfwGetTime() when the line was added
    };

    void closeChat(GLFWwindow* window);
    void sendCurrentInput(GLFWwindow* window);
    void executeCommand(const std::string& cmd);
    void recallHistory(int direction);
    void insertText(const std::string& text);
    void deleteBack();
    void deleteForward();
    void pushLine(const std::string& text, const glm::vec3& color);

    void ensureRectGpu();
    void drawRect(float x0, float y0, float x1, float y1, const glm::vec4& color);

    std::deque<ChatLine> m_lines;
    std::deque<std::string> m_sentHistory;
    int m_historyIndex = -1;          // -1 = not browsing history
    size_t m_cursor = 0;              // caret position inside currentInput
    bool suppressNextChar = false;    // ignores the character produced by the key that opened chat
    CommandHandler m_commandHandler;

    bool m_gpuReady = false;
    GLuint m_rectProgram = 0;
    GLuint m_rectVAO = 0;
    GLuint m_rectVBO = 0;
};