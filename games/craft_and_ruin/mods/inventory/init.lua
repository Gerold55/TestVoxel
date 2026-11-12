-- creative_inventory.lua - Dynamic Group-Sorted Creative Inventory & Tooltips
print("[UI] Loading Dynamic Group-Sorted Creative Inventory...")

local creative_open = false
local current_tab = 1
local search_query = ""
local scroll_offset = 0

-- --- TABS & GROUP MAPPINGS ---
local tabs = {
    { name = "Building", group_key = "building" },
    { name = "Nature",   group_key = "nature" },
    { name = "Flora",    group_key = "flora" },
    { name = "All Items",group_key = "all" }
}

local categorized_items = { {}, {}, {}, {} }
local block_display_names = {}
local block_tooltips = {} -- Stores custom lore/descriptions if provided by engine

-- Automatically load registered blocks, their groups, and display info from the engine
function refresh_creative_items()
    categorized_items = { {}, {}, {}, {} }
    
    local registered = {}
    if engine.get_registered_blocks then
        registered = engine.get_registered_blocks()
    elseif engine.get_block_list then
        registered = engine.get_block_list()
    else
        registered = {"stone", "dirt", "grass", "sand", "gravel", "log_oak", "leaves_oak", "cactus", "flower_poppy"}
    end

    for _, block_name in ipairs(registered) do
        if block_name and block_name ~= "air" then
            -- 1. Add to "All Items" tab (Tab 4)
            table.insert(categorized_items[4], block_name)

            -- 2. Query engine for block group metadata if available
            local block_group = "building" -- default fallback group
            if engine.get_block_group then
                block_group = string.lower(engine.get_block_group(block_name) or "building")
            else
                -- Fallback keyword auto-grouping if engine doesn't have a direct get_block_group function
                if string.find(block_name, "flower") or string.find(block_name, "tulip") then
                    block_group = "flora"
                elseif string.find(block_name, "grass") or string.find(block_name, "leaves") or string.find(block_name, "cactus") then
                    block_group = "nature"
                else
                    block_group = "building"
                end
            end

            -- 3. Query custom display names and tooltips from engine if available
            if engine.get_block_display_name then
                block_display_names[block_name] = engine.get_block_display_name(block_name)
            end
            if engine.get_block_tooltip then
                block_tooltips[block_name] = engine.get_block_tooltip(block_name)
            end

            -- 4. Sort into the matching tab based on group key
            local placed = false
            for i = 1, 3 do
                if tabs[i].group_key == block_group then
                    table.insert(categorized_items[i], block_name)
                    placed = true
                    break
                end
            end
            -- Default fallback if group didn't match tabs 1-3
            if not placed then
                table.insert(categorized_items[1], block_name)
            end
        end
    end
    print("[UI] Successfully sorted blocks into creative tabs by groups.")
end

-- Initialize on script load
refresh_creative_items()

function get_block_display_name(block_name)
    if not block_name or block_name == "air" then return "" end
    if block_display_names[block_name] then
        return block_display_names[block_name]
    end
    local formatted = string.gsub(block_name, "_", " ")
    return (string.gsub(formatted, "^%l", string.upper))
end

function toggle_creative_inventory()
    creative_open = not creative_open
    search_query = ""
    scroll_offset = 0
    if creative_open then
        refresh_creative_items()
        print("[UI] Creative Inventory Opened")
    else
        print("[UI] Creative Inventory Closed")
    end
end

