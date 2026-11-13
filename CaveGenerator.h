#pragma once
#include <cstdint>

class ChunkMesh;

// Minecraft-style caves, carved into chunks after terrain generation:
//   - Spaghetti caves : long winding tunnels, 3-6 blocks wide
//   - Noodle caves    : thin twisty tunnels, 1-3 blocks wide (in some regions only)
//   - Cheese caves    : big open caverns with natural pillars, deep underground
//   - Cave entrances  : tunnels occasionally break through the surface
//   - Lava lakes      : open cave space near the bottom fills with lava (if a "lava" block exists)
// Everything depends on the world seed, so the same seed always gives the same caves.
class CaveGenerator {
public:
    static void init();                       // kept for compatibility (does nothing)
    static void setSeed(uint32_t seed);
    static void setEnabled(bool enabled);
    static bool isEnabled();

    // Carves caves into a freshly generated chunk. Never carves through water,
    // tree trunks/leaves, or the bottom bedrock layers.
    static void carveChunk(ChunkMesh& chunk, int chunkX, int chunkZ);

    // Per-block query without surface information (slower; kept for old code).
    static bool isCarvedOut(int x, int y, int z);
};
