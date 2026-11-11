#include "Hotbar.h"
#include "Block.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Prints the bar rectangle and texture IDs about every 5 seconds at 60 FPS.
constexpr bool kHotbarDebug = false;

// Draws a solid red box top-left, to test the 2D path independently of the textures.
constexpr bool kDebugRedBox = false;

// Fallback atlas size, used only if the texture size query fails.
constexpr float kFallbackTexW = 182.0f;
constexpr float kFallbackTexH = 22.0f;

// Fallback selection frame size, used only if its texture size query fails.
constexpr float kFallbackSelW = 84.0f;
constexpr float kFallbackSelH = 84.0f;

// Slot layout inside the bar atlas, in atlas pixels. Estimated from hotbar_bg.png (668x78).
constexpr float kBarSlotPitch  = 74.0f;   // distance between slot origins
constexpr float kBarSlotOffset = 3.0f;    // offset of the first slot's inner area
constexpr float kBarSlotInner  = 71.0f;   // inner slot size

// Icon size as a fraction of the slot. The isometric cube's corners reach about
// 1.2x its scale, so this keeps the projected corners inside the slot.
constexpr float kIconScale = 0.50f;

// Extra pixels added to the icon size. Each unit is one screen pixel.
constexpr float kIconGrowPx = 1.0f;

constexpr float kBottomMargin   = 20.0f;  // pixels between the bar and the bottom edge
constexpr float kTabW           = 100.0f;
constexpr float kTabH           = 24.0f;
constexpr float kTabGap         = 6.0f;
constexpr int   kCreativeCols   = 9;
constexpr float kCreativeMargin = 16.0f;
constexpr float kCreativeHeader = 45.0f;
constexpr float kCreativeFooter = 15.0f;

// ---------------------------------------------------------------------------
// 2D UI drawing: pixel coordinates with a top-left origin, one shader.
// ---------------------------------------------------------------------------

struct Vtx {
    float x, y;      // pixels
    float u, v;      // texture coordinates
    float r, g, b, a;
};

const char* kUiVS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0,
                    1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* kUiFS = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
uniform int uTextured;
out vec4 fragColor;
void main() {
    if (uTextured == 1) {
        vec4 t = texture(uTex, vUV) * vColor;
        if (t.a < 0.01) discard;
        fragColor = t;
    } else {
        fragColor = vColor;
    }
}
)";

struct UiGL {
    GLuint prog = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint locScreen = -1;
    GLint locTex = -1;
    GLint locTextured = -1;
    bool ready = false;
    bool failed = false;
};
UiGL g_ui;

GLuint compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Hotbar shader] compile error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Builds the UI program and its VAO once. Called from renderSlots, when a context exists.
bool ensureUiGL() {
    if (g_ui.ready) return true;
    if (g_ui.failed) return false;

    GLuint vs = compileStage(GL_VERTEX_SHADER, kUiVS);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, kUiFS);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        g_ui.failed = true;
        std::cerr << "[Hotbar shader] UI shader failed to compile" << std::endl;
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[Hotbar shader] link error: " << log << std::endl;
        glDeleteProgram(prog);
        g_ui.failed = true;
        return false;
    }

    g_ui.prog = prog;
    g_ui.locScreen = glGetUniformLocation(prog, "uScreen");
    g_ui.locTex = glGetUniformLocation(prog, "uTex");
    g_ui.locTextured = glGetUniformLocation(prog, "uTextured");

    glGenVertexArrays(1, &g_ui.vao);
    glGenBuffers(1, &g_ui.vbo);
    glBindVertexArray(g_ui.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ui.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, r));
    glBindVertexArray(0);

    g_ui.ready = true;
    std::cout << "[Hotbar shader] UI program ready, id=" << prog << std::endl;
    return true;
}

void addQuad(std::vector<Vtx>& o, float x0, float y0, float x1, float y1,
             float u0, float v0, float u1, float v1, const glm::vec4& c) {
    o.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a});
    o.push_back({x1, y0, u1, v0, c.r, c.g, c.b, c.a});
    o.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a});
    o.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a});
    o.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a});
    o.push_back({x0, y1, u0, v1, c.r, c.g, c.b, c.a});
}

void addRect(std::vector<Vtx>& o, float x0, float y0, float x1, float y1, const glm::vec4& c) {
    addQuad(o, x0, y0, x1, y1, 0.0f, 0.0f, 0.0f, 0.0f, c);
}

