#include "Lighting.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
extern std::string g_CurrentModPath;
extern std::string g_ActiveGameFolder;
extern GLuint loadStandardTexture(const std::string& path);

LightingSystem::LightingSystem() {
    for (int i = 0; i < 8; ++i) {
        moonTextureIDs[i] = 0;
    }
}

LightingSystem::~LightingSystem() = default;

void LightingSystem::update(float deltaTime) {
    // Optional per-frame updates for dynamic lighting transitions or weather effects
}

float LightingSystem::getTimeOfDay() const {
    if (customTimeOfDay >= 0.0f) {
        return customTimeOfDay;
    }
    return static_cast<float>(glfwGetTime()) * 0.03f;
}

glm::vec3 LightingSystem::getSunDirection() const {
    float time = getTimeOfDay();
    return glm::normalize(glm::vec3(cos(time), sin(time), 0.3f));
}

glm::vec3 LightingSystem::getMoonDirection() const {
    return -getSunDirection();
}

int LightingSystem::getMoonPhase() const {
    if (forcedMoonPhase >= 0) {
        return forcedMoonPhase;
    }
    int dayCount = static_cast<int>(static_cast<float>(glfwGetTime()) / 60.0f);
    return dayCount % 8;
}

void LightingSystem::setSunTexture(const std::string& path) {
    sunTextureID = loadStandardTexture(path);
    std::cout << "[LightingSystem] Loaded Sun Texture: " << path << std::endl;
}

void LightingSystem::setMoonTexture(const std::string& path) {
    moonTextureIDs[0] = loadStandardTexture(path);
    std::cout << "[LightingSystem] Loaded Moon Texture (Phase 1): " << path << std::endl;
}

void LightingSystem::setMoonPhaseTextures(const std::vector<std::string>& paths) {
    for (size_t i = 0; i < paths.size() && i < 8; ++i) {
        moonTextureIDs[i] = loadStandardTexture(paths[i]);
    }
    std::cout << "[LightingSystem] Loaded custom moon phases from Lua.\n";
}

void LightingSystem::setSunColor(float r, float g, float b) {
    sunColor = glm::vec3(r, g, b);
}

void LightingSystem::setAmbientColor(float r, float g, float b) {
    ambientColor = glm::vec3(r, g, b);
}

void LightingSystem::setFogColor(float r, float g, float b) {
    customFogColor = glm::vec3(r, g, b);
}

void LightingSystem::setSunIntensity(float intensity) {
    sunIntensity = intensity;
}

void LightingSystem::setAmbientIntensity(float intensity) {
    ambientIntensity = intensity;
}

void LightingSystem::resetToDefaults() {
    sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
    ambientColor = glm::vec3(0.2f, 0.2f, 0.25f);
    customFogColor = glm::vec3(-1.0f);
    sunIntensity = 1.0f;
    ambientIntensity = 1.0f;
    customTimeOfDay = -1.0f;
    forcedMoonPhase = -1;
    m_PointLights.clear();
}

int LightingSystem::addPointLight(glm::vec3 pos, glm::vec3 color, float radius, float intensity) {
    PointLight light;
    light.position = pos;
    light.color = color;
    light.radius = radius;
    light.intensity = intensity;
    m_PointLights.push_back(light);
    return static_cast<int>(m_PointLights.size()) - 1;
}

void LightingSystem::updatePointLight(int id, glm::vec3 pos, glm::vec3 color, float radius, float intensity) {
    if (id >= 0 && id < static_cast<int>(m_PointLights.size())) {
        m_PointLights[id].position = pos;
        m_PointLights[id].color = color;
        m_PointLights[id].radius = radius;
        m_PointLights[id].intensity = intensity;
    }
}

void LightingSystem::removePointLight(int id) {
    if (id >= 0 && id < static_cast<int>(m_PointLights.size())) {
        m_PointLights.erase(m_PointLights.begin() + id);
    }
}

const std::vector<PointLight>& LightingSystem::getPointLights() const {
    return m_PointLights;
}