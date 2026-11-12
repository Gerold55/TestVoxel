#include "Clouds.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr int kTile = 128;   // cells per tile edge; the pattern repeats every tile

int wrapCell(int v) { return ((v % kTile) + kTile) % kTile; }

float hash01(int x, int z, int salt) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u
               ^ static_cast<uint32_t>(z) * 668265263u
               ^ static_cast<uint32_t>(salt) * 1274126177u;
    h = (h ^ (h >> 13)) * 1103515245u;
    h ^= h >> 16;
    return (h & 0xFFFFu) / 65535.0f;
}

float blockNoise(int x, int z, int cellsPerGroup, int salt) {
    return hash01(x / cellsPerGroup, z / cellsPerGroup, salt);
}

bool isFilled(int x, int z, float threshold) {
    x = wrapCell(x);
    z = wrapCell(z);
    
    // Classic Minecraft anisotropic scaling: stretch the noise horizontally along X 
    // using block noise for sharp, crisp voxel edges instead of smooth interpolation.
    const float large = blockNoise(x * 2, z, 24, 1); 
    const float small = blockNoise(x, z, 8, 2);  
    return (0.45f * large + 0.55f * small) > threshold;
}

void addQuad(std::vector<float>& out,
             const glm::vec3& a, const glm::vec3& b,
             const glm::vec3& c, const glm::vec3& d, float shade) {
    const glm::vec3 verts[6] = { a, b, c, a, c, d };
    for (const auto& p : verts) {
        out.push_back(p.x);
        out.push_back(p.y);
        out.push_back(p.z);
        out.push_back(shade);
    }
}

// Builds one tile. Geometry depends on height, thickness, cell size, and threshold.
std::vector<float> buildTileVertices(float cellBlocks, float baseY, float thickness, float threshold) {
    std::vector<float> out;
    const float T  = cellBlocks;
    const float y0 = baseY;
    const float y1 = baseY + thickness;

    for (int i = 0; i < kTile; ++i) {
        for (int k = 0; k < kTile; ++k) {
            if (!isFilled(i, k, threshold)) continue;

            const float x0 = i * T,  x1 = (i + 1) * T;
            const float z0 = k * T,  z1 = (k + 1) * T;

            addQuad(out, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, 1.00f);
            addQuad(out, {x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, 0.65f);

            if (!isFilled(i + 1, k, threshold)) addQuad(out, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, 0.85f);
            if (!isFilled(i - 1, k, threshold)) addQuad(out, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, 0.85f);
            if (!isFilled(i, k + 1, threshold)) addQuad(out, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, 0.75f);
            if (!isFilled(i, k - 1, threshold)) addQuad(out, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, 0.75f);
        }
    }
    return out;
}

// Linear blend of two settings for weather transitions.
CloudSettings lerpSettings(const CloudSettings& a, const CloudSettings& b, float t) {
    CloudSettings r = b;
    r.color      = glm::mix(a.color, b.color, t);
    r.nightColor = glm::mix(a.nightColor, b.nightColor, t);
    r.opacity    = glm::mix(a.opacity, b.opacity, t);
    r.scrollSpeed= glm::mix(a.scrollSpeed, b.scrollSpeed, t);
    r.darken     = glm::mix(a.darken, b.darken, t);
    r.fadeStart  = glm::mix(a.fadeStart, b.fadeStart, t);
    r.fadeEnd    = glm::mix(a.fadeEnd, b.fadeEnd, t);
    if (t >= 0.5f) {
        r.height    = b.height;
        r.thickness = b.thickness;
        r.cellSize  = b.cellSize;
        r.threshold = b.threshold;
    } else {
        r.height    = a.height;
        r.thickness = a.thickness;
        r.cellSize  = a.cellSize;
        r.threshold = a.threshold;
    }
    return r;
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

const char* kCloudVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aShade;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 u_Scroll;
out float vShade;
out vec3 vWorld;
void main() {
    vec4 world = model * vec4(aPos, 1.0);
    world.xz -= u_Scroll;
    vWorld = world.xyz;
    vShade = aShade;
    gl_Position = projection * view * world;
}
)";

const char* kCloudFS = R"(
#version 330 core
in float vShade;
in vec3 vWorld;
out vec4 FragColor;
uniform vec3  u_Color;
uniform vec3  u_Fog;
uniform vec3  u_CamPos;
uniform float u_FadeStart;
uniform float u_FadeEnd;
uniform sampler2D u_Tex;
uniform int   u_UseTex;
uniform float u_TexScale;
void main() {
    vec3 base = u_Color;
    if (u_UseTex == 1) {
        base *= texture(u_Tex, fract(vWorld.xz / u_TexScale)).rgb;
    }
    vec3 lit = base * vShade;
    float dist = distance(vWorld.xz, u_CamPos.xz);
    float fade = clamp((u_FadeEnd - dist) / max(u_FadeEnd - u_FadeStart, 1.0), 0.0, 1.0);
    
    vec3 finalColor = mix(u_Fog, lit, fade);
    FragColor = vec4(finalColor, fade);
}
)";

} // namespace

Clouds::~Clouds() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_program) glDeleteProgram(m_program);
    if (m_texture) glDeleteTextures(1, &m_texture);
}

