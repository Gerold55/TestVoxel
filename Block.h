#ifndef BLOCK_H
#define BLOCK_H

#include "Types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <sol/sol.hpp>

class BlockRegistry {
public:
    static BlockRegistry& getInstance();

    void registerBlock(const BlockProperty& block);
    void registerBlockFromLua(uint8_t id, const std::string& name, glm::vec3 tint, const sol::table& options);
    
    const BlockProperty& getBlock(uint8_t id) const;
    uint8_t getBlockId(const std::string& name) const;

    const std::vector<BlockProperty>& getAllBlocks() const { return blocks; }

private:
    BlockRegistry();
    ~BlockRegistry() = default;

    std::vector<BlockProperty> blocks;
    std::unordered_map<std::string, uint8_t> nameToId;
};

#endif // BLOCK_H