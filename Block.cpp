#include "Block.h"
#include <sol/sol.hpp>

BlockRegistry& BlockRegistry::getInstance() {
    static BlockRegistry instance;
    return instance;
}

BlockRegistry::BlockRegistry() {
    // Ensure Air (ID 0) is registered by default
    BlockProperty airBlock;
    airBlock.id = 0;
    airBlock.name = "air";
    airBlock.defaultTint = glm::vec3(1.0f);
    airBlock.isTransparent = true;
    airBlock.isWalkable = false;
    airBlock.isSelectable = false;
    airBlock.hasGravity = false;
    airBlock.in_creative = false; // Air should never be in creative inventory

    registerBlock(airBlock);
}

void BlockRegistry::registerBlock(const BlockProperty& block) {
    if (block.id >= blocks.size()) {
        blocks.resize(block.id + 1);
    }
    blocks[block.id] = block;
    nameToId[block.name] = block.id;
}

void BlockRegistry::registerBlockFromLua(uint8_t id, const std::string& name, glm::vec3 tint, const sol::table& options) {
    BlockProperty prop;
    prop.id = id;
    prop.name = name;
    prop.defaultTint = tint;

    if (options.valid()) {
        if (options["transparent"].is<bool>())    prop.isTransparent = options["transparent"].get<bool>();
        if (options["cutout"].is<bool>())         prop.isCutout      = options["cutout"].get<bool>();
        if (options["walkable"].is<bool>())       prop.isWalkable    = options["walkable"].get<bool>();
        if (options["selectable"].is<bool>())     prop.isSelectable  = options["selectable"].get<bool>();
        if (options["gravity"].is<bool>())        prop.hasGravity    = options["gravity"].get<bool>();
        if (options["in_creative"].is<bool>())    prop.in_creative   = options["in_creative"].get<bool>();
        if (options["pattern"].is<std::string>()) prop.pattern       = options["pattern"].get<std::string>();
    }

    // Must come after every field is set, because registerBlock stores a copy.
    registerBlock(prop);
}

const BlockProperty& BlockRegistry::getBlock(uint8_t id) const {
    if (id < blocks.size()) {
        return blocks[id];
    }

    // Fallback for unknown IDs. Named fields, so the order of members in
    // BlockProperty can't cause a type mismatch.
    static const BlockProperty dummy = [] {
        BlockProperty p;
        p.id = 0;
        p.name = "air";
        p.defaultTint = glm::vec3(1.0f);
        p.isTransparent = true;
        p.isWalkable = false;
        p.isSelectable = false;
        p.hasGravity = false;
        p.in_creative = false;
        p.pattern = "cube";
        return p;
    }();
    return dummy;
}

uint8_t BlockRegistry::getBlockId(const std::string& name) const {
    auto it = nameToId.find(name);
    if (it != nameToId.end()) {
        return it->second;
    }
    return 0; // Default to Air (ID 0)
}