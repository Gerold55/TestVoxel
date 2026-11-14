-- multiplayer.lua - Multiplayer screen: your name, saved servers, join / add / edit / delete
-- ---------------------------------------------------------------------------
-- Shown after clicking Multiplayer on the title screen. Everything is editable.
--
-- To play together, one player opens a world and clicks "Host Game" in the pause menu.
-- Everyone else adds that player's address here and joins. Everybody needs the same
-- game selected (and the same mods).
--
-- Your name is shown at the top; change it in Settings > Player.
--
-- Engine functions used:
--   engine.list_servers()                 -> { { name = "Home", address = "192.168.1.20" }, ... }
--   engine.save_servers(list)             -- same shape as list_servers
--   engine.get_player_name()              -> "Steve"
--   engine.join_server(address)           -> true, or false + error   (starts connecting)
--   engine.ping_server(address)           -- checks a server without joining (takes up to ~5 s)
--   engine.get_server_status(address)     -> nil, or { state = "pinging"|"online"|"offline",
--                                             motd, players, max_players, ping, names = {...},
--                                             version, same_game, compatible, error }
--   engine.cancel_join()
--   engine.get_connection_status()        -> { state = "idle"|"connecting"|"joined", address, error }
--   engine.get_game_info()                -> { selected, loaded, title, can_play }
--   engine.back_to_title()
--
-- C++ calls:
--   render_multiplayer(W, H)
--   handle_multiplayer_click(mx, my, W, H)
--   handle_multiplayer_scroll(dy)
--   handle_multiplayer_key(key, action, mods)  -> true = handled
--   handle_multiplayer_char(codepoint, char)   -> true = handled
--   on_multiplayer_opened()
-- ---------------------------------------------------------------------------

print("[UI] multiplayer.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local Theme = {
    colors = {
        background     = { 0.10, 0.10, 0.10, 1.00 },
        list_bg        = { 0.00, 0.00, 0.00, 0.45 },
        row_hover      = { 1.00, 1.00, 1.00, 0.08 },
        row_selected   = { 1.00, 1.00, 1.00, 1.00 },
        row_selected_bg= { 0.00, 0.00, 0.00, 1.00 },
        title          = { 1.00, 1.00, 1.00, 1.00 },
        name           = { 1.00, 1.00, 1.00, 1.00 },
        detail         = { 0.63, 0.63, 0.63, 1.00 },
        warning        = { 1.00, 0.45, 0.40, 1.00 },
        good           = { 0.55, 1.00, 0.55, 1.00 },
        bar_good       = { 0.35, 0.90, 0.35, 1.00 },
        bar_ok         = { 0.95, 0.85, 0.30, 1.00 },
        bar_bad        = { 0.95, 0.35, 0.30, 1.00 },
        bar_off        = { 0.30, 0.30, 0.30, 1.00 },
        tooltip_bg     = { 0.08, 0.02, 0.15, 0.95 },
        tooltip_border = { 0.30, 0.10, 0.60, 1.00 },
        outline        = { 0.00, 0.00, 0.00, 1.00 },
        outline_hover  = { 1.00, 1.00, 1.00, 1.00 },
        button         = { 0.44, 0.44, 0.44, 1.00 },
        button_hover   = { 0.50, 0.55, 0.78, 1.00 },
        button_off     = { 0.27, 0.27, 0.27, 1.00 },
        danger         = { 0.55, 0.20, 0.20, 1.00 },
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

    -- Font metrics at scale 1.0, measured from the game's PixeloidSans text:
    --   char_w   average letter width (only used if the engine has no ui.text_width)
    --   char_h   height of capital letters
    --   y_offset where the text baseline sits, as a fraction of char_h below the top
    font = { char_w = 18, char_h = 20, base_scale = 0.5, y_offset = 1.05 },

    list_w = 330, row_h = 36,
    button_w = 100, button_h = 20, button_gap = 4,
    button_pad = 12,          -- buttons grow to fit their text plus this much space
    field_w = 200, field_h = 20,
    max_name = 16, max_server_name = 32, max_address = 64,
    double_click = 0.35,
}

local C = Theme.colors
local KEY_TAB = 258

-- ===========================================================================
-- STATE
-- ===========================================================================

local state = {
    mode = "list",          -- "list", "edit", "delete", "connecting"
    servers = nil,
    selected = nil,
    scroll = 0,
    last_click_row = nil,
    last_click_time = 0,
    player_name = "",
    focus = nil,            -- "name"/"address" on the edit screen
    edit_index = nil,       -- nil = adding a new server
    edit_name = "",
    edit_address = "",
    message = nil,          -- error line
    game = nil,
}

-- ===========================================================================
-- HELPERS
-- ===========================================================================

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function inside(mx, my, x, y, w, h) return mx >= x and mx < x + w and my >= y and my < y + h end

local function rect(x, y, w, h, c)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], c[4] or 1)
end