void drawUi(const std::vector<Vtx>& verts, GLuint texture, bool textured, float W, float H) {
    if (verts.empty() || !ensureUiGL()) return;

    glUseProgram(g_ui.prog);
    glUniform2f(g_ui.locScreen, W, H);
    glUniform1i(g_ui.locTex, 0);
    glUniform1i(g_ui.locTextured, textured ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textured ? texture : 0);

    glBindVertexArray(g_ui.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ui.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vtx)),
                 verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Hotbar draw] GL error 0x" << std::hex << err << std::dec
                  << " prog=" << g_ui.prog << " vao=" << g_ui.vao
                  << " verts=" << verts.size() << std::endl;
    }
}

// Reads the real pixel size of a 2D texture. Falls back to the given defaults.
void textureSize(GLuint tex, float& w, float& h, float fallbackW, float fallbackH) {
    w = fallbackW;
    h = fallbackH;
    if (tex == 0) return;

    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint tw = 0, th = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));

    if (tw > 0 && th > 0) {
        w = static_cast<float>(tw);
        h = static_cast<float>(th);
    }
}

// ---------------------------------------------------------------------------
// Layout. Every draw and every click uses these same functions.
// ---------------------------------------------------------------------------

float slotPixelSize(float cfg, float screenH) {
    const float px = cfg <= 1.5f ? cfg * screenH * 0.55f : cfg;
    return std::min(px, 60.0f);
}

float clampInside(float x, float w, float W) {
    if (w >= W) return 0.0f;
    return std::clamp(x, 0.0f, W - w);
}

struct BarLayout {
    bool textured = false;
    float s = 1.0f;                 // atlas pixel -> screen pixel scale (textured mode)
    float x = 0.0f, y = 0.0f;       // top-left of the bar
    float w = 0.0f, h = 0.0f;       // size of the bar
    float pitch = 0.0f;             // distance between slot origins
    float box = 0.0f;               // size of one slot's icon area
};

BarLayout computeBar(const Hotbar& hb, float W, float H) {
    BarLayout b;
    const float slotPx = slotPixelSize(hb.slotSize, H);
    const float n = static_cast<float>(hb.slotCount);
    const float centerX = W * 0.5f + hb.posX * 0.5f * W;
    const float bottom = H - (hb.posY + kBottomMargin);

    if (hb.customTextureID != 0) {
        b.textured = true;
        float texW = kFallbackTexW, texH = kFallbackTexH;
        textureSize(hb.customTextureID, texW, texH, kFallbackTexW, kFallbackTexH);

        // Scale the atlas so its height is about 1.25x a slot, then apply textureScale.
        b.s = (slotPx * 1.25f * hb.textureScale) / texH;
        b.w = texW * b.s;
        b.h = texH * b.s;
        b.x = std::round(clampInside(centerX - b.w * 0.5f, b.w, W));
        b.y = std::round(bottom - b.h);
        b.pitch = kBarSlotPitch * b.s;
        b.box = kBarSlotInner * b.s;
    } else {
        b.textured = false;
        b.pitch = slotPx + hb.padding;
        b.w = n * slotPx + (n - 1.0f) * hb.padding;
        b.h = slotPx;
        b.x = std::round(clampInside(centerX - b.w * 0.5f, b.w, W));
        b.y = std::round(bottom - slotPx);
        b.box = slotPx;
    }
    return b;
}

// Top-left of slot i's icon area, in pixels.
glm::vec2 slotOrigin(const BarLayout& b, int i) {
    if (b.textured) {
        return { b.x + (kBarSlotOffset + kBarSlotPitch * i) * b.s,
                 b.y + kBarSlotOffset * b.s };
    }
    return { std::round(b.x + i * b.pitch), b.y };
}

glm::vec2 slotCenter(const BarLayout& b, int i) {
    const glm::vec2 o = slotOrigin(b, i);
    return { o.x + b.box * 0.5f, o.y + b.box * 0.5f };
}

struct CreativeLayout {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float box = 0.0f, gap = 6.0f;
    float gridX = 0.0f, gridY = 0.0f;
};

CreativeLayout computeCreative(const Hotbar& hb, float W, float H, size_t itemCount) {
    CreativeLayout c;
    c.box = slotPixelSize(hb.slotSize, H);
    c.gap = 6.0f;

    int rows = static_cast<int>((itemCount + kCreativeCols - 1) / kCreativeCols);
    if (rows < 3) rows = 3;

    c.w = kCreativeCols * c.box + (kCreativeCols - 1) * c.gap + 2.0f * kCreativeMargin;
    c.h = kCreativeHeader + rows * (c.box + c.gap) + kCreativeFooter;
    c.x = std::round((W - c.w) * 0.5f);
    c.y = std::round((H - c.h) * 0.5f);
    c.gridX = c.x + kCreativeMargin;
    c.gridY = c.y + kCreativeHeader;
    return c;
}

