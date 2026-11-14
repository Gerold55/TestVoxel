-- settings_menu.lua - Settings screen (title screen "Settings", or pause menu "Options...")
-- ---------------------------------------------------------------------------
-- Three tabs:
--   Controls - click a control, then press the new key. Escape cancels.
--              Keys used by more than one control are shown in red.
--   Video    - Field of View, Render Distance, VSync (applied immediately).
--   Player   - your name (used in multiplayer and chat). Click the box, type, press Enter.
--   Server   - what other players see in their server list when you host: the description
--              (MOTD), max players and port.
-- Everything is saved to settings.txt automatically. Everything here is editable.
--
-- Engine functions used:
--   engine.get_keybinds()              -> { { action, label, key, key_name, default_key }, ... }
--   engine.set_keybind(action, key)    -> true, or false + reason
--   engine.reset_keybinds()
--   engine.get_video_settings()        -> { { id, value, min, max, step }, ... }
--   engine.set_video_setting(id, v)    -> true, or false + reason
--   engine.get_player_name()           -> "Steve"
--   engine.set_player_name(name)       -> true, or false + reason
--   engine.get_server_settings()       -> { motd, max_players, port }
--   engine.set_server_setting(id, v)   -> true, or false + reason   (id: motd, max_players, port)
--   engine.close_settings()
--   engine.is_in_game()
--
-- C++ calls:
--   render_settings(W, H)
--   handle_settings_click(mx, my, W, H)
--   handle_settings_scroll(dy)
--   handle_settings_key(key, action, mods)   -> true = handled
--   handle_settings_char(codepoint, char)    -> true = handled
--   on_settings_opened()
-- ---------------------------------------------------------------------------

print("[UI] settings_menu.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local Theme = {
    colors = {
        background      = { 0.10, 0.10, 0.10, 1.00 },
        background_game = { 0.00, 0.00, 0.00, 0.65 },   -- over the world when opened in-game
        panel_bg        = { 0.00, 0.00, 0.00, 0.45 },
        row_hover       = { 1.00, 1.00, 1.00, 0.06 },
        title           = { 1.00, 1.00, 1.00, 1.00 },
        name            = { 1.00, 1.00, 1.00, 1.00 },
        detail          = { 0.63, 0.63, 0.63, 1.00 },
        conflict        = { 1.00, 0.40, 0.35, 1.00 },
        waiting         = { 1.00, 0.85, 0.35, 1.00 },
        error           = { 1.00, 0.45, 0.40, 1.00 },
        outline         = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover   = { 1.00, 1.00, 1.00, 1.00 },
        button          = { 0.44, 0.44, 0.44, 1.00 },
        button_hover    = { 0.50, 0.55, 0.78, 1.00 },
        button_off      = { 0.27, 0.27, 0.27, 1.00 },
        tab_active      = { 0.55, 0.55, 0.55, 1.00 },
        bevel_light     = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark      = { 0.00, 0.00, 0.00, 0.35 },
        text            = { 0.88, 0.88, 0.88, 1.00 },
        text_hover      = { 1.00, 1.00, 0.63, 1.00 },
        text_off        = { 0.63, 0.63, 0.63, 1.00 },
        field_bg        = { 0.00, 0.00, 0.00, 1.00 },
        field_border    = { 0.63, 0.63, 0.63, 1.00 },
        field_focus     = { 1.00, 1.00, 1.00, 1.00 },
    },

    -- Font metrics at scale 1.0 (PixeloidSans is about 20px tall at scale 1)
    font = { char_w = 13, char_h = 20, base_scale = 0.5, y_offset = 0.75 },
    line_gap = 4,

    -- Sizes in GUI pixels (multiplied by the GUI scale)
    panel_w = 320,
    row_h = 24,
    key_w = 110,              -- width of the key button on each Controls row
    small_w = 22,             -- the - / + buttons on the Video tab
    button_w = 150, button_h = 20, button_gap = 4,
    tab_w = 80,
    name_field_w = 150,       -- text boxes (player name, port)
    wide_field_w = 200,       -- the server description box
}

