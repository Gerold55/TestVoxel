-- player_list.lua - Player list shown at the top of the screen while Tab is held
-- ---------------------------------------------------------------------------
-- The key can be changed in Settings > Controls ("List Players"). Everything is editable.
--
-- Engine functions used:
--   engine.get_player_list() -> { title = "Hosting on port 30000",
--                                 players = { { name, ping, host, you }, ... } }
--     ping is in milliseconds (-1 = not known yet, e.g. singleplayer)
--
-- C++ calls:
--   render_player_list(W, H)
-- ---------------------------------------------------------------------------

print("[UI] player_list.lua loaded")

local Theme = {
    colors = {
        panel      = { 0.00, 0.00, 0.00, 0.55 },
        row        = { 1.00, 1.00, 1.00, 0.08 },
        title      = { 1.00, 1.00, 1.00, 1.00 },
        name       = { 1.00, 1.00, 1.00, 1.00 },
        you        = { 1.00, 1.00, 0.60, 1.00 },   -- your own name
        host_tag   = { 0.60, 0.90, 1.00, 1.00 },
        bar_good   = { 0.35, 0.90, 0.35, 1.00 },
        bar_ok     = { 0.95, 0.85, 0.30, 1.00 },
        bar_bad    = { 0.95, 0.35, 0.30, 1.00 },
        bar_off    = { 0.25, 0.25, 0.25, 1.00 },
        detail     = { 0.70, 0.70, 0.70, 1.00 },
    },
    -- Keep in sync with settings_menu.lua
    font = { char_w = 13, char_h = 20, base_scale = 0.5, y_offset = 0.75 },

    -- Sizes in GUI pixels (multiplied by the GUI scale)
    min_width = 160,
    row_h = 11,
    rows_per_column = 20, -- like Minecraft: a new column every 20 players
    max_columns = 4,      -- 4 x 20 = 80 names; beyond that "...and N more"
    max_height = 0.75,    -- the list never covers more than this much of the screen height
    max_width = 0.95,     -- ...or this much of its width (text shrinks to fit first)
    min_text = 0.6,       -- smallest the text may shrink to (fraction of normal)
    top = 6,
}

local C = Theme.colors

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function rect(x, y, w, h, c)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], c[4] or 1)
end

local function text_width(str, scale) return #str * Theme.font.char_w * scale end
local function text_height(scale) return Theme.font.char_h * scale end