void Clouds::init() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kCloudVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kCloudFS);
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    m_locModel    = glGetUniformLocation(m_program, "model");
    m_locView     = glGetUniformLocation(m_program, "view");
    m_locProj     = glGetUniformLocation(m_program, "projection");
    m_locScroll   = glGetUniformLocation(m_program, "u_Scroll");
    m_locCam      = glGetUniformLocation(m_program, "u_CamPos");
    m_locColor    = glGetUniformLocation(m_program, "u_Color");
    m_locFog      = glGetUniformLocation(m_program, "u_Fog");
    m_locFadeS    = glGetUniformLocation(m_program, "u_FadeStart");
    m_locFadeE    = glGetUniformLocation(m_program, "u_FadeEnd");
    m_locTex      = glGetUniformLocation(m_program, "u_Tex");
    m_locUseTex   = glGetUniformLocation(m_program, "u_UseTex");
    m_locTexScale = glGetUniformLocation(m_program, "u_TexScale");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    m_meshDirty = true;
}

bool Clouds::loadTexture(const std::string& path) {
    if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
    if (path.empty()) return false;

    int w, h, n;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        std::cerr << "[Clouds] Could not load texture: " << path << std::endl;
        return false;
    }
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    // Nearest filtering keeps textures sharp and voxel-accurate
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Clouds::rebuildMesh() {
    const auto verts = buildTileVertices(m_settings.cellSize, m_settings.height,
                                         m_settings.thickness, m_settings.threshold);
    m_vertexCount = static_cast<GLsizei>(verts.size() / 4);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_meshDirty = false;
}

void Clouds::setSettings(const CloudSettings& s) {
    const bool geometryChanged =
        s.height != m_settings.height || s.thickness != m_settings.thickness ||
        s.cellSize != m_settings.cellSize || s.threshold != m_settings.threshold;
    const bool textureChanged = s.texturePath != m_settings.texturePath;

    m_settings = s;
    if (geometryChanged) m_meshDirty = true;
    if (textureChanged) loadTexture(s.texturePath);
}

void Clouds::setWeather(const std::string& name, float transitionSeconds) {
    CloudSettings target = m_settings;
    // Clean, high-threshold spacing reminiscent of classic vanilla skies
    if (name == "clear") {
        target.threshold = 0.68f; target.opacity = 1.0f; target.darken = 0.0f;
        target.color = glm::vec3(1.0f); target.scrollSpeed = 1.0f;
    } else if (name == "cloudy") {
        target.threshold = 0.55f; target.darken = 0.1f;
        target.color = glm::vec3(0.92f); target.scrollSpeed = 2.0f;
    } else if (name == "rain") {
        target.threshold = 0.46f; target.darken = 0.35f;
        target.color = glm::vec3(0.7f); target.scrollSpeed = 3.0f;
    } else if (name == "storm") {
        target.threshold = 0.38f; target.darken = 0.6f;
        target.color = glm::vec3(0.45f); target.scrollSpeed = 5.0f;
    } else {
        std::cerr << "[Clouds] Unknown weather: " << name << std::endl;
        return;
    }

    m_from = m_settings;
    m_to = target;
    m_transitionTime = 0.0f;
    m_transitionDuration = transitionSeconds > 0.0f ? transitionSeconds : 0.0001f;
    m_transition = 0.0f;
}

void Clouds::update(float dt) {
    if (m_transition >= 1.0f) return;
    m_transitionTime += dt;
    m_transition = glm::clamp(m_transitionTime / m_transitionDuration, 0.0f, 1.0f);

    const CloudSettings blended = lerpSettings(m_from, m_to, m_transition);
    setSettings(blended);
}

void Clouds::render(const glm::mat4& view, const glm::mat4& proj,
                    const glm::vec3& camPos, float timeSeconds,
                    float sunDirY, const glm::vec3& fogColor) {
    if (m_meshDirty) rebuildMesh();
    if (m_vertexCount == 0) return;

    const float dayAmount = glm::clamp(sunDirY * 2.0f + 0.5f, 0.0f, 1.0f);
    glm::vec3 color = glm::mix(m_settings.nightColor, m_settings.color, dayAmount);
    color *= (1.0f - m_settings.darken);

    const float kTileWorld = kTile * m_settings.cellSize;
    const float scrollX = std::fmod(timeSeconds * m_settings.scrollSpeed, kTileWorld);

    const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);

    glUseProgram(m_program);
    glUniformMatrix4fv(m_locView, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_locProj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform2f(m_locScroll, scrollX, 0.0f);
    glUniform3f(m_locCam, camPos.x, camPos.y, camPos.z);
    glUniform3f(m_locColor, color.r, color.g, color.b);
    glUniform3f(m_locFog, fogColor.r, fogColor.g, fogColor.b);
    glUniform1f(m_locFadeS, m_settings.fadeStart);
    glUniform1f(m_locFadeE, m_settings.fadeEnd);
    glUniform1f(m_locTexScale, 16.0f);

    const bool useTex = m_texture != 0;
    glUniform1i(m_locUseTex, useTex ? 1 : 0);
    if (useTex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glUniform1i(m_locTex, 0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    const int tx = static_cast<int>(std::floor(camPos.x / kTileWorld));
    const int tz = static_cast<int>(std::floor(camPos.z / kTileWorld));

    const int renderRadius = 3;

    glBindVertexArray(m_vao);
    for (int dz = -renderRadius; dz <= renderRadius; ++dz) {
        for (int dx = -renderRadius; dx <= renderRadius; ++dx) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f),
                glm::vec3((tx + dx) * kTileWorld, 0.0f, (tz + dz) * kTileWorld));
            glUniformMatrix4fv(m_locModel, 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
        }
    }
    glBindVertexArray(0);

    if (wasCull) glEnable(GL_CULL_FACE);
}