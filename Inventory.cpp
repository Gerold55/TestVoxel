#include "Inventory.h"
#include "Block.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include <iostream>
#include <algorithm>
#include <tuple>

extern Hotbar* g_Hotbar;
extern FontRenderer* g_FontRenderer;
extern GLuint g_TextureArrayID;

namespace {

// If text doesn't appear in the inventory, set this to true.
constexpr bool kTextUsesTexturedProgram = false;

// ===========================================================================
// Shared state between C++ and the Lua bindings
// ===========================================================================

struct DrawContext {
    bool   active = false;     // drawing is only allowed while a Lua screen is being rendered
    GLuint uiProgram = 0;
    GLuint texturedProgram = 0;
    float  W = 0.0f, H = 0.0f;
};

DrawContext  s_ctx;
sol::state*  s_lua = nullptr;
GLFWwindow*  s_window = nullptr;
std::string  s_lastLuaError;
bool         s_wasOpen = false;

// ===========================================================================
// GL state guard
// ===========================================================================

struct GLStateGuard {
    GLint program = 0, vao = 0, arrayBuffer = 0, activeTex = 0, tex2DArray = 0, tex2D = 0;
    GLint depthFunc = GL_LESS;
    GLint blendSrcRGB = 0, blendDstRGB = 0, blendSrcA = 0, blendDstA = 0;
    GLboolean depth = GL_FALSE, cull = GL_FALSE, blend = GL_FALSE, scissor = GL_FALSE;
    GLboolean depthMask = GL_TRUE;

    GLStateGuard() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &tex2DArray);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2D);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
        depth   = glIsEnabled(GL_DEPTH_TEST);
        cull    = glIsEnabled(GL_CULL_FACE);
        blend   = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    }

    ~GLStateGuard() {
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex2DArray);
        glBindTexture(GL_TEXTURE_2D, tex2D);
        glActiveTexture(activeTex);
        glBlendFuncSeparate(blendSrcRGB, blendDstRGB, blendSrcA, blendDstA);
        glDepthFunc(depthFunc);
        glDepthMask(depthMask);
        depth   ? glEnable(GL_DEPTH_TEST)   : glDisable(GL_DEPTH_TEST);
        cull    ? glEnable(GL_CULL_FACE)    : glDisable(GL_CULL_FACE);
        blend   ? glEnable(GL_BLEND)        : glDisable(GL_BLEND);
        scissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }
};

// ===========================================================================
// Low-level drawing
// ===========================================================================

struct QuadCorner { float x, y, u, v; };

const FaceDefinition* getIconFace(const BlockProperty& block) {
    const int order[] = { 4, 2, 0, 1, 5, 3 };
    for (int f : order) {
        if (block.faces[f].baseLayer >= 0) return &block.faces[f];
    }
    return nullptr;
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Inventory] Icon shader compile error: " << log << std::endl;
    }
    return s;
}

GLuint getIconProgram() {
    static GLuint program = 0;
    if (program) return program;

    const char* vs = R"(#version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec3 aUVL;
        out vec3 vUVL;
        void main() { vUVL = aUVL; gl_Position = vec4(aPos, 0.0, 1.0); })";

    const char* fs = R"(#version 330 core
        in vec3 vUVL;
        uniform sampler2DArray uTex;
        uniform vec4 uTint;
        out vec4 FragColor;
        void main() {
            vec4 c = texture(uTex, vUVL) * uTint;
            if (c.a < 0.1) discard;
            FragColor = c;
        })";

    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[Inventory] Icon shader link error: " << log << std::endl;
    }
    return program;
}

