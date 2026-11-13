-- world_select.lua - Singleplayer world list, "Create New World" and "Delete World" screens
-- ---------------------------------------------------------------------------
-- Shown after clicking Singleplayer on the title screen. Everything is editable.
--
-- Worlds belong to the game they were made with. Only the selected game's worlds
-- are listed, and they can only be played while that game's mods are loaded
-- (the game that was selected when the program started).
--
-- Engine functions used:
--   engine.list_worlds()               -> { { name = "My World", seed = "123" }, ... } newest first
--   engine.create_world(name, seed)    -> folder name, or nil + error
--   engine.play_world(name)            -> true, or false + error
--   engine.delete_world(name)          -> true, or false + error
--   engine.get_game_info()             -> { selected, loaded, title, can_play }
--   engine.back_to_title()
--
-- C++ calls:
--   render_world_select(W, H)
--   handle_world_select_click(mx, my, W, H)
--   handle_world_select_scroll(dy)
--   handle_world_select_key(key, action, mods)  -> true = handled
--   handle_world_select_char(codepoint, char)   -> true = handled
--   on_world_select_opened()
-- ---------------------------------------------------------------------------

print("[UI] world_select.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local Theme = {
    colors = {
        background     = { 0.10, 0.10, 0.10, 1.00 },
        list_bg        = { 0.00, 0.00, 0.00, 0.45 },
        row_hover      = { 1.00, 1.00, 1.00, 0.08 },
        row_selected   = { 1.00, 1.00, 1.00, 1.00 },   -- outline of the selected row
        row_selected_bg= { 0.00, 0.00, 0.00, 1.00 },
        title          = { 1.00, 1.00, 1.00, 1.00 },
        name           = { 1.00, 1.00, 1.00, 1.00 },
        detail         = { 0.63, 0.63, 0.63, 1.00 },
        warning        = { 1.00, 0.45, 0.40, 1.00 },
        outline        = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover  = { 1.00, 1.00, 1.00, 1.00 },
        button         = { 0.44, 0.44, 0.44, 1.00 },
        button_hover   = { 0.50, 0.55, 0.78, 1.00 },
        button_off     = { 0.27, 0.27, 0.27, 1.00 },
        danger         = { 0.55, 0.20, 0.20, 1.00 },   -- the confirm-delete button
        danger_hover   = { 0.75, 0.25, 0.25, 1.00 },
        bevel_light    = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark     = { 0.00, 0.00, 0.00, 0.35 },
        text           = { 0.88, 0.88, 0.88, 1.00 },
        text_hover     = { 1.00, 1.00, 0.63, 1.00 },
        text_off       = { 0.63, 0.63, 0.63, 1.00 },
        field_bg       = { 0.00, 0.00, 0.00, 1.00 },
        field_border   = { 0.63, 0.63, 0.63, 1.00 },
        field_focus    = { 1.00, 1.00, 1.00, 1.00 },
    },

    -- Keep in sync with inventory.lua / pause_menu.lua
    font = { char_w = 13, char_h = 14, base_scale = 0.5, y_offset = 0.8 },

    -- Sizes in GUI pixels (multiplied by the GUI scale)
    list_w = 270, row_h = 36,
    button_w = 150, button_h = 20, button_gap = 4,
    field_w = 200, field_h = 20,
    max_name = 32, max_seed = 32,
    double_click = 0.35,   -- seconds between clicks to count as a double-click
}

local C = Theme.colors
local KEY_TAB = 258        -- GLFW_KEY_TAB

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    mode = "list",         -- "list", "create" or "delete"
    worlds = nil,
    selected = nil,
    scroll = 0,
    last_click_row = nil,
    last_click_time = 0,
    name = "New World",    -- create screen fields
    seed = "",
    focus = "name",
    message = nil,         -- error shown under the title (e.g. wrong game)
    game = nil,            -- engine.get_game_info()
}

-- ===========================================================================
-- HELPERS
-- ===========================================================================

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function inside(mx, my, x, y, w, h)
    return mx >= x and mx < x + w and my >= y and my < y + h
end

local function rect(x, y, w, h, c)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], c[4] or 1)
end

local function text_width(str, scale) return #str * Theme.font.char_w * scale end
local function text_height(scale) return Theme.font.char_h * scale end

-- `y` is the TOP of the text
local function text(str, x, y, scale, c, shadow, S)
    y = y + Theme.font.char_h * scale * Theme.font.y_offset
    if shadow then
        ui.draw_text(str, x + S, y + S, scale, c[1] * 0.25, c[2] * 0.25, c[3] * 0.25, c[4] or 1)
    end
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end

