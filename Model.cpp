#include "Model.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using json = nlohmann::json;

namespace {

// Euler angles in degrees, applied X first, then Y, then Z.
glm::mat4 eulerMatrix(const glm::vec3& deg) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(deg.z), glm::vec3(0, 0, 1));
    m = glm::rotate(m, glm::radians(deg.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(deg.x), glm::vec3(1, 0, 0));
    return m;
}

// 1x1 white texture for models without one
GLuint whiteTexture() {
    static GLuint tex = 0;
    if (tex == 0) {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return tex;
}

// Flat normals for triangles that came without normals
void computeFlatNormals(std::vector<ModelVertex>& tris) {
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const glm::vec3 n = glm::cross(tris[i + 1].position - tris[i].position,
                                       tris[i + 2].position - tris[i].position);
        const float len = glm::length(n);
        const glm::vec3 nn = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
        tris[i].normal = tris[i + 1].normal = tris[i + 2].normal = nn;
    }
}

} // namespace

// ===========================================================================
// Mesh
// ===========================================================================

Mesh::Mesh(const std::vector<ModelVertex>& vertices) {
    vertexCount = static_cast<int>(vertices.size());
    if (vertexCount == 0) return;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ModelVertex), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, texCoords));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

Mesh::~Mesh() {
    if (VBO) glDeleteBuffers(1, &VBO);
    if (VAO) glDeleteVertexArrays(1, &VAO);
}

void Mesh::draw() const {
    if (VAO == 0 || vertexCount == 0) return;
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

// ===========================================================================
// Bone
// ===========================================================================

// position, then rotate/scale around the pivot (file pose first, then the Lua animation rotation)
glm::mat4 Bone::getLocalTransform() const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position + pivot);
    m *= glm::mat4_cast(restRotation);
    m *= eulerMatrix(rotation);
    m = glm::scale(m, scale);
    m = glm::translate(m, -pivot);
    return m;
}

// ===========================================================================
// AnimatedModel
// ===========================================================================

void AnimatedModel::loadFromFile(const std::string& filepath) {
    const std::string ext = filepath.size() >= 4 ? filepath.substr(filepath.size() - 4) : "";
    if (ext == ".b3d" || ext == ".B3D") {
        loadFromB3D(filepath);
    } else {
        loadFromJson(filepath);
    }
}

std::shared_ptr<Bone> AnimatedModel::getBone(const std::string& name) {
    auto it = boneMap.find(name);
    return it != boneMap.end() ? it->second : nullptr;
}

static void drawBoneRecursive(const std::shared_ptr<Bone>& bone, GLint modelLoc, const glm::mat4& parentMatrix) {
    const glm::mat4 globalMatrix = parentMatrix * bone->getLocalTransform();

    if (!bone->meshes.empty()) {
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(globalMatrix));
        for (const auto& m : bone->meshes) m->draw();
    }
    for (const auto& child : bone->children) {
        drawBoneRecursive(child, modelLoc, globalMatrix);
    }
}

void AnimatedModel::draw(GLuint shaderProgram, const glm::mat4& worldTransform) const {
    if (!loaded) return;

    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture != 0 ? texture : whiteTexture());
    const GLint texLoc = glGetUniformLocation(shaderProgram, "texture_diffuse1");
    if (texLoc >= 0) glUniform1i(texLoc, 0);

    const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    const glm::mat4 root = glm::scale(worldTransform, glm::vec3(scale));
    for (const auto& rootBone : rootBones) {
        drawBoneRecursive(rootBone, modelLoc, root);
    }
}

// ===========================================================================
// JSON (Bedrock / Blockbench "Bedrock Model" geometry)
//   Supports the 1.12+ format { "minecraft:geometry": [ ... ] } and the older
//   { "geometry.name": { ... } } format. Coordinates are in pixels (16 per block).
//   Per bone: pivot, rotation, parent, and any number of cubes.
//   Per cube: origin, size, uv (box UV [u, v] or per-face), inflate, mirror, pivot, rotation.
// ===========================================================================

namespace {

glm::vec3 vec3Of(const json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) return fallback;
    return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
}

struct UVRect { float u0, v0, u1, v1; bool use = true; };   // pixels; (u0,v0) = top-left

enum Face { EAST, WEST, UP, DOWN, SOUTH, NORTH };   // +X, -X, +Y, -Y, +Z, -Z