void resetUIState() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    const float sw = s_ctx.W, sh = s_ctx.H;
    if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0) return;
    float x0 = (x / sw) * 2.0f - 1.0f;
    float x1 = ((x + w) / sw) * 2.0f - 1.0f;
    float y1 = 1.0f - (y / sh) * 2.0f;
    float y0 = 1.0f - ((y + h) / sh) * 2.0f;

    float vertices[36] = {
        x0, y0, r, g, b, a,   x1, y0, r, g, b, a,   x1, y1, r, g, b, a,
        x0, y0, r, g, b, a,   x1, y1, r, g, b, a,   x0, y1, r, g, b, a
    };

    static GLuint vao = 0, vbo = 0;
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }

    resetUIState();
    glUseProgram(s_ctx.uiProgram);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void drawTexturedQuad(const QuadCorner c[4], int layer, float r, float g, float b) {
    const float sw = s_ctx.W, sh = s_ctx.H;
    if (sw <= 0 || sh <= 0 || layer < 0 || g_TextureArrayID == 0) return;

    float L = static_cast<float>(layer);
    float v[4][5];
    for (int i = 0; i < 4; ++i) {
        v[i][0] = (c[i].x / sw) * 2.0f - 1.0f;
        v[i][1] = 1.0f - (c[i].y / sh) * 2.0f;
        v[i][2] = c[i].u;
        v[i][3] = c[i].v;
        v[i][4] = L;
    }
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    float vertices[30];
    for (int i = 0; i < 6; ++i)
        for (int k = 0; k < 5; ++k)
            vertices[i * 5 + k] = v[idx[i]][k];

    static GLuint vao = 0, vbo = 0;
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }

    GLuint prog = getIconProgram();
    resetUIState();
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
    glUniform4f(glGetUniformLocation(prog, "uTint"), r, g, b, 1.0f);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void drawFace(const FaceDefinition& face, const glm::vec3& tint, const QuadCorner c[4], float shade) {
    if (face.baseLayer < 0) return;

    glm::vec3 base = face.tintBase ? tint : glm::vec3(1.0f);
    drawTexturedQuad(c, face.baseLayer, base.r * shade, base.g * shade, base.b * shade);

    if (face.overlayLayer >= 0) {
        glm::vec3 ov = face.tintOverlay ? tint : glm::vec3(1.0f);
        drawTexturedQuad(c, face.overlayLayer, ov.r * shade, ov.g * shade, ov.b * shade);
    }
}

void drawFlatIcon(const BlockProperty& block, float x, float y, float s) {
    const FaceDefinition* face = getIconFace(block);
    if (!face) return;
    QuadCorner c[4] = {
        { x,     y,     0.0f, 0.0f },
        { x + s, y,     1.0f, 0.0f },
        { x + s, y + s, 1.0f, 1.0f },
        { x,     y + s, 0.0f, 1.0f },
    };
    drawFace(*face, block.defaultTint, c, 1.0f);
}

// True isometric cube (matches the hotbar's 35.264 degree rotation)
void drawCubeIcon(const BlockProperty& block, float x, float y, float s) {
    const FaceDefinition* fallback = getIconFace(block);
    if (!fallback) return;

    auto pick = [&](int f) -> const FaceDefinition& {
        return block.faces[f].baseLayer >= 0 ? block.faces[f] : *fallback;
    };

    const float w    = s * 0.8660f;
    const float edge = w * 0.5774f;
    const float d    = w * 0.2887f;

    const float x0 = x + (s - w) * 0.5f;
    const float x1 = x0 + w;
    const float cx = x0 + w * 0.5f;

    const float yTop = y;
    const float yQ1  = y + d;
    const float yMid = y + 2.0f * d;
    const float yQ3  = yQ1 + edge;
    const float yBot = yMid + edge;

    QuadCorner top[4] = {
        { cx, yTop, 0.0f, 0.0f }, { x1, yQ1, 1.0f, 0.0f },
        { cx, yMid, 1.0f, 1.0f }, { x0, yQ1, 0.0f, 1.0f },
    };
    QuadCorner left[4] = {
        { x0, yQ1, 0.0f, 0.0f }, { cx, yMid, 1.0f, 0.0f },
        { cx, yBot, 1.0f, 1.0f }, { x0, yQ3, 0.0f, 1.0f },
    };
    QuadCorner right[4] = {
        { cx, yMid, 0.0f, 0.0f }, { x1, yQ1, 1.0f, 0.0f },
        { x1, yQ3, 1.0f, 1.0f }, { cx, yBot, 0.0f, 1.0f },
    };

    drawFace(pick(2), block.defaultTint, top,   1.0f);
    drawFace(pick(4), block.defaultTint, left,  0.8f);
    drawFace(pick(0), block.defaultTint, right, 0.6f);
}

// Purple/black checkerboard for blocks without a texture
void drawMissing(float x, float y, float s) {
    const float h = s * 0.5f;
    drawRect(x,     y,     h, h, 1, 0, 1, 1);
    drawRect(x + h, y,     h, h, 0, 0, 0, 1);
    drawRect(x,     y + h, h, h, 0, 0, 0, 1);
    drawRect(x + h, y + h, h, h, 1, 0, 1, 1);
}

