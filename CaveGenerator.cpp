#include "CaveGenerator.h"
#include "ChunkData.h"
#include "Block.h"
#include "Types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

// ===========================================================================
// Tunables - change these to shape the caves
// ===========================================================================

constexpr int   kBedrockTop      = 4;      // y <= this is never carved
constexpr int   kLavaLevel       = 10;     // carved space at or below this becomes lava
constexpr int   kSurfaceCrust    = 6;      // tunnels stay this far below the surface...
constexpr float kEntranceChance  = 0.30f;  // ...except where entrance noise is above this (higher = rarer)

// Spaghetti: long winding tunnels
constexpr float kSpaghettiScaleXZ = 1.0f / 70.0f;
constexpr float kSpaghettiScaleY  = 1.0f / 45.0f;
constexpr float kSpaghettiRadius  = 0.10f;    // base tunnel width
constexpr float kSpaghettiVary    = 0.04f;    // how much the width changes along the tunnel

// Noodles: thin tunnels, only in some regions
constexpr float kNoodleScale      = 1.0f / 30.0f;
constexpr float kNoodleRadius     = 0.065f;
constexpr float kNoodleRegion     = 0.15f;    // higher = noodle regions are rarer

// Cheese: big caverns
constexpr float kCheeseScaleXZ    = 1.0f / 90.0f;
constexpr float kCheeseScaleY     = 1.0f / 55.0f;
constexpr float kCheeseThreshold  = 0.42f;    // higher = smaller/fewer caverns
constexpr int   kCheeseMinDepth   = 18;       // caverns stay at least this far below the surface
constexpr float kCheeseTaper      = 0.02f;    // caverns shrink as they approach that depth

// Noise is sampled every 4 blocks and interpolated in between (much faster)
constexpr int kStepXZ = 4;
constexpr int kStepY  = 4;

// ===========================================================================
// State
// ===========================================================================

uint32_t g_seed = 0;
bool     g_enabled = true;

// Derived per-field seeds so each noise field is independent
uint32_t fieldSeed(int field) {
    return g_seed ^ (0x9E3779B9u * static_cast<uint32_t>(field + 1));
}

// ===========================================================================
// Seeded 3D Perlin noise (roughly -1..1)
// ===========================================================================

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float t, float a, float b) { return a + t * (b - a); }

inline float grad(uint32_t hash, float x, float y, float z) {
    const uint32_t h = hash & 15u;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1u) == 0 ? u : -u) + ((h & 2u) == 0 ? v : -v);
}

// Unsigned math only - signed overflow is undefined behaviour in C++.
inline uint32_t hash3D(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x27D4EB2Du; h = (h ^ (h >> 15)) * 0x85EBCA6Bu;
    h ^= static_cast<uint32_t>(y) * 0x165667B1u; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
    h ^= static_cast<uint32_t>(z) * 0x9E3779B1u; h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
    return h ^ (h >> 13);
}

float noise3D(float x, float y, float z, uint32_t seed) {
    const int X = static_cast<int>(std::floor(x));
    const int Y = static_cast<int>(std::floor(y));
    const int Z = static_cast<int>(std::floor(z));
    const float fx = x - X, fy = y - Y, fz = z - Z;
    const float u = fade(fx), v = fade(fy), w = fade(fz);

    const float g000 = grad(hash3D(X,     Y,     Z,     seed), fx,        fy,        fz);
    const float g100 = grad(hash3D(X + 1, Y,     Z,     seed), fx - 1.0f, fy,        fz);
    const float g010 = grad(hash3D(X,     Y + 1, Z,     seed), fx,        fy - 1.0f, fz);
    const float g110 = grad(hash3D(X + 1, Y + 1, Z,     seed), fx - 1.0f, fy - 1.0f, fz);
    const float g001 = grad(hash3D(X,     Y,     Z + 1, seed), fx,        fy,        fz - 1.0f);
    const float g101 = grad(hash3D(X + 1, Y,     Z + 1, seed), fx - 1.0f, fy,        fz - 1.0f);
    const float g011 = grad(hash3D(X,     Y + 1, Z + 1, seed), fx,        fy - 1.0f, fz - 1.0f);
    const float g111 = grad(hash3D(X + 1, Y + 1, Z + 1, seed), fx - 1.0f, fy - 1.0f, fz - 1.0f);

    const float x00 = lerp(u, g000, g100);
    const float x10 = lerp(u, g010, g110);
    const float x01 = lerp(u, g001, g101);
    const float x11 = lerp(u, g011, g111);
    return lerp(w, lerp(v, x00, x10), lerp(v, x01, x11));
}

// ===========================================================================
// Cave density fields. Negative = inside a cave.
// ===========================================================================