local function text_centered(str, cx, y, scale, c, S)
    text(str, cx - text_width(str, scale) / 2, y, scale, c, true, S)
end

local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

local function can_play()
    return state.game == nil or state.game.can_play ~= false
end

local function selected_world()
    return state.worlds and state.selected and state.worlds[state.selected] or nil
end

local function refresh_worlds()
    state.game = (engine and engine.get_game_info and engine.get_game_info()) or nil
    state.worlds = (engine and engine.list_worlds and engine.list_worlds()) or {}
    if state.selected and state.selected > #state.worlds then state.selected = nil end
    if not state.selected and #state.worlds > 0 then state.selected = 1 end
end

-- ===========================================================================
-- ACTIONS
-- ===========================================================================

local function play_selected()
    local w = selected_world()
    if not w then return end
    local ok, err = engine.play_world(w.name)
    if ok == false then state.message = err end
end

local function open_create()
    state.mode = "create"
    state.name = "New World"
    state.seed = ""
    state.focus = "name"
    state.message = nil
end

local function create_and_play()
    local folder, err = engine.create_world(state.name, state.seed)
    state.mode = "list"
    if not folder then
        state.message = err
        return
    end
    refresh_worlds()
    local ok, perr = engine.play_world(folder)
    if ok == false then state.message = perr end
end

local function open_delete()
    if not selected_world() then return end
    state.mode = "delete"
    state.message = nil
end

local function confirm_delete()
    local w = selected_world()
    state.mode = "list"
    if not w then return end
    local ok, err = engine.delete_world(w.name)
    if ok == false then
        state.message = err
    else
        state.message = nil
        state.selected = nil
    end
    refresh_worlds()
end

-- ===========================================================================
-- WIDGETS
-- ===========================================================================

local function draw_button(b, mx, my, S, fs)
    local hovered = b.enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local fill
    if not b.enabled then
        fill = C.button_off
    elseif b.danger then
        fill = hovered and C.danger_hover or C.danger
    else
        fill = hovered and C.button_hover or C.button
    end
    local outline = hovered and C.outline_hover or C.outline

    rect(b.x, b.y, b.w, b.h, outline)
    rect(b.x + S, b.y + S, b.w - 2 * S, b.h - 2 * S, fill)
    if b.enabled then
        rect(b.x + S, b.y + S, b.w - 2 * S, S, C.bevel_light)
        rect(b.x + S, b.y + 2 * S, S, b.h - 3 * S, C.bevel_light)
        rect(b.x + S, b.y + b.h - 2 * S, b.w - 2 * S, S, C.bevel_dark)
    end

    local tc = (not b.enabled) and C.text_off or (hovered and C.text_hover or C.text)
    text_centered(b.label, b.x + b.w / 2, b.y + (b.h - text_height(fs)) / 2, fs, tc, S)
end

local function draw_field(f, value, focused, placeholder, S, fs)
    rect(f.x, f.y, f.w, f.h, focused and C.field_focus or C.field_border)
    rect(f.x + S, f.y + S, f.w - 2 * S, f.h - 2 * S, C.field_bg)

    local ty = f.y + (f.h - text_height(fs)) / 2
    if value == "" and not focused and placeholder then
        text(placeholder, f.x + 4 * S, ty, fs, C.detail, false, S)
        return
    end

    local max_chars = math.max(1, math.floor((f.w - 8 * S) / (Theme.font.char_w * fs)) - 1)
    local shown = value
    if #shown > max_chars then shown = shown:sub(#shown - max_chars + 1) end
    local blink = focused and (ui.get_time() % 1.0) < 0.5
    text(shown .. (blink and "_" or ""), f.x + 4 * S, ty, fs, C.name, false, S)
end

local function click_buttons(buttons, mx, my)
    for _, b in ipairs(buttons) do
        if b.enabled and inside(mx, my, b.x, b.y, b.w, b.h) then
            b.action()
            return true
        end
    end
    return false
end

-- Two rows of two equal buttons, centred at the bottom of the screen
local function bottom_rows(W, H, S)
    local bw, bh, gap = Theme.button_w * S, Theme.button_h * S, Theme.button_gap * S
    local totalW = bw * 2 + gap
    local bx = math.floor((W - totalW) / 2)
    local row2Y = H - 10 * S - bh
    local row1Y = row2Y - gap - bh
    return bx, bw, bh, gap, row1Y, row2Y
end

-- ===========================================================================
-- WORLD LIST SCREEN
-- ===========================================================================

