#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <exception>

#include "Types.h"
#include "Block.h"
#include "ChunkMesh.h"
#include "World.h"
#include "Player.h"
#include "Raycast.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include "ChatSystem.h"

namespace fs = std::filesystem;

std::string g_CurrentModPath = "";
std::string g_ActiveGameFolder = "craft_and_ruin";
std::string g_ActiveWorldName = "world_alpha";
Hotbar* g_Hotbar = nullptr;
FontRenderer* g_FontRenderer = nullptr;
ChatSystem* g_ChatSystem = nullptr;
float g_FieldOfView = 70.0f;

enum class GameState {
    MENU,
    PLAYING
};
GameState g_CurrentGameState = GameState::MENU;

struct GameInfo {
    std::string folderName;
    std::string title = "Unnamed Game";
    std::string author = "Unknown";
    std::string description = "";
};

GameInfo parseGameConfig(const fs::path& gameDirPath) {
    GameInfo info;
    info.folderName = gameDirPath.filename().string();
    
    fs::path confPath = gameDirPath / "game.conf";
    if (!fs::exists(confPath)) {
        info.title = info.folderName;
        return info;
    }

    std::ifstream file(confPath);
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        size_t delimiter = line.find('=');
        if (delimiter != std::string::npos) {
            std::string key = line.substr(0, delimiter);
            std::string val = line.substr(delimiter + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);

            if (key == "title") info.title = val;
            else if (key == "author") info.author = val;
            else if (key == "description") info.description = val;
        }
    }
    return info;
}

std::vector<GameInfo> getAvailableGames() {
    std::vector<GameInfo> games;
    std::string gamesDir = "games";
    if (!fs::exists(gamesDir) && fs::exists("../../games")) gamesDir = "../../games";

    if (fs::exists(gamesDir)) {
        for (const auto& entry : fs::directory_iterator(gamesDir)) {
            if (entry.is_directory()) {
                fs::path confPath = entry.path() / "game.conf";
                if (fs::exists(confPath)) {
                    games.push_back(parseGameConfig(entry.path()));
                }
            }
        }
    }

    if (games.empty()) {
        games.push_back({"craft_and_ruin", "Craft & Ruin", "Brandon", "Default Voxel Survival Game"});
    }
    return games;
}

std::vector<std::string> getAvailableWorlds() {
    std::vector<std::string> worlds;
    std::string savesDir = "saves";
    if (!fs::exists(savesDir) && fs::exists("../../saves")) savesDir = "../../saves";

    if (fs::exists(savesDir)) {
        for (const auto& entry : fs::directory_iterator(savesDir)) {
            if (entry.is_directory()) {
                worlds.push_back(entry.path().filename().string());
            }
        }
    }
    if (worlds.empty()) worlds.push_back("world_alpha");
    return worlds;
}

struct EngineContext {
    Player* player = nullptr;
    sol::state* lua = nullptr;
};

const int TEX_SIZE = 16;
const int MAX_TEXTURE_LAYERS = 256;
GLuint g_TextureArrayID = 0;
int g_NextTextureLayer = 0;

void initTextureArray() {
    glGenTextures(1, &g_TextureArrayID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, MAX_TEXTURE_LAYERS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

int loadTextureToLayer(const std::string& path) {
    std::string finalPath = path;

    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path
    };

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = p;
            found = true;
            break;
        }
    }

    if (!found) {
        std::vector<unsigned char> dummyData(16 * 16 * 4, 255);
        int layer = g_NextTextureLayer++;
        glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, dummyData.data());
        return layer;
    }

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(finalPath.c_str(), &width, &height, &nrChannels, 4);
    if (!data) return -1;

    int layer = g_NextTextureLayer++;
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return layer;
}

static uint8_t g_NextBlockId = 1;