local C = Theme.colors

local VIDEO_LABELS = {
    fov             = "Field of View",
    render_distance = "Render Distance",
    vsync           = "VSync",
}

local function video_value_text(s)
    if s.id == "vsync" then return (s.value ~= 0) and "On" or "Off" end
    if s.id == "render_distance" then return s.value .. " chunks" end
    return tostring(s.value)
end

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    tab = "controls",     -- "controls", "video", "player" or "server"
    binds = {},
    video = {},
    player = {},
    server = {},
    editing = nil,        -- the text item being typed in (nil = none)
    edit_text = "",       -- what's in the box while typing
    scroll = 0,
    waiting = nil,        -- action waiting for a key press
    message = nil,
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

local function refresh()
    state.binds = (engine and engine.get_keybinds and engine.get_keybinds()) or {}
    state.video = (engine and engine.get_video_settings and engine.get_video_settings()) or {}
    -- Text items: kind = "text", value, max (length), allowed (pattern for one character)
    state.player = {
        { id = "name", kind = "text", label = "Player Name", max = 16, allowed = "^[%w_%-]$",
          value = (engine and engine.get_player_name and engine.get_player_name()) or "" },
    }
    local sv = (engine and engine.get_server_settings and engine.get_server_settings()) or {}
    state.server = {
        { id = "motd", kind = "text", label = "Description", max = 120, allowed = "^[ -~]$", wide = true,
          value = sv.motd or "" },
        { id = "max_players", kind = "number", label = "Max Players",
          value = sv.max_players or 20, min = 2, max = 200, step = 1 },
        { id = "port", kind = "text", label = "Port", max = 5, allowed = "^%d$",
          value = tostring(sv.port or 30000) },
    }
end

-- How many controls use each key (to highlight duplicates)
local function key_counts()
    local counts = {}
    for _, b in ipairs(state.binds) do
        counts[b.key] = (counts[b.key] or 0) + 1
    end
    return counts
end

local function draw_button(b, mx, my, S, fs, label_color)
    local enabled = b.enabled ~= false
    local hovered = enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local fill = (not enabled) and C.button_off or (b.selected and C.tab_active)
                 or (hovered and C.button_hover or C.button)
    rect(b.x, b.y, b.w, b.h, hovered and C.outline_hover or C.outline)
    rect(b.x + S, b.y + S, b.w - 2 * S, b.h - 2 * S, fill)
    if enabled then
        rect(b.x + S, b.y + S, b.w - 2 * S, S, C.bevel_light)
        rect(b.x + S, b.y + 2 * S, S, b.h - 3 * S, C.bevel_light)
        rect(b.x + S, b.y + b.h - 2 * S, b.w - 2 * S, S, C.bevel_dark)
    end
    local tc = label_color or ((not enabled) and C.text_off or (hovered and C.text_hover or C.text))
    text_centered(b.label, b.x + b.w / 2, b.y + (b.h - text_height(fs)) / 2, fs, tc, S)
end

-- ===========================================================================
-- LAYOUT
-- ===========================================================================