// Spaghetti + noodles: a tunnel is where TWO independent noise fields are both near zero.
// The intersection of two such surfaces is a long winding line - that's the tunnel.
float tunnelField(float x, float y, float z) {
    // Spaghetti
    const float sx = x * kSpaghettiScaleXZ, sy = y * kSpaghettiScaleY, sz = z * kSpaghettiScaleXZ;
    const float a = noise3D(sx, sy, sz, fieldSeed(0));
    const float b = noise3D(sx, sy, sz, fieldSeed(1));
    float r = kSpaghettiRadius + kSpaghettiVary * noise3D(x / 120.0f, y / 120.0f, z / 120.0f, fieldSeed(2));
    r = std::max(r, 0.035f);
    float value = (a * a + b * b) - r * r;

    // Noodles, only inside "noodle regions"
    const float region = noise3D(x / 200.0f, 0.0f, z / 200.0f, fieldSeed(3));
    if (region > kNoodleRegion) {
        const float nx = x * kNoodleScale, ny = y * kNoodleScale, nz = z * kNoodleScale;
        const float c = noise3D(nx, ny, nz, fieldSeed(4));
        const float d = noise3D(nx, ny, nz, fieldSeed(5));
        value = std::min(value, (c * c + d * d) - kNoodleRadius * kNoodleRadius);
    }
    return value;
}

// Cheese: large blobs where the noise is high. Pillars appear naturally between blobs.
float cheeseField(float x, float y, float z) {
    const float e = noise3D(x * kCheeseScaleXZ, y * kCheeseScaleY, z * kCheeseScaleXZ, fieldSeed(6))
                  + 0.5f * noise3D(x * kCheeseScaleXZ * 2.0f, y * kCheeseScaleY * 2.0f,
                                   z * kCheeseScaleXZ * 2.0f, fieldSeed(7));
    return kCheeseThreshold - e;
}

bool entranceAt(int x, int z) {
    return noise3D(x / 64.0f, 7.5f, z / 64.0f, fieldSeed(8)) > kEntranceChance;
}

// ===========================================================================
// Which blocks can be carved
// ===========================================================================

struct BlockClasses {
    bool ready = false;
    bool carvable[256] = {};
    bool liquid[256] = {};
    uint8_t lavaId = 0;
};
BlockClasses g_blocks;

bool containsAny(const std::string& s, std::initializer_list<const char*> parts) {
    for (const char* p : parts) if (s.find(p) != std::string::npos) return true;
    return false;
}

void classifyBlocks() {
    if (g_blocks.ready) return;
    auto& reg = BlockRegistry::getInstance();
    for (int id = 1; id < 256; ++id) {
        const auto& b = reg.getBlock(static_cast<uint8_t>(id));
        if (b.name.empty() || b.name == "air") continue;

        if (containsAny(b.name, { "water", "lava" })) {
            g_blocks.liquid[id] = true;
            continue;
        }
        // Solid terrain only - leave trees, plants, and placed building blocks alone
        g_blocks.carvable[id] = b.isWalkable &&
            !containsAny(b.name, { "log", "leaves", "planks", "wood", "bedrock", "door", "glass" });
    }
    g_blocks.lavaId = reg.getBlockId("lava");
    g_blocks.ready = true;
}

} // namespace

// ===========================================================================
// CaveGenerator
// ===========================================================================

void CaveGenerator::init() {}

void CaveGenerator::setSeed(uint32_t seed) { g_seed = seed; }
void CaveGenerator::setEnabled(bool enabled) { g_enabled = enabled; }
bool CaveGenerator::isEnabled() { return g_enabled; }

bool CaveGenerator::isCarvedOut(int x, int y, int z) {
    if (!g_enabled || y <= kBedrockTop || y > 62) return false;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
    return tunnelField(fx, fy, fz) < 0.0f || (y < 40 && cheeseField(fx, fy, fz) < 0.0f);
}

