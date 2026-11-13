-- inventory.lua — Creative Inventory
-- ---------------------------------------------------------------------------
-- All layout, colours and behaviour live in this file. Edit freely!
--
-- C++ provides:
--   ui.draw_rect(x, y, w, h, r, g, b [, a])
--   ui.draw_text(text, x, y, scale, r, g, b [, a])
--   ui.draw_block_icon(name, x, y, size)
--   ui.get_all_blocks()            -> { "stone", "dirt", ... }
--   ui.get_block_info(name)        -> { id, name, pattern, has_texture, in_creative } or nil
--   ui.get_mouse()                 -> x, y, left_down, right_down
--   ui.get_time()                  -> seconds
--   ui.keys.*, ui.actions.*        -> key / action constants
--   hotbar.get_slot_count(), hotbar.get_active_slot(), hotbar.set_active_slot(i)
--   hotbar.get_slot(i) -> name, count      hotbar.set_slot(i, name [, count])
--   (hotbar slots are 1-based)
--
-- C++ calls these (all optional):
--   render_creative_inventory(width, height)
--   handle_creative_click(mx, my, width, height)
--   handle_creative_scroll(dy)
--   handle_creative_key(key, action, mods)  -> return true to consume the key
--   handle_creative_char(codepoint, char)   -> return true to consume the char
--   on_creative_opened(), on_creative_closed()
-- ---------------------------------------------------------------------------

print("[UI] inventory.lua loaded")

-- ===========================================================================
-- THEME — change these to restyle the inventory
-- ===========================================================================

local Theme = {
    colors = {
        black       = { 0.000, 0.000, 0.000, 1.00 },
        white       = { 1.000, 1.000, 1.000, 1.00 },
        dim         = { 0.000, 0.000, 0.000, 0.55 },
        panel       = { 0.776, 0.776, 0.776, 1.00 },
        shadow      = { 0.333, 0.333, 0.333, 1.00 },
        tab_idle    = { 0.560, 0.560, 0.560, 1.00 },
        slot_fill   = { 0.545, 0.545, 0.545, 1.00 },
        slot_dark   = { 0.216, 0.216, 0.216, 1.00 },
        hover       = { 1.000, 1.000, 1.000, 0.40 },
        text_dark   = { 0.250, 0.250, 0.250, 1.00 },
        text_gray   = { 0.550, 0.550, 0.550, 1.00 },
        search_bdr  = { 0.627, 0.627, 0.627, 1.00 },
        tip_bg      = { 0.063, 0.000, 0.063, 0.94 },
        tip_border  = { 0.314, 0.000, 1.000, 0.50 },
        selected    = { 1.000, 1.000, 1.000, 1.00 },
    },

    -- Font metrics at scale 1.0.
    --   char_w   : average character width (for centring / tooltip width)
    --   char_h   : line height
    --   y_offset : fraction of char_h to shift text down (the font draws from its baseline)
    font = { char_w = 13, char_h = 14, base_scale = 0.5, y_offset = 0.8 },

    -- Sizes in "GUI pixels" (multiplied by the GUI scale)
    cols = 9, rows = 5,
    margin = 11, slot = 36, slot_gap = 2,
    tab_w = 52, tab_h = 30, tab_gap = 2,
    tab_raise = 0,       -- extra height of the selected tab
    tab_tuck  = 4,       -- how far unselected tabs tuck behind the panel
    tab_icon  = 20,
    search_w = 150, search_h = 20,
    scrollbar_w = 14, scrollbar_gap = 6,
    hotbar_gap = 12,
    max_search = 32,
    stack_size = 64,     -- items picked from the creative grid come as a stack of this size
}

-- ===========================================================================
-- TABS — add, remove or reorder tabs here
-- `icons` = first block in the list that exists (and has a texture) is the tab icon
-- `filter(name, info)` returns true if the block belongs in the tab
-- ===========================================================================

local function contains(s, sub) return string.find(s, sub, 1, true) ~= nil end

local function is_fluid(name) return contains(name, "water") or contains(name, "lava") end

local function is_item(name)
    return name == "hoe" or name == "scythe" or name == "stick"
