#pragma once
// Chat window: message log, text input with history, "/" commands with suggestions.
// In multiplayer, typed messages are handed to the message sender (see main.cpp),
// which sends them to the server; otherwise they're shown locally.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;
class FontRenderer;

class ChatSystem {
public:
    ChatSystem();
    ~ChatSystem();

    // ---- Opening / input (called from the GLFW callbacks in main.cpp) ----
    void openChat(GLFWwindow* window, int triggerKey);
    void closeChat(GLFWwindow* window);
    void toggleChat(GLFWwindow* window);
    void handleCharInput(unsigned int codepoint);
    void handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window);

    // ---- Messages ----
    void addMessage(const std::string& message);                          // system colour
    void addMessage(const std::string& message, const glm::vec3& color);

    // ---- Commands ----
    // Shown in /help and the suggestion list.
    void registerCommand(const std::string& name, const std::string& usage, const std::string& description);
    // Gets every "/command" first (without the slash). Return true if it was handled.
    void setCommandHandler(std::function<bool(const std::string&)> handler) { m_commandHandler = std::move(handler); }

    // ---- Multiplayer ----
    // Name shown before your own messages ("<Name> hello").
    void setPlayerName(const std::string& name) { m_playerName = name; }
    const std::string& playerName() const { return m_playerName; }
    // Called with every typed (non-command) message. Return true if it was sent online
    // (the server echoes it back to everyone), false to show it locally instead.
    void setMessageSender(std::function<bool(const std::string&)> sender) { m_messageSender = std::move(sender); }

    void renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight);

    bool isOpen = false;
    bool suppressNextChar = false;   // skip the 'T' that opened the chat
    std::string currentInput;
    size_t maxHistorySize = 100;     // lines kept in the log

private:
    struct ChatLine {
        std::string text;
        glm::vec3 color;
        double timeAdded;
    };
    struct CommandInfo {
        std::string name;
        std::string usage;
        std::string description;
    };

    std::vector<const CommandInfo*> matchingCommands() const;
    void acceptSuggestion();
    void pushLine(const std::string& text, const glm::vec3& color);
    void insertText(const std::string& text);
    void deleteBack();
    void deleteForward();
    void recallHistory(int direction);
    void sendCurrentInput(GLFWwindow* window);
    void executeCommand(const std::string& cmd);
    void showHelp(const std::string& topic);
    void ensureRectGpu();

    std::deque<ChatLine> m_lines;
    std::deque<std::string> m_sentHistory;
    std::vector<CommandInfo> m_commands;
    std::function<bool(const std::string&)> m_commandHandler;
    std::function<bool(const std::string&)> m_messageSender;
    std::string m_playerName = "Player";

    size_t m_cursor = 0;
    int m_historyIndex = -1;
    int m_suggestSel = 0;

    bool m_gpuReady = false;
    GLuint m_rectProgram = 0;
    GLuint m_rectVAO = 0;
    GLuint m_rectVBO = 0;
};
