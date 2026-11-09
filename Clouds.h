#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

struct CloudSettings {
    glm::vec3 color      = glm::vec3(1.0f);
    glm::vec3 nightColor = glm::vec3(0.25f, 0.28f, 0.35f);
    float     opacity    = 1.0f;     // 1.0 = solid; lower values are not used by the depth-tested path
    float     height     = 150.0f;   // base height in blocks
    float     thickness  = 2.0f;     // layer thickness in blocks
    float     cellSize   = 2.0f;     // width of one cloud cell in blocks
    float     threshold  = 0.56f;    // coverage: higher = fewer clouds
    float     scrollSpeed = 2.0f;    // blocks per second
    float     fadeStart  = 250.0f;   // distance where fog fade begins
    float     fadeEnd    = 450.0f;   // distance where clouds fully fade to fog
    float     darken     = 0.0f;     // 0..1, multiplies color for storm darkening
    std::string texturePath;          // optional; empty = flat color
};

class Clouds {
public:
    Clouds() = default;
    ~Clouds();

    void init();   // call after the GL context exists

    // Lua-facing setters. Geometry-affecting values mark the mesh dirty.
    void setSettings(const CloudSettings& s);
    const CloudSettings& settings() const { return m_settings; }
    void setWeather(const std::string& name, float transitionSeconds);

    void update(float dt);   // advances weather interpolation
    void render(const glm::mat4& view, const glm::mat4& proj,
                const glm::vec3& camPos, float timeSeconds,
                float sunDirY, const glm::vec3& fogColor);

private:
    void rebuildMesh();
    bool loadTexture(const std::string& path);

    CloudSettings m_settings;
    CloudSettings m_from;     // weather transition start
    CloudSettings m_to;       // weather transition target
    float m_transition = 1.0f;
    float m_transitionTime = 0.0f;
    float m_transitionDuration = 0.0f;
    bool  m_meshDirty = true;

    GLuint  m_program = 0;
    GLuint  m_vao = 0;
    GLuint  m_vbo = 0;
    GLuint  m_texture = 0;
    GLsizei m_vertexCount = 0;

    GLint m_locModel   = -1;
    GLint m_locView    = -1;
    GLint m_locProj    = -1;
    GLint m_locScroll  = -1;
    GLint m_locCam     = -1;
    GLint m_locColor   = -1;
    GLint m_locFog     = -1;
    GLint m_locFadeS   = -1;
    GLint m_locFadeE   = -1;
    GLint m_locTex     = -1;
    GLint m_locUseTex  = -1;
    GLint m_locTexScale= -1;
};