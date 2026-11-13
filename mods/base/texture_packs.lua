-- texture_packs.lua - "Textures" screen from the title screen
-- ---------------------------------------------------------------------------
-- Pick a texture pack. Switching applies immediately - no restart needed.
-- Everything here is editable.
--
-- Making a pack:
--   texturepacks/<Pack Name>/             one folder per pack
--   texturepacks/<Pack Name>/pack.conf    optional: title, author, description
--   Put images in the folder (or a "textures" subfolder) using the SAME file names
--   as the game's textures, e.g. stone.png, grass_top.png, hotbar_bg.png.
--   Any size works: block textures are scaled to 16x16 automatically.
--
-- Engine functions used:
--   engine.list_texture_packs()   -> { { id, title, author, description, path,
--                                        images, replaces, total, active }, ... }
--                                    (first entry is the default textures, id = "")
--   engine.set_texture_pack(id)   -> true, or false + reason
--   engine.open_texture_packs_folder()
--   engine.back_to_title()
--
-- C++ calls:
--   render_texture_packs(W, H)
--   handle_texture_packs_click(mx, my, W, H)
--   handle_texture_packs_scroll(dy)
--   handle_texture_packs_key(key, action, mods)   -> true = handled
--   on_texture_packs_opened()
-- ---------------------------------------------------------------------------

print("[UI] texture_packs.lua loaded")

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
        active          = { 0.40, 0.85, 0.40, 1.00 },
        warning         = { 1.00, 0.85, 0.35, 1.00 },
        error           = { 1.00, 0.45, 0.40, 1.00 },
        outline         = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover   = { 1.00, 1.00, 1.00, 1.00 },
        button          = { 0.44, 0.44, 0.44, 1.00 },
        button_hover    = { 0.50, 0.55, 0.78, 1.00 },
        button_off      = { 0.27, 0.27, 0.27, 1.00 },
        bevel_light     = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark      = { 0.00, 0.00, 0.00, 0.35 },
        text            = { 0.88, 0.88, 0.88, 1.00 },
        text_hover      = { 1.00, 1.00, 0.63, 1.00 },
        text_off        = { 0.63, 0.63, 0.63, 1.00 },
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

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    packs = nil,
    selected = nil,
    scroll = 0,
    message = nil,      -- error from the last switch
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

local function refresh_packs()
    state.packs = (engine and engine.list_texture_packs and engine.list_texture_packs()) or {}
    if state.selected and state.selected > #state.packs then state.selected = nil end
    if not state.selected then
        -- Start with the pack that's in use selected
        for i, p in ipairs(state.packs) do
            if p.active then state.selected = i break end
        end
        if not state.selected and #state.packs > 0 then state.selected = 1 end
    end
end

