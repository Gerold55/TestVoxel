-- pause_menu.lua — Pause Menu
-- ---------------------------------------------------------------------------
-- Opened with Escape while playing. Everything here is editable.
--
-- Uses the same ui.* drawing functions as inventory.lua, plus:
--   engine.resume()        close the menu and go back to the game
--   engine.quit_to_menu()  return to the title screen
--   engine.quit()          close the game
--   engine.is_paused()
--
-- C++ calls:
--   render_pause_menu(width, height)
--   handle_pause_click(mx, my, width, height)
-- ---------------------------------------------------------------------------

print("[UI] pause_menu.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local Theme = {
    colors = {
        dim            = { 0.00, 0.00, 0.00, 0.60 },
        title          = { 1.00, 1.00, 1.00, 1.00 },
        outline        = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover  = { 1.00, 1.00, 1.00, 1.00 },
        button         = { 0.44, 0.44, 0.44, 1.00 },
        button_hover   = { 0.50, 0.55, 0.78, 1.00 },
        button_off     = { 0.27, 0.27, 0.27, 1.00 },
        bevel_light    = { 1.00, 1.00, 1.00, 0.30 },
        bevel_dark     = { 0.00, 0.00, 0.00, 0.35 },
        text           = { 0.88, 0.88, 0.88, 1.00 },
        text_hover     = { 1.00, 1.00, 0.63, 1.00 },
        text_off       = { 0.63, 0.63, 0.63, 1.00 },
    },

    -- Keep these in sync with inventory.lua so text looks the same everywhere
    font = { char_w = 13, char_h = 14, base_scale = 0.5, y_offset = 0.8 },

    -- Sizes in GUI pixels (multiplied by the GUI scale)
    button_w   = 200,
    button_h   = 20,
    button_gap = 4,
    title_gap  = 24,
}

-- ===========================================================================
-- BUTTONS — add, remove or reorder freely
--   label    : text on the button
--   action   : function called when clicked
--   disabled : greyed out and not clickable
--   { gap = n } adds n GUI pixels of empty space
-- ===========================================================================

local Title = "Game Menu"

local Buttons = {
    { label = "Back to Game",  action = function() engine.resume() end },
    { gap = 12 },
    { label = "Options...",    disabled = true },
    { gap = 12 },
    { label = "Quit to Title", action = function() engine.quit_to_menu() end },
    { label = "Quit Game",     action = function() engine.quit() end },
}

-- ===========================================================================
-- HELPERS
-- ===========================================================================

local C = Theme.colors

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
local function text(str, x, y, scale, c, S)
    y = y + Theme.font.char_h * scale * Theme.font.y_offset
    ui.draw_text(str, x + S, y + S, scale, c[1] * 0.25, c[2] * 0.25, c[3] * 0.25, c[4] or 1)  -- shadow
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end

local function text_centered(str, cx, y, scale, c, S)
    text(str, cx - text_width(str, scale) / 2, y, scale, c, S)
end

-- ===========================================================================
-- LAYOUT — shared by drawing and clicking
-- ===========================================================================

local function compute_layout(W, H)
    local S = math.min(W / 480, H / 360)
    S = clamp(math.floor(S * 2) / 2, 1, 4)

    local fs = Theme.font.base_scale * S
    local bw, bh = Theme.button_w * S, Theme.button_h * S

    -- Total height of title + buttons, so the whole block is centred
    local total = text_height(fs) + Theme.title_gap * S
    for i, b in ipairs(Buttons) do
        if b.gap then
            total = total + b.gap * S
        else
            total = total + bh
            if i < #Buttons then total = total + Theme.button_gap * S end
        end
    end

    local L = { S = S, fs = fs, items = {} }
    L.cx = W / 2
    local y = math.floor((H - total) / 2)
    L.titleY = y
    y = y + text_height(fs) + Theme.title_gap * S

    local x = math.floor((W - bw) / 2)
    for _, b in ipairs(Buttons) do
        if b.gap then
            y = y + b.gap * S
        else
            table.insert(L.items, { x = x, y = y, w = bw, h = bh, button = b })
            y = y + bh + Theme.button_gap * S
        end
    end
    return L
end

-- ===========================================================================
-- WIDGETS
-- ===========================================================================

local function draw_button(it, hovered, S, fs)
    local b = it.button
    local x, y, w, h = it.x, it.y, it.w, it.h

    local fill = b.disabled and C.button_off or (hovered and C.button_hover or C.button)
    local outline = (hovered and not b.disabled) and C.outline_hover or C.outline

    rect(x, y, w, h, outline)                                   -- outline
    rect(x + S, y + S, w - 2 * S, h - 2 * S, fill)              -- body
    if not b.disabled then
        rect(x + S, y + S, w - 2 * S, S, C.bevel_light)         -- top highlight
        rect(x + S, y + 2 * S, S, h - 3 * S, C.bevel_light)     -- left highlight
        rect(x + S, y + h - 2 * S, w - 2 * S, S, C.bevel_dark)  -- bottom shadow
    end

    local tc = b.disabled and C.text_off or (hovered and C.text_hover or C.text)
    text_centered(b.label or "", x + w / 2, y + (h - text_height(fs)) / 2, fs, tc, S)
end

-- ===========================================================================
-- RENDER / INPUT
-- ===========================================================================

function render_pause_menu(W, H)
    if not ui or not ui.draw_rect then return end

    local L = compute_layout(W, H)
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.dim)
    text_centered(Title, L.cx, L.titleY, L.fs, C.title, L.S)

    for _, it in ipairs(L.items) do
        local hovered = inside(mx, my, it.x, it.y, it.w, it.h)
        draw_button(it, hovered, L.S, L.fs)
    end
end

function handle_pause_click(mx, my, W, H)
    local L = compute_layout(W, H)
    for _, it in ipairs(L.items) do
        local b = it.button
        if inside(mx, my, it.x, it.y, it.w, it.h) and not b.disabled and b.action then
            b.action()
            return true
        end
    end
    return false
end
