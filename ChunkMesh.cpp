#include "ChunkMesh.h"
#include "Block.h"
#include "Types.h"
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

// Water top sits 1/16 below the block top. Every side face uses the same
// height so the surface is flat.
constexpr float kWaterSurfaceDrop = 0.0625f;
constexpr float kPlantInset       = 0.15f;

// Face order matches the neighbor slots: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
constexpr float kFaceShading[6] = { 0.8f, 0.8f, 1.0f, 0.5f, 0.7f, 0.7f };
constexpr int   kFaceOffset[6][3] = {
    { 1,  0,  0}, {-1,  0,  0},
    { 0,  1,  0}, { 0, -1,  0},
    { 0,  0,  1}, { 0,  0, -1}
};

struct FaceStyle {
    float r, g, b;
    float baseLayer;
    float overlayLayer;
    float tintBase;
    float tintOverlay;
    float shade;
};

inline float tintFlag(bool on) { return on ? 1.0f : 0.0f; }

inline Vertex makeVertex(float x, float y, float z, float u, float v, const FaceStyle& s) {
    return Vertex{x, y, z, s.r, s.g, s.b, u, v,
                  s.baseLayer, s.overlayLayer, s.tintBase, s.tintOverlay, s.shade};
}

// Two triangles: (0,1,2) and (0,2,3).
inline void pushQuad(std::vector<Vertex>& buf, const Vertex (&q)[4]) {
    buf.push_back(q[0]); buf.push_back(q[1]); buf.push_back(q[2]);
    buf.push_back(q[0]); buf.push_back(q[2]); buf.push_back(q[3]);
}

// Same quad, wound both ways, so plants are visible from either side.
inline void pushDoubleSided(std::vector<Vertex>& buf, const Vertex (&q)[4]) {
    pushQuad(buf, q);
    buf.push_back(q[0]); buf.push_back(q[2]); buf.push_back(q[1]);
    buf.push_back(q[0]); buf.push_back(q[3]); buf.push_back(q[2]);
}

// Corner positions for a cube face. Corners go in the same order as the UVs
// (0,1), (1,1), (1,0), (0,0).
void faceCorners(int f, float x, float y, float z, float topY, float out[4][3]) {
    auto set = [&](int i, float px, float py, float pz) {
        out[i][0] = px; out[i][1] = py; out[i][2] = pz;
    };
    switch (f) {
    case 0: // +X
        set(0, x + 1, y,    z + 1);
        set(1, x + 1, y,    z);
        set(2, x + 1, topY, z);
        set(3, x + 1, topY, z + 1);
        break;
    case 1: // -X
        set(0, x, y,    z);
        set(1, x, y,    z + 1);
        set(2, x, topY, z + 1);
        set(3, x, topY, z);
        break;
    case 2: // +Y (top)
        set(0, x,     topY, z + 1);
        set(1, x + 1, topY, z + 1);
        set(2, x + 1, topY, z);
        set(3, x,     topY, z);
        break;
    case 3: // -Y (bottom)
        set(0, x,     y, z);
        set(1, x + 1, y, z);
        set(2, x + 1, y, z + 1);
        set(3, x,     y, z + 1);
        break;
    case 4: // +Z
        set(0, x,     y,    z + 1);
        set(1, x + 1, y,    z + 1);
        set(2, x + 1, topY, z + 1);
        set(3, x,     topY, z + 1);
        break;
    default: // -Z
        set(0, x + 1, y,    z);
        set(1, x,     y,    z);
        set(2, x,     topY, z);
        set(3, x + 1, topY, z);
        break;
    }
}

void setupVertexAttributes() {
    const GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, posX));          glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintR));         glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, texCoordX));     glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, baseLayer));     glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, overlayLayer));  glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintBase));     glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintOverlay));  glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, faceShading));  glEnableVertexAttribArray(7);
}

void uploadBuffer(GLuint& vao, GLuint& vbo, const std::vector<Vertex>& verts) {
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 verts.size() * sizeof(Vertex),
                 verts.empty() ? nullptr : verts.data(),
                 GL_STATIC_DRAW);
    setupVertexAttributes();
}

inline bool isWaterProp(const BlockProperty& p) {
    return p.name == "water" || p.name == "flowing_water";
}

inline bool isPlantLike(const BlockProperty& p) {
    return p.pattern == "plantlike";
}

inline bool isCutout(const BlockProperty& p) {
    return p.isCutout;
}

inline bool canWaterlog(const BlockProperty& p) {
    return (isPlantLike(p) || p.isTransparent) && !isWaterProp(p);
}

