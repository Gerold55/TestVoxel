#include "ChunkData.h"
#include <cstring>

ChunkData::ChunkData() {
    std::memset(blocks, 0, sizeof(blocks));
    std::memset(metadata, 0, sizeof(metadata));
    for (int i = 0; i < 6; ++i) neighbors[i] = nullptr;
}

void ChunkData::setBlock(int x, int y, int z, uint8_t blockId) {
    if (inside(x, y, z)) blocks[x][y][z] = blockId;
}

void ChunkData::setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta) {
    if (inside(x, y, z)) {
        blocks[x][y][z] = blockId;
        metadata[x][y][z] = meta;
    }
}

void ChunkData::setMetadata(int x, int y, int z, uint8_t meta) {
    if (inside(x, y, z)) metadata[x][y][z] = meta;
}

uint8_t ChunkData::getLocalBlock(int x, int y, int z) const {
    return inside(x, y, z) ? blocks[x][y][z] : 0;
}

uint8_t ChunkData::getLocalMetadata(int x, int y, int z) const {
    return inside(x, y, z) ? metadata[x][y][z] : 0;
}

const ChunkData* ChunkData::resolve(int& x, int& z) const {
    if (x < 0)            { x += CHUNK_WIDTH; return neighbors[1]; }
    if (x >= CHUNK_WIDTH) { x -= CHUNK_WIDTH; return neighbors[0]; }
    if (z < 0)            { z += CHUNK_WIDTH; return neighbors[5]; }
    if (z >= CHUNK_WIDTH) { z -= CHUNK_WIDTH; return neighbors[4]; }
    return this;
}

uint8_t ChunkData::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;
    const ChunkData* c = resolve(x, z);
    return c ? c->getLocalBlock(x, y, z) : 0;
}

uint8_t ChunkData::getMetadata(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;
    const ChunkData* c = resolve(x, z);
    return c ? c->getLocalMetadata(x, y, z) : 0;
}