void drawItemIcon(uint8_t id, float x, float y, float s) {
    if (id == 0) return;
    const auto& block = BlockRegistry::getInstance().getBlock(id);
    if (!getIconFace(block)) {
        const float inset = s * 0.15f;
        drawMissing(x + inset, y + inset, s - 2 * inset);
        return;
    }
    if (block.pattern == "cube") drawCubeIcon(block, x, y, s);
    else                         drawFlatIcon(block, x, y, s);
}

// ===========================================================================
// Lua helpers
// ===========================================================================

void reportLuaError(const char* fn, const std::string& msg) {
    // Only print when the error changes, so a broken script doesn't spam every frame
    std::string full = std::string(fn) + ": " + msg;
    if (full != s_lastLuaError) {
        std::cerr << "[Lua UI] " << full << std::endl;
        s_lastLuaError = full;
    }
}

// Calls a global Lua function if it exists. Returns the first return value (or nil).
template <typename... Args>
sol::object callLua(const char* fn, Args&&... args) {
    if (!s_lua) return sol::lua_nil;
    sol::protected_function f = (*s_lua)[fn];
    if (!f.valid()) return sol::make_object(s_lua->lua_state(), sol::lua_nil);

    sol::protected_function_result res = f(std::forward<Args>(args)...);
    if (!res.valid()) {
        sol::error err = res;
        reportLuaError(fn, err.what());
        return sol::make_object(s_lua->lua_state(), sol::lua_nil);
    }
    if (res.return_count() == 0) return sol::make_object(s_lua->lua_state(), sol::lua_nil);
    return res.get<sol::object>();
}

bool luaTruthy(const sol::object& o) {
    return o.valid() && o.get_type() != sol::type::lua_nil &&
           !(o.get_type() == sol::type::boolean && !o.as<bool>());
}

} // namespace

// ===========================================================================
// InventorySystem
// ===========================================================================

void InventorySystem::toggleCreative() {
    creativeOpen = !creativeOpen;
}

void InventorySystem::setCreativeOpen(bool open) {
    creativeOpen = open;
}

bool InventorySystem::isCreativeOpen() const {
    if (g_Hotbar && g_Hotbar->isCreativeOpen) return true;
    return creativeOpen;
}

// Every creative-visible block name.
std::vector<std::string> InventorySystem::getCurrentItems() {
    std::vector<std::string> out;
    auto& reg = BlockRegistry::getInstance();
    for (int id = 1; id < 255; ++id) {
        const auto& b = reg.getBlock(static_cast<uint8_t>(id));
        if (!b.name.empty() && b.name != "air" && b.in_creative) out.push_back(b.name);
    }
    return out;
}

