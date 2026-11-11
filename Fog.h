#pragma once
#include <glm/glm.hpp>

struct FogSettings {
    bool        enabled     = true;
    bool        exponential = false;   // false = linear, true = exp2
    float       start       = 30.0f;   // linear: distance where fog begins
    float       end         = 60.0f;   // linear: distance where terrain is fully fogged
    float       density     = 0.012f;  // exp2 only
    float       rainScale   = 0.5f;    // 0..1, how much full rain pulls the end distance in
    glm::vec3 dayColor    = glm::vec3(0.55f, 0.78f, 0.92f);
    glm::vec3 sunsetColor = glm::vec3(0.82f, 0.48f, 0.30f);
    glm::vec3 nightColor  = glm::vec3(0.04f, 0.06f, 0.12f);
};

// Function declarations (no 'inline', no function bodies)
glm::vec3 fogColorForSun(const FogSettings& f, float sunDirY);
float effectiveFogEnd(const FogSettings& f, float rainIntensity);
float effectiveFogStart(const FogSettings& f, float fogEnd);