local function layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local bw, bh, gap = Theme.button_w * S, Theme.button_h * S, Theme.button_gap * S

    local L = { S = S, fs = fs }
    L.titleY = 8 * S
    L.noteY = L.titleY + text_height(fs) + Theme.line_gap * S

    -- Tabs
    local tw = Theme.tab_w * S
    local tabsY = L.noteY + text_height(fs * 0.8) + 6 * S
    local tx = math.floor((W - (tw * 4 + gap * 3)) / 2)
    L.tabs = {
        { id = "controls", label = "Controls", x = tx,                  y = tabsY, w = tw, h = bh },
        { id = "video",    label = "Video",    x = tx + tw + gap,       y = tabsY, w = tw, h = bh },
        { id = "player",   label = "Player",   x = tx + (tw + gap) * 2, y = tabsY, w = tw, h = bh },
        { id = "server",   label = "Server",   x = tx + (tw + gap) * 3, y = tabsY, w = tw, h = bh },
    }

    -- Panel with the rows
    local margin = 10 * S
    local buttonsY = H - margin - bh
    L.panelW = math.min(Theme.panel_w * S, W - 2 * margin)
    L.panelX = math.floor((W - L.panelW) / 2)
    L.panelY = tabsY + bh + 6 * S
    L.panelH = buttonsY - 8 * S - L.panelY
    L.rowH = Theme.row_h * S
    L.visible = math.max(1, math.floor((L.panelH - 4 * S) / L.rowH))

    -- Bottom buttons
    local totalW = bw * 2 + gap
    local bx = math.floor((W - totalW) / 2)
    L.buttons = {
        { x = bx,            y = buttonsY, w = bw, h = bh, label = "Reset Keys",
          enabled = state.tab == "controls",
          action = function()
              engine.reset_keybinds()
              state.waiting = nil
              refresh()
          end },
        { x = bx + bw + gap, y = buttonsY, w = bw, h = bh, label = "Done",
          action = function()
              state.waiting = nil
              engine.close_settings()
          end },
    }
    return L
end

-- Position of row i (1-based, after scrolling) inside the panel
local function row_rect(L, row)
    local S = L.S
    return L.panelX + 4 * S, L.panelY + 2 * S + (row - 1) * L.rowH, L.panelW - 8 * S, L.rowH - 2 * S
end

-- Buttons for one row on the current tab
local function row_widgets(L, item, rx, ry, rw, rh)
    local S = L.S
    local bh = Theme.button_h * S
    local by = ry + (rh - bh) / 2
    if state.tab == "controls" then
        local kw = Theme.key_w * S
        return { key = { x = rx + rw - kw, y = by, w = kw, h = bh } }
    end
    if item.kind == "text" then
        local fw = (item.wide and Theme.wide_field_w or Theme.name_field_w) * S
        fw = math.min(fw, rw * 0.65)
        return { field = { x = rx + rw - fw, y = by, w = fw, h = bh } }
    end
    if item.id == "vsync" then
        local kw = Theme.key_w * S
        return { toggle = { x = rx + rw - kw, y = by, w = kw, h = bh } }
    end
    local sw = Theme.small_w * S
    return {
        plus  = { x = rx + rw - sw,              y = by, w = sw, h = bh },
        minus = { x = rx + rw - sw * 2 - 4 * S,  y = by, w = sw, h = bh },
    }
end

local function current_items()
    if state.tab == "controls" then return state.binds end
    if state.tab == "player" then return state.player end
    if state.tab == "server" then return state.server end
    return state.video
end

-- Saves a setting (text box, +/- or toggle) through the right engine function.
local function set_value(item, value)
    if item.id == "name" then return engine.set_player_name(value) end
    if state.tab == "server" then return engine.set_server_setting(item.id, tostring(value)) end
    return engine.set_video_setting(item.id, value)
end

-- Saves what's typed. Keeps the box open (with the error shown) if it's not allowed.
local function commit_edit()
    if not state.editing then return end
    local ok, err = set_value(state.editing, state.edit_text)
    if ok == false then
        state.message = err
        return
    end
    state.editing = nil
    state.message = nil
    refresh()
end

local function cancel_edit()
    state.editing = nil
    state.message = nil
end