inline bool isOpaqueSolid(const BlockProperty& p) {
    return !p.isTransparent && !isPlantLike(p) && !isCutout(p);
}

// Robust face culling rules preventing clipping artifacts between water and waterlogged foliage
bool isFaceHidden(const BlockProperty& self, uint8_t selfId, bool renderingWater,
                  const BlockProperty& nb, uint8_t nbId, bool nbWaterlogged) {
    if (isPlantLike(nb) && !nbWaterlogged) return false; // dry plants do not occlude

    if (renderingWater) {
        return isWaterProp(nb) || nbWaterlogged || isOpaqueSolid(nb);
    }
    if (isCutout(self)) {
        return isOpaqueSolid(nb);
    }
    if (self.isTransparent) {
        return isOpaqueSolid(nb) || nbId == selfId;
    }
    return isOpaqueSolid(nb);
}

} // namespace

ChunkMesh::ChunkMesh() : opaqueVAO(0), opaqueVBO(0), opaqueVertexCount(0),
                         transparentVAO(0), transparentVBO(0), transparentVertexCount(0),
                         isDirty(true), meshReadyForUpload(false) {
    std::memset(blocks, 0, sizeof(blocks));
    std::memset(metadata, 0, sizeof(metadata));
    for (int i = 0; i < 6; ++i) neighbors[i] = nullptr;
}

ChunkMesh::~ChunkMesh() {
    if (opaqueVAO != 0)       glDeleteVertexArrays(1, &opaqueVAO);
    if (opaqueVBO != 0)       glDeleteBuffers(1, &opaqueVBO);
    if (transparentVAO != 0) glDeleteVertexArrays(1, &transparentVAO);
    if (transparentVBO != 0) glDeleteBuffers(1, &transparentVBO);
}

void ChunkMesh::setBlock(int x, int y, int z, uint8_t blockId) {
    if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH) {
        blocks[x][y][z] = blockId;
        isDirty = true;
    }
}

uint8_t ChunkMesh::getLocalBlock(int x, int y, int z) const {
    if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH) {
        return blocks[x][y][z];
    }
    return 0;
}

uint8_t ChunkMesh::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;

    if (x < 0)            return neighbors[1] ? neighbors[1]->getLocalBlock(x + CHUNK_WIDTH, y, z) : 0;
    if (x >= CHUNK_WIDTH) return neighbors[0] ? neighbors[0]->getLocalBlock(x - CHUNK_WIDTH, y, z) : 0;
    if (z < 0)            return neighbors[5] ? neighbors[5]->getLocalBlock(x, y, z + CHUNK_WIDTH) : 0;
    if (z >= CHUNK_WIDTH) return neighbors[4] ? neighbors[4]->getLocalBlock(x, y, z - CHUNK_WIDTH) : 0;

    return blocks[x][y][z];
}

void ChunkMesh::setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta) {
    if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH) {
        blocks[x][y][z] = blockId;
        metadata[x][y][z] = meta;
        isDirty = true;
    }
}

void ChunkMesh::setMetadata(int x, int y, int z, uint8_t meta) {
    if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH) {
        metadata[x][y][z] = meta;
    }
}

uint8_t ChunkMesh::getMetadata(int x, int y, int z) const {
    if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH) {
        return metadata[x][y][z];
    }
    return 0;
}

