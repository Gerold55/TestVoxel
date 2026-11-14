#pragma once
// A chunk's blocks and metadata - no graphics. Used by the world, saving, world generation
// and (later) the server and networking. The client builds a ChunkMesh from it to draw it.

#include <cstdint>
#include "Types.h"

class ChunkData {
public:
    ChunkData();

    // Coordinates are local to the chunk (0..15, 0..255, 0..15).
    void setBlock(int x, int y, int z, uint8_t blockId);
    void setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta);
    void setMetadata(int x, int y, int z, uint8_t meta);

    // getBlock / getMetadata also look into neighbouring chunks when x or z is just outside
    // this chunk (returns 0 if that neighbour isn't loaded). getLocal* never does.
    uint8_t getBlock(int x, int y, int z) const;
    uint8_t getMetadata(int x, int y, int z) const;
    uint8_t getLocalBlock(int x, int y, int z) const;
    uint8_t getLocalMetadata(int x, int y, int z) const;

    // Neighbour slots: 0 = +X, 1 = -X, 4 = +Z, 5 = -Z (2/3 unused). Managed by World.
    ChunkData* neighbors[6];

private:
    static bool inside(int x, int y, int z) {
        return x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH;
    }
    // Resolves an out-of-range x/z to (chunk, local x, local z). Returns nullptr if not loaded.
    const ChunkData* resolve(int& x, int& z) const;

    uint8_t blocks[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
    uint8_t metadata[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
};
