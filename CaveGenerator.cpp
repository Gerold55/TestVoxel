#include "CaveGenerator.h"
#include <cmath>
#include <algorithm>

namespace {
    float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    float lerp(float t, float a, float b) { return a + t * (b - a); }
    
    float grad(int hash, float x, float y, float z) {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    // Robust 3D hash function eliminating vertical column artifacts
    int hash3D(int x, int y, int z) {
        int h = x * 374761393 + y * 668265263 + z * 1274126177;
        h = (h ^ (h >> 13)) * 1103515245;
        return (h ^ (h >> 16)) & 255;
    }

    float noise3D(float x, float y, float z) {
        int X = (int)floor(x);
        int Y = (int)floor(y);
        int Z = (int)floor(z);
        
        float fx = x - X;
        float fy = y - Y;
        float fz = z - Z;
        
        float u = fade(fx);
        float v = fade(fy);
        float w = fade(fz);

        int n000 = hash3D(X,     Y,     Z);
        int n100 = hash3D(X + 1, Y,     Z);
        int n010 = hash3D(X,     Y + 1, Z);
        int n110 = hash3D(X + 1, Y + 1, Z);
        int n001 = hash3D(X,     Y,     Z + 1);
        int n101 = hash3D(X + 1, Y,     Z + 1);
        int n011 = hash3D(X,     Y + 1, Z + 1);
        int n111 = hash3D(X + 1, Y + 1, Z + 1);

        float g000 = grad(n000, fx,     fy,     fz);
        float g100 = grad(n100, fx - 1, fy,     fz);
        float g010 = grad(n010, fx,     fy - 1, fz);
        float g110 = grad(n110, fx - 1, fy - 1, fz);
        float g001 = grad(n001, fx,     fy,     fz - 1);
        float g101 = grad(n101, fx - 1, fy,     fz - 1);
        float g011 = grad(n011, fx,     fy - 1, fz - 1);
        float g111 = grad(n111, fx - 1, fy - 1, fz - 1);

        float x1 = lerp(u, g000, g100);
        float x2 = lerp(u, g010, g110);
        float y1 = lerp(u, g001, g101);
        float y2 = lerp(u, g011, g111);

        float z1 = lerp(v, x1, x2);
        float z2 = lerp(v, y1, y2);

        return lerp(w, z1, z2);
    }
}

void CaveGenerator::init() {}

bool CaveGenerator::isCarvedOut(int x, int y, int z) {
    // Keep bedrock solid at the bottom and sky clear at the top
    if (y < 4 || y > 68) return false;

    // Multi-octave 3D worm tunnels
    float scale = 0.035f;
    float n1 = noise3D(x * scale, y * scale, z * scale);
    float n2 = noise3D(x * scale * 2.0f, y * scale * 2.0f, z * scale * 2.0f) * 0.5f;
    
    float combinedNoise = n1 + n2;
    bool isWormCave = (std::abs(combinedNoise) < 0.22f && y < 62);

    return isWormCave;
}