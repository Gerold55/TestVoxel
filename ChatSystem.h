#pragma once
#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;
namespace sol {
    class state;
}
class FontRenderer;

struct ChatMessage {
    std::string text;
    float timestamp;
};

class ChatSystem {
public:
    bool isOpen = false;
    std::string currentInput = "";
    std::vector<ChatMessage> history;
    std::vector<std::string> commandHistory;

    void toggleChat(GLFWwindow* window);
    void handleCharInput(unsigned int codepoint);
    void handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window);
    void executeCommand(const std::string& cmd, sol::state& lua);
    void renderChatOverlay(FontRenderer* fontRenderer, int windowWidth, int windowHeight);
};