void ChunkMesh::generateMeshData() {
    cpuOpaqueVertices.clear();
    cpuTransparentVertices.clear();

    auto& registry = BlockRegistry::getInstance();
    const uint8_t waterId = registry.getBlockId("water");

    auto emitPlant = [&](int x, int y, int z, const BlockProperty& prop) {
        const FaceStyle style{
            prop.defaultTint.r, prop.defaultTint.g, prop.defaultTint.b,
            static_cast<float>(prop.faces[0].baseLayer >= 0 ? prop.faces[0].baseLayer
                                                            : prop.faces[2].baseLayer),
            prop.faces[0].overlayLayer >= 0 ? static_cast<float>(prop.faces[0].overlayLayer) : -1.0f,
            tintFlag(prop.faces[0].tintBase),
            tintFlag(prop.faces[0].tintOverlay),
            1.0f
        };

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        const float minX = fx + kPlantInset, maxX = fx + 1.0f - kPlantInset;
        const float minZ = fz + kPlantInset, maxZ = fz + 1.0f - kPlantInset;
        const float minY = fy, maxY = fy + 1.0f;

        const Vertex p1[4] = {
            makeVertex(minX, minY, minZ, 0.0f, 1.0f, style),
            makeVertex(maxX, minY, maxZ, 1.0f, 1.0f, style),
            makeVertex(maxX, maxY, maxZ, 1.0f, 0.0f, style),
            makeVertex(minX, maxY, minZ, 0.0f, 0.0f, style),
        };
        const Vertex p2[4] = {
            makeVertex(minX, minY, maxZ, 0.0f, 1.0f, style),
            makeVertex(maxX, minY, minZ, 1.0f, 1.0f, style),
            makeVertex(maxX, maxY, minZ, 1.0f, 0.0f, style),
            makeVertex(minX, maxY, maxZ, 0.0f, 0.0f, style),
        };

        pushDoubleSided(cpuOpaqueVertices, p1);
        pushDoubleSided(cpuOpaqueVertices, p2);
    };

    auto emitCube = [&](int x, int y, int z, uint8_t blockId, const BlockProperty& selfProp,
                        const BlockProperty& renderProp, bool renderingWater) {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        const float topY = renderingWater ? (fy + 1.0f - kWaterSurfaceDrop) : (fy + 1.0f);

        for (int f = 0; f < 6; ++f) {
            const int nx = x + kFaceOffset[f][0];
            const int ny = y + kFaceOffset[f][1];
            const int nz = z + kFaceOffset[f][2];

            const uint8_t neighborId = getBlock(nx, ny, nz);
            if (neighborId != 0) {
                const auto& nProp = registry.getBlock(neighborId);
                const uint8_t neighborMeta = getMetadata(nx, ny, nz);
                const bool nWaterlogged = canWaterlog(nProp) && ((neighborMeta & 1) != 0);

                if (isFaceHidden(selfProp, blockId, renderingWater, nProp, neighborId, nWaterlogged)) {
                    continue;
                }
            }

            const auto& faceProp = renderProp.faces[f];
            const FaceStyle style{
                renderProp.defaultTint.r, renderProp.defaultTint.g, renderProp.defaultTint.b,
                static_cast<float>(faceProp.baseLayer),
                faceProp.overlayLayer >= 0 ? static_cast<float>(faceProp.overlayLayer) : -1.0f,
                tintFlag(faceProp.tintBase),
                tintFlag(faceProp.tintOverlay),
                kFaceShading[f]
            };

            float fv[4][3];
            faceCorners(f, fx, fy, fz, topY, fv);

            const Vertex q[4] = {
                makeVertex(fv[0][0], fv[0][1], fv[0][2], 0.0f, 1.0f, style),
                makeVertex(fv[1][0], fv[1][1], fv[1][2], 1.0f, 1.0f, style),
                makeVertex(fv[2][0], fv[2][1], fv[2][2], 1.0f, 0.0f, style),
                makeVertex(fv[3][0], fv[3][1], fv[3][2], 0.0f, 0.0f, style),
            };

            const bool blended = renderProp.isTransparent && !isCutout(renderProp);
            auto& buf = blended ? cpuTransparentVertices : cpuOpaqueVertices;
            pushQuad(buf, q);
        }
    };

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                const uint8_t blockId = blocks[x][y][z];
                if (blockId == 0) continue;

                const auto& prop = registry.getBlock(blockId);
                const bool waterlogged = canWaterlog(prop) && ((metadata[x][y][z] & 1) != 0);

                if (isPlantLike(prop)) {
                    emitPlant(x, y, z, prop);
                    if (!waterlogged) continue;
                }

                const bool useWater = waterlogged && waterId != 0;
                const BlockProperty& renderProp = useWater ? registry.getBlock(waterId) : prop;
                emitCube(x, y, z, blockId, prop, renderProp, isWaterProp(renderProp));
            }
        }
    }

    meshReadyForUpload = true;
    isDirty = true;
}

void ChunkMesh::uploadMesh() {
    if (!meshReadyForUpload) return;

    opaqueVertexCount      = static_cast<GLsizei>(cpuOpaqueVertices.size());
    transparentVertexCount = static_cast<GLsizei>(cpuTransparentVertices.size());

    uploadBuffer(opaqueVAO,      opaqueVBO,      cpuOpaqueVertices);
    uploadBuffer(transparentVAO, transparentVBO, cpuTransparentVertices);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    meshReadyForUpload = false;
    isDirty = false;
}

void ChunkMesh::drawOpaque() const {
    if (opaqueVertexCount == 0 || opaqueVAO == 0) return;
    glBindVertexArray(opaqueVAO);
    glDrawArrays(GL_TRIANGLES, 0, opaqueVertexCount);
    glBindVertexArray(0);
}

void ChunkMesh::drawTransparent() const {
    if (transparentVertexCount == 0 || transparentVAO == 0) return;
    glBindVertexArray(transparentVAO);
    glDrawArrays(GL_TRIANGLES, 0, transparentVertexCount);
    glBindVertexArray(0);
}