glm::vec2 cellOrigin(const CreativeLayout& c, size_t k) {
    const int col = static_cast<int>(k % kCreativeCols);
    const int row = static_cast<int>(k / kCreativeCols);
    return { std::round(c.gridX + col * (c.box + c.gap)),
             std::round(c.gridY + row * (c.box + c.gap)) };
}

bool matchesCreativeTab(const BlockProperty& prop, int tab) {
    if (prop.name.empty() || prop.name == "air" || prop.name == "unknown") return false;
    if (!prop.in_creative) return false;

    const bool isNature = (prop.pattern == "plantlike" ||
                           prop.name.find("leaves") != std::string::npos ||
                           prop.name.find("water") != std::string::npos ||
                           prop.name.find("sand") != std::string::npos ||
                           prop.name.find("reed") != std::string::npos);

    const bool isBuilding = !isNature && (prop.name.find("stone") != std::string::npos ||
                                          prop.name.find("dirt") != std::string::npos ||
                                          prop.name.find("log") != std::string::npos ||
                                          prop.name.find("gravel") != std::string::npos ||
                                          prop.name.find("bedrock") != std::string::npos);

    if (tab == 0) return true;
    if (tab == 1) return isNature;
    if (tab == 2) return isBuilding;
    return false;
}

std::vector<uint8_t> creativeItemIds(int tab) {
    std::vector<uint8_t> ids;
    for (int i = 1; i <= 255; ++i) {
        try {
            const auto& prop = BlockRegistry::getInstance().getBlock(static_cast<uint8_t>(i));
            if (matchesCreativeTab(prop, tab)) ids.push_back(static_cast<uint8_t>(i));
        } catch (...) {
            break;
        }
    }
    return ids;
}

// ---------------------------------------------------------------------------
// 3D item icons (voxel shader). Vertex layout: 13 floats, matching the voxel shader.
// ---------------------------------------------------------------------------

const float kFaceVerts[6][18] = {
    { 0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f,  -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f },
    {-0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,   0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f },
    {-0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,   0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f },
    {-0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,   0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f },
    {-0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f },
    { 0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,   0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f }
};

const float kTexCoords[12] = {
    0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
    1.0f, 1.0f,  0.0f, 1.0f,  0.0f, 0.0f
};

const float kFaceShading[6] = { 0.7f, 0.7f, 1.0f, 0.5f, 0.8f, 0.8f };