void appendCube(const json& cube, float texW, float texH, bool boneMirror, std::vector<ModelVertex>& out) {
    const glm::vec3 origin = vec3Of(cube, "origin", glm::vec3(0.0f));
    const glm::vec3 size   = vec3Of(cube, "size", glm::vec3(0.0f));
    const float inflate    = cube.value("inflate", 0.0f);
    const bool mirror      = cube.value("mirror", boneMirror);

    const glm::vec3 lo = (origin - inflate) / 16.0f;
    const glm::vec3 hi = (origin + size + inflate) / 16.0f;

    // ---- UV rectangles for each face (in pixels)
    UVRect uv[6];
    const float w = size.x, h = size.y, d = size.z;
    if (cube.contains("uv") && cube["uv"].is_array() && cube["uv"].size() >= 2) {
        // Box UV: the classic unfolded-box layout starting at (u, v)
        const float u = cube["uv"][0].get<float>(), v = cube["uv"][1].get<float>();
        uv[EAST]  = { u,                 v + d, u + d,             v + d + h };
        uv[NORTH] = { u + d,             v + d, u + d + w,         v + d + h };
        uv[WEST]  = { u + d + w,         v + d, u + d + w + d,     v + d + h };
        uv[SOUTH] = { u + d + w + d,     v + d, u + d + w + d + w, v + d + h };
        uv[UP]    = { u + d,             v,     u + d + w,         v + d };
        uv[DOWN]  = { u + d + w,         v + d, u + d + w + w,     v };       // stored upside down
        if (mirror) {
            std::swap(uv[EAST], uv[WEST]);
            for (auto& r : uv) std::swap(r.u0, r.u1);
        }
    } else if (cube.contains("uv") && cube["uv"].is_object()) {
        // Per-face UV: { "north": { "uv": [u, v], "uv_size": [w, h] }, ... }
        const char* names[6] = { "east", "west", "up", "down", "south", "north" };
        for (int f = 0; f < 6; ++f) {
            uv[f].use = false;
            if (!cube["uv"].contains(names[f])) continue;
            const json& fj = cube["uv"][names[f]];
            if (!fj.contains("uv") || fj["uv"].size() < 2) continue;
            const float fu = fj["uv"][0].get<float>(), fv = fj["uv"][1].get<float>();
            float fw = 0.0f, fh = 0.0f;
            if (fj.contains("uv_size") && fj["uv_size"].size() >= 2) {
                fw = fj["uv_size"][0].get<float>();
                fh = fj["uv_size"][1].get<float>();
            }
            uv[f] = { fu, fv, fu + fw, fv + fh, true };
        }
    } else {
        for (auto& r : uv) r = { 0.0f, 0.0f, texW, texH, true };   // no UV: whole texture
    }

    // ---- Corners of each face, seen from outside: bottom-left, bottom-right, top-right, top-left
    const float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
    const glm::vec3 corners[6][4] = {
        { {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1} },   // EAST  +X
        { {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0} },   // WEST  -X
        { {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0} },   // UP    +Y
        { {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1} },   // DOWN  -Y
        { {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1} },   // SOUTH +Z
        { {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0} },   // NORTH -Z
    };
    const glm::vec3 normals[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };

    // ---- Optional cube rotation around its own pivot
    glm::mat4 rot(1.0f);
    glm::vec3 cubePivot(0.0f);
    const bool rotated = cube.contains("rotation");
    if (rotated) {
        rot = eulerMatrix(vec3Of(cube, "rotation", glm::vec3(0.0f)));
        cubePivot = vec3Of(cube, "pivot", origin + size * 0.5f) / 16.0f;
    }

    for (int f = 0; f < 6; ++f) {
        if (!uv[f].use) continue;
        const UVRect& r = uv[f];
        const glm::vec2 tc[4] = {
            { r.u0 / texW, r.v1 / texH },   // bottom-left
            { r.u1 / texW, r.v1 / texH },   // bottom-right
            { r.u1 / texW, r.v0 / texH },   // top-right
            { r.u0 / texW, r.v0 / texH },   // top-left
        };
        const int order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k : order) {
            ModelVertex v;
            v.position = corners[f][k];
            v.normal = normals[f];
            v.texCoords = tc[k];
            if (rotated) {
                v.position = cubePivot + glm::vec3(rot * glm::vec4(v.position - cubePivot, 1.0f));
                v.normal = glm::vec3(rot * glm::vec4(v.normal, 0.0f));
            }
            out.push_back(v);
        }
    }
}

} // namespace