void registerBlockFromLua(const std::string& name, float r, float g, float b, sol::object texObj, sol::object overlayObj, sol::optional<sol::table> propsTable) {
    BlockProperty block;
    block.id = g_NextBlockId++;
    block.name = name;
    block.defaultTint = glm::vec3(r, g, b);

    bool shouldTint = (name == "grass" || name == "short_grass");

    if (propsTable.has_value()) {
        sol::table props = propsTable.value();
        block.isTransparent = props.get_or("transparent", false);
        block.isWalkable = props.get_or("walkable", (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red"));
        block.isSelectable = props.get_or("selectable", true);
        block.hasGravity = props.get_or("gravity", false);
        block.pattern = props.get_or("pattern", std::string("cube"));
        shouldTint = props.get_or("tint", shouldTint);
    } else {
        block.isTransparent = (name == "water" || name == "flowing_water" || name == "leaves" || name == "birch_leaves" || name == "short_grass" || name == "flower_red");
        block.isWalkable = (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red");
        block.hasGravity = (name == "sand" || name == "gravel");
        block.pattern = (name == "short_grass" || name == "flower_red") ? "plantlike" : "cube";
    }

    if (texObj.is<std::string>()) {
        int layer = loadTextureToLayer(texObj.as<std::string>());
        for (int f = 0; f < 6; ++f) {
            block.faces[f].baseLayer = layer;
        }
        if (shouldTint) {
            if (name == "grass") {
                block.faces[2].tintBase = true;
            } else {
                for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true;
            }
        }
    } else if (texObj.is<sol::table>()) {
        sol::table t = texObj.as<sol::table>();
        sol::optional<std::string> top = t["top"];
        sol::optional<std::string> bottom = t["bottom"];
        sol::optional<std::string> sides = t["sides"];

        int topL = top.has_value() ? loadTextureToLayer(top.value()) : -1;
        int botL = bottom.has_value() ? loadTextureToLayer(bottom.value()) : -1;
        int sideL = sides.has_value() ? loadTextureToLayer(sides.value()) : -1;

        block.faces[2].baseLayer = topL;
        block.faces[3].baseLayer = botL;
        block.faces[0].baseLayer = block.faces[1].baseLayer = block.faces[4].baseLayer = block.faces[5].baseLayer = sideL;

        if (shouldTint) {
            if (name == "grass") {
                block.faces[2].tintBase = true;
            } else {
                for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true;
            }
        }
    }

    if (overlayObj.is<std::string>()) {
        int overlayLayer = loadTextureToLayer(overlayObj.as<std::string>());
        for (int f = 0; f < 6; ++f) {
            if (f != 2 && f != 3) {
                block.faces[f].overlayLayer = overlayLayer;
                block.faces[f].tintOverlay = true;
            }
        }
    } else if (overlayObj.is<sol::table>()) {
        sol::table ot = overlayObj.as<sol::table>();
        sol::optional<std::string> sideOverlay = ot["sides"];
        if (sideOverlay.has_value()) {
            int sideL = loadTextureToLayer(sideOverlay.value());
            for (int f = 0; f < 6; ++f) {
                if (f != 2 && f != 3) {
                    block.faces[f].overlayLayer = sideL;
                    block.faces[f].tintOverlay = true;
                }
            }
        }
    }

    BlockRegistry::getInstance().registerBlock(block);
}

uint8_t getBlockIdLua(const std::string& name) {
    return BlockRegistry::getInstance().getBlockId(name);
}

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aTintColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aBaseLayer;
layout (location = 4) in float aOverlayLayer;
layout (location = 5) in float aTintBase;
layout (location = 6) in float aTintOverlay;
layout (location = 7) in float aFaceShading;

out vec3 TintColor;
out vec2 TexCoord;
out float BaseLayer;
out float OverlayLayer;
out float TintBase;
out float TintOverlay;
out float LightIntensity;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 u_SunDir;
uniform int u_Unlit;
uniform float u_Time;

void main() {
    vec3 pos = aPos;
    if (aBaseLayer >= 0.0 && aFaceShading < 0.9f && pos.y - floor(pos.y) > 0.4f) {
        float wave = sin(u_Time * 3.5f + pos.x + pos.z) * 0.04f;
        pos.x += wave;
    }
    TintColor = aTintColor;
    TexCoord = aTexCoord;
    BaseLayer = aBaseLayer;
    OverlayLayer = aOverlayLayer;
    TintBase = aTintBase;
    TintOverlay = aTintOverlay;

    if (u_Unlit == 1) {
        LightIntensity = 1.0f;
    } else {
        float sunDot = max(dot(vec3(0.0, 1.0, 0.0), u_SunDir), 0.2f);
        LightIntensity = clamp(aFaceShading * (sunDot * 0.75f + 0.25f), 0.2f, 1.0f);
    }

    vec4 viewPos = view * model * vec4(pos, 1.0);
    FragPos = viewPos.xyz;
    gl_Position = projection * viewPos;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 TintColor;
in vec2 TexCoord;
in float BaseLayer;
in float OverlayLayer;
in float TintBase;
in float TintOverlay;
in float LightIntensity;
in vec3 FragPos;

out vec4 FragColorOut;
uniform sampler2DArray textureArray;
uniform int u_IsGUI;

void main() {
    vec4 finalColor = vec4(TintColor, 1.0);

    if (BaseLayer >= 0.0) {
        vec4 baseTex = texture(textureArray, vec3(TexCoord, BaseLayer));
        if (baseTex.a < 0.05) discard;
        
        vec3 baseRgb = (TintBase > 0.5) ? (baseTex.rgb * TintColor) : baseTex.rgb;
        finalColor = vec4(baseRgb, baseTex.a);

        if (OverlayLayer >= 0.0) {
            vec4 overlayTex = texture(textureArray, vec3(TexCoord, OverlayLayer));
            if (overlayTex.a > 0.05) {
                vec3 overlayRgb = (TintOverlay > 0.5) ? (overlayTex.rgb * TintColor) : overlayTex.rgb;
                finalColor.rgb = mix(finalColor.rgb, overlayRgb, overlayTex.a);
            }
        }
    }

    if (u_IsGUI == 1) {
        FragColorOut = finalColor;
        return;
    }

    vec3 shadedColor = finalColor.rgb * LightIntensity;
    float dist = length(FragPos);
    float fogStart = 32.0f;
    float fogEnd = 80.0f;
    float fogFactor = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0f, 1.0f);
    vec3 fogColor = vec3(0.5f, 0.8f, 0.95f);
    vec3 finalRgb = mix(fogColor, shadedColor, fogFactor);

    FragColorOut = vec4(finalRgb, finalColor.a);
}
)";

const char* sunVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* sunFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.95, 0.6, 1.0);
}
)";

