#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "Types.h"

class ChunkMesh {
public:
    uint8_t blocks[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
    uint8_t metadata[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
    ChunkMesh* neighbors[6]; // +X, -X, +Y, -Y, +Z, -Z

    // --- Opaque Buffers ---
    GLuint opaqueVAO, opaqueVBO;
    GLsizei opaqueVertexCount;
    std::vector<Vertex> cpuOpaqueVertices;

    // --- Transparent Buffers (Water, Leaves, Plants) ---
    GLuint transparentVAO, transparentVBO;
    GLsizei transparentVertexCount;
    std::vector<Vertex> cpuTransparentVertices;

    bool isDirty;
    bool meshReadyForUpload;

    ChunkMesh();
    ~ChunkMesh();

    void setBlock(int x, int y, int z, uint8_t blockId);
    uint8_t getLocalBlock(int x, int y, int z) const;
    uint8_t getBlock(int x, int y, int z) const;

    void setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta);
    void setMetadata(int x, int y, int z, uint8_t meta);
    uint8_t getMetadata(int x, int y, int z) const;

    void generateMeshData();
    void uploadMesh();
    void drawOpaque() const;
    void drawTransparent() const;
};