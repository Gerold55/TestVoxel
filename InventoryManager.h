#ifndef INVENTORY_MANAGER_H
#define INVENTORY_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct ItemStack {
    uint8_t blockId = 0;
    int count = 0;

    bool isEmpty() const { return blockId == 0 || count <= 0; }
};

class Inventory {
public:
    std::string id;
    std::vector<ItemStack> slots;

    Inventory(const std::string& invId, int size) : id(invId), slots(size) {}
};

class InventoryManager {
public:
    static InventoryManager& getInstance() {
        static InventoryManager instance;
        return instance;
    }

    ItemStack cursorStack; // Item currently being dragged on the mouse cursor

    std::unordered_map<std::string, Inventory> inventories;

    void createInventory(const std::string& id, int slots) {
        inventories[id] = Inventory(id, slots);
    }

    Inventory* getInventory(const std::string& id) {
        auto it = inventories.find(id);
        return (it != inventories.end()) ? &it->second : nullptr;
    }

    // Handles Minecraft-style slot click logic (Left-click full swap, Right-click single drop)
    void handleSlotClick(const std::string& invId, int slotIdx, int mouseButton) {
        Inventory* inv = getInventory(invId);
        if (!inv || slotIdx < 0 || slotIdx >= static_cast<int>(inv->slots.size())) return;

        ItemStack& slot = inv->slots[slotIdx];

        if (mouseButton == 0) { // Left Click: Swap or Stack
            if (cursorStack.isEmpty()) {
                // Pick up stack
                cursorStack = slot;
                slot = ItemStack();
            } else if (slot.isEmpty()) {
                // Place stack down
                slot = cursorStack;
                cursorStack = ItemStack();
            } else if (slot.blockId == cursorStack.blockId) {
                // Combine stacks
                slot.count += cursorStack.count;
                cursorStack = ItemStack();
            } else {
                // Swap stacks
                std::swap(slot, cursorStack);
            }
        }
    }
};

#endif // INVENTORY_MANAGER_H