void InventorySystem::registerLuaBindings(sol::state& lua) {
    s_lua = &lua;

    // ---------------- ui.* ----------------
    sol::table ui = lua["ui"].get_or_create<sol::table>();

    // ui.draw_rect(x, y, w, h, r, g, b [, a])
    ui.set_function("draw_rect", [](float x, float y, float w, float h,
                                     float r, float g, float b, sol::optional<float> a) {
        if (!s_ctx.active) return;
        drawRect(x, y, w, h, r, g, b, a.value_or(1.0f));
    });

    // ui.draw_text(text, x, y, scale, r, g, b [, a])
    // NOTE: main.cpp replaces this with a version that uses the real font shader.
    ui.set_function("draw_text", [](const std::string& text, float x, float y, float scale,
                                     float r, float g, float b, sol::optional<float> a) {
        if (!s_ctx.active || !g_FontRenderer) return;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        Dimensions2D winSize{ static_cast<int>(s_ctx.W), static_cast<int>(s_ctx.H) };
        GLuint prog = kTextUsesTexturedProgram ? s_ctx.texturedProgram : s_ctx.uiProgram;
        g_FontRenderer->renderText(prog, text, x, y, scale, scale,
                                   glm::vec4(r, g, b, a.value_or(1.0f)), winSize);
    });

    // ui.draw_block_icon(name, x, y, size) - 3D cube or flat sprite, picked automatically
    ui.set_function("draw_block_icon", [](const std::string& name, float x, float y, float size) {
        if (!s_ctx.active || name.empty()) return;
        drawItemIcon(BlockRegistry::getInstance().getBlockId(name), x, y, size);
    });

    // ui.get_all_blocks() -> { "stone", "dirt", ... }  (only blocks with in_creative = true)
    ui.set_function("get_all_blocks", [this]() {
        return sol::as_table(getCurrentItems());
    });

    // ui.get_block_info(name) -> { id, name, pattern, has_texture, in_creative } or nil
    ui.set_function("get_block_info", [](const std::string& name, sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        auto& reg = BlockRegistry::getInstance();
        uint8_t id = reg.getBlockId(name);
        if (id == 0) return sol::make_object(lv, sol::lua_nil);
        const auto& b = reg.getBlock(id);
        if (b.name.empty()) return sol::make_object(lv, sol::lua_nil);

        sol::table t = lv.create_table();
        t["id"]          = static_cast<int>(id);
        t["name"]        = b.name;
        t["pattern"]     = b.pattern;
        t["has_texture"] = (getIconFace(b) != nullptr);
        t["in_creative"] = b.in_creative;
        return t;
    });

    // ui.get_mouse() -> x, y, left_down, right_down
    // NOTE: main.cpp replaces this with a version that scales to framebuffer pixels.
    ui.set_function("get_mouse", []() {
        double mx = -1.0, my = -1.0;
        bool left = false, right = false;
        if (s_window) {
            glfwGetCursorPos(s_window, &mx, &my);
            left  = glfwGetMouseButton(s_window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
            right = glfwGetMouseButton(s_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        }
        return std::make_tuple(mx, my, left, right);
    });

    // ui.get_time() -> seconds since start (for blinking cursors, animations)
    ui.set_function("get_time", []() { return glfwGetTime(); });

    // Key / action constants for handle_creative_key
    sol::table keys = lua.create_table();
    keys["BACKSPACE"] = GLFW_KEY_BACKSPACE;
    keys["ESCAPE"]    = GLFW_KEY_ESCAPE;
    keys["ENTER"]     = GLFW_KEY_ENTER;
    keys["KP_ENTER"]  = GLFW_KEY_KP_ENTER;
    keys["DELETE"]    = GLFW_KEY_DELETE;
    keys["UP"]        = GLFW_KEY_UP;
    keys["DOWN"]      = GLFW_KEY_DOWN;
    keys["PAGE_UP"]   = GLFW_KEY_PAGE_UP;
    keys["PAGE_DOWN"] = GLFW_KEY_PAGE_DOWN;
    ui["keys"] = keys;

    sol::table actions = lua.create_table();
    actions["PRESS"]   = GLFW_PRESS;
    actions["RELEASE"] = GLFW_RELEASE;
    actions["REPEAT"]  = GLFW_REPEAT;
    ui["actions"] = actions;

    // NOTE: the hotbar.* table is defined in main.cpp (1-based slots).
}

void InventorySystem::renderCreativeInventory(GLFWwindow* window, sol::state& lua, float width, float height,
                                              GLuint uiProgram, GLuint texturedUIProgram) {
    s_lua = &lua;
    s_window = window;

    const bool open = isCreativeOpen();
    if (!open) {
        if (s_wasOpen) callLua("on_creative_closed");
        s_wasOpen = false;
        return;
    }
    if (!s_wasOpen) callLua("on_creative_opened");
    s_wasOpen = true;

    renderLuaScreen(window, lua, "render_creative_inventory", width, height, uiProgram, texturedUIProgram);
}

void InventorySystem::renderLuaScreen(GLFWwindow* window, sol::state& lua, const char* luaFunction,
                                      float width, float height, GLuint uiProgram, GLuint texturedUIProgram) {
    s_lua = &lua;
    s_window = window;

    GLStateGuard stateGuard;   // restores all GL state when this function returns

    s_ctx.active = true;
    s_ctx.uiProgram = uiProgram;
    s_ctx.texturedProgram = texturedUIProgram;
    s_ctx.W = width;
    s_ctx.H = height;

    callLua(luaFunction, width, height);

    s_ctx.active = false;
}

void InventorySystem::handleCreativeClick(double mx, double my, float width, float height) {
    if (!isCreativeOpen()) return;
    callLua("handle_creative_click", mx, my, width, height);
}

void InventorySystem::handleCreativeScroll(double yoffset) {
    if (!isCreativeOpen()) return;
    callLua("handle_creative_scroll", yoffset);
}

bool InventorySystem::handleCreativeKey(int key, int action, int mods) {
    if (!isCreativeOpen()) return false;
    return luaTruthy(callLua("handle_creative_key", key, action, mods));
}

bool InventorySystem::handleCreativeChar(unsigned int codepoint) {
    if (!isCreativeOpen()) return false;
    std::string ch;
    if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
    return luaTruthy(callLua("handle_creative_char", static_cast<int>(codepoint), ch));
}