const char* cloudVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 FragPos;
void main() {
    vec4 viewPos = view * model * vec4(aPos, 1.0);
    FragPos = viewPos.xyz;
    gl_Position = projection * viewPos;
}
)";

const char* cloudFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
void main() {
    float dist = length(FragPos);
    float fogStart = 150.0f;
    float fogEnd = 300.0f;
    float fogFactor = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0f, 1.0f);
    vec3 cloudColor = vec3(1.0f, 1.0f, 1.0f);
    vec3 fogColor = vec3(0.5f, 0.8f, 0.95f);
    vec3 finalColor = mix(fogColor, cloudColor, fogFactor);
    FragColor = vec4(finalColor, 0.85f * fogFactor);
}
)";

const char* wireframeVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* wireframeFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.0, 0.0, 0.0, 0.8);
}
)";

const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
out vec4 FragColor;
void main() {
    FragColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* uiFragmentShaderSource = R"(
#version 330 core
in vec4 FragColor;
out vec4 finalColor;
void main() {
    finalColor = FragColor;
}
)";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;
    glViewport(0, 0, width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_ChatSystem && g_ChatSystem->isOpen) return;
    EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(window));
    if (ctx && ctx->player) {
        if (yoffset > 0) g_Hotbar->previousSlot();
        else if (yoffset < 0) g_Hotbar->nextSlot();
    }
}