void AnimatedModel::loadFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open JSON model file: " << filepath << std::endl;
        return;
    }

    try {
        json j;
        file >> j;

        // Find the geometry and texture size (new and old Bedrock formats)
        json geom;
        float texW = 64.0f, texH = 64.0f;
        if (j.contains("minecraft:geometry") && j["minecraft:geometry"].is_array() && !j["minecraft:geometry"].empty()) {
            geom = j["minecraft:geometry"][0];
            if (geom.contains("description")) {
                texW = geom["description"].value("texture_width", 64.0f);
                texH = geom["description"].value("texture_height", 64.0f);
            }
        } else {
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key().rfind("geometry.", 0) == 0 && it->is_object()) {
                    geom = *it;
                    texW = geom.value("texturewidth", 64.0f);
                    texH = geom.value("textureheight", 64.0f);
                    break;
                }
            }
        }
        if (geom.is_null() || !geom.contains("bones")) {
            std::cerr << "[Model Error] No geometry/bones found in " << filepath << std::endl;
            return;
        }
        if (texW <= 0.0f) texW = 64.0f;
        if (texH <= 0.0f) texH = 64.0f;

        // Pass 1: create every bone
        std::vector<std::pair<std::shared_ptr<Bone>, std::string>> parents;
        for (const auto& bData : geom["bones"]) {
            auto bone = std::make_shared<Bone>();
            bone->name = bData.value("name", "bone_" + std::to_string(boneMap.size()));
            bone->pivot = vec3Of(bData, "pivot", glm::vec3(0.0f)) / 16.0f;
            bone->restRotation = glm::quat_cast(eulerMatrix(vec3Of(bData, "rotation", glm::vec3(0.0f))));

            if (bData.contains("cubes")) {
                std::vector<ModelVertex> verts;
                const bool boneMirror = bData.value("mirror", false);
                for (const auto& cube : bData["cubes"]) appendCube(cube, texW, texH, boneMirror, verts);
                if (!verts.empty()) {
                    bone->meshes.push_back(std::make_shared<Mesh>(verts));
                    bone->mesh = bone->meshes.front();
                }
            }

            boneMap[bone->name] = bone;
            parents.emplace_back(bone, bData.value("parent", std::string()));
        }

        // Pass 2: link parents (a child may be listed before its parent)
        for (auto& [bone, parentName] : parents) {
            auto it = boneMap.find(parentName);
            if (!parentName.empty() && it != boneMap.end() && it->second != bone) {
                it->second->children.push_back(bone);
            } else {
                rootBones.push_back(bone);
            }
        }

        loaded = true;
        std::cout << "[Model] Loaded JSON model: " << filepath << " (" << boneMap.size() << " bones)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Model Error] Could not read JSON model " << filepath << ": " << e.what() << std::endl;
    }
}

// ===========================================================================
// B3D (Blitz3D, used by Luanti mods)
//   Chunks: [tag(4)][size(4)][data(size)] - size does NOT include the 8-byte header.
//   B3D is left-handed, so Z is flipped and triangles are reversed to keep faces outward.
//   Static pose only: skinned meshes are shown in their bind pose (BONE/KEYS are skipped).
// ===========================================================================