void CaveGenerator::carveChunk(ChunkData& chunk, int chunkX, int chunkZ) {
    if (!g_enabled) return;
    classifyBlocks();

    const int baseX = chunkX * CHUNK_WIDTH;
    const int baseZ = chunkZ * CHUNK_WIDTH;

    // 1. Surface height of each column (highest carvable block; ignores trees)
    int surface[CHUNK_WIDTH][CHUNK_WIDTH];
    int topY = 0;
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            surface[x][z] = -1;
            for (int y = CHUNK_HEIGHT - 1; y > kBedrockTop; --y) {
                if (g_blocks.carvable[chunk.getBlock(x, y, z)]) { surface[x][z] = y; break; }
            }
            topY = std::max(topY, surface[x][z]);
        }
    }
    if (topY <= kBedrockTop) return;   // empty chunk

    // 2. Sample both cave fields on a coarse grid (world-aligned, so chunks join seamlessly)
    const int nx = CHUNK_WIDTH / kStepXZ + 1;
    const int nz = CHUNK_WIDTH / kStepXZ + 1;
    const int ny = std::min(topY, CHUNK_HEIGHT - 1) / kStepY + 2;

    std::vector<float> tunnel(static_cast<size_t>(nx * ny * nz));
    std::vector<float> cheese(static_cast<size_t>(nx * ny * nz));
    auto idx = [ny, nz](int ix, int iy, int iz) { return (ix * ny + iy) * nz + iz; };

    for (int ix = 0; ix < nx; ++ix) {
        for (int iy = 0; iy < ny; ++iy) {
            for (int iz = 0; iz < nz; ++iz) {
                const float wx = static_cast<float>(baseX + ix * kStepXZ);
                const float wy = static_cast<float>(iy * kStepY);
                const float wz = static_cast<float>(baseZ + iz * kStepXZ);
                tunnel[idx(ix, iy, iz)] = tunnelField(wx, wy, wz);
                cheese[idx(ix, iy, iz)] = cheeseField(wx, wy, wz);
            }
        }
    }

    auto sample = [&](const std::vector<float>& f, int x, int y, int z) {
        const int ix = x / kStepXZ, iy = y / kStepY, iz = z / kStepXZ;
        const float tx = static_cast<float>(x % kStepXZ) / kStepXZ;
        const float ty = static_cast<float>(y % kStepY) / kStepY;
        const float tz = static_cast<float>(z % kStepXZ) / kStepXZ;
        const float c00 = lerp(tx, f[idx(ix, iy,     iz)],     f[idx(ix + 1, iy,     iz)]);
        const float c10 = lerp(tx, f[idx(ix, iy + 1, iz)],     f[idx(ix + 1, iy + 1, iz)]);
        const float c01 = lerp(tx, f[idx(ix, iy,     iz + 1)], f[idx(ix + 1, iy,     iz + 1)]);
        const float c11 = lerp(tx, f[idx(ix, iy + 1, iz + 1)], f[idx(ix + 1, iy + 1, iz + 1)]);
        return lerp(tz, lerp(ty, c00, c10), lerp(ty, c01, c11));
    };

    // Is there water/lava right next to this block (inside this chunk)?
    auto touchesLiquid = [&](int x, int y, int z) {
        if (y + 1 < CHUNK_HEIGHT && g_blocks.liquid[chunk.getBlock(x, y + 1, z)]) return true;
        if (x > 0               && g_blocks.liquid[chunk.getBlock(x - 1, y, z)]) return true;
        if (x < CHUNK_WIDTH - 1 && g_blocks.liquid[chunk.getBlock(x + 1, y, z)]) return true;
        if (z > 0               && g_blocks.liquid[chunk.getBlock(x, y, z - 1)]) return true;
        if (z < CHUNK_WIDTH - 1 && g_blocks.liquid[chunk.getBlock(x, y, z + 1)]) return true;
        return false;
    };

    // 3. Carve, top-down per column
    std::vector<std::array<int, 3>> lavaCells;
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int surf = surface[x][z];
            if (surf <= kBedrockTop) continue;

            const bool entrance = entranceAt(baseX + x, baseZ + z);
            const int tunnelTop = entrance ? surf : surf - kSurfaceCrust;
            const int cheeseTop = surf - kCheeseMinDepth;

            for (int y = surf; y > kBedrockTop; --y) {
                const uint8_t id = chunk.getBlock(x, y, z);
                if (!g_blocks.carvable[id]) continue;

                bool carve = false;
                if (y <= tunnelTop && sample(tunnel, x, y, z) < 0.0f) carve = true;
                if (!carve && y <= cheeseTop) {
                    // Shrink caverns as they get close to their maximum height
                    const float taper = std::max(0, y - (cheeseTop - 12)) * kCheeseTaper;
                    if (sample(cheese, x, y, z) + taper < 0.0f) carve = true;
                }
                if (!carve || touchesLiquid(x, y, z)) continue;

                chunk.setBlock(x, y, z, 0);
                if (y <= kLavaLevel && g_blocks.lavaId != 0) lavaCells.push_back({ x, y, z });
            }
        }
    }

    // 4. Fill low cave space with lava last, so the lava we add doesn't stop
    //    neighbouring blocks from being carved (only pre-existing water/lava should).
    for (const auto& c : lavaCells) {
        chunk.setBlock(c[0], c[1], c[2], g_blocks.lavaId);
    }
}
