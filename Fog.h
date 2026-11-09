#pragma once
#include <glm/glm.hpp>

struct FogSettings {
    bool      enabled     = true;
    bool      exponential = false;   // false = linear, true = exp2
    float     start       = 30.0f;   // linear: distance where fog begins
    float     end         = 60.0f;   // linear: distance where terrain is fully fogged
    float     density     = 0.012f;  // exp2 only
    float     rainScale   = 0.5f;    // 0..1, how much full rain pulls the end distance in
    glm::vec3 dayColor    = glm::vec3(0.55f, 0.78f, 0.92f);
    glm::vec3 sunsetColor = glm::vec3(0.82f, 0.48f, 0.30f);
    glm::vec3 nightColor  = glm::vec3(0.04f, 0.06f, 0.12f);
};

// Blends sunset to day above the horizon, and to night below it.
inline glm::vec3 fogColorForSun(const FogSettings& f, float sunDirY) {
    glm::vec3 c = glm::mix(f.sunsetColor, f.dayColor, glm::clamp(sunDirY + 0.2f, 0.0f, 1.0f));
    if (sunDirY < 0.0f) {
        c = glm::mix(f.sunsetColor, f.nightColor, glm::clamp(-sunDirY * 2.0f, 0.0f, 1.0f));
    }
    return c;
}

// Rain pulls the fog end inward. The start stays at least 1 block behind the end.
inline float effectiveFogEnd(const FogSettings& f, float rainIntensity) {
    return f.end * (1.0f - f.rainScale * glm::clamp(rainIntensity, 0.0f, 1.0f));
}

inline float effectiveFogStart(const FogSettings& f, float fogEnd) {
    return glm::min(f.start, fogEnd - 1.0f);
}