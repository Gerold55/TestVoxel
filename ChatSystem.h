#pragma once

#include <string>
#include <deque>
#include <vector>
#include <functional>
#include <utility>
#include <glad/glad.h>
#include <glm/glm.hpp>

struct GLFWwindow;
class FontRenderer;

class ChatSystem {
public:
    // Returns true if the command was handled (for example, by a Lua mod).
    using CommandHandler = std::function<bool(const std::string& command)>;

    struct CommandInfo {
        std::string name;         // without the slash, lowercase
        std::string usage;        // e.g. "<day|night>", may be empty
        std::string description;  // shown in suggestions and /help
    };

    ChatSystem();
    ~ChatSystem();
    ChatSystem(const ChatSystem&) = delete;
    ChatSystem& operator=(const ChatSystem&) = delete;

    bool isOpen = false;
    std::string currentInput;
    size_t maxHistorySize = 100;

    void openChat(GLFWwindow* window, int triggerKey);
    void toggleChat(GLFWwindow* window);

    void addMessage(const std::string& message);
    void addMessage(const std::string& message, const glm::vec3& color);

    void setCommandHandler(CommandHandler handler) { m_commandHandler = std::move(handler); }

    // Adds a command to the suggestion list and /help. Registering an existing name replaces it.
    void registerCommand(const std::string& name, const std::string& usage, const std::string& description);

    void handleCharInput(unsigned int codepoint);
    void handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window);

    void renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight);

private:
    struct ChatLine {
        std::string text;
        glm::vec3 color;
        double timeAdded;
    };

    void closeChat(GLFWwindow* window);
    void sendCurrentInput(GLFWwindow* window);
    void executeCommand(const std::string& cmd);
    void showHelp(const std::string& topic);
    void recallHistory(int direction);
    void insertText(const std::string& text);
    void deleteBack();
    void deleteForward();
    void pushLine(const std::string& text, const glm::vec3& color);

    // Commands whose names start with what's typed after the slash.
    std::vector<const CommandInfo*> matchingCommands() const;
    void acceptSuggestion();

    void ensureRectGpu();

    std::deque<ChatLine> m_lines;
    std::deque<std::string> m_sentHistory;
    std::vector<CommandInfo> m_commands;
    int m_historyIndex = -1;
    int m_suggestSel = 0;               // highlighted suggestion row
    size_t m_cursor = 0;
    bool suppressNextChar = false;
    CommandHandler m_commandHandler;

    bool m_gpuReady = false;
    GLuint m_rectProgram = 0;
    GLuint m_rectVAO = 0;
    GLuint m_rectVBO = 0;
};