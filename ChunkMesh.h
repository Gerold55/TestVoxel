#pragma once
// Client-side drawable mesh for one chunk, built from its ChunkData.
// Holds no block data itself - the world (ChunkData) is the single source of truth.

#include <glad/glad.h>
#include <vector>
#include "Types.h"

class ChunkData;

class ChunkMesh {
public:
    ChunkMesh() = default;
    ~ChunkMesh();
    ChunkMesh(const ChunkMesh&) = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

    // Builds vertex lists on the CPU (needs the chunk's neighbours linked for border faces).
    void generateMeshData(const ChunkData& data);
    // Sends the last built vertex lists to the GPU. Needs the GL context.
    void uploadMesh();

    void drawOpaque() const;
    void drawTransparent() const;

    GLuint opaqueVAO = 0, opaqueVBO = 0;
    GLsizei opaqueVertexCount = 0;
    GLuint transparentVAO = 0, transparentVBO = 0;
    GLsizei transparentVertexCount = 0;

    bool meshReadyForUpload = false;   // built on the CPU, not yet on the GPU
    bool everUploaded = false;         // has reached the GPU at least once

private:
    std::vector<Vertex> cpuOpaqueVertices;
    std::vector<Vertex> cpuTransparentVertices;
};