-- Exact width from the engine when it can measure text, otherwise an estimate
local function text_width(str, scale)
    if ui.text_width then return ui.text_width(str, scale) end
    return #str * Theme.font.char_w * scale
end
local function text_height(scale) return Theme.font.char_h * scale end

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

local function selected_server()
    return state.servers and state.selected and state.servers[state.selected] or nil
end

local function refresh()
    state.game = engine.get_game_info and engine.get_game_info() or nil
    state.servers = engine.list_servers and engine.list_servers() or {}
    state.player_name = engine.get_player_name and engine.get_player_name() or ""
    if state.selected and state.selected > #state.servers then state.selected = nil end
    if not state.selected and #state.servers > 0 then state.selected = 1 end
end

local function save()
    if engine.save_servers then engine.save_servers(state.servers) end
end

-- Ask every server in the list how it's doing (results show up as they arrive)
local function ping_all()
    if not engine.ping_server then return end
    for _, s in ipairs(state.servers or {}) do engine.ping_server(s.address) end
end

local function server_status(s)
    return engine.get_server_status and engine.get_server_status(s.address) or nil
end

-- ===========================================================================
-- ACTIONS
-- ===========================================================================

local function join(address)
    if not address or address == "" then return end
    local ok, err = engine.join_server(address)
    if ok == false then
        state.message = err
        return
    end
    state.message = nil
    state.mode = "connecting"
end

local function join_selected()
    local s = selected_server()
    if s then join(s.address) end
end

local function open_edit(index)
    local s = index and state.servers[index]
    state.mode = "edit"
    state.edit_index = index
    state.edit_name = s and s.name or "My Server"
    state.edit_address = s and s.address or ""
    state.focus = s and "name" or "address"
    state.message = nil
end

local function save_edit()
    local name = state.edit_name ~= "" and state.edit_name or "Server"
    local address = state.edit_address:gsub("^%s+", ""):gsub("%s+$", "")
    if address == "" then
        state.message = "Type the server's address first"
        return
    end
    local entry = { name = name, address = address }
    if state.edit_index and state.servers[state.edit_index] then
        state.servers[state.edit_index] = entry
        state.selected = state.edit_index
    else
        table.insert(state.servers, entry)
        state.selected = #state.servers
    end
    save()
    if engine.ping_server then engine.ping_server(address) end
    state.mode = "list"
    state.message = nil
end

local function connect_direct()
    local address = state.edit_address:gsub("^%s+", ""):gsub("%s+$", "")
    if address == "" then
        state.message = "Type the server's address first"
        return
    end
    state.mode = "list"
    join(address)
end

local function confirm_delete()
    if state.selected and state.servers[state.selected] then
        table.remove(state.servers, state.selected)
        save()
    end
    state.selected = nil
    state.mode = "list"
    refresh()
end

-- ===========================================================================
-- WIDGETS
-- ===========================================================================

local function draw_button(b, mx, my, S, fs)
    local hovered = b.enabled and inside(mx, my, b.x, b.y, b.w, b.h)
    local fill
    if not b.enabled then fill = C.button_off
    elseif b.danger then fill = hovered and C.danger_hover or C.danger
    else fill = hovered and C.button_hover or C.button end

    rect(b.x, b.y, b.w, b.h, hovered and C.outline_hover or C.outline)
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

