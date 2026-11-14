#pragma once
// Draws other players (multiplayer) as simple coloured figures with the world shader,
// and works out where their name tags go on screen.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Net.h"

class PlayerAvatars {
public:
    PlayerAvatars();
    ~PlayerAvatars();
    PlayerAvatars(const PlayerAvatars&) = delete;
    PlayerAvatars& operator=(const PlayerAvatars&) = delete;

    // Call after the world is drawn (world shader, depth test on).
    void render(const net::PlayerMap& players, GLuint worldShader,
                const glm::mat4& view, const glm::mat4& proj);

    struct NameTag {
        std::string text;
        float x, y;      // framebuffer pixels from the top-left (same as renderText / ui.draw_text)
        float distance;
    };
    // Screen positions for the names of players in front of the camera within maxDistance.
    static std::vector<NameTag> nameTags(const net::PlayerMap& players, const glm::mat4& view,
                                         const glm::mat4& proj, const glm::vec3& eye,
                                         int width, int height, float maxDistance = 48.0f);

private:
    GLuint m_vao = 0, m_vbo = 0;
    std::vector<float> m_data;
};