end

local function is_nature(name, info)
    return (info and info.pattern == "plantlike")
        or contains(name, "leaves") or contains(name, "sapling")
        or contains(name, "flower") or name == "short_grass"
end

local Tabs = {
    { name = "All Items",
      icons = { "grass_block", "grass", "dirt" },
      filter = function(name, info) return true end },

    { name = "Building Blocks",
      icons = { "bricks", "stone", "cobblestone" },
      filter = function(name, info) return not is_item(name) and not is_nature(name, info) end },

    { name = "Nature",
      icons = { "flower_red", "sapling", "leaves", "short_grass" },
      filter = function(name, info) return is_nature(name, info) end },

    { name = "Items",
      icons = { "stick", "scythe", "hoe", "oak_log" },
      filter = function(name, info) return is_item(name) end },
}

-- Blocks hidden from every tab
local function is_hidden(name) return is_fluid(name) end

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    tab = 1,
    search = "",
    search_focused = false,
    scroll = 0,
    drag_thumb = false,
    held = nil,          -- item stuck to the cursor: { name = "stone", count = 64 } or nil
}

local info_cache = {}
local function block_info(name)
    local v = info_cache[name]
    if v == nil then
        v = (ui and ui.get_block_info and ui.get_block_info(name)) or false
        info_cache[name] = v
    end
    return v or nil
end

-- ===========================================================================
-- HELPERS
-- ===========================================================================

local C = Theme.colors

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function inside(mx, my, x, y, w, h)
    return mx >= x and mx < x + w and my >= y and my < y + h
end

local function pretty_name(name)
    local s = name:gsub("_", " ")
    s = s:gsub("(%a)(%w*)", function(a, b) return a:upper() .. b end)
    return s
end

local function rect(x, y, w, h, c)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], c[4] or 1)
end

-- Draws text with `y` meaning the TOP of the text.
local function text(str, x, y, scale, c, shadow, S)
    y = y + Theme.font.char_h * scale * Theme.font.y_offset
    if shadow then
        ui.draw_text(str, x + S, y + S, scale, c[1] * 0.25, c[2] * 0.25, c[3] * 0.25, c[4] or 1)
    end
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end

local function text_width(str, scale) return #str * Theme.font.char_w * scale end
local function text_height(scale) return Theme.font.char_h * scale end

local function has_icon(name)
    local info = block_info(name)
    return info ~= nil and info.has_texture
end

local function tab_icon(tab)
    for _, n in ipairs(tab.icons) do
        if has_icon(n) then return n end
    end
    return nil
end

-- ===========================================================================
-- WIDGETS
-- ===========================================================================

-- Raised panel: black outline, white top-left bevel, dark bottom-right bevel
local function draw_panel(x, y, w, h, S, fill)
    rect(x, y, w, h, C.black)
    rect(x + S, y + S, w - 2 * S, h - 2 * S, C.white)
    rect(x + 3 * S, y + 3 * S, w - 4 * S, h - 4 * S, C.shadow)
    rect(x + 3 * S, y + 3 * S, w - 6 * S, h - 6 * S, fill)
end

-- Recessed slot: dark top-left edge, white bottom-right edge
local function draw_slot(x, y, w, h, S)
    rect(x, y, w, h, C.slot_dark)
    rect(x + S, y + S, w - S, h - S, C.white)
    rect(x + S, y + S, w - 2 * S, h - 2 * S, C.slot_fill)
end

-- Raised button (scrollbar thumb)
local function draw_button(x, y, w, h, S, fill)
    rect(x, y, w, h, C.shadow)
    rect(x, y, w - S, h - S, C.white)
    rect(x + S, y + S, w - 2 * S, h - 2 * S, fill)
end

-- Selected tab: bevel on top/left/right only, and its fill runs down over the
-- panel's top border so the tab and panel join seamlessly.
local function draw_active_tab(x, top, w, panel_y, S)
    local bottom = panel_y + 3 * S           -- just past the panel's top border
    local h = bottom - top
    rect(x, top, w, h, C.black)                                  -- outline
    rect(x + S, top + S, w - 2 * S, h - S, C.white)              -- top/left highlight
    rect(x + w - 3 * S, top + 3 * S, 2 * S, h - 3 * S, C.shadow) -- right shadow
    rect(x + 3 * S, top + 3 * S, w - 6 * S, h - 3 * S, C.panel)  -- fill (covers panel border)
