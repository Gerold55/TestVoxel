#pragma once
#include <glad/glad.h>
#include <vector>
#include <cstdint>
#include "Types.h"

class ChunkMesh {
public:
    ChunkMesh();
    ~ChunkMesh();

    void setBlock(int x, int y, int z, uint8_t blockId);
    uint8_t getBlock(int x, int y, int z) const;
    uint8_t getLocalBlock(int x, int y, int z) const;

    void setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta);
    void setMetadata(int x, int y, int z, uint8_t meta);
    uint8_t getMetadata(int x, int y, int z) const;

    // Neighbor linking for seamless chunk boundary water culling
    // Indices: 0: +X, 1: -X, 2: +Y, 3: -Y, 4: +Z, 5: -Z
    ChunkMesh* neighbors[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    void setNeighbor(int dir, ChunkMesh* neighbor) { neighbors[dir] = neighbor; }

    void generateMeshData();
    void uploadMesh();
    void draw() const;

    bool isDirty;
    bool meshReadyForUpload;

    GLuint VAO, VBO;
    GLsizei vertexCount;
    std::vector<Vertex> cpuVertices;

private:
    uint8_t blocks[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
    uint8_t metadata[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
};