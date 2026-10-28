-- creative_inventory.lua - Minecraft-style Creative Inventory
print("[UI] Loading Minecraft-Style Creative Inventory...")

local creative_open = false

-- --- BLOCK DISPLAY NAMES REGISTRY ---
local block_display_names = {
    ["stone"] = "Stone",
    ["dirt"] = "Dirt",
    ["grass"] = "Grass Block",
    ["sand"] = "Sand",
    ["gravel"] = "Gravel",
    ["water"] = "Water",
    ["log_oak"] = "Oak Log",
    ["leaves_oak"] = "Oak Leaves",
    ["log_birch"] = "Birch Wood",
    ["leaves_birch"] = "Birch Leaves",
    ["cactus"] = "Cactus",
    ["short_grass"] = "Short Grass",
    ["flower_poppy"] = "Poppy",
    ["flower_daisy"] = "Daisy",
    ["flower_bluebell"] = "Bluebell",
    ["flower_azure_bluet"] = "Azure Bluet",
    ["flower_allium"] = "Allium",
    ["flower_tulip_white"] = "White Tulip",
    ["flower_tulip_red"] = "Red Tulip",
    ["flower_tulip_pink"] = "Pink Tulip"
}

-- List of items to display in the inventory grid
local creative_items = {
    "stone", "dirt", "grass", "sand", "gravel", 
    "log_oak", "leaves_oak", "log_birch", "leaves_birch", "cactus",
    "short_grass", "flower_poppy", "flower_daisy", "flower_bluebell", 
    "flower_azure_bluet", "flower_allium", "flower_tulip_white", 
    "flower_tulip_red", "flower_tulip_pink"
}

function get_block_display_name(block_name)
    if not block_name or block_name == "air" then return "" end
    return block_display_names[block_name] or block_name
end

function toggle_creative_inventory()
    creative_open = not creative_open
    if creative_open then
        print("[UI] Creative Inventory Opened")
    else
        print("[UI] Creative Inventory Closed")
    end
end

-- Render function called every frame when open
function render_creative_inventory(width, height)
    if not creative_open then return end

    -- 1. Dim background
    ui.draw_rect(0, 0, width, height, 0.0, 0.0, 0.0, 0.4)

    -- 2. Minecraft Creative Menu Window Panel
    local inv_w = 420
    local inv_h = 240
    local inv_x = (width - inv_w) / 2
    local inv_y = (height - inv_h) / 2

    -- Window Background (Classic Minecraft GUI greyish tone)
    ui.draw_rect(inv_x, inv_y, inv_w, inv_h, 0.75, 0.75, 0.75, 0.98)
    ui.draw_border(inv_x, inv_y, inv_w, inv_h, 0.2, 0.2, 0.2, 1.0)

    -- Title Bar
    ui.draw_text("Item Selection", inv_x + 12, inv_y + 12, 0.2, 0.2, 0.2)

    -- 3. Draw Item Grid Slots (9 columns)
    local slot_size = 36
    local padding = 6
    local start_x = inv_x + 16
    local start_y = inv_y + 45
    local col_count = 9

    -- Get mouse position if available
    local mx, my = -1, -1
    if ui.get_mouse_pos then
        mx, my = ui.get_mouse_pos()
    end

    local hovered_item_name = nil

    for i, block_name in ipairs(creative_items) do
        local col = (i - 1) % col_count
        local row = math.floor((i - 1) / col_count)

        local sx = start_x + col * (slot_size + padding)
        local sy = start_y + row * (slot_size + padding)

        -- Slot background
        ui.draw_rect(sx, sy, slot_size, slot_size, 0.52, 0.52, 0.52, 1.0)
        ui.draw_border(sx, sy, slot_size, slot_size, 0.35, 0.35, 0.35, 1.0)

        -- Draw item icon if engine supports drawing item icons in Lua UI
        local block_id = engine.get_block_id(block_name)
        if block_id and block_id > 0 and ui.draw_item_icon then
            ui.draw_item_icon(block_id, sx + 2, sy + 2, slot_size - 4)
        end

        -- Check hover collision
        if mx >= sx and mx <= sx + slot_size and my >= sy and my <= sy + slot_size then
            hovered_item_name = block_name
            -- Highlight hovered slot
            ui.draw_rect(sx, sy, slot_size, slot_size, 1.0, 1.0, 1.0, 0.3)
        end
    end

    -- 4. Draw Minecraft-style Hover Tooltip
    if hovered_item_name then
        local display_name = get_block_display_name(hovered_item_name)
        if mx > 0 and my > 0 then
            local text_w = #display_name * 8
            -- Dark purple/black Minecraft tooltip box
            ui.draw_rect(mx + 12, my - 22, text_w + 12, 22, 0.1, 0.0, 0.2, 0.95)
            ui.draw_border(mx + 12, my - 22, text_w + 12, 22, 0.5, 0.1, 0.8, 1.0)
            ui.draw_text(display_name, mx + 18, my - 18, 1.0, 1.0, 1.0)
        end
    end
end

-- Handle clicks in creative inventory to select block into active hotbar slot
function handle_creative_click(x, y, width, height)
    if not creative_open then return end

    local inv_w = 420
    local inv_h = 240
    local inv_x = (width - inv_w) / 2
    local inv_y = (height - inv_h) / 2

    local slot_size = 36
    local padding = 6
    local start_x = inv_x + 16
    local start_y = inv_y + 45
    local col_count = 9

    for i, block_name in ipairs(creative_items) do
        local col = (i - 1) % col_count
        local row = math.floor((i - 1) / col_count)

        local sx = start_x + col * (slot_size + padding)
        local sy = start_y + row * (slot_size + padding)

        if x >= sx and x <= sx + slot_size and y >= sy and y <= sy + slot_size then
            print("[UI] Selected block from creative inventory: " .. block_name)
            if hotbar and hotbar.set_slot then
                local active_slot = hotbar.get_active_slot and hotbar.get_active_slot() or 1
                hotbar.set_slot(active_slot, block_name)
            end
            break
        end
    end
end