end

local function draw_tooltip(str, mx, my, W, H, S, fs)
    local pad = 4 * S
    local tw = text_width(str, fs) + pad * 2
    local th = text_height(fs) + pad * 2
    local tx = mx + 12 * S
    local ty = my - 12 * S - th * 0.5
    if tx + tw > W then tx = mx - 12 * S - tw end
    ty = clamp(ty, 0, math.max(0, H - th))

    rect(tx, ty, tw, th, C.tip_bg)
    rect(tx + S, ty + S, tw - 2 * S, S, C.tip_border)
    rect(tx + S, ty + th - 2 * S, tw - 2 * S, S, C.tip_border)
    rect(tx + S, ty + 2 * S, S, th - 4 * S, C.tip_border)
    rect(tx + tw - 2 * S, ty + 2 * S, S, th - 4 * S, C.tip_border)
    text(str, tx + pad, ty + pad, fs, C.white, true, S)
end

-- ===========================================================================
-- LAYOUT — shared by drawing and clicking so they always match
-- ===========================================================================

local function compute_layout(W, H)
    local T = Theme
    local S = math.min(W / 560, H / 470)
    S = clamp(math.floor(S * 2) / 2, 1, 3)   -- snap to 0.5 for crisp edges

    local L = { S = S, cols = T.cols, rows = T.rows }
    L.m      = T.margin * S
    L.slot   = T.slot * S
    L.pitch  = (T.slot + T.slot_gap) * S
    L.gridW  = L.cols * L.pitch - T.slot_gap * S
    L.gridH  = L.rows * L.pitch - T.slot_gap * S
    L.sbW    = T.scrollbar_w * S
    L.searchW, L.searchH = T.search_w * S, T.search_h * S
    L.tabW, L.tabH, L.tabGap = T.tab_w * S, T.tab_h * S, T.tab_gap * S

    local header = L.searchH + 8 * S
    L.w = 2 * L.m + L.gridW + T.scrollbar_gap * S + L.sbW
    L.h = 2 * L.m + header + L.gridH + T.hotbar_gap * S + L.slot
    L.x = math.floor((W - L.w) / 2 + 0.5)
    L.y = math.floor((H - L.h + L.tabH) / 2 + 0.5)

    -- Tabs
    L.tabTopActive = L.y - L.tabH - T.tab_raise * S   -- selected tab: raised
    L.tabTopIdle   = L.y - L.tabH + T.tab_tuck * S    -- other tabs: bottom hidden behind panel

    L.searchX = L.x + L.w - L.m - L.searchW
    L.searchY = L.y + L.m
    L.clearX  = L.searchX + L.searchW - L.searchH
    L.gridX   = L.x + L.m
    L.gridY   = L.y + L.m + header
    L.sbX     = L.gridX + L.gridW + T.scrollbar_gap * S
    L.sbY     = L.gridY
    L.sbH     = L.gridH
    L.hotX    = L.gridX
    L.hotY    = L.gridY + L.gridH + T.hotbar_gap * S
    return L
end

local function tab_x(L, i) return L.x + (i - 1) * (L.tabW + L.tabGap) end

-- Clickable / hoverable area of a tab (same for all tabs)
local function tab_hit(L, i, mx, my)
    return inside(mx, my, tab_x(L, i), L.tabTopActive, L.tabW, L.y - L.tabTopActive)
end

local function thumb_for(L, count)
    local S = L.S
    local total_rows = math.floor((count + L.cols - 1) / L.cols)
    local max_scroll = math.max(0, total_rows - L.rows)
    local track = L.sbH - 2 * S
    local t = { max_scroll = max_scroll }
    if max_scroll == 0 then
        t.h, t.y = track, L.sbY + S
    else
        t.h = math.max(16 * S, track * L.rows / total_rows)
        local row = clamp(state.scroll, 0, max_scroll)
        t.y = L.sbY + S + (track - t.h) * row / max_scroll
    end
    return t