-- Rows of `per_row` equal buttons, centred at the bottom
local function bottom_buttons(W, H, S, rows)
    local bh, gap = Theme.button_h * S, Theme.button_gap * S
    -- Every button is as wide as the longest label needs (so they all line up)
    local fs = Theme.font.base_scale * S
    local bw = Theme.button_w * S
    for _, row in ipairs(rows) do
        for _, b in ipairs(row) do
            bw = math.max(bw, text_width(b.label, fs) + Theme.button_pad * S)
        end
    end
    local out = {}
    local y = H - 10 * S - bh * #rows - gap * (#rows - 1)
    for _, row in ipairs(rows) do
        local total = bw * #row + gap * (#row - 1)
        local x = math.floor((W - total) / 2)
        for _, b in ipairs(row) do
            b.x, b.y, b.w, b.h = x, y, bw, bh
            if b.enabled == nil then b.enabled = true end
            table.insert(out, b)
            x = x + bw + gap
        end
        y = y + bh + gap
    end
    return out, out[1] and out[1].y or H
end

-- ===========================================================================
-- SERVER LIST SCREEN
-- ===========================================================================

local function list_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local has_sel = selected_server() ~= nil

    local buttons, top = bottom_buttons(W, H, S, {
        {
            { label = "Join Server", enabled = has_sel and can_play(), action = join_selected },
            { label = "Direct Connect", enabled = can_play(), action = function()
                open_edit(nil); state.mode = "direct" end },
            { label = "Add Server", action = function() open_edit(nil) end },
        },
        {
            { label = "Edit", enabled = has_sel, action = function() open_edit(state.selected) end },
            { label = "Delete", enabled = has_sel, action = function()
                state.mode = "delete"; state.message = nil end },
            { label = "Refresh", action = ping_all },
            { label = "Back", action = function() engine.back_to_title() end },
        },
    })

    local L = { S = S, fs = fs, buttons = buttons }
    L.titleY = 10 * S
    L.nameY = L.titleY + text_height(fs) + 8 * S
    L.messageY = L.nameY + text_height(fs) + 5 * S
    L.listW = math.min(Theme.list_w * S, W - 20 * S)
    L.listX = math.floor((W - L.listW) / 2)
    L.listY = L.messageY + text_height(fs) + 5 * S
    L.listH = top - 8 * S - L.listY
    L.rowH = Theme.row_h * S
    L.visible = math.max(1, math.floor(L.listH / L.rowH))
    return L
end

-- Cuts text to fit a width, adding "..." if it was too long
local function fit(str, width, scale)
    if text_width(str, scale) <= width then return str end
    local cut = str
    while #cut > 0 and text_width(cut .. "...", scale) > width do cut = cut:sub(1, -2) end
    return #cut > 0 and (cut .. "...") or ""
end

-- Five ping bars (Minecraft style). ping < 0 / nil = grey. offline = red cross look.
local function draw_ping_bars(x, y, h, st, S)
    local bars, color = 0, C.bar_off
    if st and st.state == "online" then
        local p = st.ping or 9999
        bars = p < 150 and 5 or p < 300 and 4 or p < 600 and 3 or p < 1000 and 2 or 1
        color = bars >= 4 and C.bar_good or (bars >= 3 and C.bar_ok or C.bar_bad)
    elseif st and st.state == "pinging" then
        -- Animated while waiting
        bars = math.floor(ui.get_time() * 6) % 6
        color = C.bar_off
    end
    for i = 1, 5 do
        local bh = math.floor(h * (0.25 + 0.15 * i))
        local c = (i <= bars) and color or C.bar_off
        if st and st.state == "pinging" and i == bars then c = C.detail end
        if st and st.state == "offline" then c = C.bar_bad end
        rect(x + (i - 1) * 2 * S, y + h - bh, S, bh, c)
    end
end