local function list_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local bx, bw, bh, gap, row1Y, row2Y = bottom_rows(W, H, S)

    local L = { S = S, fs = fs }
    L.titleY = 12 * S
    L.messageY = L.titleY + text_height(fs) + 4 * S
    L.listW = math.min(Theme.list_w * S, W - 20 * S)
    L.listX = math.floor((W - L.listW) / 2)
    L.listY = L.messageY + text_height(fs) + 6 * S
    L.listH = row1Y - 10 * S - L.listY
    L.rowH = Theme.row_h * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))

    local has_sel = selected_world() ~= nil
    L.buttons = {
        { x = bx,            y = row1Y, w = bw, h = bh, label = "Play Selected World",
          enabled = has_sel and can_play(), action = play_selected },
        { x = bx + bw + gap, y = row1Y, w = bw, h = bh, label = "Create New World",
          enabled = can_play(), action = open_create },
        { x = bx,            y = row2Y, w = bw, h = bh, label = "Delete",
          enabled = has_sel, action = open_delete },
        { x = bx + bw + gap, y = row2Y, w = bw, h = bh, label = "Back",
          enabled = true, action = function() engine.back_to_title() end },
    }
    return L
end

local function render_list(W, H)
    local L = list_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local worlds = state.worlds

    rect(0, 0, W, H, C.background)

    local title = "Select World"
    if state.game and state.game.title and state.game.title ~= "" then
        title = title .. " - " .. state.game.title
    end
    text_centered(title, W / 2, L.titleY, fs, C.title, S)

    -- Warning line: an error from the last action, or the wrong-game notice
    local warning = state.message
    if not warning and not can_play() then
        warning = "This game isn't loaded. Restart with it selected to play its worlds."
    end
    if warning then
        text_centered(warning, W / 2, L.messageY, fs * 0.85, C.warning, S)
    end

    rect(L.listX, L.listY, L.listW, L.listH, C.list_bg)

    if #worlds == 0 then
        text_centered("No worlds yet - create one!", W / 2,
                      L.listY + L.listH / 2 - text_height(fs) / 2, fs, C.detail, S)
    end

    state.scroll = clamp(state.scroll, 0, math.max(0, #worlds - L.visible))

    for row = 1, L.visible do
        local i = row + state.scroll
        local w = worlds[i]
        if not w then break end

        local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
        local rw, rh = L.listW - 4 * S, L.rowH - 2 * S

        if i == state.selected then
            rect(rx, ry, rw, rh, C.row_selected)
            rect(rx + S, ry + S, rw - 2 * S, rh - 2 * S, C.row_selected_bg)
        elseif inside(mx, my, rx, ry, rw, rh) then
            rect(rx, ry, rw, rh, C.row_hover)
        end

        text(w.name, rx + 6 * S, ry + 4 * S, fs, C.name, true, S)
        text("Seed: " .. tostring(w.seed), rx + 6 * S, ry + 6 * S + text_height(fs), fs * 0.85, C.detail, false, S)
    end

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_list(mx, my, W, H)
    local L = list_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end

    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local row = math.floor((my - L.listY) / L.rowH) + 1
        local i = row + state.scroll
        if state.worlds[i] then
            local now = ui.get_time()
            local double = state.selected == i and state.last_click_row == i
                           and (now - state.last_click_time) < Theme.double_click
            state.selected = i
            state.last_click_row = i
            state.last_click_time = now
            state.message = nil
            if double and can_play() then play_selected() end
        end
    end
end

-- ===========================================================================
-- CREATE WORLD SCREEN
-- ===========================================================================

local function create_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local bx, bw, bh, gap, _, row2Y = bottom_rows(W, H, S)
    local fw, fh = Theme.field_w * S, Theme.field_h * S
    local fx = math.floor((W - fw) / 2)

    local L = { S = S, fs = fs, fx = fx }
    L.titleY = 12 * S
    L.nameLabelY = 50 * S
    L.nameField = { x = fx, y = L.nameLabelY + text_height(fs) + 4 * S, w = fw, h = fh }
    L.seedLabelY = L.nameField.y + fh + 16 * S
    L.seedField = { x = fx, y = L.seedLabelY + text_height(fs) + 4 * S, w = fw, h = fh }
    L.hintY = L.seedField.y + fh + 4 * S

    L.buttons = {
        { x = bx,            y = row2Y, w = bw, h = bh, label = "Create New World",
          enabled = true, action = create_and_play },
        { x = bx + bw + gap, y = row2Y, w = bw, h = bh, label = "Cancel",
          enabled = true, action = function() state.mode = "list" end },
    }
    return L
end

local function render_create(W, H)
    local L = create_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    text_centered("Create New World", W / 2, L.titleY, fs, C.title, S)

    text("World Name", L.fx, L.nameLabelY, fs, C.detail, false, S)
    draw_field(L.nameField, state.name, state.focus == "name", nil, S, fs)

    text("Seed for the World Generator", L.fx, L.seedLabelY, fs, C.detail, false, S)
    draw_field(L.seedField, state.seed, state.focus == "seed", "Random", S, fs)
    text("Leave blank for a random seed", L.fx, L.hintY, fs * 0.85, C.detail, false, S)

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_create(mx, my, W, H)
    local L = create_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end

    local f = L.nameField
    if inside(mx, my, f.x, f.y, f.w, f.h) then state.focus = "name" return end
    f = L.seedField
    if inside(mx, my, f.x, f.y, f.w, f.h) then state.focus = "seed" return end
end

-- ===========================================================================
-- DELETE CONFIRMATION SCREEN
-- ===========================================================================

local function delete_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local bx, bw, bh, gap = bottom_rows(W, H, S)
    local y = math.floor(H / 2 + 10 * S)

    local L = { S = S, fs = fs }
    L.titleY = math.floor(H / 2 - 40 * S)
    L.line1Y = L.titleY + text_height(fs) + 10 * S
    L.line2Y = L.line1Y + text_height(fs) + 4 * S
    L.buttons = {
        { x = bx,            y = y, w = bw, h = bh, label = "Delete",
          enabled = true, danger = true, action = confirm_delete },
        { x = bx + bw + gap, y = y, w = bw, h = bh, label = "Cancel",
          enabled = true, action = function() state.mode = "list" end },
    }
    return L
end

local function render_delete(W, H)
    local L = delete_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local w = selected_world()

    rect(0, 0, W, H, C.background)
    text_centered("Delete this world?", W / 2, L.titleY, fs, C.title, S)
    text_centered("'" .. (w and w.name or "?") .. "' will be permanently deleted.", W / 2, L.line1Y, fs, C.detail, S)
    text_centered("This cannot be undone.", W / 2, L.line2Y, fs, C.warning, S)

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_delete(mx, my, W, H)
    click_buttons(delete_layout(W, H).buttons, mx, my)
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_world_select_opened()
    state.mode = "list"
    state.scroll = 0
    state.message = nil
    refresh_worlds()
end

function render_world_select(W, H)
    if not ui or not ui.draw_rect then return end
    if not state.worlds then refresh_worlds() end

    if state.mode == "create" then render_create(W, H)
    elseif state.mode == "delete" then render_delete(W, H)
    else render_list(W, H) end
end

function handle_world_select_click(mx, my, W, H)
    if state.mode == "create" then click_create(mx, my, W, H)
    elseif state.mode == "delete" then click_delete(mx, my, W, H)
    else click_list(mx, my, W, H) end
    return true
end

function handle_world_select_scroll(dy)
    if state.mode ~= "list" then return end
    if dy > 0 then state.scroll = state.scroll - 1
    elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end   -- upper bound clamped while rendering
end

function handle_world_select_key(key, action, mods)
    local press = action == ui.actions.PRESS or action == ui.actions.REPEAT

    if state.mode == "delete" then
        -- Escape cancels. Enter does nothing here on purpose: deleting needs a click.
        if press and key == ui.keys.ESCAPE then state.mode = "list" end
        return true
    end

    if state.mode == "create" then
        if not press then return true end
        if key == ui.keys.ESCAPE then
            state.mode = "list"
        elseif key == KEY_TAB then
            state.focus = (state.focus == "name") and "seed" or "name"
        elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            create_and_play()
        elseif key == ui.keys.BACKSPACE then
            state[state.focus] = state[state.focus]:sub(1, -2)
        end
        return true
    end

    -- List screen: arrows move the selection, Enter plays, Delete asks to delete,
    -- Escape falls through to C++ (back to the title screen)
    if not press then return false end
    local count = state.worlds and #state.worlds or 0
    if key == ui.keys.UP and count > 0 then
        state.selected = clamp((state.selected or 1) - 1, 1, count)
        return true
    elseif key == ui.keys.DOWN and count > 0 then
        state.selected = clamp((state.selected or 0) + 1, 1, count)
        return true
    elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
        if can_play() then play_selected() end
        return true
    elseif key == ui.keys.DELETE then
        open_delete()
        return true
    end
    return false
end

function handle_world_select_char(codepoint, ch)
    if state.mode ~= "create" or not ch or ch == "" then return false end
    local field = state.focus
    local limit = (field == "name") and Theme.max_name or Theme.max_seed
    if codepoint >= 32 and codepoint < 127 and #state[field] < limit then
        state[field] = state[field] .. ch
    end
    return true
end