local function text(str, x, y, scale, c, S)
    y = y + Theme.font.char_h * scale * Theme.font.y_offset
    ui.draw_text(str, x + S * 0.5, y + S * 0.5, scale, 0, 0, 0, 0.8)   -- shadow
    ui.draw_text(str, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end

local function gui_scale(W, H)
    return clamp(math.floor(math.min(W / 480, H / 360) * 2) / 2, 1, 4)
end

-- Ping -> number of bars (1-5), like Minecraft. nil = unknown.
local function ping_bars(ping)
    if not ping or ping < 0 then return nil end
    if ping < 150 then return 5 end
    if ping < 300 then return 4 end
    if ping < 600 then return 3 end
    if ping < 1000 then return 2 end
    return 1
end

local function draw_bars(x, y, h, ping, S)
    local bars = ping_bars(ping)
    local color = C.bar_off
    if bars then color = bars >= 4 and C.bar_good or (bars >= 3 and C.bar_ok or C.bar_bad) end
    for i = 1, 5 do
        local bh = math.floor(h * (0.25 + 0.15 * i))
        local c = (bars and i <= bars) and color or C.bar_off
        rect(x + (i - 1) * 2 * S, y + h - bh, S, bh, c)
    end
end

function render_player_list(W, H)
    if not ui or not ui.draw_rect or not engine.get_player_list then return end
    local info = engine.get_player_list()
    local players = info.players or {}

    -- Host first, then you, then everyone else by name
    table.sort(players, function(a, b)
        if a.host ~= b.host then return a.host end
        if a.you ~= b.you then return a.you end
        return a.name:lower() < b.name:lower()
    end)

    local S = gui_scale(W, H)
    local title = info.title or ""
    local count = #players .. (#players == 1 and " player" or " players") .. " online"
    local headerFs = Theme.font.base_scale * S * 0.9
    local pad = 3 * S
    local headerH = text_height(headerFs) + text_height(headerFs * 0.85) + pad * 3

    -- Fit the list on screen: work out rows per column and columns, shrinking the
    -- text if the columns would be too wide. k = text/row size (1 = normal).
    local function measure(k)
        local fs = headerFs * k
        local rowH = math.floor(Theme.row_h * S * k)
        local nameW = 0
        for _, p in ipairs(players) do
            nameW = math.max(nameW, text_width(p.name .. (p.host and " (host)" or ""), fs))
        end
        local barsW = 10 * S * k
        local colW = nameW + barsW + pad * 4
        local fitRows = math.floor((H * Theme.max_height - Theme.top * S - headerH - pad) / rowH)
        local rowsPerCol = math.max(1, math.min(Theme.rows_per_column, fitRows))
        local cols = math.max(1, math.min(Theme.max_columns, math.ceil(#players / rowsPerCol)))
        return { fs = fs, rowH = rowH, barsW = barsW, colW = colW, rowsPerCol = rowsPerCol, cols = cols }
    end

    local k = 1.0
    local L = measure(k)
    while L.colW * L.cols > W * Theme.max_width and k > Theme.min_text do
        k = k - 0.05
        L = measure(k)
    end
    -- Still too wide (very long names): use fewer columns
    while L.cols > 1 and L.colW * L.cols > W * Theme.max_width do L.cols = L.cols - 1 end

    local fs, rowH, barsW, colW, cols = L.fs, L.rowH, L.barsW, L.colW, L.cols
    local capacity = cols * L.rowsPerCol
    local shown = #players
    local hidden = 0
    if shown > capacity then
        shown = capacity - 1           -- keep the last spot for "...and N more"
        hidden = #players - shown
    end
    local slots = shown + (hidden > 0 and 1 or 0)
    local perCol = math.ceil(slots / cols)   -- spread evenly across the columns

    local panelW = math.max(Theme.min_width * S, colW * cols + pad * 2,
                            text_width(title, headerFs) + pad * 4, text_width(count, headerFs * 0.85) + pad * 4)
    local panelH = headerH + perCol * rowH + pad
    local x = math.floor((W - panelW) / 2)
    local y = Theme.top * S

    rect(x, y, panelW, panelH, C.panel)
    text(title, W / 2 - text_width(title, headerFs) / 2, y + pad, headerFs, C.title, S)
    text(count, W / 2 - text_width(count, headerFs * 0.85) / 2, y + pad * 2 + text_height(headerFs),
         headerFs * 0.85, C.detail, S)

    -- Rows, filling each column top to bottom
    local startX = math.floor((W - colW * cols) / 2)
    for i = 1, slots do
        local col = math.floor((i - 1) / perCol)
        local row = (i - 1) % perCol
        local rx = startX + col * colW
        local ry = y + headerH + row * rowH
        rect(rx + S, ry, colW - 2 * S, rowH - S, C.row)
        local ty = ry + (rowH - S - text_height(fs)) / 2

        if i > shown then
            text("...and " .. hidden .. " more", rx + pad, ty, fs, C.detail, S)
        else
            local p = players[i]
            text(p.name, rx + pad, ty, fs, p.you and C.you or C.name, S)
            if p.host then
                text(" (host)", rx + pad + text_width(p.name, fs), ty, fs, C.host_tag, S)
            end
            draw_bars(rx + colW - barsW - pad, ry + 2 * S * k, rowH - 5 * S * k, p.ping, S)
        end
    end
end
