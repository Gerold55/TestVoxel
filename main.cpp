#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <set>
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
#include "Lighting.h"
#include "Skybox.h"
#include "Model.h"
#include "Clouds.h"
#include "Fog.h"
#include "MainMenu.h"

namespace fs = std::filesystem;

namespace {
    constexpr Dimensions2D baseResolution{1920, 1080};
}

struct UnderwaterSettings {
    glm::vec3 shallowColor      = glm::vec3(0.16f, 0.38f, 0.72f);
    glm::vec3 deepColor         = glm::vec3(0.02f, 0.07f, 0.20f);
    float       shallowVisibility = 14.0f;
    float       deepVisibility    = 7.0f;
    float       fullDepth         = 40.0f;
    float       fadeInSeconds     = 0.6f;
    float       fadeOutSeconds    = 0.4f;
    float       tintStrength      = 0.45f;
};

std::string g_CurrentModPath = "";
std::string g_ActiveGameFolder = "craft_and_ruin";
std::string g_ActiveWorldName = "world_alpha";
Hotbar* g_Hotbar = nullptr;
FontRenderer* g_FontRenderer = nullptr;
ChatSystem* g_ChatSystem = nullptr;
LightingSystem* g_LightingSystem = nullptr;
Skybox* g_Skybox = nullptr;
Clouds* g_Clouds = nullptr;
World* g_WorldInstance = nullptr;
MainMenu* g_MainMenu = nullptr;
float g_FieldOfView = 70.0f;
FogSettings g_Fog;
UnderwaterSettings g_Underwater;
float g_UnderwaterBlend = 0.0f;

// Loading screen state
bool g_SpawnPlaced = false;
double g_LoadStartTime = 0.0;
float g_LoadingProgress = 0.0f;
constexpr int kSpawnReadyRadius = 2;       // chunks around the spawn that must be ready
constexpr double kLoadTimeoutSec = 30.0;   // start anyway after this long

struct WaterColors {
    glm::vec3 shallow;
    glm::vec3 deep;
};

enum class GameState {
    MENU,
    LOADING,
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
        confPath = gameDirPath / "mod.conf";
    }
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

            if (key == "title" || key == "name") info.title = val;
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
                if (fs::exists(confPath) || fs::exists(entry.path() / "mod.conf")) {
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
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(finalPath.c_str(), &width, &height, &nrChannels, 4);
    if (!data) return -1;

    int layer = g_NextTextureLayer++;
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return layer;
}

GLuint loadStandardTexture(const std::string& path) {
    std::string finalPath = path;
    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path,
        "games/" + g_ActiveGameFolder + "/textures/" + path,
        "../../games/" + g_ActiveGameFolder + "/textures/" + path
    };

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = fs::absolute(p).string();
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "[Texture] NOT FOUND: " << path
                  << " (also looked in games/" << g_ActiveGameFolder << "/textures/)" << std::endl;
        return 0;
    }

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(finalPath.c_str(), &width, &height, &nrChannels, 4);
    if (!data) {
        std::cerr << "[Texture] Failed to decode: " << finalPath << std::endl;
        return 0;
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[Texture] Loaded " << finalPath << " (" << width << "x" << height << ")" << std::endl;
    return texID;
}

static uint8_t g_NextBlockId = 1;

