#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "ChatSystem.h"
#include "FontRenderer.h"
#include <iostream>
#include <sstream>

namespace {
    constexpr size_t kMaxInputLength = 200;
}

// Opens the chat from a key press. The character for the same press arrives
// right after, so T's character is suppressed. "/" is not suppressed, so the
// character callback adds it once.
void ChatSystem::openChat(GLFWwindow* window, int triggerKey) {
    if (isOpen) return;
    isOpen = true;
    currentInput.clear();
    suppressNextChar = (triggerKey == GLFW_KEY_T);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void ChatSystem::toggleChat(GLFWwindow* window) {
    isOpen = !isOpen;
    suppressNextChar = false;
    if (isOpen) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        currentInput.clear();
    }
}

void ChatSystem::addMessage(const std::string& message) {
    messageHistory.push_back(message);
    if (messageHistory.size() > maxHistorySize) {
        messageHistory.pop_front();
    }
}

void ChatSystem::handleCharInput(unsigned int codepoint) {
    if (!isOpen) return;

    if (suppressNextChar) {
        suppressNextChar = false;
        return;
    }

    if (codepoint >= 32 && codepoint < 127 && currentInput.size() < kMaxInputLength) {
        currentInput += static_cast<char>(codepoint);
    }
}

void ChatSystem::handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window) {
    if (!isOpen) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    // Any key press while the chat is open means the trigger character has
    // already been handled, or it never arrives. Either way, stop suppressing.
    suppressNextChar = false;

    if (key == GLFW_KEY_BACKSPACE) {
        if (!currentInput.empty()) currentInput.pop_back();
    } else if (key == GLFW_KEY_ENTER) {
        if (!currentInput.empty()) {
            addMessage("> " + currentInput);
            executeCommand(currentInput);
            currentInput.clear();
        }
        toggleChat(window);
    } else if (key == GLFW_KEY_ESCAPE) {
        toggleChat(window);
    }
}

void ChatSystem::executeCommand(const std::string& cmd) {
    if (!cmd.empty() && cmd[0] == '/') {
        // Placeholder: command dispatch goes here (e.g. forwarding to Lua).
        addMessage("[System] Command executed: " + cmd);
    } else {
        addMessage(cmd);
    }
}

void ChatSystem::renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight) {
    if (!fontRenderer || fontShader == 0) return;

    // If chat is open, render the input prompt at the bottom
    if (isOpen) {
        std::string displayLine = "> " + currentInput + "_";
        fontRenderer->renderText(fontShader, displayLine, -0.95f, -0.85f, 0.05f,
               0.05f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
               Dimensions2D{screenWidth, screenHeight});
    }

    // Render message history
    int startIdx = 0;
    if (messageHistory.size() > 8) {
        startIdx = static_cast<int>(messageHistory.size()) - 8;
    }

    float startY = isOpen ? -0.75f : -0.85f;
    float lineSpacing = 0.06f;

    int lineCount = 0;
    for (size_t i = startIdx; i < messageHistory.size(); ++i) {
        float yPos = startY - (lineCount * lineSpacing);
        float alpha = isOpen ? 1.0f : 0.75f;
        fontRenderer->renderText(fontShader, messageHistory[i], -0.95f, yPos,
                0.045f, 0.045f, glm::vec4(1.0f, 1.0f, 1.0f, alpha),
                Dimensions2D{screenWidth, screenHeight});
        lineCount++;
    }
}