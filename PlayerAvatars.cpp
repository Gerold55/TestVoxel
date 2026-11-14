#include "PlayerAvatars.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

// Same 13-float vertex layout as chunks (see Vertex in Types.h). A base layer of -1 tells
// the shader to use the tint colour instead of a texture.
namespace {
constexpr int kFloatsPerVertex = 13;

// Faces: +X (front, where the player looks), -X, +Y, -Y, +Z, -Z
const float kFaceShade[6] = {0.8f, 0.8f, 1.0f, 0.55f, 0.65f, 0.65f};

void addBox(std::vector<float>& out, const glm::mat4& m, glm::vec3 lo, glm::vec3 hi,
            const glm::vec3& color, const glm::vec3& frontColor) {
    const glm::vec3 c[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    // Corner indices per face, counter-clockwise from outside
    const int f[6][4] = {
        {1, 2, 6, 5},   // +X
        {4, 7, 3, 0},   // -X
        {3, 7, 6, 2},   // +Y
        {0, 1, 5, 4},   // -Y
        {5, 6, 7, 4},   // +Z
        {0, 3, 2, 1},   // -Z
    };
    for (int face = 0; face < 6; ++face) {
        const glm::vec3 col = face == 0 ? frontColor : color;
        auto vert = [&](int idx) {
            const glm::vec3 p = glm::vec3(m * glm::vec4(c[idx], 1.0f));
            const float v[kFloatsPerVertex] = {p.x, p.y, p.z, col.r, col.g, col.b,
                                               0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f,
                                               kFaceShade[face]};
            out.insert(out.end(), v, v + kFloatsPerVertex);
        };
        const int* q = f[face];
        vert(q[0]); vert(q[1]); vert(q[2]);
        vert(q[0]); vert(q[2]); vert(q[3]);
    }
}
} // namespace

PlayerAvatars::PlayerAvatars() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    const int sizes[8] = {3, 3, 2, 1, 1, 1, 1, 1};
    size_t offset = 0;
    for (GLuint i = 0; i < 8; ++i) {
        glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offset * sizeof(float)));
        glEnableVertexAttribArray(i);
        offset += static_cast<size_t>(sizes[i]);
    }
    glBindVertexArray(0);
}

PlayerAvatars::~PlayerAvatars() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
}

void PlayerAvatars::render(const net::PlayerMap& players, GLuint worldShader,
                           const glm::mat4& view, const glm::mat4& proj) {
    m_data.clear();
    for (const auto& [id, p] : players) {
        if (!p.hasPosition) continue;
        const glm::vec3 shirt = p.color;
        const glm::vec3 skin(0.86f, 0.68f, 0.52f);
        const glm::vec3 legs = p.color * 0.45f;

        // Body turns with yaw; local +X is the way they're looking
        const glm::mat4 body = glm::rotate(glm::translate(glm::mat4(1.0f), p.position),
                                           glm::radians(-p.yaw), glm::vec3(0, 1, 0));
        addBox(m_data, body, {-0.15f, 0.0f, -0.25f}, {0.15f, 0.75f, 0.25f}, legs, legs);
        addBox(m_data, body, {-0.15f, 0.75f, -0.3f}, {0.15f, 1.4f, 0.3f}, shirt, shirt);
        addBox(m_data, body, {-0.12f, 0.75f, -0.45f}, {0.12f, 1.4f, -0.3f}, shirt * 0.85f, shirt * 0.85f);
        addBox(m_data, body, {-0.12f, 0.75f, 0.3f}, {0.12f, 1.4f, 0.45f}, shirt * 0.85f, shirt * 0.85f);

        // Head also nods with pitch; the face side is a little darker so you can see where they look
        const glm::mat4 head = glm::rotate(glm::translate(body, glm::vec3(0.0f, 1.4f, 0.0f)),
                                           glm::radians(std::clamp(p.pitch, -89.0f, 89.0f)),
                                           glm::vec3(0, 0, 1));
        addBox(m_data, head, {-0.25f, 0.0f, -0.25f}, {0.25f, 0.5f, 0.25f}, skin, skin * 0.75f);
    }
    if (m_data.empty()) return;

    glUseProgram(worldShader);
    glUniformMatrix4fv(glGetUniformLocation(worldShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(worldShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    const glm::mat4 identity(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(worldShader, "model"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniform1f(glGetUniformLocation(worldShader, "u_ChunkFade"), 1.0f);
    glUniform1i(glGetUniformLocation(worldShader, "u_IsGUI"), 0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_data.size() * sizeof(float)),
                 m_data.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_data.size() / kFloatsPerVertex));
    glBindVertexArray(0);
}

std::vector<PlayerAvatars::NameTag> PlayerAvatars::nameTags(const net::PlayerMap& players,
                                                            const glm::mat4& view, const glm::mat4& proj,
                                                            const glm::vec3& eye, int width, int height,
                                                            float maxDistance) {
    std::vector<NameTag> tags;
    for (const auto& [id, p] : players) {
        if (!p.hasPosition || p.name.empty()) continue;
        const glm::vec3 top = p.position + glm::vec3(0.0f, 2.15f, 0.0f);
        const float dist = glm::distance(eye, top);
        if (dist > maxDistance) continue;

        const glm::vec4 clip = proj * view * glm::vec4(top, 1.0f);
        if (clip.w <= 0.05f) continue;   // behind the camera
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f) continue;

        tags.push_back({p.name, (ndc.x * 0.5f + 0.5f) * width, (0.5f - ndc.y * 0.5f) * height, dist});
    }
    // Far ones first so near names draw on top
    std::sort(tags.begin(), tags.end(), [](const NameTag& a, const NameTag& b) { return a.distance > b.distance; });
    return tags;
}