int main() {
    try {
        if (!glfwInit()) return -1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow* window = glfwCreateWindow(1280, 720, "Craft & Ruin - Voxel Engine", NULL, NULL);
        if (!window) { glfwTerminate(); return -1; }
        glfwMakeContextCurrent(window);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Error] Failed to initialize GLAD\n";
            return -1;
        }

        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        g_Hotbar = new Hotbar();
        g_FontRenderer = new FontRenderer();
        g_ChatSystem = new ChatSystem();

        g_FontRenderer->init("fonts/default.ttf");

        glfwSetCharCallback(window, [](GLFWwindow* win, unsigned int codepoint) {
            if (g_ChatSystem) {
                g_ChatSystem->handleCharInput(codepoint);
            }
        });

        glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
            if (!ctx) return;

            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleKeyInput(key, scancode, action, mods, win);
                return;
            }

            if (g_CurrentGameState == GameState::MENU && action == GLFW_PRESS) {
                if (key == GLFW_KEY_ENTER) {
                    try {
                        sol::state* lua = ctx->lua;
                        sol::function startGameFunc = (*lua)["engine"]["start_game"];
                        if (startGameFunc.valid()) {
                            startGameFunc();
                        } else {
                            g_CurrentGameState = GameState::PLAYING;
                            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[Menu Error starting game]: " << e.what() << std::endl;
                        g_CurrentGameState = GameState::PLAYING;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }
                }
                if (key == GLFW_KEY_ESCAPE) {
                    glfwSetWindowShouldClose(win, true);
                }
                return;
            }

            if (g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS) {
                if (key == GLFW_KEY_T || key == GLFW_KEY_SLASH) {
                    g_ChatSystem->toggleChat(win);
                    if (key == GLFW_KEY_SLASH) {
                        g_ChatSystem->currentInput = "/";
                    }
                    return;
                }

                if (key == GLFW_KEY_E) {
                    if (g_Hotbar) {
                        g_Hotbar->toggleCreativeInventory();
                        int currentMode = glfwGetInputMode(win, GLFW_CURSOR);
                        if (currentMode == GLFW_CURSOR_DISABLED) {
                            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        } else {
                            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        }
                    }

                    try {
                        sol::state* lua = ctx->lua;
                        sol::function toggleLuaUI = (*lua)["toggle_creative_inventory"];
                        if (toggleLuaUI.valid()) {
                            toggleLuaUI();
                        }
                    } catch (...) {}
                    return;
                }

                if (key == GLFW_KEY_ESCAPE) {
                    if (g_Hotbar && g_Hotbar->isCreativeOpen) {
                        g_Hotbar->isCreativeOpen = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }

                    int currentMode = glfwGetInputMode(win, GLFW_CURSOR);
                    if (currentMode == GLFW_CURSOR_DISABLED) {
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    } else {
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }
                }
            }
        });

        // --- UPDATED MOUSE BUTTON CALLBACK WITH MENU CLICK SUPPORT ---
        glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx || !ctx->lua) return;
                sol::state* lua = ctx->lua;

                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);

                if (g_CurrentGameState == GameState::MENU) {
                    try {
                        sol::function menuClick = (*lua)["handle_menu_click"];
                        if (menuClick.valid()) {
                            menuClick(mx, my, sw, sh);
                        }
                    } catch (const sol::error& e) {
                        std::cerr << "[Lua Menu Click Error]: " << e.what() << std::endl;
                    }
                } 
                else if (g_CurrentGameState == GameState::PLAYING && g_Hotbar && g_Hotbar->isCreativeOpen) {
                    g_Hotbar->handleMouseClick(mx, my, sw, sh);
                }
            }
        });

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);

        initTextureArray();

        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table);

        auto compileShader = [](GLenum type, const char* src) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            return shader;
        };

        GLuint uiProgram = glCreateProgram();
        glAttachShader(uiProgram, compileShader(GL_VERTEX_SHADER, uiVertexShaderSource));
        glAttachShader(uiProgram, compileShader(GL_FRAGMENT_SHADER, uiFragmentShaderSource));
        glLinkProgram(uiProgram);

        // --- REGISTER HOTBAR TABLE FIRST SO SCRIPTS CAN ACCESS IT ---
        sol::table hotbarTable = lua.create_named_table("hotbar");
        hotbarTable["set_size"] = [](int size) { if (g_Hotbar) g_Hotbar->resize(size); };
        hotbarTable["get_size"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["set_slot_size"] = [](float size) { if (g_Hotbar) g_Hotbar->slotSize = size; };
        hotbarTable["set_position"] = [](float x, float y) { if (g_Hotbar) { g_Hotbar->posX = x; g_Hotbar->posY = y; } };
        hotbarTable["set_slot"] = [](int slotIndex, const std::string& blockName) {
            if (g_Hotbar) {
                uint8_t blockId = BlockRegistry::getInstance().getBlockId(blockName);
                int adjustedIndex = slotIndex - 1;
                if (adjustedIndex >= 0 && adjustedIndex < g_Hotbar->slotCount) {
                    g_Hotbar->setSlot(adjustedIndex, blockId, 64);
                }
            }
        };
        hotbarTable["get_active_slot"] = []() { return g_Hotbar ? g_Hotbar->activeSlot + 1 : 1; };
        hotbarTable["set_active_slot"] = [](int slotIndex) {
            if (g_Hotbar) {
                int adj = slotIndex - 1;
                if (adj >= 0 && adj < g_Hotbar->slotCount) g_Hotbar->activeSlot = adj;
            }
        };
        hotbarTable["is_creative_open"] = []() { return g_Hotbar ? g_Hotbar->isCreativeOpen : false; };
        hotbarTable["toggle_creative"] = []() { if (g_Hotbar) g_Hotbar->toggleCreativeInventory(); };
        hotbarTable["get_slot_block"] = [](int slotIndex) -> std::string {
            if (!g_Hotbar) return "air";
            int adj = slotIndex - 1;
            HotbarSlot slot = g_Hotbar->getSlot(adj);
            if (slot.blockId == 0) return "air";
            return BlockRegistry::getInstance().getBlock(slot.blockId).name;
        };

        auto engineTable = lua.create_named_table("engine");
        engineTable["register_block"] = &registerBlockFromLua;
        engineTable["get_block_id"] = &getBlockIdLua;
        engineTable["set_fov"] = [](float fov) { g_FieldOfView = fov; };

        engineTable["draw_text"] = [&](std::string text, float x, float y) {
            if (g_FontRenderer) {
                int fw, fh;
                glfwGetFramebufferSize(window, &fw, &fh);
                float aspect = static_cast<float>(fw) / static_cast<float>(fh);
                g_FontRenderer->renderText(uiProgram, text, x, y, 0.05f, 0.05f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), aspect);
            }
        };

        engineTable["get_games"] = [&]() {
            auto games = getAvailableGames();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& g : games) {
                sol::table gameTable = lua.create_table();
                gameTable["folder"] = g.folderName;
                gameTable["title"] = g.title;
                gameTable["author"] = g.author;
                gameTable["description"] = g.description;
                t[idx++] = gameTable;
            }
            return t;
        };

        engineTable["get_worlds"] = [&]() {
            auto worlds = getAvailableWorlds();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : worlds) {
                t[idx++] = w;
            }
            return t;
        };

        engineTable["get_active_game"] = []() { return g_ActiveGameFolder; };
        engineTable["set_active_game"] = [](std::string name) { g_ActiveGameFolder = name; };
        engineTable["get_active_world"] = []() { return g_ActiveWorldName; };
        engineTable["set_active_world"] = [](std::string name) { g_ActiveWorldName = name; };
        engineTable["quit"] = [&]() { glfwSetWindowShouldClose(window, true); };

        engineTable["start_game"] = [&]() {
            std::cout << "[Engine] Starting game state..." << std::endl;
            g_CurrentGameState = GameState::PLAYING;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        };

        // --- LOAD MODS & SCRIPTS WITH DYNAMIC MOD PATH TRACKING ---
        std::vector<std::string> modSearchPaths = {
            "mods",
            "../mods",
            "../../mods",
            "games/" + g_ActiveGameFolder + "/mods",
            "../games/" + g_ActiveGameFolder + "/mods",
            "../../games/" + g_ActiveGameFolder + "/mods"
        };

        for (const auto& searchPath : modSearchPaths) {
            std::string resolvedPath = searchPath;
            if (!fs::exists(resolvedPath) && fs::exists("../../" + searchPath)) {
                resolvedPath = "../../" + searchPath;
            }

            if (fs::exists(resolvedPath)) {
                std::cout << "[ModLoader] Scanning: " << fs::absolute(resolvedPath) << std::endl;
                for (const auto& entry : fs::recursive_directory_iterator(resolvedPath)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".lua") {
                            std::string scriptPath = entry.path().string();
                            
                            g_CurrentModPath = entry.path().parent_path().string();
                            lua["g_CurrentModPath"] = g_CurrentModPath;

                            try {
                                lua.script_file(scriptPath);
                                std::cout << "  -> [Loaded]: " << scriptPath << std::endl;
                            } catch (const sol::error& e) {
                                std::cerr << "  -> [Lua Error in " << scriptPath << "]: " << e.what() << std::endl;
                            }
                        }
                    }
                }
            }
        }

        lua.new_usertype<ChunkMesh>("Chunk",
            "set_block", &ChunkMesh::setBlock,
            "setBlock",  &ChunkMesh::setBlock,
            "get_block", &ChunkMesh::getBlock,
            "getBlock",  &ChunkMesh::getBlock,
            "set_block_with_meta", &ChunkMesh::setBlockWithMeta,
            "set_metadata", &ChunkMesh::setMetadata,
            "get_metadata", &ChunkMesh::getMetadata,
            "generate_mesh_data", &ChunkMesh::generateMeshData,
            "upload_mesh", &ChunkMesh::uploadMesh
        );

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, compileShader(GL_VERTEX_SHADER, vertexShaderSource));
        glAttachShader(shaderProgram, compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource));
        glLinkProgram(shaderProgram);

        GLuint sunProgram = glCreateProgram();
        glAttachShader(sunProgram, compileShader(GL_VERTEX_SHADER, sunVertexShaderSource));
        glAttachShader(sunProgram, compileShader(GL_FRAGMENT_SHADER, sunFragmentShaderSource));
        glLinkProgram(sunProgram);

        GLuint cloudProgram = glCreateProgram();
        glAttachShader(cloudProgram, compileShader(GL_VERTEX_SHADER, cloudVertexShaderSource));
        glAttachShader(cloudProgram, compileShader(GL_FRAGMENT_SHADER, cloudFragmentShaderSource));
        glLinkProgram(cloudProgram);

        GLuint wireframeProgram = glCreateProgram();
        glAttachShader(wireframeProgram, compileShader(GL_VERTEX_SHADER, wireframeVertexShaderSource));
        glAttachShader(wireframeProgram, compileShader(GL_FRAGMENT_SHADER, wireframeFragmentShaderSource));
        glLinkProgram(wireframeProgram);

        float sunVertices[] = {
            -4.0f,  4.0f, 0.0f,
            -4.0f, -4.0f, 0.0f,
             4.0f, -4.0f, 0.0f,
             4.0f,  4.0f, 0.0f
        };
        unsigned int sunIndices[] = { 0, 1, 2, 2, 3, 0 };
        GLuint sunVAO, sunVBO, sunEBO;
        glGenVertexArrays(1, &sunVAO);
        glGenBuffers(1, &sunVBO);
        glGenBuffers(1, &sunEBO);
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        float cloudVertices[] = {
            -256.0f, 0.0f, -256.0f,
            -256.0f, 0.0f,  256.0f,
             256.0f, 0.0f,  256.0f,
             256.0f, 0.0f, -256.0f
        };
        unsigned int cloudIndices[] = { 0, 1, 2, 2, 3, 0 };
        GLuint cloudVAO, cloudVBO, cloudEBO;
        glGenVertexArrays(1, &cloudVAO);
        glGenBuffers(1, &cloudVBO);
        glGenBuffers(1, &cloudEBO);
        glBindVertexArray(cloudVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cloudVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cloudVertices), cloudVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cloudEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cloudIndices), cloudIndices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        float crosshairVertices[] = {
            -0.015f,  0.000f,  1.0f, 1.0f, 1.0f, 0.8f,
             0.015f,  0.000f,  1.0f, 1.0f, 1.0f, 0.8f,
             0.000f, -0.025f,  1.0f, 1.0f, 1.0f, 0.8f,
             0.000f,  0.025f,  1.0f, 1.0f, 1.0f, 0.8f
        };
        GLuint crosshairVAO, crosshairVBO;
        glGenVertexArrays(1, &crosshairVAO);
        glGenBuffers(1, &crosshairVBO);
        glBindVertexArray(crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

        World world;
        Player player;
        
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);

        EngineContext ctx;
        ctx.player = &player;
        ctx.lua = &lua;
        glfwSetWindowUserPointer(window, &ctx);

        glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
            if (g_CurrentGameState == GameState::PLAYING) {
                if (g_ChatSystem && g_ChatSystem->isOpen) return;
                EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (c && c->player && glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
                    c->player->handleMouseLook(xpos, ypos);
                }
            }
        });

        float lastFrameTime = static_cast<float>(glfwGetTime());
        float fluidTimer = 0.0f;
        bool leftMousePressedLast = false;
        bool rightMousePressedLast = false;

        while (!glfwWindowShouldClose(window)) {
            float currentFrameTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            static float fpsTimer = 0.0f;
            static int frameCount = 0;
            frameCount++;
            fpsTimer += deltaTime;

            if (fpsTimer >= 1.0f) {
                float fps = static_cast<float>(frameCount) / fpsTimer;
                std::string title = "Craft & Ruin - Voxel Engine | FPS: " + std::to_string(static_cast<int>(fps));
                glfwSetWindowTitle(window, title.c_str());
                frameCount = 0;
                fpsTimer = 0.0f;
            }

            glfwPollEvents();

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0) height = 1;

            bool isCursorDisabled = (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);

            if (g_CurrentGameState == GameState::PLAYING) {
                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    for (int k = GLFW_KEY_1; k <= GLFW_KEY_9; ++k) {
                        if (glfwGetKey(window, k) == GLFW_PRESS) {
                            int slotIndex = k - GLFW_KEY_1;
                            if (slotIndex < g_Hotbar->slotCount) g_Hotbar->activeSlot = slotIndex;
                        }
                    }

                    world.update(player.position, lua);
                    player.update(deltaTime, world, window, lua);

                    fluidTimer += deltaTime;
                    if (fluidTimer >= 0.15f) {
                        world.updateFluids();
                        fluidTimer = 0.0f;
                    }
                }
            }

            glm::vec3 rayOrigin = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
            glm::vec3 rayDir;
            rayDir.x = cos(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir.y = sin(glm::radians(player.pitch));
            rayDir.z = sin(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir = glm::normalize(rayDir);

            RaycastResult rayResult = raycastBlock(rayOrigin, rayDir, 6.0f, world);

            bool leftPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool rightPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (g_CurrentGameState == GameState::PLAYING) {
                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    if (leftPress && !leftMousePressedLast && rayResult.hit) {
                        world.setBlockAt(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z, 0);
                    }
                    if (rightPress && !rightMousePressedLast && rayResult.hit) {
                        uint8_t selectedBlock = g_Hotbar->getActiveBlockId();
                        if (selectedBlock != 0) {
                            uint8_t targetBlockId = world.getBlockAt(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                            bool replaceTarget = false;

                            if (targetBlockId != 0) {
                                const auto& prop = BlockRegistry::getInstance().getBlock(targetBlockId);
                                if (prop.name == "water" || prop.name == "flowing_water" || 
                                    prop.name == "short_grass" || prop.name == "flower_red" || prop.pattern == "plantlike") {
                                    replaceTarget = true;
                                }
                            }

                            glm::ivec3 placePos;
                            if (replaceTarget) {
                                placePos = rayResult.blockPos;
                            } else {
                                placePos = rayResult.blockPos + rayResult.faceNormal;
                            }

                            world.setBlockAt(placePos.x, placePos.y, placePos.z, selectedBlock);
                        }
                    }
                }
            }
            leftMousePressedLast = leftPress;
            rightMousePressedLast = rightPress;

            glClearColor(0.5f, 0.8f, 0.95f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            glm::mat4 proj = glm::perspective(glm::radians(g_FieldOfView), aspectRatio, 0.1f, 100.0f);
            glm::mat4 view = player.getViewMatrix();

            float timeOfDay = static_cast<float>(glfwGetTime()) * 0.03f;
            glm::vec3 sunDir = glm::normalize(glm::vec3(cos(timeOfDay), sin(timeOfDay), 0.3f));

            if (g_CurrentGameState == GameState::PLAYING) {
                // Render World
                glUseProgram(shaderProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_SunDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_Time"), static_cast<float>(glfwGetTime()));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));

                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE); 
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);

                world.render(shaderProgram);

                // Render Sun
                glUseProgram(sunProgram);
                glm::vec3 sunWorldPos = player.position + (sunDir * 80.0f);
                glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), sunWorldPos);
                sunModel[0][0] = view[0][0]; sunModel[0][1] = view[1][0]; sunModel[0][2] = view[2][0];
                sunModel[1][0] = view[0][1]; sunModel[1][1] = view[1][1]; sunModel[1][2] = view[2][1];
                sunModel[2][0] = view[0][2]; sunModel[2][1] = view[1][2]; sunModel[2][2] = view[2][2];
                glUniformMatrix4fv(glGetUniformLocation(sunProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(sunProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(sunModel));
                glBindVertexArray(sunVAO);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);

                // Render Clouds
                glUseProgram(cloudProgram);
                float cloudDrift = static_cast<float>(glfwGetTime()) * 0.8f;
                glm::vec3 cloudCenter = glm::vec3(player.position.x - fmod(player.position.x + cloudDrift, 256.0f), 120.0f, player.position.z - fmod(player.position.z, 256.0f));
                glm::mat4 cloudModel = glm::translate(glm::mat4(1.0f), cloudCenter);
                glUniformMatrix4fv(glGetUniformLocation(cloudProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(cloudProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(glGetUniformLocation(cloudProgram, "model"), 1, GL_FALSE, glm::value_ptr(cloudModel));
                glBindVertexArray(cloudVAO);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen) && rayResult.hit) {
                    glUseProgram(wireframeProgram);
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(rayResult.blockPos) + glm::vec3(0.5f));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    
                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_CULL_FACE);
                    glLineWidth(3.0f);

                    static GLuint boxVAO = 0, boxVBO = 0;
                    if (boxVAO == 0) {
                        float vertices[] = {
                            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
                             0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,

                            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
                             0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,

                            -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f
                        };
                        glGenVertexArrays(1, &boxVAO);
                        glGenBuffers(1, &boxVBO);
                        glBindVertexArray(boxVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, boxVBO);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                        glEnableVertexAttribArray(0);
                        glBindVertexArray(0);
                    }

                    glBindVertexArray(boxVAO);
                    glDrawArrays(GL_LINES, 0, 24);
                    glBindVertexArray(0);

                    glEnable(GL_CULL_FACE);
                }

                glDisable(GL_DEPTH_TEST);
                glUseProgram(uiProgram);
                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    glBindVertexArray(crosshairVAO);
                    glLineWidth(2.0f);
                    glDrawArrays(GL_LINES, 0, 4);
                    glBindVertexArray(0);
                }

                g_Hotbar->updateBuffers(width, height);
                g_Hotbar->renderSlots(uiProgram);

                try {
                    sol::function renderLuaUI = lua["render_creative_inventory"];
                    if (renderLuaUI.valid()) {
                        renderLuaUI(width, height);
                    }
                } catch (...) {}

                if (g_ChatSystem) {
                    g_ChatSystem->renderChatOverlay(g_FontRenderer, width, height);
                }

                glEnable(GL_DEPTH_TEST);
                glUseProgram(shaderProgram);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 1);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

                g_Hotbar->render3DIcons(shaderProgram, g_TextureArrayID);
            } else {
                // --- RENDER MAIN MENU ---
                glDisable(GL_DEPTH_TEST);
                glUseProgram(uiProgram);

                try {
                    sol::function renderMenu = lua["render_main_menu"];
                    if (renderMenu.valid()) {
                        renderMenu(width, height);
                    }
                } catch (...) {}
            }

            glfwSwapBuffers(window);
        }

        glfwDestroyWindow(window);
        glfwTerminate();

        delete g_Hotbar;
        delete g_FontRenderer;
        delete g_ChatSystem;
    } 
    catch (const std::exception& e) {
        std::cerr << "\n[CRITICAL EXCEPTION]: " << e.what() << std::endl;
        system("pause");
        return -1;
    }
    return 0;
}