void appendIcon(std::vector<float>& out, uint8_t blockId, float cx, float cy, float box) {
    if (blockId == 0) return;
    const BlockProperty& prop = BlockRegistry::getInstance().getBlock(blockId);
    if (prop.name.empty() || prop.name == "air") return;

    const float scale = box * kIconScale + kIconGrowPx;
    const glm::vec3 tint = prop.defaultTint;

    if (prop.pattern == "plantlike") {
        const float h = scale * 0.5f;
        const float x0 = cx - h, x1 = cx + h, y0 = cy - h, y1 = cy + h;
        const float z = -5.0f;
        const float layer = static_cast<float>(prop.faces[0].baseLayer >= 0
                                               ? prop.faces[0].baseLayer
                                               : prop.faces[2].baseLayer);
        const float overlay = prop.faces[0].overlayLayer >= 0
                                  ? static_cast<float>(prop.faces[0].overlayLayer)
                                  : -1.0f;
        const float tintBase = prop.faces[0].tintBase ? 1.0f : 0.0f;
        const float tintOverlay = prop.faces[0].tintOverlay ? 1.0f : 0.0f;

        auto pv = [&](float x, float y, float u, float v) {
            const float tail[10] = { tint.r, tint.g, tint.b, u, v,
                                     layer, overlay, tintBase, tintOverlay, 1.0f };
            out.push_back(x);
            out.push_back(y);
            out.push_back(z);
            out.insert(out.end(), tail, tail + 10);
        };

        pv(x0, y1, 0.0f, 1.0f);
        pv(x1, y1, 1.0f, 1.0f);
        pv(x1, y0, 1.0f, 0.0f);
        pv(x0, y1, 0.0f, 1.0f);
        pv(x1, y0, 1.0f, 0.0f);
        pv(x0, y0, 0.0f, 0.0f);
        return;
    }

    glm::mat4 xf = glm::rotate(glm::mat4(1.0f), glm::radians(35.264f), glm::vec3(1, 0, 0));
    xf = glm::rotate(xf, glm::radians(45.0f), glm::vec3(0, 1, 0));
    xf = glm::scale(xf, glm::vec3(scale));

    for (int face = 0; face < 6; ++face) {
        int idx = face;
        if (face == 2 && prop.faces[3].baseLayer >= 0) idx = 3;
        else if (face == 3 && prop.faces[2].baseLayer >= 0) idx = 2;

        const FaceDefinition& fd = prop.faces[idx];
        const float layer = static_cast<float>(fd.baseLayer);
        const float overlay = fd.overlayLayer >= 0 ? static_cast<float>(fd.overlayLayer) : -1.0f;

        for (int v = 0; v < 6; ++v) {
            const glm::vec4 p = xf * glm::vec4(kFaceVerts[face][v * 3 + 0],
                                               kFaceVerts[face][v * 3 + 1],
                                               kFaceVerts[face][v * 3 + 2], 1.0f);
            out.push_back(cx + p.x);
            out.push_back(cy + p.y);
            out.push_back(-5.0f + p.z * 0.01f);

            out.push_back(tint.r);
            out.push_back(tint.g);
            out.push_back(tint.b);

            out.push_back(kTexCoords[v * 2 + 0]);
            out.push_back(kTexCoords[v * 2 + 1]);

            out.push_back(layer);
            out.push_back(overlay);
            out.push_back(fd.tintBase ? 1.0f : 0.0f);
            out.push_back(fd.tintOverlay ? 1.0f : 0.0f);
            out.push_back(kFaceShading[face]);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Hotbar
// ---------------------------------------------------------------------------

Hotbar::Hotbar() {
    resize(9);
}

Hotbar::~Hotbar() {
    if (iconVAO != 0) glDeleteVertexArrays(1, &iconVAO);
    if (iconVBO != 0) glDeleteBuffers(1, &iconVBO);
}

void Hotbar::resize(int count) {
    if (count <= 0) count = 1;
    slotCount = count;
    slots.resize(slotCount);
    if (activeSlot >= slotCount) activeSlot = slotCount - 1;
}

void Hotbar::setSlot(int index, uint8_t blockId, int count) {
    if (index >= 0 && index < slotCount) {
        slots[index].blockId = blockId;
        slots[index].count = count;
    }
}

HotbarSlot Hotbar::getSlot(int index) const {
    if (index >= 0 && index < slotCount) return slots[index];
    return {0, 0};
}

void Hotbar::nextSlot() {
    if (isCreativeOpen) return;
    activeSlot = (activeSlot + 1) % slotCount;
}

void Hotbar::previousSlot() {
    if (isCreativeOpen) return;
    activeSlot = (activeSlot - 1 + slotCount) % slotCount;
}

uint8_t Hotbar::getActiveBlockId() const {
    if (activeSlot >= 0 && activeSlot < static_cast<int>(slots.size())) {
        return slots[activeSlot].blockId;
    }
    return 0;
}

void Hotbar::toggleCreativeInventory() {
    isCreativeOpen = !isCreativeOpen;
    std::cout << "[Creative Inventory] " << (isCreativeOpen ? "Opened" : "Closed") << std::endl;
}

void Hotbar::handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (!isCreativeOpen) return;

    const float W = static_cast<float>(screenWidth);
    const float H = static_cast<float>(screenHeight);
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);

    const std::vector<uint8_t> ids = creativeItemIds(creativeTab);
    const CreativeLayout cl = computeCreative(*this, W, H, ids.size());
    creativeX = cl.x;
    creativeY = cl.y;
    creativeW = cl.w;
    creativeH = cl.h;

    // Tabs sit just above the panel.
    for (int t = 0; t < 3; ++t) {
        const float tx = cl.x + kCreativeMargin + t * (kTabW + kTabGap);
        const float ty = cl.y - kTabH;
        if (mx >= tx && mx <= tx + kTabW && my >= ty && my <= ty + kTabH) {
            creativeTab = t;
            std::cout << "[Creative] Switched to tab: " << creativeTab << std::endl;
            return;
        }
    }

    // Item cells.
    for (size_t k = 0; k < ids.size(); ++k) {
        const glm::vec2 o = cellOrigin(cl, k);
        if (mx >= o.x && mx <= o.x + cl.box && my >= o.y && my <= o.y + cl.box) {
            setSlot(activeSlot, ids[k], 64);
            std::cout << "[Creative] Selected Block ID: " << static_cast<int>(ids[k]) << std::endl;
            return;
        }
    }
}

void Hotbar::updateBuffers(int screenWidth, int screenHeight) {
    const float W = static_cast<float>(screenWidth);
    const float H = static_cast<float>(screenHeight);
    const BarLayout bar = computeBar(*this, W, H);

    std::vector<float> iconVerts;

    for (int i = 0; i < slotCount; ++i) {
        const glm::vec2 c = slotCenter(bar, i);
        appendIcon(iconVerts, slots[i].blockId, c.x, c.y, bar.box);
    }

    if (isCreativeOpen) {
        const std::vector<uint8_t> ids = creativeItemIds(creativeTab);
        const CreativeLayout cl = computeCreative(*this, W, H, ids.size());
        creativeX = cl.x;
        creativeY = cl.y;
        creativeW = cl.w;
        creativeH = cl.h;

        for (size_t k = 0; k < ids.size(); ++k) {
            const glm::vec2 o = cellOrigin(cl, k);
            appendIcon(iconVerts, ids[k], o.x + cl.box * 0.5f, o.y + cl.box * 0.5f, cl.box);
        }
    }

    iconVertexCount = static_cast<unsigned int>(iconVerts.size() / 13);

    if (iconVAO == 0) glGenVertexArrays(1, &iconVAO);
    if (iconVBO == 0) glGenBuffers(1, &iconVBO);
    if (iconVertexCount == 0) return;

    glBindVertexArray(iconVAO);
    glBindBuffer(GL_ARRAY_BUFFER, iconVBO);
    glBufferData(GL_ARRAY_BUFFER, iconVerts.size() * sizeof(float), iconVerts.data(), GL_DYNAMIC_DRAW);

    const GLsizei stride = 13 * sizeof(float);
    for (int j = 0; j < 8; ++j) glEnableVertexAttribArray(j);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)(10 * sizeof(float)));
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)(11 * sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    glBindVertexArray(0);
}

