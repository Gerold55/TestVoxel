#pragma once

#include <string>
#include <deque>
#include <glad/glad.h>

struct GLFWwindow;
class FontRenderer;

class ChatSystem {
public:
    bool isOpen = false;
    std::string currentInput = "";
    std::deque<std::string> messageHistory;
    size_t maxHistorySize = 50;

    void toggleChat(GLFWwindow* window);
    void addMessage(const std::string& message);
    
    void handleCharInput(unsigned int codepoint);
    void handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window);

    // Accept uiProgram so FontRenderer gets the valid shader
    void renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight);

private:
    void executeCommand(const std::string& cmd);
};