#include "FontRenderer.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace fs = std::filesystem;

FontRenderer::FontRenderer(Dimensions2D resolution)
    : baseResolution{resolution}
{
}

FontRenderer::~FontRenderer() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (fontTextureID != 0) glDeleteTextures(1, &fontTextureID);
}

bool FontRenderer::init(const std::string& fontPath) {
    std::vector<std::string> searchPaths = {
        fontPath,
        "../" + fontPath,
        "../../" + fontPath
    };

    #if defined(_WIN32)
    searchPaths.push_back("C:/Windows/Fonts/arial.ttf");
    searchPaths.push_back("C:/Windows/Fonts/segoeui.ttf");
    #elif defined(__APPLE__)
    searchPaths.push_back("/Library/Fonts/Arial.ttf");
    searchPaths.push_back("/System/Library/Fonts/Helvetica.ttc");
    #else
    searchPaths.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    searchPaths.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    #endif

    std::string resolvedPath = "";
    bool found = false;

    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p) && fs::is_regular_file(p)) {
            resolvedPath = fs::absolute(p).string();
            found = true;
            break;
        }
    }

    if (!found) {
        std::cout << "[Font Error] Could not find font file '" << fontPath << "' in any search path or system fallback locations." << std::endl;
        return false;
    }

    std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "[Font Error] Failed to open resolved font file: " << resolvedPath << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> fontBuffer(size);
    if (!file.read((char*)fontBuffer.data(), size)) {
        std::cout << "[Font Error] Failed to read font file data from: " << resolvedPath << std::endl;
        return false;
    }

    int fontOffset = stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0);
    if (fontOffset < 0) {
        std::cout << "[Font Error] stb_truetype failed to parse font structure: " << resolvedPath << std::endl;
        return false;
    }

    int bitmapWidth = 512;
    int bitmapHeight = 512;
    std::vector<unsigned char> bitmap(bitmapWidth * bitmapHeight);

    int bakeResult = stbtt_BakeFontBitmap(fontBuffer.data(), fontOffset, 32.0f, bitmap.data(), bitmapWidth, bitmapHeight, 32, 96, cdata);
    if (bakeResult <= 0) {
        std::cout << "[Font Error] stbtt_BakeFontBitmap failed (return code: " << bakeResult << ")." << std::endl;
        return false;
    }

    glGenTextures(1, &fontTextureID);
    glBindTexture(GL_TEXTURE_2D, fontTextureID);
    
    // Ensure byte alignment is set to 1 for single-byte pixel data
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, bitmapWidth, bitmapHeight, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    std::cout << "[Font Success] Loaded TrueType font from: " << resolvedPath << std::endl;
    return true;
}

void FontRenderer::renderText(GLuint fontShader, const std::string& text,
        float x, float y, float scaleX, float scaleY, glm::vec4 color,
        Dimensions2D resolution) {
    if (text.empty() || fontTextureID == 0) return;

    std::vector<float> vertices = this->generateFontVertices(x, y, scaleX,
            scaleY, resolution, text);

    if (vertices.empty()) return;

    this->drawGLFontQuads(fontShader, color, resolution, vertices);
}

std::vector<float> FontRenderer::generateFontVertices(float x, float y,
        float scaleX, float scaleY, Dimensions2D resolution,
        std::string_view text) const
{
    std::vector<float> vertices;
    float rx = 0.0f;
    float ry = 0.0f;

    float scaleWidth = scaleX * static_cast<float>(resolution.width)
                     / static_cast<float>(this->baseResolution.width);
    float scaleHeight = scaleY * static_cast<float>(resolution.height)
                      / static_cast<float>(this->baseResolution.height);

    for (size_t i = 0; i < text.length(); ++i) {
        int charIndex = text[i];
        if (charIndex < 32 || charIndex >= 128) continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(const_cast<stbtt_bakedchar*>(cdata), 512, 512, charIndex - 32, &rx, &ry, &q, 1);

        float x0 = q.x0 * scaleWidth + x;
        float x1 = q.x1 * scaleWidth + x;
        float y0 = q.y0 * scaleHeight + y;
        float y1 = q.y1 * scaleHeight + y;

        float quad[] = {
            x0, y0, q.s0, q.t0,
            x1, y0, q.s1, q.t0,
            x1, y1, q.s1, q.t1,

            x0, y0, q.s0, q.t0,
            x1, y1, q.s1, q.t1,
            x0, y1, q.s0, q.t1
        };
        vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
    }

    return vertices;
}

void FontRenderer::drawGLFontQuads(GLuint fontShader, glm::vec4 color,
        Dimensions2D resolution, const std::vector<float>& vertices)
{
    glDisable(GL_DEPTH_TEST);
    glUseProgram(fontShader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTextureID);
    glUniform1i(glGetUniformLocation(fontShader, "fontTexture"), 0);
    glUniform4f(glGetUniformLocation(fontShader, "textColor"), color.r, color.g, color.b, color.a);

    glm::mat4 projection = glm::ortho(0.0f,
            static_cast<float>(resolution.width),
            static_cast<float>(resolution.height), 0.0f);
    glUniformMatrix4fv(glGetUniformLocation(fontShader, "projection"), 1,
            GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}