void registerBlockFromLua(const std::string& name, float r, float g, float b, sol::object texObj, sol::object overlayObj, sol::optional<sol::table> propsTable) {
    if (r > 1.0f || g > 1.0f || b > 1.0f) {
        r = glm::clamp(r / 255.0f, 0.0f, 1.0f);
        g = glm::clamp(g / 255.0f, 0.0f, 1.0f);
        b = glm::clamp(b / 255.0f, 0.0f, 1.0f);
    }

    BlockProperty block;
    block.id = g_NextBlockId++;
    block.name = name;
    block.defaultTint = glm::vec3(r, g, b);

    bool shouldTint = (name == "grass" || name == "short_grass");

    if (propsTable.has_value()) {
        sol::table props = propsTable.value();
        block.isTransparent = props.get_or("transparent", false);
        block.isCutout      = props.get_or("cutout", false);
        block.isWalkable    = props.get_or("walkable", (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red"));
        block.isSelectable  = props.get_or("selectable", (name != "water" && name != "flowing_water" && name != "air" && name != "short_grass" && name != "flower_red"));
        block.hasGravity    = props.get_or("gravity", false);
        block.pattern       = props.get_or("pattern", std::string("cube"));
        shouldTint          = props.get_or("tint", shouldTint);
    } else {
        block.isTransparent = (name == "water" || name == "flowing_water" || name == "leaves" || name == "birch_leaves" || name == "short_grass" || name == "flower_red");
        block.isCutout      = (name.find("leaves") != std::string::npos);
        block.isWalkable    = (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red");
        block.isSelectable  = (name != "water" && name != "flowing_water" && name != "air" && name != "short_grass" && name != "flower_red");
        block.hasGravity    = (name == "sand" || name == "gravel");
        block.pattern       = (name == "short_grass" || name == "flower_red") ? "plantlike" : "cube";
    }

    if (texObj.is<std::string>()) {
        int layer = loadTextureToLayer(texObj.as<std::string>());
        for (int f = 0; f < 6; ++f) block.faces[f].baseLayer = layer;
        if (shouldTint) {
            if (name == "grass") block.faces[2].tintBase = true;
            else { for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true; }
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
            if (name == "grass") block.faces[2].tintBase = true;
            else { for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true; }
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

bool isWaterBlockName(const std::string& name) {
    return name == "water" || name == "flowing_water";
}

float underwaterDepth(const World& world, const glm::vec3& eye) {
    const int bx = static_cast<int>(std::floor(eye.x));
    const int bz = static_cast<int>(std::floor(eye.z));
    const int by = static_cast<int>(std::floor(eye.y));

    auto isWaterAt = [&](int y) {
        const uint8_t id = world.getBlockAt(glm::ivec3(bx, y, bz));
        return id != 0 && isWaterBlockName(BlockRegistry::getInstance().getBlock(id).name);
    };

    if (!isWaterAt(by)) return -1.0f;

    int top = by;
    for (int y = by + 1; y < by + 128 && isWaterAt(y); ++y) {
        top = y;
    }

    const float surfaceY = static_cast<float>(top) + 1.0f;
    return glm::max(surfaceY - eye.y, 0.0f);
}

WaterColors queryWaterColors(sol::state& lua, const glm::vec3& eye) {
    const WaterColors fallback{ g_Underwater.shallowColor, g_Underwater.deepColor };
    try {
        sol::function fn = lua["get_water_color"];
        if (!fn.valid()) return fallback;
        sol::table t = fn(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.z)));
        if (t.size() < 6) return fallback;
        return WaterColors{
            glm::vec3(t.get<float>(1), t.get<float>(2), t.get<float>(3)),
            glm::vec3(t.get<float>(4), t.get<float>(5), t.get<float>(6))
        };
    } catch (...) {
        return fallback;
    }
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
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 u_SunDir;
uniform int u_Unlit;
uniform float u_Time;

void main() {
    vec3 pos = aPos;
    TintColor = aTintColor;
    TexCoord = aTexCoord;
    BaseLayer = aBaseLayer;
    OverlayLayer = aOverlayLayer;
    TintBase = aTintBase;
    TintOverlay = aTintOverlay;

    WorldPos = (model * vec4(pos, 1.0)).xyz;

    if (u_Unlit == 1) {
        LightIntensity = 1.0f;
    } else {
        float sunDot = dot(vec3(0.0, 1.0, 0.0), u_SunDir);
        float directLight = clamp(sunDot * 0.6f + 0.4f, 0.28f, 1.0f);
        LightIntensity = clamp(aFaceShading * directLight, 0.25f, 1.0f);
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
in vec3 WorldPos;

out vec4 FragColorOut;
uniform sampler2DArray textureArray;
uniform int u_IsGUI;
uniform int u_Unlit;
uniform vec3 u_SunDir;
uniform float u_Time;

struct PointLight {
    vec3 position;
    vec3 color;
    float radius;
    float intensity;
};
uniform int u_NumPointLights;
uniform PointLight u_PointLights[16];

uniform int   u_FogEnabled;
uniform int   u_FogExp;
uniform float u_FogStart;
uniform float u_FogEnd;
uniform float u_FogDensity;
uniform vec3  u_FogColor;

uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;
uniform float u_ChunkFade;

void main() {
    vec4 finalColor = vec4(TintColor, 1.0);

    if (u_Unlit == 0 && u_IsGUI == 0 && u_ChunkFade < 1.0) {
        float threshold = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        if (threshold > u_ChunkFade) discard;
    }

    if (BaseLayer >= 0.0) {
        vec2 animatedUV = TexCoord;

        if (TintColor.b > TintColor.r && TintColor.b > TintColor.g && BaseLayer > 0.0 && u_IsGUI == 0) {
            animatedUV.x += sin(u_Time * 1.2f + WorldPos.z * 0.5f) * 0.02f;
            animatedUV.y += u_Time * 0.08f;
        }

        vec4 baseTex = texture(textureArray, vec3(animatedUV, BaseLayer));
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

    vec3 dayLightColor = vec3(1.0f, 0.98f, 0.92f);
    vec3 sunsetLightColor = vec3(1.0f, 0.62f, 0.32f);
    vec3 nightLightColor = vec3(0.35f, 0.5f, 0.75f);

    vec3 currentSunLightColor = mix(sunsetLightColor, dayLightColor, clamp(u_SunDir.y + 0.15f, 0.0f, 1.0f));
    if (u_SunDir.y < 0.0f) {
        currentSunLightColor = mix(sunsetLightColor, nightLightColor, clamp(-u_SunDir.y * 2.0f, 0.0f, 1.0f));
    }

    vec3 shadedColor = finalColor.rgb * LightIntensity * currentSunLightColor;

    vec3 pointLightContribution = vec3(0.0f);
    for(int i = 0; i < u_NumPointLights && i < 16; i++) {
        float dist = distance(WorldPos, u_PointLights[i].position);
        if(dist < u_PointLights[i].radius) {
            float attenuation = 1.0f - (dist / u_PointLights[i].radius);
            attenuation = attenuation * attenuation;
            pointLightContribution += u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
        }
    }
    shadedColor += finalColor.rgb * pointLightContribution;

    if (u_Unlit == 0) {
        shadedColor *= mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    }

    float dist = length(FragPos);
    float fogFactor = 1.0;
    if (u_FogEnabled == 1) {
        if (u_FogExp == 1) {
            float d = dist * u_FogDensity;
            fogFactor = exp(-d * d);
        } else {
            fogFactor = clamp((u_FogEnd - dist) / max(u_FogEnd - u_FogStart, 0.001), 0.0, 1.0);
        }
    }

    vec3 finalRgb = mix(u_FogColor, shadedColor, fogFactor);
    FragColorOut = vec4(finalRgb, finalColor.a);
}
)";

const char* sunVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* sunFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D u_Texture;
uniform int u_HasTexture;

void main() {
    if (u_HasTexture == 1) {
        vec4 texColor = texture(u_Texture, TexCoord);
        if (texColor.a < 0.1) discard;
        FragColor = texColor;
    } else {
        FragColor = vec4(1.0, 0.95, 0.6, 1.0);
    }
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
    FragColor = vec4(0.0, 0.0, 0.0, 0.9);
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

const char* texturedUIVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* texturedUIFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D u_Texture;
void main() {
    vec4 texColor = texture(u_Texture, TexCoords);
    if (texColor.a < 0.01) discard;
    FragColor = texColor;
}
)";

const char* fontVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
uniform mat4 projection;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* fontFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D fontTexture;
uniform vec4 textColor;
void main() {
    float alpha = texture(fontTexture, TexCoords).r;
    if (alpha < 0.05) discard;
    FragColor = vec4(textColor.rgb, textColor.a * alpha);
}
)";

const char* modelVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* modelFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D texture_diffuse1;
uniform vec3 u_LightDir;
uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;

void main() {
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -u_LightDir), 0.2f);
    vec4 texColor = texture(texture_diffuse1, TexCoords);
    if(texColor.a < 0.1) discard;

    vec3 litColor = texColor.rgb * diff * mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    FragColor = vec4(litColor, texColor.a);
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

// Draws the loading screen: dark background, title, and a progress bar.
void drawLoadingScreen(GLuint uiProgram, GLuint fontProgram, float progress, int width, int height) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    auto quad = [&](float x0, float y0, float x1, float y1, glm::vec4 c) {
        const float v[36] = {
            x0, y0, c.r, c.g, c.b, c.a,  x1, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,
            x0, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,  x0, y1, c.r, c.g, c.b, c.a
        };
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glUseProgram(uiProgram);
    quad(-1.0f, -1.0f, 1.0f, 1.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));
    quad(-0.40f, -0.12f, 0.40f, -0.06f, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
    const float fillX = -0.40f + 0.80f * glm::clamp(progress, 0.0f, 1.0f);
    quad(-0.40f, -0.12f, fillX, -0.06f, glm::vec4(0.35f, 0.75f, 0.35f, 1.0f));
    glBindVertexArray(0);

    if (g_FontRenderer) {
        const Dimensions2D screen{width, height};
        glUseProgram(fontProgram);
        g_FontRenderer->renderText(fontProgram, "Loading world...",
                                   width * 0.5f - 120.0f, height * 0.5f + 60.0f,
                                   1.2f, 1.2f, glm::vec4(1.0f), screen);
        const int percent = static_cast<int>(glm::clamp(progress, 0.0f, 1.0f) * 100.0f);
        g_FontRenderer->renderText(fontProgram, std::to_string(percent) + "%",
                                   width * 0.5f - 20.0f, height * 0.5f - 60.0f,
                                   0.9f, 0.9f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), screen);
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
        glfwSwapInterval(60);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Error] Failed to initialize GLAD\n";
            return -1;
        }

        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        g_Hotbar = new Hotbar();
        g_FontRenderer = new FontRenderer(baseResolution);
        g_ChatSystem = new ChatSystem();
        g_LightingSystem = new LightingSystem();
        g_Skybox = new Skybox();
        g_Skybox->init();
        g_Clouds = new Clouds();
        g_Clouds->init();
        g_MainMenu = new MainMenu();

        g_FontRenderer->init("fonts/PixeloidSans.ttf");

        glfwSetCharCallback(window, [](GLFWwindow* win, unsigned int codepoint) {
            if (g_ChatSystem) g_ChatSystem->handleCharInput(codepoint);
        });

        glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
            if (!ctx) return;

            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleKeyInput(key, scancode, action, mods, win);
                return;
            }

            if (g_CurrentGameState == GameState::MENU && action == GLFW_PRESS) {
                if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, true);
                return;
            }

            if (g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS) {
                if (key == GLFW_KEY_T || key == GLFW_KEY_SLASH) {
                    g_ChatSystem->openChat(win, key);
                    return;
                }

                if (key == GLFW_KEY_E) {
                    if (g_Hotbar) {
                        g_Hotbar->toggleCreativeInventory();
                        int currentMode = glfwGetInputMode(win, GLFW_CURSOR);
                        if (currentMode == GLFW_CURSOR_DISABLED) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        else glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }
                    try {
                        sol::state* lua = ctx->lua;
                        sol::function toggleLuaUI = (*lua)["toggle_creative_inventory"];
                        if (toggleLuaUI.valid()) toggleLuaUI();
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
                    if (currentMode == GLFW_CURSOR_DISABLED) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    else glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
            }
        });

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

        GLuint texturedUIProgram = glCreateProgram();
        glAttachShader(texturedUIProgram, compileShader(GL_VERTEX_SHADER, texturedUIVertexShaderSource));
        glAttachShader(texturedUIProgram, compileShader(GL_FRAGMENT_SHADER, texturedUIFragmentShaderSource));
        glLinkProgram(texturedUIProgram);

        GLuint fontProgram = glCreateProgram();
        glAttachShader(fontProgram, compileShader(GL_VERTEX_SHADER, fontVertexShaderSource));
        glAttachShader(fontProgram, compileShader(GL_FRAGMENT_SHADER, fontFragmentShaderSource));
        glLinkProgram(fontProgram);

        GLuint modelShaderProgram = glCreateProgram();
        glAttachShader(modelShaderProgram, compileShader(GL_VERTEX_SHADER, modelVertexShaderSource));
        glAttachShader(modelShaderProgram, compileShader(GL_FRAGMENT_SHADER, modelFragmentShaderSource));
        glLinkProgram(modelShaderProgram);

        lua.new_usertype<Bone>("Bone",
            "set_rotation", [](Bone& b, float x, float y, float z) { b.rotation = glm::vec3(x, y, z); },
            "get_rotation", [](Bone& b) { return std::make_tuple(b.rotation.x, b.rotation.y, b.rotation.z); },
            "set_position", [](Bone& b, float x, float y, float z) { b.position = glm::vec3(x, y, z); }
        );

        lua.new_usertype<AnimatedModel>("Model",
            "get_bone", &AnimatedModel::getBone
        );

        sol::table hotbarTable = lua.create_named_table("hotbar");
        hotbarTable["set_size"] = [](int size) { if (g_Hotbar) g_Hotbar->resize(size); };
        hotbarTable["get_size"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["set_slot_size"] = [](float size) { if (g_Hotbar) g_Hotbar->slotSize = size; };
        hotbarTable["set_position"] = [](float x, float y) { if (g_Hotbar) { g_Hotbar->posX = x; g_Hotbar->posY = y; } };
        hotbarTable["set_slot"] = [](int slotIndex, const std::string& blockName) {
            if (g_Hotbar) {
                uint8_t blockId = BlockRegistry::getInstance().getBlockId(blockName);
                int adj = slotIndex - 1;
                if (adj >= 0 && adj < g_Hotbar->slotCount) g_Hotbar->setSlot(adj, blockId, 64);
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
        hotbarTable["set_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->customTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_selected_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->selectedTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_texture_scale"] = [](float s) {
            if (g_Hotbar) g_Hotbar->textureScale = glm::clamp(s, 0.25f, 4.0f);
        };

        auto skyTable = lua.create_named_table("sky");
        skyTable["set_time"] = [](float time) { if (g_LightingSystem) g_LightingSystem->customTimeOfDay = time; };
        skyTable["get_time"] = []() { return g_LightingSystem ? g_LightingSystem->getTimeOfDay() : 0.0f; };
        skyTable["set_moon_phase"] = [](int phase) { if (g_LightingSystem) g_LightingSystem->forcedMoonPhase = phase; };
        skyTable["get_moon_phase"] = []() { return g_LightingSystem ? g_LightingSystem->getMoonPhase() : 0; };
        skyTable["set_moon_textures"] = [](sol::table paths) {
            if (!g_LightingSystem) return;
            std::vector<std::string> pList;
            for (int i = 0; i < paths.size(); ++i) pList.push_back(paths[i + 1]);
            g_LightingSystem->setMoonPhaseTextures(pList);
        };
        skyTable["set_rain_intensity"] = [](float intensity) {
            if (g_LightingSystem) g_LightingSystem->weatherRainIntensity = glm::clamp(intensity, 0.0f, 1.0f);
        };
        skyTable["get_rain_intensity"] = []() { return g_LightingSystem ? g_LightingSystem->weatherRainIntensity : 0.0f; };
        skyTable["set_sun_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setSunColor(r, g, b); };
        skyTable["set_ambient_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setAmbientColor(r, g, b); };
        skyTable["set_fog_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setFogColor(r, g, b); };
        skyTable["set_sun_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setSunIntensity(intensity); };
        skyTable["set_ambient_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setAmbientIntensity(intensity); };
        skyTable["reset"] = []() { if (g_LightingSystem) g_LightingSystem->resetToDefaults(); };
        skyTable["add_light"] = [](float x, float y, float z, float r, float g, float b, float radius, float intensity) {
            return g_LightingSystem ? g_LightingSystem->addPointLight(glm::vec3(x, y, z), glm::vec3(r, g, b), radius, intensity) : -1;
        };
        skyTable["remove_light"] = [](int id) { if (g_LightingSystem) g_LightingSystem->removePointLight(id); };

        auto cloudTable = lua.create_named_table("clouds");
        cloudTable["set_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.color = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_night_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.nightColor = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_height"] = [](float y) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.height = y; g_Clouds->setSettings(s);
        };
        cloudTable["set_thickness"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.thickness = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_cell_size"] = [](float c) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.cellSize = c; g_Clouds->setSettings(s);
        };
        cloudTable["set_threshold"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.threshold = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_scroll_speed"] = [](float v) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.scrollSpeed = v; g_Clouds->setSettings(s);
        };
        cloudTable["set_texture"] = [](const std::string& path) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.texturePath = path; g_Clouds->setSettings(s);
        };
        cloudTable["set_weather"] = [](const std::string& name, float seconds) {
            if (g_Clouds) g_Clouds->setWeather(name, seconds);
        };

        auto fogTable = lua.create_named_table("fog");
        fogTable["set_enabled"]      = [](bool on) { g_Fog.enabled = on; };
        fogTable["set_exponential"]  = [](bool on) { g_Fog.exponential = on; };
        fogTable["set_range"]        = [](float s, float e) { g_Fog.start = s; g_Fog.end = e; };
        fogTable["set_density"]      = [](float d) { g_Fog.density = d; };
        fogTable["set_rain_scale"]   = [](float s) { g_Fog.rainScale = glm::clamp(s, 0.0f, 1.0f); };
        fogTable["set_day_color"]    = [](float r, float g, float b) { g_Fog.dayColor = glm::vec3(r, g, b); };
        fogTable["set_sunset_color"] = [](float r, float g, float b) { g_Fog.sunsetColor = glm::vec3(r, g, b); };
        fogTable["set_night_color"]  = [](float r, float g, float b) { g_Fog.nightColor = glm::vec3(r, g, b); };

        auto chatTable = lua.create_named_table("chat");
        chatTable["send"] = [](const std::string& text,
                               sol::optional<float> r,
                               sol::optional<float> g,
                               sol::optional<float> b) {
            if (!g_ChatSystem) return;
            if (r.has_value() && g.has_value() && b.has_value()) {
                g_ChatSystem->addMessage(text, glm::vec3(*r, *g, *b));
            } else {
                g_ChatSystem->addMessage(text);
            }
        };

        g_ChatSystem->setCommandHandler([&lua](const std::string& command) -> bool {
            try {
                sol::protected_function fn = lua["on_chat_command"];
                if (!fn.valid()) return false;
                sol::protected_function_result result = fn(command);
                if (!result.valid()) return false;
                if (result.get_type() == sol::type::boolean) return result.get<bool>();
                return false;
            } catch (...) {
                return false;
            }
        });

        auto worldTable = lua.create_named_table("world");
        worldTable["set_render_distance"] = [](int chunks) { if (g_WorldInstance) g_WorldInstance->setRenderDistance(chunks); };
        worldTable["get_render_distance"] = []() { return g_WorldInstance ? g_WorldInstance->renderDistance() : 10; };
        worldTable["set_load_budget"]     = [](float ms) { if (g_WorldInstance) g_WorldInstance->setLoadBudgetMs(ms); };
        worldTable["set_mesh_budget"]     = [](float ms) { if (g_WorldInstance) g_WorldInstance->setMeshBudgetMs(ms); };
        worldTable["get_loaded_chunks"]   = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->loadedChunkCount()) : 0; };
        worldTable["get_pending_chunks"]  = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->pendingLoadCount()) : 0; };

        auto underwaterTable = lua.create_named_table("underwater");
        underwaterTable["set_shallow_color"] = [](float r, float g, float b) { g_Underwater.shallowColor = glm::vec3(r, g, b); };
        underwaterTable["set_deep_color"]    = [](float r, float g, float b) { g_Underwater.deepColor = glm::vec3(r, g, b); };
        underwaterTable["set_visibility"]    = [](float shallow, float deep) {
            g_Underwater.shallowVisibility = glm::max(shallow, 1.0f);
            g_Underwater.deepVisibility    = glm::max(deep, 1.0f);
        };
        underwaterTable["set_full_depth"]    = [](float d) { g_Underwater.fullDepth = glm::max(d, 1.0f); };
        underwaterTable["set_fade"]          = [](float inSeconds, float outSeconds) {
            g_Underwater.fadeInSeconds  = glm::max(inSeconds, 0.01f);
            g_Underwater.fadeOutSeconds = glm::max(outSeconds, 0.01f);
        };
        underwaterTable["set_tint_strength"] = [](float s) { g_Underwater.tintStrength = glm::clamp(s, 0.0f, 1.0f); };

        auto menuTable = lua.create_named_table("menu");
        menuTable["get_games"] = [&]() {
            auto games = getAvailableGames();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& g : games) {
                sol::table gt = lua.create_table();
                gt["folder"] = g.folderName;
                gt["title"] = g.title;
                gt["author"] = g.author;
                gt["description"] = g.description;
                t[idx++] = gt;
            }
            return t;
        };
        menuTable["get_active_game"] = []() { return g_ActiveGameFolder; };
        menuTable["set_active_game"] = [](std::string folderName) { g_ActiveGameFolder = folderName; };

        auto engineTable = lua.create_named_table("engine");
        engineTable["register_block"] = &registerBlockFromLua;
        engineTable["get_block_id"] = &getBlockIdLua;
        engineTable["set_fov"] = [](float fov) { g_FieldOfView = fov; };
        engineTable["set_sun_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setSunTexture(path); };
        engineTable["set_moon_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setMoonTexture(path); };

        engineTable["load_model"] = [](std::string path) {
            auto model = std::make_shared<AnimatedModel>();
            model->loadFromFile(path);
            return model;
        };

        engineTable["set_block"] = [](int x, int y, int z, const std::string& blockName) {
            if (g_WorldInstance) {
                uint8_t id = BlockRegistry::getInstance().getBlockId(blockName);
                g_WorldInstance->setBlockAt(glm::ivec3(x, y, z), id);
            }
        };
        engineTable["get_block_name"] = [](int x, int y, int z) -> std::string {
            if (g_WorldInstance) {
                uint8_t id = g_WorldInstance->getBlockAt(glm::ivec3(x, y, z));
                if (id == 0) return "air";
                return BlockRegistry::getInstance().getBlock(id).name;
            }
            return "air";
        };

        engineTable["draw_text"] = [&](std::string text, float x, float y) {
            if (g_FontRenderer) {
                Dimensions2D windowSize;
                glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
                g_FontRenderer->renderText(fontProgram, text, x, y, 1.0f, 1.0f, glm::vec4(1.0f), windowSize);
            }
        };

        engineTable["get_worlds"] = [&]() {
            auto worlds = getAvailableWorlds();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : worlds) t[idx++] = w;
            return t;
        };

        engineTable["get_active_world"] = []() { return g_ActiveWorldName; };
        engineTable["set_active_world"] = [](std::string name) { g_ActiveWorldName = name; };
        engineTable["quit"] = [&]() { glfwSetWindowShouldClose(window, true); };
        engineTable["start_game"] = [&]() {
            g_CurrentGameState = GameState::LOADING;
            g_SpawnPlaced = false;
            g_LoadStartTime = glfwGetTime();
            g_LoadingProgress = 0.0f;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };

        World world;
        g_WorldInstance = &world;

        std::vector<std::string> modSearchPaths = { "mods", "../mods", "../../mods", "games/" + g_ActiveGameFolder + "/mods" };
        std::vector<fs::path> modFiles;
        std::set<std::string> initFolders;

        for (const auto& searchPath : modSearchPaths) {
            std::string resolvedPath = searchPath;
            if (!fs::exists(resolvedPath) && fs::exists("../../" + searchPath)) resolvedPath = "../../" + searchPath;
            if (!fs::exists(resolvedPath)) continue;

            for (const auto& entry : fs::recursive_directory_iterator(resolvedPath)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
                if (entry.path().filename() == "init.lua") {
                    initFolders.insert(entry.path().parent_path().string());
                }
                modFiles.push_back(entry.path());
            }
        }

        auto isInsideInitFolder = [&initFolders](const fs::path& file) {
            for (fs::path p = file.parent_path(); ; p = p.parent_path()) {
                if (initFolders.count(p.string())) return true;
                if (p.empty() || p == p.parent_path()) return false;
            }
        };

        auto runModFile = [&](const fs::path& file) {
            g_CurrentModPath = file.parent_path().string();
            lua["g_CurrentModPath"] = g_CurrentModPath;
            try {
                lua.script_file(file.string());
            } catch (const std::exception& e) {
                std::cerr << "[Mod] Failed to load " << file.string() << ": " << e.what() << std::endl;
            }
        };

        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua") runModFile(file);
        }
        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua" || isInsideInitFolder(file)) continue;
            runModFile(file);
        }

        lua.new_usertype<ChunkMesh>("Chunk",
            "set_block", &ChunkMesh::setBlock, "setBlock", &ChunkMesh::setBlock,
            "get_block", &ChunkMesh::getBlock, "getBlock", &ChunkMesh::getBlock,
            "set_block_with_meta", &ChunkMesh::setBlockWithMeta,
            "set_metadata", &ChunkMesh::setMetadata, "get_metadata", &ChunkMesh::getMetadata,
            "generate_mesh_data", &ChunkMesh::generateMeshData, "upload_mesh", &ChunkMesh::uploadMesh
        );

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, compileShader(GL_VERTEX_SHADER, vertexShaderSource));
        glAttachShader(shaderProgram, compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource));
        glLinkProgram(shaderProgram);

        GLuint sunProgram = glCreateProgram();
        glAttachShader(sunProgram, compileShader(GL_VERTEX_SHADER, sunVertexShaderSource));
        glAttachShader(sunProgram, compileShader(GL_FRAGMENT_SHADER, sunFragmentShaderSource));
        glLinkProgram(sunProgram);

        GLuint wireframeProgram = glCreateProgram();
        glAttachShader(wireframeProgram, compileShader(GL_VERTEX_SHADER, wireframeVertexShaderSource));
        glAttachShader(wireframeProgram, compileShader(GL_FRAGMENT_SHADER, wireframeFragmentShaderSource));
        glLinkProgram(wireframeProgram);

        float sunVertices[] = { -12.0f, 12.0f, 0.0f, 0.0f, 1.0f, -12.0f, -12.0f, 0.0f, 0.0f, 0.0f, 12.0f, -12.0f, 0.0f, 1.0f, 0.0f, 12.0f, 12.0f, 0.0f, 1.0f, 1.0f };
        unsigned int sunIndices[] = { 0, 1, 2, 2, 3, 0 };
        GLuint sunVAO, sunVBO, sunEBO;
        glGenVertexArrays(1, &sunVAO); glGenBuffers(1, &sunVBO); glGenBuffers(1, &sunEBO);
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        float crosshairVertices[] = { -0.015f, 0.0f, 1,1,1,0.8f, 0.015f, 0.0f, 1,1,1,0.8f, 0.0f, -0.025f, 1,1,1,0.8f, 0.0f, 0.025f, 1,1,1,0.8f };
        GLuint crosshairVAO, crosshairVBO;
        glGenVertexArrays(1, &crosshairVAO); glGenBuffers(1, &crosshairVBO);
        glBindVertexArray(crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

        Player player;
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);

        EngineContext ctx;
        ctx.player = &player; ctx.lua = &lua;
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

        glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx) return;

                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);

                if (g_CurrentGameState == GameState::MENU && g_MainMenu) {
                    std::string actionResult = g_MainMenu->handleMouseClick(mx, my, sw, sh);
                    if (actionResult == "play") {
                        g_CurrentGameState = GameState::LOADING;
                        g_SpawnPlaced = false;
                        g_LoadStartTime = glfwGetTime();
                        g_LoadingProgress = 0.0f;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    } else if (actionResult == "quit") {
                        glfwSetWindowShouldClose(win, true);
                    }
                } else if (g_CurrentGameState == GameState::PLAYING && g_Hotbar && g_Hotbar->isCreativeOpen) {
                    g_Hotbar->handleMouseClick(mx, my, sw, sh);
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

            glfwPollEvents();

            try {
                sol::function onUpdate = lua["on_update"];
                if (onUpdate.valid()) onUpdate(deltaTime);
            } catch (...) {}

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0) height = 1;

            bool isCursorDisabled = (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);

            // --- Loading: stream the spawn area and place the player on the ground.
            // Physics and gameplay are paused until the area is ready.
            if (g_CurrentGameState == GameState::LOADING) {
                world.setLoadBudgetMs(40.0f);
                world.setMeshBudgetMs(30.0f);
                world.update(player.position, lua);
                world.upload();

                const int sx = static_cast<int>(std::floor(player.position.x));
                const int sz = static_cast<int>(std::floor(player.position.z));
                const bool timedOut = (glfwGetTime() - g_LoadStartTime) > kLoadTimeoutSec;

                if (!g_SpawnPlaced) {
                    const int ground = world.groundHeightAt(sx, sz);
                    if (ground >= 0) {
                        player.position.y = static_cast<float>(ground);
                        g_SpawnPlaced = true;
                    } else if (timedOut) {
                        player.position.y = 100.0f;
                        g_SpawnPlaced = true;
                    }
                }

                g_LoadingProgress = world.areaLoadProgress(player.position, kSpawnReadyRadius);

                if (g_SpawnPlaced && (g_LoadingProgress >= 1.0f || timedOut)) {
                    world.setLoadBudgetMs(4.0f);
                    world.setMeshBudgetMs(4.0f);
                    g_CurrentGameState = GameState::PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
            }

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

                    try {
                        sol::function updateAnimFunc = lua["update_player_animation"];
                        if (updateAnimFunc.valid()) {
                            bool isMoving = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
                                             glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
                                             glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
                                             glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);
                            updateAnimFunc(deltaTime, isMoving, player.pitch);
                        }
                    } catch (...) {}

                    fluidTimer += deltaTime;
                    if (fluidTimer >= 0.15f) { world.updateFluids(); fluidTimer = 0.0f; }
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

            if (g_CurrentGameState == GameState::PLAYING && isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                uint8_t activeBlockId = g_Hotbar->getActiveBlockId();
                std::string activeItemName = (activeBlockId != 0) ? BlockRegistry::getInstance().getBlock(activeBlockId).name : "air";

                if (leftPress && !leftMousePressedLast && rayResult.hit) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = BlockRegistry::getInstance().getBlock(targetBlockId);

                        if (targetProp.isSelectable) {
                            if (activeItemName.find("scythe") != std::string::npos) {
                                sol::protected_function scytheFunc = lua["on_use_scythe"];
                                if (scytheFunc.valid()) {
                                    scytheFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                                }
                            } else {
                                world.setBlockAt(hitPos, 0);
                            }
                        }
                    }
                }

                if (rightPress && !rightMousePressedLast && rayResult.hit) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = BlockRegistry::getInstance().getBlock(targetBlockId);
                        if (activeItemName.find("hoe") != std::string::npos) {
                            sol::protected_function hoeFunc = lua["on_use_hoe"];
                            if (hoeFunc.valid()) {
                                hoeFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                            }
                        } else {
                            sol::protected_function clickFunc = lua["on_right_click_block"];
                            bool handled = false;
                            if (clickFunc.valid()) {
                                sol::protected_function_result res = clickFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name, activeItemName);
                                if (res.valid()) handled = res.get<bool>();
                            }

                            if (!handled) {
                                uint8_t selectedBlock = g_Hotbar->getActiveBlockId();
                                if (selectedBlock != 0) {
                                    bool replaceTarget = false;
                                    if (targetProp.name == "short_grass" || targetProp.name == "flower_red" || targetProp.pattern == "plantlike" || targetProp.name.find("farmland") != std::string::npos) {
                                        replaceTarget = true;
                                    }
                                    glm::vec3 placePos = replaceTarget ? (rayResult.blockPos + glm::ivec3(0, (targetProp.name.find("farmland") != std::string::npos ? 1 : 0), 0)) : (glm::vec3(rayResult.blockPos) + glm::vec3(rayResult.faceNormal));
                                    world.setBlockAt(glm::ivec3(static_cast<int>(placePos.x), static_cast<int>(placePos.y), static_cast<int>(placePos.z)), selectedBlock);
                                }
                            }
                        }
                    }
                }
            }
            leftMousePressedLast = leftPress;
            rightMousePressedLast = rightPress;

            float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            glm::mat4 proj = glm::perspective(glm::radians(g_FieldOfView), aspectRatio, 0.1f, 500.0f);
            glm::mat4 view = player.getViewMatrix();

            glm::vec3 sunDir = g_LightingSystem->getSunDirection();
            int moonPhase = g_LightingSystem->getMoonPhase();

            const glm::vec3 eyePos = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
            const float waterDepth = (g_CurrentGameState == GameState::PLAYING)
                ? underwaterDepth(world, eyePos)
                : -1.0f;
            const bool submerged = waterDepth >= 0.0f;

            {
                const float target  = submerged ? 1.0f : 0.0f;
                const float seconds = submerged ? g_Underwater.fadeInSeconds : g_Underwater.fadeOutSeconds;
                const float step    = deltaTime / glm::max(seconds, 0.01f);
                if (g_UnderwaterBlend < target) g_UnderwaterBlend = glm::min(g_UnderwaterBlend + step, target);
                else                            g_UnderwaterBlend = glm::max(g_UnderwaterBlend - step, target);
            }
            const float blend      = g_UnderwaterBlend;
            const bool  skyHidden = blend > 0.5f;

            const WaterColors water = (blend > 0.0f)
                ? queryWaterColors(lua, eyePos)
                : WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor };
            const float depthT = submerged
                ? glm::clamp(waterDepth / g_Underwater.fullDepth, 0.0f, 1.0f)
                : 0.0f;
            const glm::vec3 underwaterFog = glm::mix(water.shallow, water.deep, depthT);
            const float underwaterVis = glm::mix(g_Underwater.shallowVisibility, g_Underwater.deepVisibility, depthT);

            const glm::vec3 underwaterTint = glm::mix(
                glm::vec3(0.6f, 0.85f, 1.0f),
                glm::normalize(underwaterFog),
                0.4f);
            const float tintAmount = blend * g_Underwater.tintStrength;

            const glm::vec3 skyClear(0.5f, 0.8f, 0.95f);
            const glm::vec3 clearCol = glm::mix(skyClear, underwaterFog, blend);
            glClearColor(clearCol.r, clearCol.g, clearCol.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (g_CurrentGameState == GameState::PLAYING) {
                const glm::vec3 aboveFog   = fogColorForSun(g_Fog, sunDir.y);
                const float loadedEdge     = g_WorldInstance->renderDistanceBlocks();
                const float aboveEnd       = glm::min(effectiveFogEnd(g_Fog, g_LightingSystem->weatherRainIntensity), loadedEdge - 8.0f);
                const float aboveStart     = effectiveFogStart(g_Fog, aboveEnd);

                const glm::vec3 fogCol     = glm::mix(aboveFog, underwaterFog, blend);
                const float     fogStart   = glm::mix(aboveStart, 0.0f, blend);
                const float     fogEnd     = glm::mix(aboveEnd, underwaterVis, blend);
                const float     fogDensity = g_Fog.density;
                const bool fogExp = g_Fog.exponential && blend <= 0.0f;
                const bool fogOn  = g_Fog.enabled || blend > 0.0f;

                if (!skyHidden) {
                    g_Skybox->render(view, proj, sunDir.y);

                    glDepthMask(GL_FALSE);
                    glUseProgram(sunProgram);

                    glm::vec3 sunWorldPos = player.position + (sunDir * 80.0f);
                    glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), sunWorldPos);
                    sunModel[0][0] = view[0][0]; sunModel[0][1] = view[1][0]; sunModel[0][2] = view[2][0];
                    sunModel[1][0] = view[0][1]; sunModel[1][1] = view[1][1]; sunModel[1][2] = view[2][1];
                    sunModel[2][0] = view[0][2]; sunModel[2][1] = view[1][2]; sunModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(sunModel));

                    if (g_LightingSystem->sunTextureID != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, g_LightingSystem->sunTextureID);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }

                    glBindVertexArray(sunVAO);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glm::vec3 moonDir = -sunDir;
                    glm::vec3 moonWorldPos = player.position + (moonDir * 80.0f);
                    glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonWorldPos);
                    moonModel[0][0] = view[0][0]; moonModel[0][1] = view[1][0]; moonModel[0][2] = view[2][0];
                    moonModel[1][0] = view[0][1]; moonModel[1][1] = view[1][1]; moonModel[1][2] = view[2][1];
                    moonModel[2][0] = view[0][2]; moonModel[2][1] = view[1][2]; moonModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(moonModel));

                    GLuint activeMoonTex = g_LightingSystem->moonTextureIDs[moonPhase] != 0
                        ? g_LightingSystem->moonTextureIDs[moonPhase]
                        : g_LightingSystem->moonTextureIDs[0];
                    if (activeMoonTex != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, activeMoonTex);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glBindVertexArray(0);
                    glDepthMask(GL_TRUE);
                }

                glUseProgram(shaderProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_SunDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_Time"), static_cast<float>(glfwGetTime()));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));

                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogEnabled"), fogOn ? 1 : 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogExp"), fogExp ? 1 : 0);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogStart"), fogStart);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogEnd"), fogEnd);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogDensity"), fogDensity);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_FogColor"), fogCol.r, fogCol.g, fogCol.b);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_UnderwaterAmount"), tintAmount);

                const auto& pointLights = g_LightingSystem->getPointLights();
                glUniform1i(glGetUniformLocation(shaderProgram, "u_NumPointLights"), static_cast<int>(pointLights.size()));
                for (size_t i = 0; i < pointLights.size() && i < 16; ++i) {
                    std::string base = "u_PointLights[" + std::to_string(i) + "].";
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "position").c_str()), pointLights[i].position.x, pointLights[i].position.y, pointLights[i].position.z);
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "color").c_str()), pointLights[i].color.x, pointLights[i].color.y, pointLights[i].color.z);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "radius").c_str()), pointLights[i].radius);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "intensity").c_str()), pointLights[i].intensity);
                }

                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);

                g_WorldInstance->render(shaderProgram, proj * view, eyePos);

                g_Clouds->update(deltaTime);
                if (!skyHidden) {
                    g_Clouds->render(view, proj, player.position,
                                     static_cast<float>(glfwGetTime()),
                                     sunDir.y,
                                     fogCol);
                }

                glUseProgram(modelShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LightDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterAmount"), tintAmount);

                glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), player.position);
                modelMatrix = glm::rotate(modelMatrix, glm::radians(-player.yaw + 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

                try {
                    sol::object modelObj = lua["g_PlayerModel"];
                    if (modelObj.is<std::shared_ptr<AnimatedModel>>()) {
                        auto model = modelObj.as<std::shared_ptr<AnimatedModel>>();
                        if (model) {
                            model->draw(modelShaderProgram, modelMatrix);
                        }
                    }
                } catch (...) {}

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen) && rayResult.hit) {
                    glUseProgram(wireframeProgram);
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(rayResult.blockPos) + glm::vec3(0.5f));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
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
                            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f
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
                    glEnable(GL_BLEND);
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
                g_Hotbar->renderSlots(uiProgram, texturedUIProgram);

                try {
                    sol::function renderLuaUI = lua["render_creative_inventory"];
                    if (renderLuaUI.valid()) renderLuaUI(width, height);
                } catch (...) {}

                if (g_ChatSystem) g_ChatSystem->renderChatOverlay(g_FontRenderer, fontProgram, width, height);

                glEnable(GL_DEPTH_TEST);
                glUseProgram(shaderProgram);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

                g_Hotbar->render3DIcons(shaderProgram, g_TextureArrayID);
            } else if (g_CurrentGameState == GameState::LOADING) {
                glDisable(GL_DEPTH_TEST);
                drawLoadingScreen(uiProgram, fontProgram, g_LoadingProgress, width, height);
            } else {
                int sw, sh;
                glfwGetFramebufferSize(window, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(window, &mx, &my);

                if (g_MainMenu) {
                    g_MainMenu->updateAndBuildBuffers(mx, my, sw, sh);
                    g_MainMenu->render(uiProgram, texturedUIProgram, fontProgram, *g_FontRenderer, sw, sh);
                }
            }

            glfwSwapBuffers(window);
        }

        glfwDestroyWindow(window);
        glfwTerminate();

        delete g_Hotbar;
        delete g_FontRenderer;
        delete g_ChatSystem;
        delete g_LightingSystem;
        delete g_Skybox;
        delete g_Clouds;
        delete g_MainMenu;
    } catch (const std::exception& e) {
        std::cerr << "\n[CRITICAL EXCEPTION]: " << e.what() << std::endl;
        system("pause");
        return -1;
    }
    return 0;
}