local function use_pack(p)
    if not p or p.active then return end
    local ok, err = engine.set_texture_pack(p.id)
    state.message = (ok == false) and err or nil
    refresh_packs()
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

    -- "Use This Pack" button at the bottom of the details panel
    L.use = { x = L.infoX + 8 * S, y = L.infoY + L.infoH - bh - 8 * S, w = bw, h = bh }

    local totalW = bw * 2 + gap
    local bx = math.floor((W - totalW) / 2)
    L.buttons = {
        { x = bx,            y = buttonsY, w = bw, h = bh, label = "Open Packs Folder",
          action = function() engine.open_texture_packs_folder() end },
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
    local packs = state.packs

    rect(L.listX, L.listY, L.listW, L.listH, C.panel_bg)
    state.scroll = clamp(state.scroll, 0, math.max(0, #packs - L.visible))

    for row = 1, L.visible do
        local i = row + state.scroll
        local p = packs[i]
        if not p then break end

        local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
        local rw, rh = L.listW - 4 * S, L.rowH - 2 * S

        if i == state.selected then
            rect(rx, ry, rw, rh, C.row_selected)
            rect(rx + S, ry + S, rw - 2 * S, rh - 2 * S, C.row_selected_bg)
        elseif inside(mx, my, rx, ry, rw, rh) then
            rect(rx, ry, rw, rh, C.row_hover)
        end

        -- Green dot on the pack in use
        if p.active then
            local dot = 4 * S
            rect(rx + 5 * S, ry + (rh - dot) / 2, dot, dot, C.active)
        end

        local tx = rx + 13 * S
        local tw = rw - 17 * S
        local block = text_height(fs) + 2 * S + text_height(fs * 0.85)
        local ty = ry + (rh - block) / 2
        text(fit(p.title, tw, fs), tx, ty, fs, C.name, true, S)

        local sub
        if p.active then
            sub = "In use"
        elseif p.id == "" then
            sub = "Built in"
        elseif p.author ~= "" then
            sub = "by " .. p.author
        else
            sub = p.replaces .. " textures"
        end
        text(fit(sub, tw, fs * 0.85), tx, ty + text_height(fs) + 2 * S, fs * 0.85,
             p.active and C.active or C.detail, false, S)
    end
end

local function draw_info(L, mx, my)
    local S, fs = L.S, L.fs
    rect(L.infoX, L.infoY, L.infoW, L.infoH, C.panel_bg)

    local p = state.packs and state.packs[state.selected]
    if not p then return end

    -- Use / In Use button
    local u = L.use
    draw_button({ x = u.x, y = u.y, w = u.w, h = u.h,
                  label = p.active and "In Use" or "Use This Pack", enabled = not p.active },
                mx, my, S, fs)

    local x = L.infoX + 8 * S
    local w = L.infoW - 16 * S
    local y = L.infoY + 8 * S
    local bottom = u.y - 6 * S
    local small = fs * 0.85
    local line_h = text_height(small) + Theme.line_gap * S

    text(fit(p.title, w, fs), x, y, fs, C.title, true, S)
    y = y + text_height(fs) + 10 * S

    local value_x = x + text_width("Replaces:", small) + 12 * S
    local function row(label, value, color)
        if not value or value == "" or y + line_h > bottom then return end
        text(label .. ":", x, y, small, C.label, false, S)
        text(fit(value, x + w - value_x, small), value_x, y, small, color or C.detail, false, S)
        y = y + line_h
    end

    row("Status", p.active and "In use" or "Not in use", p.active and C.active or C.detail)
    row("Author", p.author)
    if p.id ~= "" then
        -- Warn when a pack doesn't match any of the game's texture names
        local color = (p.replaces == 0) and C.warning or C.detail
        row("Replaces", p.replaces .. " of " .. p.total .. " textures", color)
        row("Images", tostring(p.images))
    end
    y = y + 8 * S

    local function paragraph(str, color)
        if not str or str == "" then return end
        for _, line in ipairs(wrap(str, w, small)) do
            if y + line_h > bottom then return end
            text(line, x, y, small, color, false, S)
            y = y + line_h
        end
        y = y + 8 * S
    end

    paragraph(p.description ~= "" and p.description or "No description.", C.text)
    if p.id ~= "" and p.replaces == 0 then
        paragraph("None of this pack's image names match the game's textures, so it won't change anything. " ..
                  "Name images like the originals, e.g. stone.png.", C.warning)
    end
    if p.path ~= "" then paragraph(p.path, C.detail) end
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_texture_packs_opened()
    state.scroll = 0
    state.message = nil
    state.selected = nil
    refresh_packs()
end

function render_texture_packs(W, H)
    if not ui or not ui.draw_rect then return end
    if not state.packs then refresh_packs() end

    local L = layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    text_centered("Texture Packs", W / 2, L.titleY, fs, C.title, S)
    if state.message then
        text_centered(state.message, W / 2, L.noteY, fs * 0.8, C.error, S)
    else
        text_centered("Put pack folders in the texturepacks folder. Images replace game textures with the same name.",
                      W / 2, L.noteY, fs * 0.8, C.detail, S)
    end

    draw_list(L, mx, my)
    draw_info(L, mx, my)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

function handle_texture_packs_click(mx, my, W, H)
    local L = layout(W, H)

    for _, b in ipairs(L.buttons) do
        if inside(mx, my, b.x, b.y, b.w, b.h) then
            b.action()
            return true
        end
    end

    local u = L.use
    if inside(mx, my, u.x, u.y, u.w, u.h) then
        use_pack(state.packs[state.selected])
        return true
    end

    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local row = math.floor((my - L.listY) / L.rowH) + 1
        local i = row + state.scroll
        if state.packs[i] then state.selected = i end
    end
    return true
end

function handle_texture_packs_scroll(dy)
    if dy > 0 then state.scroll = state.scroll - 1
    elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end   -- upper bound clamped while rendering
end

function handle_texture_packs_key(key, action, mods)
    if action ~= ui.actions.PRESS and action ~= ui.actions.REPEAT then return false end
    local count = state.packs and #state.packs or 0
    if key == ui.keys.UP and count > 0 then
        state.selected = clamp((state.selected or 1) - 1, 1, count)
        if state.selected <= state.scroll then state.scroll = state.selected - 1 end
        return true
    elseif key == ui.keys.DOWN and count > 0 then
        state.selected = clamp((state.selected or 0) + 1, 1, count)
        return true
    elseif key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
        use_pack(state.packs[state.selected])
        return true
    end
    return false   -- Escape falls through to C++ (back to the title screen)
end
