#include "ChunkMesh.h"
#include "Block.h"
#include "Types.h"
#include <iostream>
#include <vector>
#include <cstring>

ChunkMesh::ChunkMesh() : opaqueVAO(0), opaqueVBO(0), opaqueVertexCount(0),
                         transparentVAO(0), transparentVBO(0), transparentVertexCount(0),
                         isDirty(true), meshReadyForUpload(false) {
    std::memset(blocks, 0, sizeof(blocks));
    std::memset(metadata, 0, sizeof(metadata));
    for (int i = 0; i < 6; ++i) neighbors[i] = nullptr;
}

ChunkMesh::~ChunkMesh() {
    if (opaqueVAO != 0) glDeleteVertexArrays(1, &opaqueVAO);
    if (opaqueVBO != 0) glDeleteBuffers(1, &opaqueVBO);
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

    if (x < 0) {
        return neighbors[1] ? neighbors[1]->getLocalBlock(x + CHUNK_WIDTH, y, z) : 0;
    }
    if (x >= CHUNK_WIDTH) {
        return neighbors[0] ? neighbors[0]->getLocalBlock(x - CHUNK_WIDTH, y, z) : 0;
    }

    if (z < 0) {
        return neighbors[5] ? neighbors[5]->getLocalBlock(x, y, z + CHUNK_WIDTH) : 0;
    }
    if (z >= CHUNK_WIDTH) {
        return neighbors[4] ? neighbors[4]->getLocalBlock(x, y, z - CHUNK_WIDTH) : 0;
    }

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

    const float shading[6] = { 0.8f, 0.8f, 1.0f, 0.5f, 0.7f, 0.7f };

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                uint8_t blockId = blocks[x][y][z];
                if (blockId == 0) continue;

                const auto& prop = BlockRegistry::getInstance().getBlock(blockId);

                // --- 1. PLANTLIKE (Cross-planes with safe overlay check) ---
                if (prop.pattern == "plantlike") {
                    float r = prop.defaultTint.r;
                    float g = prop.defaultTint.g;
                    float b = prop.defaultTint.b;
                    
                    int baseTex = prop.faces[0].baseLayer >= 0 ? prop.faces[0].baseLayer : prop.faces[2].baseLayer;
                    // Force overlay to -1.0f unless explicitly valid >= 0
                    int rawOverlay = prop.faces[0].overlayLayer >= 0 ? prop.faces[0].overlayLayer : -1;
                    float overlayTex = (rawOverlay >= 0) ? static_cast<float>(rawOverlay) : -1.0f;
                    
                    float tintBase = prop.faces[0].tintBase ? 1.0f : 0.0f;
                    float tintOverlay = prop.faces[0].tintOverlay ? 1.0f : 0.0f;

                    float minX = (float)x + 0.15f;
                    float maxX = (float)x + 0.85f;
                    float minZ = (float)z + 0.15f;
                    float maxZ = (float)z + 0.85f;
                    float minY = (float)y;
                    float maxY = (float)y + 1.0f;
                    float faceShading = 1.0f;

                    Vertex p1_v0 = { minX, minY, minZ, r, g, b, 0.0f, 1.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };
                    Vertex p1_v1 = { maxX, minY, maxZ, r, g, b, 1.0f, 1.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };
                    Vertex p1_v2 = { maxX, maxY, maxZ, r, g, b, 1.0f, 0.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };
                    Vertex p1_v3 = { minX, maxY, minZ, r, g, b, 0.0f, 0.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };

                    Vertex p2_v0 = { minX, minY, maxZ, r, g, b, 0.0f, 1.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };
                    Vertex p2_v1 = { maxX, minY, minZ, r, g, b, 1.0f, 1.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };
                    Vertex p2_v2 = { maxX, maxY, minZ, r, g, b, 1.0f, 0.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };
                    Vertex p2_v3 = { minX, maxY, maxZ, r, g, b, 0.0f, 0.0f, (float)baseTex, overlayTex, tintBase, tintOverlay, faceShading };

                    cpuOpaqueVertices.push_back(p1_v0);
                    cpuOpaqueVertices.push_back(p1_v1);
                    cpuOpaqueVertices.push_back(p1_v2);
                    cpuOpaqueVertices.push_back(p1_v0);
                    cpuOpaqueVertices.push_back(p1_v2);
                    cpuOpaqueVertices.push_back(p1_v3);

                    cpuOpaqueVertices.push_back(p2_v0);
                    cpuOpaqueVertices.push_back(p2_v1);
                    cpuOpaqueVertices.push_back(p2_v2);
                    cpuOpaqueVertices.push_back(p2_v0);
                    cpuOpaqueVertices.push_back(p2_v2);
                    cpuOpaqueVertices.push_back(p2_v3);

                    continue;
                }

                // --- 2. STANDARD CUBE RENDERING ---
                float r = prop.defaultTint.r;
                float g = prop.defaultTint.g;
                float b = prop.defaultTint.b;

                const int neighborOffsets[6][3] = {
                    { 1,  0,  0}, {-1,  0,  0},
                    { 0,  1,  0}, { 0, -1,  0},
                    { 0,  0,  1}, { 0,  0, -1}
                };

                for (int f = 0; f < 6; ++f) {
                    int nx = x + neighborOffsets[f][0];
                    int ny = y + neighborOffsets[f][1];
                    int nz = z + neighborOffsets[f][2];

                    uint8_t neighborId = getBlock(nx, ny, nz);
                    bool renderFace = false;

                    if (neighborId == 0) {
                        renderFace = true;
                    } else if (neighborId == blockId) {
                        if (prop.name.find("leaves") != std::string::npos) {
                            renderFace = true;
                        } else if (prop.name == "water" || prop.name == "flowing_water") {
                            renderFace = false;
                        } else {
                            renderFace = false;
                        }
                    } else {
                        const auto& nProp = BlockRegistry::getInstance().getBlock(neighborId);
                        if (nProp.isTransparent) {
                            if ((prop.name == "water" || prop.name == "flowing_water") && 
                                (nProp.name == "water" || nProp.name == "flowing_water")) {
                                renderFace = false;
                            } else {
                                renderFace = true;
                            }
                        } else {
                            renderFace = false;
                        }
                    }

                    if (renderFace) {
                        float baseLayer = static_cast<float>(prop.faces[f].baseLayer);
                        int rawOverlay = prop.faces[f].overlayLayer >= 0 ? prop.faces[f].overlayLayer : -1;
                        float overlayLayer = (rawOverlay >= 0) ? static_cast<float>(rawOverlay) : -1.0f;

                        float tintBase = prop.faces[f].tintBase ? 1.0f : 0.0f;
                        float tintOverlay = prop.faces[f].tintOverlay ? 1.0f : 0.0f;
                        float faceShading = shading[f];

                        float fx = static_cast<float>(x);
                        float fy = static_cast<float>(y);
                        float fz = static_cast<float>(z);

                        float topOffset = 0.0f;
                        if ((prop.name == "water" || prop.name == "flowing_water") && f == 2) {
                            topOffset = -0.0625f;
                        }

                        float fv[4][3];
                        if (f == 0) {
                            fv[0][0] = fx + 1.0f; fv[0][1] = fy;                    fv[0][2] = fz + 1.0f;
                            fv[1][0] = fx + 1.0f; fv[1][1] = fy;                    fv[1][2] = fz;
                            fv[2][0] = fx + 1.0f; fv[2][1] = fy + 1.0f + (f == 2 ? topOffset : 0.0f); fv[2][2] = fz;
                            fv[3][0] = fx + 1.0f; fv[3][1] = fy + 1.0f + (f == 2 ? topOffset : 0.0f); fv[3][2] = fz + 1.0f;
                        } else if (f == 1) {
                            fv[0][0] = fx;        fv[0][1] = fy;                    fv[0][2] = fz;
                            fv[1][0] = fx;        fv[1][1] = fy;                    fv[1][2] = fz + 1.0f;
                            fv[2][0] = fx;        fv[2][1] = fy + 1.0f + (f == 2 ? topOffset : 0.0f); fv[2][2] = fz + 1.0f;
                            fv[3][0] = fx;        fv[3][1] = fy + 1.0f + (f == 2 ? topOffset : 0.0f); fv[3][2] = fz;
                        } else if (f == 2) { // Top Face
                            fv[0][0] = fx;        fv[0][1] = fy + 1.0f + topOffset; fv[0][2] = fz + 1.0f;
                            fv[1][0] = fx + 1.0f; fv[1][1] = fy + 1.0f + topOffset; fv[1][2] = fz + 1.0f;
                            fv[2][0] = fx + 1.0f; fv[2][1] = fy + 1.0f + topOffset; fv[2][2] = fz;
                            fv[3][0] = fx;        fv[3][1] = fy + 1.0f + topOffset; fv[3][2] = fz;
                        } else if (f == 3) { // Bottom Face
                            fv[0][0] = fx;        fv[0][1] = fy;        fv[0][2] = fz;
                            fv[1][0] = fx + 1.0f; fv[1][1] = fy;        fv[1][2] = fz;
                            fv[2][0] = fx + 1.0f; fv[2][1] = fy;        fv[2][2] = fz + 1.0f;
                            fv[3][0] = fx;        fv[3][1] = fy;        fv[3][2] = fz + 1.0f;
                        } else if (f == 4) { // North Face
                            fv[0][0] = fx;        fv[0][1] = fy;                    fv[0][2] = fz + 1.0f;
                            fv[1][0] = fx + 1.0f; fv[1][1] = fy;                    fv[1][2] = fz + 1.0f;
                            fv[2][0] = fx + 1.0f; fv[2][1] = fy + 1.0f + (prop.name == "water" || prop.name == "flowing_water" ? topOffset : 0.0f); fv[2][2] = fz + 1.0f;
                            fv[3][0] = fx;        fv[3][1] = fy + 1.0f + (prop.name == "water" || prop.name == "flowing_water" ? topOffset : 0.0f); fv[3][2] = fz + 1.0f;
                        } else { // South Face
                            fv[0][0] = fx + 1.0f; fv[0][1] = fy;                    fv[0][2] = fz;
                            fv[1][0] = fx;        fv[1][1] = fy;                    fv[1][2] = fz;
                            fv[2][0] = fx;        fv[2][1] = fy + 1.0f + (prop.name == "water" || prop.name == "flowing_water" ? topOffset : 0.0f); fv[2][2] = fz;
                            fv[3][0] = fx + 1.0f; fv[3][1] = fy + 1.0f + (prop.name == "water" || prop.name == "flowing_water" ? topOffset : 0.0f); fv[3][2] = fz;
                        }

                        Vertex v0 = { fv[0][0], fv[0][1], fv[0][2], r, g, b, 0.0f, 1.0f, baseLayer, overlayLayer, tintBase, tintOverlay, faceShading };
                        Vertex v1 = { fv[1][0], fv[1][1], fv[1][2], r, g, b, 1.0f, 1.0f, baseLayer, overlayLayer, tintBase, tintOverlay, faceShading };
                        Vertex v2 = { fv[2][0], fv[2][1], fv[2][2], r, g, b, 1.0f, 0.0f, baseLayer, overlayLayer, tintBase, tintOverlay, faceShading };
                        Vertex v3 = { fv[3][0], fv[3][1], fv[3][2], r, g, b, 0.0f, 0.0f, baseLayer, overlayLayer, tintBase, tintOverlay, faceShading };

                        auto& targetBuffer = prop.isTransparent ? cpuTransparentVertices : cpuOpaqueVertices;
                        targetBuffer.push_back(v0);
                        targetBuffer.push_back(v1);
                        targetBuffer.push_back(v2);
                        targetBuffer.push_back(v0);
                        targetBuffer.push_back(v2);
                        targetBuffer.push_back(v3);
                    }
                }
            }
        }
    }

    meshReadyForUpload = true;
}