function handle_creative_char_input(char)
    if not creative_open then return end
    if char == "\b" or char == "BACKSPACE" then
        if #search_query > 0 then
            search_query = string.sub(search_query, 1, #search_query - 1)
        end
    else
        search_query = search_query .. char
    end
    scroll_offset = 0
end

-- Render function called every frame when open
function render_creative_inventory(width, height)
    if not creative_open then return end

    -- 1. Backdrop dim
    ui.draw_rect(0, 0, width, height, 0.0, 0.0, 0.0, 0.5)

    -- 2. Minecraft Creative Menu Window Panel
    local inv_w = 460
    local inv_h = 230
    local inv_x = (width - inv_w) / 2
    local inv_y = (height - inv_h) / 2

    ui.draw_rect(inv_x, inv_y, inv_w, inv_h, 0.78, 0.76, 0.72, 0.98)
    ui.draw_border(inv_x, inv_y, inv_w, inv_h, 0.22, 0.20, 0.18, 1.0)

    -- Title Bar & Search Box
    ui.draw_text("Item Selection", inv_x + 14, inv_y + 10, 0.25, 0.23, 0.20)

    local search_x = inv_x + 220
    local search_y = inv_y + 8
    local search_w = 224
    local search_h = 22
    ui.draw_rect(search_x, search_y, search_w, search_h, 0.95, 0.95, 0.93, 1.0)
    ui.draw_border(search_x, search_y, search_w, search_h, 0.35, 0.33, 0.30, 1.0)
    
    local display_search = search_query == "" and "Search items..." or search_query
    local text_color = search_query == "" and 0.5 or 0.15
    ui.draw_text(display_search, search_x + 8, search_y + 5, text_color, text_color, text_color)

    -- 3. Draw Category Tabs
    local tab_w = 48
    local tab_h = 26
    for i, tab in ipairs(tabs) do
        local tx = inv_x + 14 + (i - 1) * (tab_w + 4)
        local ty = inv_y - tab_h
        local is_active = (current_tab == i) and search_query == ""

        local tab_r = is_active and 0.82 or 0.60
        local tab_g = is_active and 0.80 or 0.58
        local tab_b = is_active and 0.76 or 0.54

        ui.draw_rect(tx, ty, tab_w, tab_h, tab_r, tab_g, tab_b, 1.0)
        ui.draw_border(tx, ty, tab_w, tab_h, 0.22, 0.20, 0.18, 1.0)

        ui.draw_text(string.sub(tab.name, 1, 4), tx + 6, ty + 7, 0.2, 0.2, 0.2)
    end

    -- 4. Filter Items
    local active_items = {}
    if search_query ~= "" then
        for _, item in ipairs(categorized_items[4]) do
            if string.find(string.lower(item), string.lower(search_query)) or 
               string.find(string.lower(get_block_display_name(item)), string.lower(search_query)) then
                table.insert(active_items, item)
            end
        end
    else
        active_items = categorized_items[current_tab] or {}
    end

    -- 5. Draw Item Grid Slots (9 columns x 3 rows)
    local slot_size = 36
    local padding = 6
    local start_x = inv_x + 16
    local start_y = inv_y + 42
    local col_count = 9
    local row_count = 3

    local mx, my = -1, -1
    if ui.get_mouse_pos then
        mx, my = ui.get_mouse_pos()
    end

    local hovered_item_name = nil

    local total_items = #active_items
    local max_scroll = math.max(0, math.ceil(total_items / col_count) - row_count)
    scroll_offset = math.max(0, math.min(scroll_offset, max_scroll))

    local render_start_idx = scroll_offset * col_count + 1
    local render_end_idx = math.min(total_items, render_start_idx + (col_count * row_count) - 1)

    for i = render_start_idx, render_end_idx do
        local block_name = active_items[i]
        local grid_idx = i - render_start_idx
        local col = grid_idx % col_count
        local row = math.floor(grid_idx / col_count)

        local sx = start_x + col * (slot_size + padding)
        local sy = start_y + row * (slot_size + padding)

        ui.draw_rect(sx, sy, slot_size, slot_size, 0.58, 0.56, 0.52, 1.0)
        ui.draw_border(sx, sy, slot_size, slot_size, 0.35, 0.33, 0.30, 1.0)

        local block_id = engine.get_block_id(block_name)
        if block_id and block_id > 0 and ui.draw_item_icon then
            ui.draw_item_icon(block_id, sx + 2, sy + 2, slot_size - 4)
        end

        if mx >= sx and mx <= sx + slot_size and my >= sy and my <= sy + slot_size then
            hovered_item_name = block_name
            ui.draw_rect(sx, sy, slot_size, slot_size, 1.0, 1.0, 1.0, 0.35)
        end
    end

    -- 6. Advanced Minecraft-style Tooltip Box (Supports Display Name + Optional Engine Description)
    if hovered_item_name then
        local display_name = get_block_display_name(hovered_item_name)
        local tooltip_desc = block_tooltips[hovered_item_name] or nil
        
        if mx > 0 and my > 0 then
            local box_w = #display_name * 8 + 16
            local box_h = tooltip_desc and 40 or 24
            
            -- Tooltip container background
            ui.draw_rect(mx + 12, my - box_h - 4, box_w, box_h, 0.10, 0.02, 0.18, 0.96)
            ui.draw_border(mx + 12, my - box_h - 4, box_w, box_h, 0.55, 0.15, 0.85, 1.0)
            
            -- Render item title
            ui.draw_text(display_name, mx + 18, my - box_h + 2, 1.0, 1.0, 1.0)
            
            -- Render sub-description if present
            if tooltip_desc then
                ui.draw_text(tooltip_desc, mx + 18, my - box_h + 18, 0.65, 0.65, 0.65)
            end
        end
    end
end

function handle_creative_click(x, y, width, height)
    if not creative_open then return end

    local inv_w = 460
    local inv_h = 230
    local inv_x = (width - inv_w) / 2
    local inv_y = (height - inv_h) / 2

    local tab_w = 48
    local tab_h = 26
    for i = 1, #tabs do
        local tx = inv_x + 14 + (i - 1) * (tab_w + 4)
        local ty = inv_y - tab_h
        if x >= tx and x <= tx + tab_w and y >= ty and y <= ty + tab_h then
            current_tab = i
            search_query = ""
            scroll_offset = 0
            print("[UI] Switched to creative tab: " .. tabs[i].name)
            return
        end
    end

    local slot_size = 36
    local padding = 6
    local start_x = inv_x + 16
    local start_y = inv_y + 42
    local col_count = 9
    local row_count = 3

    local active_items = {}
    if search_query ~= "" then
        for _, item in ipairs(categorized_items[4]) do
            if string.find(string.lower(item), string.lower(search_query)) or 
               string.find(string.lower(get_block_display_name(item)), string.lower(search_query)) then
                table.insert(active_items, item)
            end
        end
    else
        active_items = categorized_items[current_tab] or {}
    end

    local render_start_idx = scroll_offset * col_count + 1
    local render_end_idx = math.min(#active_items, render_start_idx + (col_count * row_count) - 1)

    for i = render_start_idx, render_end_idx do
        local block_name = active_items[i]
        local grid_idx = i - render_start_idx
        local col = grid_idx % col_count
        local row = math.floor(grid_idx / col_count)

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

function handle_creative_scroll(scroll_delta)
    if not creative_open then return end
    if scroll_delta > 0 then
        scroll_offset = scroll_offset - 1
    else
        scroll_offset = scroll_offset + 1
    end
end