void Hotbar::renderSlots(GLuint /*uiShader*/, GLuint /*texturedUIShader*/) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const float W = static_cast<float>(vp[2]);
    const float H = static_cast<float>(vp[3]);

    // Save the state we change, so the rest of the frame is unaffected.
    const GLboolean cullWasOn = glIsEnabled(GL_CULL_FACE);
    const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!ensureUiGL()) {
        if (cullWasOn) glEnable(GL_CULL_FACE);
        if (depthWasOn) glEnable(GL_DEPTH_TEST);
        return;
    }

    if (kDebugRedBox) {
        std::vector<Vtx> dbg;
        addRect(dbg, 20.0f, 20.0f, 220.0f, 80.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        drawUi(dbg, 0, false, W, H);
    }

    const BarLayout bar = computeBar(*this, W, H);
    const glm::vec4 white(1.0f);

    // 1. Fallback slot boxes, only when there is no bar texture.
    if (!bar.textured) {
        std::vector<Vtx> v;
        for (int i = 0; i < slotCount; ++i) {
            const glm::vec2 o = slotOrigin(bar, i);
            const glm::vec4 border = (i == activeSlot)
                ? glm::vec4(1.0f, 0.85f, 0.1f, 1.0f)
                : glm::vec4(0.25f, 0.25f, 0.25f, 0.8f);
            addRect(v, o.x - 3.0f, o.y - 3.0f, o.x + bar.box + 3.0f, o.y + bar.box + 3.0f, border);
            addRect(v, o.x, o.y, o.x + bar.box, o.y + bar.box, glm::vec4(0.12f, 0.12f, 0.12f, 0.75f));
        }
        drawUi(v, 0, false, W, H);
    }

    // 2. Bar texture.
    if (bar.textured) {
        std::vector<Vtx> v;
        addQuad(v, bar.x, bar.y, bar.x + bar.w, bar.y + bar.h, 0.0f, 0.0f, 1.0f, 1.0f, white);
        drawUi(v, customTextureID, true, W, H);
    }

    // 3. Selection frame, centered on the active slot. Drawn at the texture's own size.
    if (bar.textured && selectedTextureID != 0) {
        float selW = kFallbackSelW, selH = kFallbackSelH;
        textureSize(selectedTextureID, selW, selH, kFallbackSelW, kFallbackSelH);

        const glm::vec2 c = slotCenter(bar, activeSlot);
        const float halfW = selW * bar.s * 0.5f;
        const float halfH = selH * bar.s * 0.5f;
        std::vector<Vtx> v;
        addQuad(v, c.x - halfW, c.y - halfH, c.x + halfW, c.y + halfH,
                0.0f, 0.0f, 1.0f, 1.0f, white);
        drawUi(v, selectedTextureID, true, W, H);
    }

    // 4. Creative panel. Borders are drawn before fills so they frame the fill.
    if (isCreativeOpen) {
        const std::vector<uint8_t> ids = creativeItemIds(creativeTab);
        const CreativeLayout cl = computeCreative(*this, W, H, ids.size());
        creativeX = cl.x;
        creativeY = cl.y;
        creativeW = cl.w;
        creativeH = cl.h;

        const glm::vec4 dark(0.2f, 0.2f, 0.2f, 1.0f);
        const glm::vec4 cellDark(0.35f, 0.35f, 0.35f, 1.0f);
        const glm::vec4 cellFill(0.52f, 0.52f, 0.52f, 1.0f);

        std::vector<Vtx> v;

        for (int t = 0; t < 3; ++t) {
            const float tx = cl.x + kCreativeMargin + t * (kTabW + kTabGap);
            const float ty = cl.y - kTabH;
            const glm::vec4 fill = (creativeTab == t)
                ? glm::vec4(0.75f, 0.75f, 0.75f, 0.98f)
                : glm::vec4(0.5f, 0.5f, 0.5f, 0.85f);
            addRect(v, tx - 1.0f, ty - 1.0f, tx + kTabW + 1.0f, ty + kTabH + 1.0f, dark);
            addRect(v, tx, ty, tx + kTabW, ty + kTabH, fill);
        }

        addRect(v, cl.x - 2.0f, cl.y - 2.0f, cl.x + cl.w + 2.0f, cl.y + cl.h + 2.0f, dark);
        addRect(v, cl.x, cl.y, cl.x + cl.w, cl.y + cl.h, glm::vec4(0.75f, 0.75f, 0.75f, 0.98f));

        for (size_t k = 0; k < ids.size(); ++k) {
            const glm::vec2 o = cellOrigin(cl, k);
            addRect(v, o.x - 1.0f, o.y - 1.0f, o.x + cl.box + 1.0f, o.y + cl.box + 1.0f, cellDark);
            addRect(v, o.x, o.y, o.x + cl.box, o.y + cl.box, cellFill);
        }

        drawUi(v, 0, false, W, H);
    }

    if (kHotbarDebug) {
        static int frame = 0;
        if (++frame % 300 == 0) {
            std::cout << "[Hotbar] textured=" << bar.textured
                      << " bar=(" << bar.x << ", " << bar.y << ") "
                      << bar.w << "x" << bar.h
                      << " scale=" << bar.s
                      << " tex=" << customTextureID
                      << " sel=" << selectedTextureID
                      << " viewport=" << W << "x" << H << std::endl;
        }
    }

    if (cullWasOn) glEnable(GL_CULL_FACE);
    if (depthWasOn) glEnable(GL_DEPTH_TEST);
}

