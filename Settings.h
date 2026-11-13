#pragma once
// Key bindings from the Settings screen. Defined in main.cpp.
// Use these instead of glfwGetKey(window, GLFW_KEY_W) etc. so players can rebind controls.
//
// Actions: forward, back, left, right, jump, sneak, sprint, inventory, chat, command, hotbar1..hotbar9

#include <string>

struct GLFWwindow;

// The GLFW key currently bound to an action (GLFW_KEY_UNKNOWN if there's no such action).
int keyForAction(const std::string& action);

// True while the key bound to `action` is held down.
bool isActionDown(GLFWwindow* window, const std::string& action);