void ChunkMesh::uploadMesh() {
    if (!meshReadyForUpload) return;

    opaqueVertexCount = static_cast<GLsizei>(cpuOpaqueVertices.size());
    transparentVertexCount = static_cast<GLsizei>(cpuTransparentVertices.size());

    // --- Upload Opaque ---
    if (opaqueVAO == 0) {
        glGenVertexArrays(1, &opaqueVAO);
        glGenBuffers(1, &opaqueVBO);
    }
    glBindVertexArray(opaqueVAO);
    glBindBuffer(GL_ARRAY_BUFFER, opaqueVBO);
    glBufferData(GL_ARRAY_BUFFER, cpuOpaqueVertices.size() * sizeof(Vertex), cpuOpaqueVertices.data(), GL_STATIC_DRAW);

    GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, posX)); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintR)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, texCoordX)); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, baseLayer)); glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, overlayLayer)); glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintBase)); glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintOverlay)); glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, faceShading)); glEnableVertexAttribArray(7);

    // --- Upload Transparent ---
    if (transparentVAO == 0) {
        glGenVertexArrays(1, &transparentVAO);
        glGenBuffers(1, &transparentVBO);
    }
    glBindVertexArray(transparentVAO);
    glBindBuffer(GL_ARRAY_BUFFER, transparentVBO);
    glBufferData(GL_ARRAY_BUFFER, cpuTransparentVertices.size() * sizeof(Vertex), cpuTransparentVertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, posX)); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintR)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, texCoordX)); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, baseLayer)); glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, overlayLayer)); glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintBase)); glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintOverlay)); glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, faceShading)); glEnableVertexAttribArray(7);

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
    if (transparentVertexCount == 0 || transparentVAO ==0) return;
    glBindVertexArray(transparentVAO);
    glDrawArrays(GL_TRIANGLES, 0, transparentVertexCount);
    glBindVertexArray(0);
}