-- mods_menu.lua - "Mods" screen from the title screen
-- ---------------------------------------------------------------------------
-- Lists every mod found in the mod folders, with details for the selected one.
-- Everything here is editable.
--
-- A mod is either a folder (optionally with a mod.conf) or a loose .lua file.
-- mod.conf keys shown here:  title (or name), author, description, version, depends
--
-- Engine functions used:
--   engine.list_mods()        -> { { id, title, author, description, version, depends,
--                                    source, path, status, error, lua_files, is_folder }, ... }
--                                status = "loaded" | "error" | "not_loaded"
--   engine.open_mods_folder()
--   engine.back_to_title()
--
-- C++ calls:
--   render_mods(W, H)
--   handle_mods_click(mx, my, W, H)
--   handle_mods_scroll(dy)
--   handle_mods_key(key, action, mods)   -> true = handled
--   on_mods_opened()
-- ---------------------------------------------------------------------------

print("[UI] mods_menu.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local Theme = {
    colors = {
        background      = { 0.10, 0.10, 0.10, 1.00 },
        panel_bg        = { 0.00, 0.00, 0.00, 0.45 },
        row_hover       = { 1.00, 1.00, 1.00, 0.08 },
        row_selected    = { 1.00, 1.00, 1.00, 1.00 },
        row_selected_bg = { 0.00, 0.00, 0.00, 1.00 },
        title           = { 1.00, 1.00, 1.00, 1.00 },
        name            = { 1.00, 1.00, 1.00, 1.00 },
        detail          = { 0.63, 0.63, 0.63, 1.00 },
        label           = { 0.75, 0.75, 0.75, 1.00 },
        loaded          = { 0.40, 0.85, 0.40, 1.00 },
        error           = { 1.00, 0.45, 0.40, 1.00 },
        not_loaded      = { 0.60, 0.60, 0.60, 1.00 },
        disabled        = { 0.45, 0.45, 0.45, 1.00 },
        warning         = { 1.00, 0.85, 0.35, 1.00 },   -- "restart to apply" notice
        check_box       = { 0.22, 0.22, 0.22, 1.00 },
        check_on        = { 0.40, 0.85, 0.40, 1.00 },
        check_locked    = { 0.55, 0.55, 0.55, 1.00 },
        button_off      = { 0.27, 0.27, 0.27, 1.00 },
        text_off        = { 0.63, 0.63, 0.63, 1.00 },
        outline         = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover   = { 1.00, 1.00, 1.00, 1.00 },
        button          = { 0.44, 0.44, 0.44, 1.00 },
        button_hover    = { 0.50, 0.55, 0.78, 1.00 },
        bevel_light     = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark      = { 0.00, 0.00, 0.00, 0.35 },
        text            = { 0.88, 0.88, 0.88, 1.00 },
        text_hover      = { 1.00, 1.00, 0.63, 1.00 },
    },

    -- Font metrics at scale 1.0 (PixeloidSans is about 20px tall at scale 1)
    font = { char_w = 13, char_h = 20, base_scale = 0.5, y_offset = 0.75 },
    line_gap = 4,               -- extra space between lines, in GUI pixels

    -- Sizes in GUI pixels (multiplied by the GUI scale)
    row_h = 30,
    button_w = 150, button_h = 20, button_gap = 4,
    list_fraction = 0.40,       -- share of the width used by the list
}

local C = Theme.colors

local STATUS_TEXT = {
    loaded     = "Loaded",
    error      = "Failed to load",
    not_loaded = "Not loaded",
    disabled   = "Disabled",
}

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    mods = nil,
    selected = nil,
    scroll = 0,
    message = nil,      -- error from the last on/off click
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

-- Cuts a string to fit `width` pixels, adding "..." if needed
local function fit(str, width, scale)
    local max_chars = math.floor(width / (Theme.font.char_w * scale))
    if #str <= max_chars then return str end
    if max_chars <= 3 then return str:sub(1, math.max(0, max_chars)) end
    return str:sub(1, max_chars - 3) .. "..."