namespace {

int readInt(std::ifstream& f)     { int v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; }
float readFloat(std::ifstream& f) { float v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; }

std::string readString(std::ifstream& f) {
    std::string s;
    char c;
    while (f.get(c) && c != '\0') s += c;
    return s;
}

struct ChunkHeader {
    std::string tag;
    std::streamoff end = 0;
};

// Reads a chunk header; fails if it doesn't fit inside `parentEnd`.
bool readChunk(std::ifstream& f, std::streamoff parentEnd, ChunkHeader& c) {
    const std::streamoff start = f.tellg();
    if (start < 0 || start + 8 > parentEnd) return false;
    char tag[4];
    if (!f.read(tag, 4)) return false;
    const int size = readInt(f);
    if (!f || size < 0) return false;
    c.tag.assign(tag, 4);
    c.end = start + 8 + size;
    return c.end <= parentEnd;
}

void parseMesh(std::ifstream& f, std::streamoff end, const std::shared_ptr<Bone>& bone) {
    readInt(f);   // brush id
    std::vector<ModelVertex> verts;
    std::vector<ModelVertex> tris;
    bool hasNormals = false;

    ChunkHeader c;
    while (f.good() && readChunk(f, end, c)) {
        if (c.tag == "VRTS") {
            const int flags = readInt(f);
            const int sets = readInt(f);
            const int setSize = readInt(f);
            hasNormals = (flags & 1) != 0;
            const std::streamoff stride = 12 + (hasNormals ? 12 : 0) + ((flags & 2) ? 16 : 0)
                                        + static_cast<std::streamoff>(sets) * setSize * 4;
            const std::streamoff count = stride > 0 ? (c.end - f.tellg()) / stride : 0;

            for (std::streamoff i = 0; i < count; ++i) {
                ModelVertex v;
                v.position.x = readFloat(f);
                v.position.y = readFloat(f);
                v.position.z = -readFloat(f);          // left-handed -> right-handed
                if (hasNormals) {
                    v.normal.x = readFloat(f);
                    v.normal.y = readFloat(f);
                    v.normal.z = -readFloat(f);
                }
                if (flags & 2) f.seekg(16, std::ios::cur);   // vertex colour (unused)
                for (int s = 0; s < sets; ++s) {
                    for (int k = 0; k < setSize; ++k) {
                        const float t = readFloat(f);
                        if (s == 0 && k == 0) v.texCoords.x = t;
                        if (s == 0 && k == 1) v.texCoords.y = t;
                    }
                }
                verts.push_back(v);
            }
        } else if (c.tag == "TRIS") {
            readInt(f);   // brush id
            while (f.good() && f.tellg() + std::streamoff(12) <= c.end) {
                const int a = readInt(f), b = readInt(f), d = readInt(f);
                const int n = static_cast<int>(verts.size());
                if (a < 0 || b < 0 || d < 0 || a >= n || b >= n || d >= n) continue;
                tris.push_back(verts[a]);   // reversed winding because Z was flipped
                tris.push_back(verts[d]);
                tris.push_back(verts[b]);
            }
        }
        f.clear();
        f.seekg(c.end, std::ios::beg);
    }

    if (tris.empty()) return;
    if (!hasNormals) computeFlatNormals(tris);
    bone->meshes.push_back(std::make_shared<Mesh>(tris));
    if (!bone->mesh) bone->mesh = bone->meshes.front();
}

void parseNode(std::ifstream& f, std::streamoff end, const std::shared_ptr<Bone>& parent, AnimatedModel& model) {
    auto bone = std::make_shared<Bone>();
    std::string name = readString(f);
    if (name.empty()) name = "bone_" + std::to_string(model.boneMap.size());
    while (model.boneMap.count(name)) name += "_";   // keep names unique

    glm::vec3 pos, scl;
    pos.x = readFloat(f); pos.y = readFloat(f); pos.z = -readFloat(f);
    scl.x = readFloat(f); scl.y = readFloat(f); scl.z = readFloat(f);
    const float rw = readFloat(f), rx = readFloat(f), ry = readFloat(f), rz = readFloat(f);   // w first

    bone->name = name;
    bone->position = pos;
    bone->scale = scl;
    bone->restRotation = glm::normalize(glm::quat(rw, -rx, -ry, rz));   // mirror for the Z flip

    model.boneMap[name] = bone;
    if (parent) parent->children.push_back(bone);
    else model.rootBones.push_back(bone);

    ChunkHeader c;
    while (f.good() && readChunk(f, end, c)) {
        if (c.tag == "MESH") parseMesh(f, c.end, bone);
        else if (c.tag == "NODE") parseNode(f, c.end, bone, model);
        // BONE / KEYS / ANIM (skeletal animation) are skipped for now
        f.clear();
        f.seekg(c.end, std::ios::beg);
    }
}

} // namespace

void AnimatedModel::loadFromB3D(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open .b3d binary file: " << filepath << std::endl;
        return;
    }

    ChunkHeader top;
    file.seekg(0, std::ios::end);
    const std::streamoff fileEnd = file.tellg();
    file.seekg(0, std::ios::beg);
    if (!readChunk(file, fileEnd, top) || top.tag != "BB3D") {
        std::cerr << "[Model Error] Invalid .b3d file header: " << filepath << std::endl;
        return;
    }
    readInt(file);   // version
    isB3D = true;
    scale = 0.1f;    // Luanti-style meshes: 10 units = 1 block

    ChunkHeader c;
    while (file.good() && readChunk(file, top.end, c)) {
        if (c.tag == "TEXS") {
            // First texture name is used if load_model isn't given one
            if (file.tellg() < c.end) textureHint = readString(file);
        } else if (c.tag == "NODE") {
            parseNode(file, c.end, nullptr, *this);
        }
        file.clear();
        file.seekg(c.end, std::ios::beg);
    }

    loaded = !rootBones.empty();
    int meshCount = 0;
    for (const auto& [n, b] : boneMap) meshCount += static_cast<int>(b->meshes.size());
    std::cout << "[Model] Loaded .b3d model: " << filepath << " (" << boneMap.size() << " nodes, "
              << meshCount << " meshes" << (textureHint.empty() ? "" : ", texture " + textureHint) << ")" << std::endl;
}
