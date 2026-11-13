-- inventory.lua - Creative Inventory with Synchronized State & Dynamic C++ Integration
print("[UI] inventory.lua loaded successfully!")

local creative_open = false
local current_tab = 1 -- 1: Blocks, 2: Nature, 3: Items

local tabs = {
    { name = "Blocks", filter = function(name) 
        return name ~= "air" and name ~= "water" and name ~= "flowing_water" 
           and name ~= "short_grass" and name ~= "flower_red" 
           and name ~= "hoe" and name ~= "scythe" and name ~= "stick" 
           and name ~= "leaves" and name ~= "birch_leaves" and name ~= "sapling" 
    end },
    { name = "Nature", filter = function(name) 
        return name == "short_grass" or name == "flower_red" 
           or name == "leaves" or name == "birch_leaves" or name == "sapling" 
    end },
    { name = "Items",  filter = function(name) 
        return name == "hoe" or name == "scythe" or name == "stick" 
    end }
}

-- Synchronize open state with C++ callbacks
function set_creative_open(isOpen)
    creative_open = isOpen
end

function toggle_creative_inventory()
    creative_open = not creative_open
end

local function is_creative_open()
    if hotbar and hotbar.is_creative_open then
        return hotbar.is_creative_open() or creative_open
    end
    return creative_open
end

local function get_current_items()
    local list = {}
    local all_items = {}
    
    -- Dynamically pull all registered blocks/items directly from C++
    if ui and ui.get_all_blocks then
        all_items = ui.get_all_blocks()
    else
        -- Fallback list if the C++ binding hasn't been initialized yet
        all_items = { "dirt", "stone", "oak_planks", "cobblestone", "sand", "gravel", "bricks", "obsidian", "oak_log", "short_grass", "flower_red", "hoe", "scythe", "stick" }
    end
    
    local filter_fn = tabs[current_tab].filter
    for _, name in ipairs(all_items) do
        if filter_fn(name) then
            table.insert(list, name)
        end
    end
    return list
end

function render_creative_inventory(width, height)
    if not is_creative_open() then return end

    if ui and ui.draw_rect then
        -- 1. Dim background overlay
        ui.draw_rect(0, 0, width, height, 0.0, 0.0, 0.0, 0.4)

        -- 2. Window Panel Dimensions
        local win_w = 520
        local win_h = 380
        local win_x = (width - win_w) / 2
        local win_y = (height - win_h) / 2

        -- Main Panel Background
        ui.draw_rect(win_x, win_y, win_w, win_h, 0.18, 0.18, 0.18, 0.95)

        -- Title
        if ui.draw_text then
            ui.draw_text("Item Selection (Creative)", win_x + 16, win_y + 14, 1.0, 1.0, 1.0)
        end

        -- 3. Render Category Tabs
        local tab_w = 90
        local tab_h = 26
        local tab_y = win_y - tab_h + 2
        for i, tab in ipairs(tabs) do
            local tab_x = win_x + 16 + (i - 1) * (tab_w + 6)
            local is_active = (i == current_tab)
            local bg_val = is_active and 0.28 or 0.12

            ui.draw_rect(tab_x, tab_y, tab_w, tab_h, bg_val, bg_val, bg_val, 0.9)
            if ui.draw_text then
                ui.draw_text(tab.name, tab_x + 20, tab_y + 6, 1.0, 1.0, 1.0)
            end
        end

        -- 4. Render Item Grid Slots & Icons
        local start_x = win_x + 24
        local start_y = win_y + 65
        local slot_size = 44
        local padding = 8
        local cols = 8

        local items = get_current_items()
        for i, block_name in ipairs(items) do
            local col = (i - 1) % cols
            local row = math.floor((i - 1) / cols)
            local sx = start_x + col * (slot_size + padding)
            local sy = start_y + row * (slot_size + padding)

            -- Slot background box
            ui.draw_rect(sx, sy, slot_size, slot_size, 0.32, 0.32, 0.32, 1.0)
            
            -- Draw block icon inside slot via C++ binding
            if ui.draw_block_icon then
                ui.draw_block_icon(block_name, sx + 4, sy + 4, slot_size - 8)
            end
            
            -- Text label fallback hint
            if ui.draw_text then
                ui.draw_text(string.sub(block_name, 1, 3), sx + 4, sy + slot_size - 14, 0.7, 0.7, 0.7)
            end
        end
    end
end

function handle_creative_click(mx, my, width, height)
    if not is_creative_open() then return end

    local win_w = 520
    local win_h = 380
    local win_x = (width - win_w) / 2
    local win_y = (height - win_h) / 2

    -- Check Tab Clicks
    local tab_w = 90
    local tab_h = 26
    local tab_y = win_y - tab_h + 2
    for i = 1, #tabs do
        local tab_x = win_x + 16 + (i - 1) * (tab_w + 6)
        if mx >= tab_x and mx <= (tab_x + tab_w) and my >= tab_y and my <= (tab_y + tab_h) then
            current_tab = i
            return
        end
    end

    -- Check Item Grid Clicks & Assign to Active Hotbar Slot
    local start_x = win_x + 24
    local start_y = win_y + 65
    local slot_size = 44
    local padding = 8
    local cols = 8

    local items = get_current_items()
    for i, block_name in ipairs(items) do
        local col = (i - 1) % cols
        local row = math.floor((i - 1) / cols)
        local sx = start_x + col * (slot_size + padding)
        local sy = start_y + row * (slot_size + padding)

        if mx >= sx and mx <= (sx + slot_size) and my >= sy and my <= (sy + slot_size) then
            if hotbar and hotbar.get_active_slot and hotbar.set_slot then
                local active_slot = hotbar.get_active_slot()
                hotbar.set_slot(active_slot, block_name)
            end
            return
        end
    end
end