void Hotbar::render3DIcons(GLuint voxelShader, GLuint textureArrayID) {
    if (iconVAO == 0 || iconVertexCount == 0) return;

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const float width = static_cast<float>(vp[2]);
    const float height = static_cast<float>(vp[3]);

    const glm::mat4 orthoProj = glm::ortho(0.0f, width, height, 0.0f, -100.0f, 100.0f);
    const glm::mat4 identity(1.0f);

    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(voxelShader);

    const GLint isGuiLoc = glGetUniformLocation(voxelShader, "u_IsGUI");
    if (isGuiLoc != -1) glUniform1i(isGuiLoc, 1);

    const GLint unlitLoc = glGetUniformLocation(voxelShader, "u_Unlit");
    if (unlitLoc != -1) glUniform1i(unlitLoc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    glUniform1i(glGetUniformLocation(voxelShader, "textureArray"), 0);

    glUniformMatrix4fv(glGetUniformLocation(voxelShader, "model"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(glGetUniformLocation(voxelShader, "view"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(glGetUniformLocation(voxelShader, "projection"), 1, GL_FALSE, glm::value_ptr(orthoProj));

    glBindVertexArray(iconVAO);
    glDrawArrays(GL_TRIANGLES, 0, iconVertexCount);
    glBindVertexArray(0);

    if (isGuiLoc != -1) glUniform1i(isGuiLoc, 0);
    if (unlitLoc != -1) glUniform1i(unlitLoc, 0);
}