local function render_list(W, H)
    local L = list_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local servers = state.servers
    local tooltip = nil

    rect(0, 0, W, H, C.background)
    text_centered("Play Multiplayer", W / 2, L.titleY, fs, C.title, S)

    text_centered("Playing as " .. state.player_name .. "  (change it in Settings > Player)",
                  W / 2, L.nameY, fs * 0.85, C.detail, S)

    local warning = state.message
    if not warning and not can_play() then
        warning = "This game isn't loaded. Restart with it selected to join its servers."
    end
    if warning then
        text_centered(warning, W / 2, L.messageY, fs * 0.85, C.warning, S)
    else
        text_centered("To host: open a world, press Esc, click Host Game", W / 2, L.messageY,
                      fs * 0.85, C.detail, S)
    end

    rect(L.listX, L.listY, L.listW, L.listH, C.list_bg)
    if #servers == 0 then
        text_centered("No servers yet - click Add Server", W / 2,
                      L.listY + L.listH / 2 - text_height(fs) / 2, fs, C.detail, S)
    end

    state.scroll = clamp(state.scroll, 0, math.max(0, #servers - L.visible))
    local small = fs * 0.85
    for row = 1, L.visible do
        local i = row + state.scroll
        local s = servers[i]
        if not s then break end
        local rx, ry = L.listX + 2 * S, L.listY + (row - 1) * L.rowH + S
        local rw, rh = L.listW - 4 * S, L.rowH - 2 * S
        if i == state.selected then
            rect(rx, ry, rw, rh, C.row_selected)
            rect(rx + S, ry + S, rw - 2 * S, rh - 2 * S, C.row_selected_bg)
        elseif inside(mx, my, rx, ry, rw, rh) then
            rect(rx, ry, rw, rh, C.row_hover)
        end

        local st = server_status(s)

        -- Right side: ping bars, and "players / max" to their left
        local barsW = 10 * S
        local barsX = rx + rw - barsW - 6 * S
        -- Two lines (name, then description), centred in the row
        local line1Y = ry + math.floor((rh - (text_height(fs) + 3 * S + text_height(small))) / 2)
        local line2Y = line1Y + text_height(fs) + 3 * S
        draw_ping_bars(barsX, line1Y + S, text_height(fs) - 2 * S, st, S)
        local countW = 0
        if st and st.state == "online" then
            local count = st.players .. "/" .. st.max_players
            countW = text_width(count, fs)
            local cx = barsX - 10 * S - countW
            text(count, cx, line1Y, fs, C.detail, false, S)
            if inside(mx, my, cx, line1Y, countW, text_height(fs)) and st.names and #st.names > 0 then
                tooltip = { names = st.names, more = st.players - #st.names }
            end
        end

        -- Left side: server name, then the description or what's wrong
        local leftW = barsX - 10 * S - countW - (rx + 6 * S) - 6 * S
        text(fit(s.name, leftW, fs), rx + 6 * S, line1Y, fs, C.name, true, S)

        local line2, color = s.address, C.detail
        if st and st.state == "online" then
            if not st.compatible then
                line2, color = "Different version: " .. (st.version or "?"), C.warning
            elseif not st.same_game then
                line2, color = "Plays a different game: " .. (st.game or "?"), C.warning
            else
                line2 = (st.motd ~= "" and st.motd) or s.address
            end
        elseif st and st.state == "offline" then
            line2, color = st.error ~= "" and st.error or "Can't connect to server", C.warning
        elseif st and st.state == "pinging" then
            line2 = "Pinging..."
        end
        text(fit(line2, rw - 12 * S, small), rx + 6 * S, line2Y, small, color, false, S)
    end

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end

    -- Hovering the player count lists who's online (drawn last, on top)
    if tooltip then
        local lines = {}
        for _, n in ipairs(tooltip.names) do lines[#lines + 1] = n end
        if tooltip.more > 0 then lines[#lines + 1] = "...and " .. tooltip.more .. " more" end
        local w = 0
        for _, l in ipairs(lines) do w = math.max(w, text_width(l, small)) end
        local lh = text_height(small) + 2 * S
        local tw, th = w + 8 * S, #lines * lh + 6 * S
        local tx = math.min(mx + 10 * S, W - tw - 2 * S)
        local ty = math.min(my + 10 * S, H - th - 2 * S)
        rect(tx - S, ty - S, tw + 2 * S, th + 2 * S, C.tooltip_border)
        rect(tx, ty, tw, th, C.tooltip_bg)
        for j, l in ipairs(lines) do
            text(l, tx + 4 * S, ty + 3 * S + (j - 1) * lh, small, C.name, false, S)
        end
    end
end

local function click_list(mx, my, W, H)
    local L = list_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end

    if inside(mx, my, L.listX, L.listY, L.listW, L.listH) then
        local i = math.floor((my - L.listY) / L.rowH) + 1 + state.scroll
        if state.servers[i] then
            local now = ui.get_time()
            local double = state.selected == i and state.last_click_row == i
                           and (now - state.last_click_time) < Theme.double_click
            state.selected = i
            state.last_click_row = i
            state.last_click_time = now
            state.message = nil
            if double and can_play() then join_selected() end
        end
    end
end

-- ===========================================================================
-- ADD / EDIT SERVER and DIRECT CONNECT SCREENS
-- ===========================================================================

local function edit_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local direct = state.mode == "direct"
    local buttons = bottom_buttons(W, H, S, {
        {
            { label = direct and "Join Server" or "Done", action = direct and connect_direct or save_edit },
            { label = "Cancel", action = function() state.mode = "list"; state.message = nil end },
        },
    })
    local fw, fh = Theme.field_w * S, Theme.field_h * S
    local fx = math.floor((W - fw) / 2)
    local L = { S = S, fs = fs, fx = fx, buttons = buttons, direct = direct }
    L.titleY = 12 * S
    local y = 46 * S
    if not direct then
        L.nameLabelY = y
        L.nameField = { x = fx, y = y + text_height(fs) + 4 * S, w = fw, h = fh }
        y = L.nameField.y + fh + 14 * S
    end
    L.addrLabelY = y
    L.addrField = { x = fx, y = y + text_height(fs) + 4 * S, w = fw, h = fh }
    L.hintY = L.addrField.y + fh + 4 * S
    L.messageY = L.hintY + text_height(fs) + 6 * S
    return L
end

local function render_edit(W, H)
    local L = edit_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()

    rect(0, 0, W, H, C.background)
    local title = L.direct and "Direct Connect" or (state.edit_index and "Edit Server Info" or "Add Server")
    text_centered(title, W / 2, L.titleY, fs, C.title, S)

    if L.nameField then
        text("Server Name", L.fx, L.nameLabelY, fs, C.detail, false, S)
        draw_field(L.nameField, state.edit_name, state.focus == "name", nil, S, fs)
    end
    text("Server Address", L.fx, L.addrLabelY, fs, C.detail, false, S)
    draw_field(L.addrField, state.edit_address, state.focus == "address", "e.g. 192.168.1.20", S, fs)
    text("Add :port if it isn't 30000", L.fx, L.hintY, fs * 0.85, C.detail, false, S)
    if state.message then text_centered(state.message, W / 2, L.messageY, fs * 0.85, C.warning, S) end

    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

local function click_edit(mx, my, W, H)
    local L = edit_layout(W, H)
    if click_buttons(L.buttons, mx, my) then return end
    local f = L.nameField
    if f and inside(mx, my, f.x, f.y, f.w, f.h) then state.focus = "name" return end
    f = L.addrField
    if inside(mx, my, f.x, f.y, f.w, f.h) then state.focus = "address" return end
end

-- ===========================================================================
-- DELETE CONFIRMATION
-- ===========================================================================

local function delete_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons = bottom_buttons(W, H, S, {
        {
            { label = "Delete", danger = true, action = confirm_delete },
            { label = "Cancel", action = function() state.mode = "list" end },
        },
    })
    return { S = S, fs = fs, buttons = buttons, titleY = math.floor(H / 2 - 30 * S) }
end

local function render_delete(W, H)
    local L = delete_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    local s = selected_server()
    rect(0, 0, W, H, C.background)
    text_centered("Remove this server from the list?", W / 2, L.titleY, fs, C.title, S)
    text_centered("'" .. (s and s.name or "?") .. "' will be removed.", W / 2,
                  L.titleY + text_height(fs) + 10 * S, fs, C.detail, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

-- ===========================================================================
-- CONNECTING SCREEN
-- ===========================================================================

local function connecting_layout(W, H)
    local S = gui_scale(W, H)
    local fs = Theme.font.base_scale * S
    local buttons = bottom_buttons(W, H, S, {
        { { label = "Cancel", action = function()
            engine.cancel_join()
            state.mode = "list"
        end } },
    })
    return { S = S, fs = fs, buttons = buttons }
end

local function render_connecting(W, H)
    local status = engine.get_connection_status()
    if status.state == "idle" then
        -- Failed (or was cancelled): back to the list with the reason
        state.mode = "list"
        state.message = status.error
        render_list(W, H)
        return
    end

    local L = connecting_layout(W, H)
    local S, fs = L.S, L.fs
    local mx, my = ui.get_mouse()
    rect(0, 0, W, H, C.background)
    local dots = string.rep(".", math.floor(ui.get_time() * 2) % 4)
    local line = status.state == "joined" and "Joining world" or "Connecting to the server"
    text_centered(line .. dots, W / 2, H / 2 - 20 * S, fs, C.title, S)
    text_centered(status.address or "", W / 2, H / 2, fs * 0.85, C.detail, S)
    for _, b in ipairs(L.buttons) do draw_button(b, mx, my, S, fs) end
end

-- ===========================================================================
-- ENTRY POINTS (called from C++)
-- ===========================================================================

function on_multiplayer_opened()
    refresh()
    ping_all()
    state.scroll = 0
    local status = engine.get_connection_status and engine.get_connection_status() or {}
    if status.state == "connecting" then
        state.mode = "connecting"
    else
        state.mode = "list"
        state.message = status.error   -- e.g. "Lost connection to the server"
    end
    state.focus = nil
end

function render_multiplayer(W, H)
    if not ui or not ui.draw_rect then return end
    if not state.servers then refresh() end
    if state.mode == "edit" or state.mode == "direct" then render_edit(W, H)
    elseif state.mode == "delete" then render_delete(W, H)
    elseif state.mode == "connecting" then render_connecting(W, H)
    else render_list(W, H) end
end

function handle_multiplayer_click(mx, my, W, H)
    if state.mode == "edit" or state.mode == "direct" then click_edit(mx, my, W, H)
    elseif state.mode == "delete" then click_buttons(delete_layout(W, H).buttons, mx, my)
    elseif state.mode == "connecting" then click_buttons(connecting_layout(W, H).buttons, mx, my)
    else click_list(mx, my, W, H) end
    return true
end

function handle_multiplayer_scroll(dy)
    if state.mode ~= "list" then return end
    if dy > 0 then state.scroll = state.scroll - 1 elseif dy < 0 then state.scroll = state.scroll + 1 end
    if state.scroll < 0 then state.scroll = 0 end
end

function handle_multiplayer_key(key, action, mods)
    local press = action == ui.actions.PRESS or action == ui.actions.REPEAT
    if not press then return true end
    local enter = key == ui.keys.ENTER or key == ui.keys.KP_ENTER

    if state.mode == "connecting" then
        if key == ui.keys.ESCAPE then engine.cancel_join(); state.mode = "list" end
        return true
    end
    if state.mode == "delete" then
        if key == ui.keys.ESCAPE then state.mode = "list" end
        return true
    end
    if state.mode == "edit" or state.mode == "direct" then
        if key == ui.keys.ESCAPE then
            state.mode = "list"; state.message = nil
        elseif key == KEY_TAB and state.mode == "edit" then
            state.focus = (state.focus == "name") and "address" or "name"
        elseif enter then
            if state.mode == "direct" then connect_direct() else save_edit() end
        elseif key == ui.keys.BACKSPACE then
            local field = state.focus == "name" and "edit_name" or "edit_address"
            state[field] = state[field]:sub(1, -2)
        end
        return true
    end

    -- List screen
    local count = state.servers and #state.servers or 0
    if key == ui.keys.UP and count > 0 then
        state.selected = clamp((state.selected or 1) - 1, 1, count)
        return true
    elseif key == ui.keys.DOWN and count > 0 then
        state.selected = clamp((state.selected or 0) + 1, 1, count)
        return true
    elseif enter then
        if can_play() then join_selected() end
        return true
    elseif key == ui.keys.DELETE and selected_server() then
        state.mode = "delete"
        return true
    end
    return false   -- Escape: C++ goes back to the title screen
end

function handle_multiplayer_char(codepoint, ch)
    if not ch or ch == "" or codepoint < 32 or codepoint >= 127 then return false end
    if state.mode == "edit" or state.mode == "direct" then
        if state.focus == "name" and state.mode == "edit" then
            if #state.edit_name < Theme.max_server_name then state.edit_name = state.edit_name .. ch end
        else
            if #state.edit_address < Theme.max_address and ch ~= " " then
                state.edit_address = state.edit_address .. ch
            end
        end
        return true
    end
    return false
end