local function draw_text_field(f, item, S, fs)
    local focused = state.editing ~= nil and state.editing.id == item.id
    rect(f.x, f.y, f.w, f.h, focused and C.field_focus or C.field_border)
    rect(f.x + S, f.y + S, f.w - 2 * S, f.h - 2 * S, C.field_bg)
    local value = focused and state.edit_text or tostring(item.value)
    -- Show the end of long text (where you're typing)
    local max_chars = math.max(1, math.floor((f.w - 8 * S) / (Theme.font.char_w * fs)) - 1)
    if #value > max_chars then value = value:sub(#value - max_chars + 1) end
    local blink = focused and (ui.get_time() % 1.0) < 0.5
    text(value .. (blink and "_" or ""), f.x + 4 * S, f.y + (f.h - text_height(fs)) / 2, fs,
         focused and C.name or C.text, false, S)
end

-- ===========================================================================
-- DRAWING
-- ===========================================================================

local function draw_rows(L, mx, my)
    local S, fs = L.S, L.fs
    local items = current_items()
    local counts = key_counts()

    rect(L.panelX, L.panelY, L.panelW, L.panelH, C.panel_bg)
    state.scroll = clamp(state.scroll, 0, math.max(0, #items - L.visible))

    for row = 1, L.visible do
        local item = items[row + state.scroll]
        if not item then break end

        local rx, ry, rw, rh = row_rect(L, row)
        if inside(mx, my, rx, ry, rw, rh) then rect(rx, ry, rw, rh, C.row_hover) end

        local label = item.label or VIDEO_LABELS[item.id] or item.id
        text(label, rx + 4 * S, ry + (rh - text_height(fs)) / 2, fs, C.name, true, S)

        local wdg = row_widgets(L, item, rx, ry, rw, rh)
        if wdg.field then
            draw_text_field(wdg.field, item, S, fs)
        elseif wdg.key then
            local waiting = state.waiting == item.action
            local k = wdg.key
            local color = nil
            if waiting then
                color = C.waiting
            elseif (counts[item.key] or 0) > 1 then
                color = C.conflict
            end
            draw_button({ x = k.x, y = k.y, w = k.w, h = k.h,
                          label = waiting and "> press a key <" or item.key_name }, mx, my, S, fs, color)
        elseif wdg.toggle then
            local t = wdg.toggle
            draw_button({ x = t.x, y = t.y, w = t.w, h = t.h, label = video_value_text(item) }, mx, my, S, fs)
        else
            local m, p = wdg.minus, wdg.plus
            draw_button({ x = m.x, y = m.y, w = m.w, h = m.h, label = "-", enabled = item.value > item.min }, mx, my, S, fs)
            draw_button({ x = p.x, y = p.y, w = p.w, h = p.h, label = "+", enabled = item.value < item.max }, mx, my, S, fs)
            local value = video_value_text(item)
            text(value, m.x - 8 * S - text_width(value, fs), ry + (rh - text_height(fs)) / 2, fs, C.detail, false, S)
        end
    end
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_settings_opened()
    state.scroll = 0
    state.waiting = nil
    state.message = nil
    state.editing = nil
    refresh()
end

function render_settings(W, H)
    if not ui or not ui.draw_rect then return end
    if #state.binds == 0 then refresh() end

    local L = layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()

    local in_game = engine.is_in_game and engine.is_in_game()
    rect(0, 0, W, H, in_game and C.background_game or C.background)

    text_centered("Settings", W / 2, L.titleY, fs, C.title, S)
    local note, note_color = "Changes are saved automatically.", C.detail
    if state.message then
        note, note_color = state.message, C.error
    elseif state.waiting then
        note, note_color = "Press a key for this control (Escape to cancel).", C.waiting
    elseif state.editing then
        note, note_color = "Type, then press Enter (Escape to cancel).", C.waiting
    elseif state.tab == "controls" then
        note = "Click a control, then press a key. Red keys are used more than once."
    elseif state.tab == "player" then
        note = "Your name in multiplayer and chat: 1-16 letters, numbers, _ or -"
    elseif state.tab == "server" then
        note = "Shown in other players' server lists when you host (Esc > Host Game)."
    end
    text_centered(note, W / 2, L.noteY, fs * 0.8, note_color, S)

    for _, t in ipairs(L.tabs) do
        draw_button({ x = t.x, y = t.y, w = t.w, h = t.h, label = t.label, selected = (state.tab == t.id) },
                    mx, my, S, fs)
    end

    draw_rows(L, mx, my)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

function handle_settings_click(mx, my, W, H)
    local L = layout(W, H)

    -- Clicking anywhere except the box being typed in saves what was typed
    if state.editing then
        local items = current_items()
        for row = 1, L.visible do
            local item = items[row + state.scroll]
            if item and item.id == state.editing.id then
                local rx, ry, rw, rh = row_rect(L, row)
                local f = row_widgets(L, item, rx, ry, rw, rh).field
                if f and inside(mx, my, f.x, f.y, f.w, f.h) then return true end
            end
        end
        commit_edit()
        if state.editing then return true end   -- not allowed: stay in the box
    end
    state.message = nil

    for _, t in ipairs(L.tabs) do
        if inside(mx, my, t.x, t.y, t.w, t.h) then
            state.tab = t.id
            state.scroll = 0
            state.waiting = nil
            return true
        end
    end

    for _, b in ipairs(L.buttons) do
        if b.enabled ~= false and inside(mx, my, b.x, b.y, b.w, b.h) then
            b.action()
            return true
        end
    end

    local items = current_items()
    for row = 1, L.visible do
        local item = items[row + state.scroll]
        if not item then break end
        local rx, ry, rw, rh = row_rect(L, row)
        local wdg = row_widgets(L, item, rx, ry, rw, rh)

        if wdg.field and inside(mx, my, wdg.field.x, wdg.field.y, wdg.field.w, wdg.field.h) then
            state.editing = item
            state.edit_text = tostring(item.value)
            state.waiting = nil
            return true
        elseif wdg.key and inside(mx, my, wdg.key.x, wdg.key.y, wdg.key.w, wdg.key.h) then
            -- Click again to cancel
            state.waiting = (state.waiting == item.action) and nil or item.action
            return true
        elseif wdg.toggle and inside(mx, my, wdg.toggle.x, wdg.toggle.y, wdg.toggle.w, wdg.toggle.h) then
            local ok, err = set_value(item, (item.value ~= 0) and 0 or 1)
            if ok == false then state.message = err end
            refresh()
            return true
        elseif wdg.minus and inside(mx, my, wdg.minus.x, wdg.minus.y, wdg.minus.w, wdg.minus.h) then
            local ok, err = set_value(item, item.value - item.step)
            if ok == false then state.message = err end
            refresh()
            return true
        elseif wdg.plus and inside(mx, my, wdg.plus.x, wdg.plus.y, wdg.plus.w, wdg.plus.h) then
            local ok, err = set_value(item, item.value + item.step)
            if ok == false then state.message = err end
            refresh()
            return true
        end
    end

    state.waiting = nil   -- clicked somewhere else: stop waiting for a key
    return true
end

function handle_settings_scroll(dy)
    if dy > 0 then state.scroll = state.scroll - 1
    elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end   -- upper bound clamped while rendering
end

function handle_settings_key(key, action, mods)
    -- Typing a name
    if state.editing then
        if action ~= ui.actions.PRESS and action ~= ui.actions.REPEAT then return true end
        if key == ui.keys.ESCAPE then
            cancel_edit()
        elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            commit_edit()
        elseif key == ui.keys.BACKSPACE then
            state.edit_text = state.edit_text:sub(1, -2)
        end
        return true
    end

    -- Capturing a key for a control
    if state.waiting then
        if action ~= ui.actions.PRESS then return true end
        if key == ui.keys.ESCAPE then
            state.waiting = nil            -- cancel, stay in settings
            return true
        end
        local ok, err = engine.set_keybind(state.waiting, key)
        state.message = (ok == false) and err or nil
        state.waiting = nil
        refresh()
        return true
    end
    return false   -- Escape falls through to C++ (closes settings)
end

function handle_settings_char(codepoint, ch)
    local item = state.editing
    if not item or not ch or ch == "" then return false end
    if #state.edit_text < (item.max or 32) and ch:match(item.allowed or "^[ -~]$") then
        state.edit_text = state.edit_text .. ch
    end
    return true
end