end

-- Splits text into lines that fit `width` pixels (breaks long words too)
local function wrap(str, width, scale)
    local max_chars = math.max(1, math.floor(width / (Theme.font.char_w * scale)))
    local lines = {}
    for paragraph in (str .. "\n"):gmatch("(.-)\n") do
        local line = ""
        for found in paragraph:gmatch("%S+") do
            local word = found   -- loop variables are read-only in newer Lua, so work on a copy
            while #word > max_chars do
                if line ~= "" then lines[#lines + 1] = line; line = "" end
                lines[#lines + 1] = word:sub(1, max_chars)
                word = word:sub(max_chars + 1)
            end
            if line == "" then
                line = word
            elseif #line + 1 + #word <= max_chars then
                line = line .. " " .. word
            else
                lines[#lines + 1] = line
                line = word
            end
        end
        lines[#lines + 1] = line
    end
    return lines
end

local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

local function refresh_mods()
    state.mods = (engine and engine.list_mods and engine.list_mods()) or {}
    if state.selected and state.selected > #state.mods then state.selected = nil end
    if not state.selected and #state.mods > 0 then state.selected = 1 end
end

-- Switches a mod on/off (saved to mods/mod_settings.txt, applied on restart)
local function toggle_mod(m)
    if not m or m.required then return end
    local ok, err = engine.set_mod_enabled(m.id, not m.enabled)
    state.message = (ok == false) and err or nil
    refresh_mods()
end

local function any_restart_needed()
    for _, m in ipairs(state.mods or {}) do
        if m.restart_needed then return true end
    end
    return false
end

local function draw_button(b, mx, my, S, fs)
    local enabled = b.enabled ~= false
    local hovered = enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    rect(b.x, b.y, b.w, b.h, hovered and C.outline_hover or C.outline)
    rect(b.x + S, b.y + S, b.w - 2 * S, b.h - 2 * S,
         (not enabled) and C.button_off or (hovered and C.button_hover or C.button))
    if enabled then
        rect(b.x + S, b.y + S, b.w - 2 * S, S, C.bevel_light)
        rect(b.x + S, b.y + 2 * S, S, b.h - 3 * S, C.bevel_light)
        rect(b.x + S, b.y + b.h - 2 * S, b.w - 2 * S, S, C.bevel_dark)
    end
    text_centered(b.label, b.x + b.w / 2, b.y + (b.h - text_height(fs)) / 2, fs,
                  (not enabled) and C.text_off or (hovered and C.text_hover or C.text), S)
end

-- Small checkbox: filled green when on, grey when required (always on)
local function draw_checkbox(x, y, size, on, locked, S)
    rect(x, y, size, size, C.outline)
    rect(x + S, y + S, size - 2 * S, size - 2 * S, C.check_box)
    if on then
        local inset = 3 * S
        rect(x + inset, y + inset, size - 2 * inset, size - 2 * inset, locked and C.check_locked or C.check_on)
    end
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

    local margin = 10 * S
    local top = L.noteY + text_height(fs * 0.8) + 8 * S
    local buttonsY = H - margin - bh
    local panelH = buttonsY - 8 * S - top
    local innerW = W - margin * 2

    L.listX, L.listY = margin, top
    L.listW = math.floor(innerW * Theme.list_fraction)
    L.listH = panelH
    L.infoX = L.listX + L.listW + 6 * S
    L.infoY = top
    L.infoW = W - margin - L.infoX
    L.infoH = panelH

    L.rowH = Theme.row_h * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))
    L.check = 12 * S   -- checkbox size in the list

    -- Enable/Disable button at the bottom of the details panel
    L.toggle = { x = L.infoX + 8 * S, y = L.infoY + L.infoH - bh - 8 * S, w = bw, h = bh }

    local totalW = bw * 2 + gap
    local bx = math.floor((W - totalW) / 2)
    L.buttons = {
        { x = bx,            y = buttonsY, w = bw, h = bh, label = "Open Mods Folder",
          action = function() engine.open_mods_folder() end },
        { x = bx + bw + gap, y = buttonsY, w = bw, h = bh, label = "Done",
          action = function() engine.back_to_title() end },
    }
    return L
end

-- ===========================================================================
-- DRAWING
-- ===========================================================================

local function draw_list(L, mx, my)
    local S, fs = L.S, L.fs
    local mods = state.mods

    rect(L.listX, L.listY, L.listW, L.listH, C.panel_bg)

    if #mods == 0 then
        text_centered("No mods found", L.listX + L.listW / 2,
                      L.listY + L.listH / 2 - text_height(fs) / 2, fs, C.detail, S)
        return
    end

    state.scroll = clamp(state.scroll, 0, math.max(0, #mods - L.visible))

    for row = 1, L.visible do
        local i = row + state.scroll
        local m = mods[i]
        if not m then break end

        local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
        local rw, rh = L.listW - 4 * S, L.rowH - 2 * S

        if i == state.selected then
            rect(rx, ry, rw, rh, C.row_selected)
            rect(rx + S, ry + S, rw - 2 * S, rh - 2 * S, C.row_selected_bg)
        elseif inside(mx, my, rx, ry, rw, rh) then
            rect(rx, ry, rw, rh, C.row_hover)
        end

        -- Status dot
        local dot = 4 * S
        rect(rx + 5 * S, ry + (rh - dot) / 2, dot, dot, C[m.status] or C.not_loaded)

        -- On/off checkbox on the right
        local cx, cy = rx + rw - L.check - 5 * S, ry + (rh - L.check) / 2
        draw_checkbox(cx, cy, L.check, m.enabled, m.required, S)

        local tx = rx + 13 * S
        local tw = rw - 17 * S - L.check - 6 * S
        -- Two lines (name + author), centred vertically in the row
        local block = text_height(fs) + 2 * S + text_height(fs * 0.85)
        local ty = ry + (rh - block) / 2
        text(fit(m.title, tw, fs), tx, ty, fs, m.enabled and C.name or C.detail, true, S)
        local sub = (m.author ~= "" and ("by " .. m.author) or m.source)
        text(fit(sub, tw, fs * 0.85), tx, ty + text_height(fs) + 2 * S, fs * 0.85, C.detail, false, S)
    end
end

local function draw_info(L, mx, my)
    local S, fs = L.S, L.fs
    rect(L.infoX, L.infoY, L.infoW, L.infoH, C.panel_bg)

    local m = state.mods and state.mods[state.selected]
    if not m then return end

    -- Enable / Disable button
    local t = L.toggle
    local label = m.required and "Required" or (m.enabled and "Disable" or "Enable")
    draw_button({ x = t.x, y = t.y, w = t.w, h = t.h, label = label, enabled = not m.required },
                mx, my, S, fs)

    local x = L.infoX + 8 * S
    local w = L.infoW - 16 * S
    local y = L.infoY + 8 * S
    local bottom = t.y - 6 * S
    local small = fs * 0.85
    local line_h = text_height(small) + Theme.line_gap * S

    -- Title + version
    local title = m.title
    if m.version ~= "" then title = title .. "  v" .. m.version end
    text(fit(title, w, fs), x, y, fs, C.title, true, S)
    y = y + text_height(fs) + 10 * S

    -- Key / value rows. Values all start in the same column, after the longest label.
    local value_x = x + text_width("Depends on:", small) + 12 * S
    local function row(label, value, color)
        if not value or value == "" or y + line_h > bottom then return end
        text(label .. ":", x, y, small, C.label, false, S)
        text(fit(value, x + w - value_x, small), value_x, y, small, color or C.detail, false, S)
        y = y + line_h
    end

    row("Status", STATUS_TEXT[m.status] or m.status, C[m.status])
    if m.required then
        row("Enabled", "Always (required)", C.check_locked)
    elseif m.restart_needed then
        row("Enabled", (m.enabled and "On" or "Off") .. " - restart to apply", C.warning)
    else
        row("Enabled", m.enabled and "On" or "Off", m.enabled and C.loaded or C.disabled)
    end
    row("Author", m.author)
    row("Source", m.source)
    row("Files", tostring(m.lua_files) .. (m.lua_files == 1 and " Lua file" or " Lua files"))
    row("Depends on", m.depends)
    y = y + 8 * S

    -- Wrapped paragraphs
    local function paragraph(str, color)
        if not str or str == "" then return end
        for _, line in ipairs(wrap(str, w, small)) do
            if y + line_h > bottom then return end
            text(line, x, y, small, color, false, S)
            y = y + line_h
        end
        y = y + 8 * S   -- gap between paragraphs
    end

    paragraph(m.description ~= "" and m.description or "No description.", C.text)
    if m.status == "error" then paragraph("Error: " .. m.error, C.error) end
    paragraph(m.path, C.detail)
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_mods_opened()
    state.scroll = 0
    state.message = nil
    refresh_mods()
end

function render_mods(W, H)
    if not ui or not ui.draw_rect then return end
    if not state.mods then refresh_mods() end

    local L = layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    text_centered("Mods (" .. #state.mods .. ")", W / 2, L.titleY, fs, C.title, S)

    if state.message then
        text_centered(state.message, W / 2, L.noteY, fs * 0.8, C.error, S)
    elseif any_restart_needed() then
        text_centered("Restart the game to apply your changes.", W / 2, L.noteY, fs * 0.8, C.warning, S)
    else
        text_centered("Mods load when the game starts - restart after adding or removing mods.",
                      W / 2, L.noteY, fs * 0.8, C.detail, S)
    end

    draw_list(L, mx, my)
    draw_info(L, mx, my)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

function handle_mods_click(mx, my, W, H)
    local L = layout(W, H)

    for _, b in ipairs(L.buttons) do
        if inside(mx, my, b.x, b.y, b.w, b.h) then
            b.action()
            return true
        end
    end

    -- Enable / Disable button in the details panel
    local t = L.toggle
    if inside(mx, my, t.x, t.y, t.w, t.h) then
        toggle_mod(state.mods[state.selected])
        return true
    end

    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local row = math.floor((my - L.listY) / L.rowH) + 1
        local i = row + state.scroll
        local m = state.mods[i]
        if m then
            state.selected = i
            -- Clicking the checkbox also switches the mod on/off
            local S = L.S
            local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
            local rw, rh = L.listW - 4 * S, L.rowH - 2 * S
            local cx, cy = rx + rw - L.check - 5 * S, ry + (rh - L.check) / 2
            if inside(mx, my, cx - 2 * S, cy - 2 * S, L.check + 4 * S, L.check + 4 * S) then
                toggle_mod(m)
            end
        end
    end
    return true
end

function handle_mods_scroll(dy)
    if dy > 0 then state.scroll = state.scroll - 1
    elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end   -- upper bound clamped while rendering
end

function handle_mods_key(key, action, mods)
    if action ~= ui.actions.PRESS and action ~= ui.actions.REPEAT then return false end
    local count = state.mods and #state.mods or 0
    if key == ui.keys.UP and count > 0 then
        state.selected = clamp((state.selected or 1) - 1, 1, count)
        if state.selected <= state.scroll then state.scroll = state.selected - 1 end
        return true
    elseif key == ui.keys.DOWN and count > 0 then
        state.selected = clamp((state.selected or 0) + 1, 1, count)
        return true
    end
    return false   -- Escape falls through to C++ (back to the title screen)
end
