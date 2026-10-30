#pragma once
#include <glm/glm.hpp>

class CaveGenerator {
public:
    static void init();
    // Returns true if the given world coordinate should be carved out (turned to air)
    static bool isCarvedOut(int x, int y, int z);
};