end

local function scroll_from_mouse(L, t, my)
    local range = L.sbH - 2 * L.S - t.h
    if range <= 0 or t.max_scroll == 0 then return 0 end
    local f = clamp((my - (L.sbY + L.S) - t.h * 0.5) / range, 0, 1)
    return math.floor(f * t.max_scroll + 0.5)
end

-- ===========================================================================
-- ITEMS
-- ===========================================================================

local function get_items()
    local all = (ui and ui.get_all_blocks and ui.get_all_blocks()) or {}
    local tab = Tabs[state.tab]
    local query = state.search:lower()
    local list = {}

    for _, name in ipairs(all) do
        if name ~= "air" and not is_hidden(name) then
            local info = block_info(name)
            if tab.filter(name, info) then
                if query == ""
                    or contains(name:lower(), query)
                    or contains(pretty_name(name):lower(), query) then
                    table.insert(list, name)
                end
            end
        end
    end
    return list
end

-- ===========================================================================
-- RENDER
-- ===========================================================================

function render_creative_inventory(W, H)
    if not ui or not ui.draw_rect then return end

    local L  = compute_layout(W, H)
    local S  = L.S
    local fs = Theme.font.base_scale * S
    local mx, my, left_down = ui.get_mouse()

    local items = get_items()
    local th = thumb_for(L, #items)
    if state.drag_thumb then
        if left_down then state.scroll = scroll_from_mouse(L, th, my)
        else state.drag_thumb = false end
    end
    state.scroll = clamp(state.scroll, 0, th.max_scroll)
    th = thumb_for(L, #items)

    local tooltip = nil

    -- 1. Dim the world
    rect(0, 0, W, H, C.dim)

    -- 2. Other tabs (drawn first so the panel covers their bottom edge)
    for i = 1, #Tabs do
        if i ~= state.tab then
            draw_panel(tab_x(L, i), L.tabTopIdle, L.tabW, L.tabH, S, C.tab_idle)
        end
    end

    -- 3. Main panel
    draw_panel(L.x, L.y, L.w, L.h, S, C.panel)

    -- 4. Selected tab (drawn after the panel, raised, joined to it)
    draw_active_tab(tab_x(L, state.tab), L.tabTopActive, L.tabW, L.y, S)

    -- 5. Tab icons + hover
    local icon_size = Theme.tab_icon * S
    for i, tab in ipairs(Tabs) do
        local tx = tab_x(L, i)
        local top, visible_h
        if i == state.tab then
            top, visible_h = L.tabTopActive, L.y - L.tabTopActive
        else
            top, visible_h = L.tabTopIdle, L.y - L.tabTopIdle   -- part above the panel
        end

        local icon = tab_icon(tab)
        if icon then
            ui.draw_block_icon(icon,
                tx + (L.tabW - icon_size) / 2,
                top + (visible_h - icon_size) / 2 + S,
                icon_size)
        end

        if tab_hit(L, i, mx, my) then tooltip = tab.name end
    end

    -- 6. Title + search box
    local text_y = L.searchY + (L.searchH - text_height(fs)) / 2
    local title = (state.search == "") and Tabs[state.tab].name or "Search Results"
    text(title, L.x + L.m, text_y, fs, C.text_dark, false, S)

    rect(L.searchX, L.searchY, L.searchW, L.searchH, state.search_focused and C.white or C.search_bdr)
    rect(L.searchX + S, L.searchY + S, L.searchW - 2 * S, L.searchH - 2 * S, C.black)

    local tx = L.searchX + 4 * S
    if state.search == "" and not state.search_focused then
        text("Search...", tx, text_y, fs, C.text_gray, false, S)
    else
        local avail = L.searchW - 8 * S - ((state.search ~= "") and L.searchH or 0)
        local max_chars = math.max(1, math.floor(avail / (Theme.font.char_w * fs)) - 1)
        local shown = state.search
        if #shown > max_chars then shown = shown:sub(#shown - max_chars + 1) end
        local blink = state.search_focused and (ui.get_time() % 1.0) < 0.5
        text(shown .. (blink and "_" or ""), tx, text_y, fs, C.white, false, S)
    end

    if state.search ~= "" then
        local hov = inside(mx, my, L.clearX, L.searchY, L.searchH, L.searchH)
        text("x", L.clearX + (L.searchH - Theme.font.char_w * fs) / 2, text_y, fs,
             hov and C.white or C.text_gray, false, S)
    end

    -- 7. Item grid
    for r = 0, L.rows - 1 do
        for c = 0, L.cols - 1 do
            local sx = L.gridX + c * L.pitch
            local sy = L.gridY + r * L.pitch
            draw_slot(sx, sy, L.slot, L.slot, S)

            local idx = (r + state.scroll) * L.cols + c + 1
            local name = items[idx]
            if name then
                ui.draw_block_icon(name, sx + 3 * S, sy + 3 * S, L.slot - 6 * S)
                if inside(mx, my, sx, sy, L.slot, L.slot) then
                    rect(sx + S, sy + S, L.slot - 2 * S, L.slot - 2 * S, C.hover)
                    tooltip = pretty_name(name)
                end
            end
        end
    end

    -- 8. Scrollbar
    draw_slot(L.sbX, L.sbY, L.sbW, L.sbH, S)
    draw_button(L.sbX + S, th.y, L.sbW - 2 * S, th.h, S,
                (th.max_scroll > 0) and C.panel or C.tab_idle)

    -- 9. Hotbar row (hotbar slots are 1-based)
    if hotbar and hotbar.get_slot_count then
        local n = math.min(hotbar.get_slot_count(), L.cols)
        local active = hotbar.get_active_slot()
        for i = 1, n do
            local sx, sy = L.hotX + (i - 1) * L.pitch, L.hotY
            draw_slot(sx, sy, L.slot, L.slot, S)

            local name, count = hotbar.get_slot(i)
            if name and name ~= "" then
                ui.draw_block_icon(name, sx + 3 * S, sy + 3 * S, L.slot - 6 * S)
                if count and count > 1 then
                    local str = tostring(count)
                    text(str, sx + L.slot - 3 * S - text_width(str, fs),
                         sy + L.slot - 3 * S - text_height(fs), fs, C.white, true, S)
                end
            end

            if inside(mx, my, sx, sy, L.slot, L.slot) then
                rect(sx + S, sy + S, L.slot - 2 * S, L.slot - 2 * S, C.hover)
                if name and name ~= "" then tooltip = pretty_name(name) end
            end

            if i == active then
                local o = 2 * S
                rect(sx - o, sy - o, L.slot + 2 * o, o, C.selected)
                rect(sx - o, sy + L.slot, L.slot + 2 * o, o, C.selected)
                rect(sx - o, sy, o, L.slot, C.selected)
                rect(sx + L.slot, sy, o, L.slot, C.selected)
            end
        end
    end

    -- 10. Tooltip (hidden while carrying an item, like Minecraft)
    if tooltip and mx >= 0 and not state.held then
        draw_tooltip(tooltip, mx, my, W, H, S, fs)
    end

    -- 11. Item held on the cursor (always on top)
    if state.held and mx >= 0 then
        local size = L.slot - 6 * S
        ui.draw_block_icon(state.held.name, mx - size / 2, my - size / 2, size)
        if state.held.count > 1 then
            local str = tostring(state.held.count)
            text(str, mx + size / 2 - text_width(str, fs), my + size / 2 - text_height(fs),
                 fs, C.white, true, S)
        end
    end
end

-- ===========================================================================
-- INPUT
-- ===========================================================================

function handle_creative_click(mx, my, W, H)
    local L = compute_layout(W, H)

    -- Tabs
    for i = 1, #Tabs do
        if tab_hit(L, i, mx, my) then
            if state.tab ~= i then state.tab = i; state.scroll = 0 end
            state.search_focused = false
            return
        end
    end

    -- Clear-search button
    if state.search ~= "" and inside(mx, my, L.clearX, L.searchY, L.searchH, L.searchH) then
        state.search = ""
        state.scroll = 0
        state.search_focused = true
        return
    end

    -- Search box
    if inside(mx, my, L.searchX, L.searchY, L.searchW, L.searchH) then
        state.search_focused = true
        return
    end
    state.search_focused = false

    local items = get_items()
    local th = thumb_for(L, #items)

    -- Scrollbar
    if inside(mx, my, L.sbX, L.sbY, L.sbW, L.sbH) then
        if th.max_scroll > 0 then
            state.drag_thumb = true
            state.scroll = scroll_from_mouse(L, th, my)
        end
        return
    end

    -- Item grid -> pick up a stack onto the cursor
    if inside(mx, my, L.gridX, L.gridY, L.gridW, L.gridH) then
        local c = math.floor((mx - L.gridX) / L.pitch)
        local r = math.floor((my - L.gridY) / L.pitch)
        local lx = mx - (L.gridX + c * L.pitch)
        local ly = my - (L.gridY + r * L.pitch)
        if lx >= L.slot or ly >= L.slot then return end   -- gap between slots

        local name = items[(r + state.scroll) * L.cols + c + 1]
        if not name then
            state.held = nil                               -- empty slot: put the held item away
        elseif state.held and state.held.name == name then
            state.held = nil                               -- same item: put it back
        else
            state.held = { name = name, count = Theme.stack_size }
        end
        return
    end

    -- Hotbar row -> place / pick up / swap (1-based slots)
    if hotbar and hotbar.get_slot_count then
        local n = math.min(hotbar.get_slot_count(), L.cols)
        for i = 1, n do
            if inside(mx, my, L.hotX + (i - 1) * L.pitch, L.hotY, L.slot, L.slot) then
                local slot_name, slot_count = hotbar.get_slot(i)
                local has_item = slot_name and slot_name ~= ""
                if has_item and (not slot_count or slot_count <= 0) then slot_count = Theme.stack_size end

                local held = state.held
                if held then
                    if has_item and slot_name == held.name then
                        -- Same item: merge stacks, leftovers stay on the cursor
                        local total = slot_count + held.count
                        local put = math.min(total, Theme.stack_size)
                        hotbar.set_slot(i, held.name, put)
                        local left = total - put
                        state.held = (left > 0) and { name = held.name, count = left } or nil
                    else
                        -- Empty slot: place. Different item: swap.
                        hotbar.set_slot(i, held.name, held.count)
                        state.held = has_item and { name = slot_name, count = slot_count } or nil
                    end
                elseif has_item then
                    -- Nothing held: pick the item up out of the slot
                    state.held = { name = slot_name, count = slot_count }
                    hotbar.set_slot(i, "", 0)
                end

                hotbar.set_active_slot(i)
                return
            end
        end
    end

    -- Clicked outside the inventory while holding something: throw it away
    if state.held and not inside(mx, my, L.x, L.y, L.w, L.h) then
        state.held = nil
    end
end

function handle_creative_scroll(dy)
    if dy > 0 then state.scroll = state.scroll - 1
    elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end   -- upper bound clamped while rendering
end

function handle_creative_key(key, action, mods)
    if not state.search_focused then return false end

    if action == ui.actions.PRESS or action == ui.actions.REPEAT then
        if key == ui.keys.BACKSPACE and #state.search > 0 then
            state.search = state.search:sub(1, -2)
            state.scroll = 0
        elseif key == ui.keys.ESCAPE or key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            state.search_focused = false
        end
    end
    return true   -- swallow keys while typing so 'E' / WASD don't trigger game actions
end

function handle_creative_char(codepoint, ch)
    if not state.search_focused then return false end
    if codepoint >= 32 and codepoint < 127 and #state.search < Theme.max_search then
        state.search = state.search .. ch
        state.scroll = 0
    end
    return true
end

function on_creative_opened()
    info_cache = {}   -- pick up any blocks registered since last time
end

function on_creative_closed()
    state.search_focused = false
    state.drag_thumb = false
    state.held = nil     -- closing the inventory drops whatever is on the cursor
end

-- Kept for compatibility with existing C++ calls
function set_creative_open(isOpen) end
function toggle_creative_inventory() end
