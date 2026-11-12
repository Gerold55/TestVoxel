#include "Fog.h" 
#include <algorithm>          // For standard algorithms if needed

// Blends sunset to day above the horizon, and to night below it.
glm::vec3 fogColorForSun(const FogSettings& f, float sunDirY) {
    glm::vec3 c = glm::mix(f.sunsetColor, f.dayColor, glm::clamp(sunDirY + 0.2f, 0.0f, 1.0f));
    if (sunDirY < 0.0f) {
        c = glm::mix(f.sunsetColor, f.nightColor, glm::clamp(-sunDirY * 2.0f, 0.0f, 1.0f));
    }
    return c;
}

// Rain pulls the fog end inward. The start stays at least 1 block behind the end.
float effectiveFogEnd(const FogSettings& f, float rainIntensity) {
    return f.end * (1.0f - f.rainScale * glm::clamp(rainIntensity, 0.0f, 1.0f));
}

float effectiveFogStart(const FogSettings& f, float fogEnd) {
    return glm::min(f.start, fogEnd - 1.0f);
}