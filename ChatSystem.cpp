#include "ChatSystem.h"
#include "FontRenderer.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <sol/sol.hpp>
#include <iostream>

void ChatSystem::toggleChat(GLFWwindow* window) {
    isOpen = !isOpen;
    if (isOpen) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        currentInput = "";
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void ChatSystem::handleCharInput(unsigned int codepoint) {
    if (isOpen) {
        if (codepoint >= 32 && codepoint <= 126) {
            currentInput += static_cast<char>(codepoint);
        }
    }
}

void ChatSystem::handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window) {
    if (!isOpen) return;

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_BACKSPACE && !currentInput.empty()) {
            currentInput.pop_back();
        }
        else if (key == GLFW_KEY_ENTER) {
            if (!currentInput.empty()) {
                history.push_back({"> " + currentInput, static_cast<float>(glfwGetTime())});
                commandHistory.push_back(currentInput);

                if (currentInput[0] == '/') {
                    history.push_back({"[System] Executed command.", static_cast<float>(glfwGetTime())});
                } else {
                    history.push_back({"[Player] " + currentInput, static_cast<float>(glfwGetTime())});
                }
                currentInput.clear();
            }
            isOpen = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else if (key == GLFW_KEY_ESCAPE) {
            isOpen = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            currentInput.clear();
        }
    }
}

void ChatSystem::renderChatOverlay(FontRenderer* fontRenderer, int windowWidth, int windowHeight) {
    if (!isOpen && history.empty()) return;

    // Use a basic 2D orthographic projection shader or direct OpenGL quad rendering
    // for the chat background box at the bottom-left of the screen.
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // If chat is open, draw input box and history. 
    // You can render text lines here using your FontRenderer or UI shader.
    // For example, displaying the active input:
    if (isOpen) {
        std::string displayText = "> " + currentInput + "_";
        // fontRenderer->renderText(displayText, 20.0f, windowHeight - 40.0f, 1.0f, glm::vec3(1.0f));
    }

    // Render last few history lines upwards from the bottom
    float startY = windowHeight - 70.0f;
    int count = 0;
    for (auto it = history.rbegin(); it != history.rend() && count < 8; ++it, ++count) {
        // fontRenderer->renderText(it->text, 20.0f, startY, 1.0f, glm::vec3(1.0f));
        startY -= 20.0f;
    }

    glEnable(GL_DEPTH_TEST);
}