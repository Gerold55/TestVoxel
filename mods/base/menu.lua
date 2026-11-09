-- menu.lua: Main Menu layout and interaction script

local buttons = {
    { id = "play",         label = "Singleplayer",            x = -0.30, y =  0.05, w = 0.60, h = 0.08 },
    { id = "cycle_game",   label = "Subgame: Craft & Ruin",   x = -0.30, y = -0.05, w = 0.60, h = 0.08 },
    { id = "settings",     label = "Settings",                x = -0.30, y = -0.15, w = 0.60, h = 0.08 },
    { id = "credits",      label = "Credits",                 x = -0.30, y = -0.25, w = 0.60, h = 0.08 },
    { id = "quit",         label = "Exit Game",               x = -0.30, y = -0.35, w = 0.60, h = 0.08 }
}

local selectedGameIndex = 1
local games = {}

-- Initialize menu data on load
function on_menu_init()
    games = engine.get_games()
    if #games > 0 then
        buttons[2].label = "Subgame: " .. games[selectedGameIndex].title
    end
end

-- Called every frame when in GameState::MENU
function on_menu_update(mouseX, mouseY, screenWidth, screenHeight)
    -- 1. Draw Background Overlay
    menu.draw_quad(-1.0, -1.0, 2.0, 2.0, 0.12, 0.12, 0.12, 1.0)

    -- 2. Render Header Title / Logo Text near top center
    local activeGame = games[selectedGameIndex]
    local titleText = activeGame and activeGame.title or "CRAFT & RUIN"
    engine.draw_text(titleText, (screenWidth / 2) - 140, 120)

    -- 3. Render Buttons & Handle Hover States
    for _, btn in ipairs(buttons) do
        -- Check if mouse is hovering over the button
        local ndcX = (mouseX / screenWidth) * 2.0 - 1.0
        local ndcY = 1.0 - (mouseY / screenHeight) * 2.0
        local hovered = (ndcX >= btn.x and ndcX <= (btn.x + btn.w) and ndcY >= btn.y and ndcY <= (btn.y + btn.h))

        -- Button background color (lighter gray when hovered)
        local r, g, b, a = 0.20, 0.20, 0.20, 0.90
        if hovered then
            r, g, b = 0.35, 0.35, 0.45
        end

        -- Draw main button quad and dark border outline
        menu.draw_quad(btn.x, btn.y, btn.w, btn.h, r, g, b, a)
        menu.draw_quad(btn.x - 0.003, btn.y - 0.003, btn.w + 0.006, btn.h + 0.006, 0.08, 0.08, 0.08, 1.0)
        menu.draw_quad(btn.x, btn.y, btn.w, btn.h, r, g, b, a)

        -- Convert NDC button coordinates to Screen Pixel Coordinates for text rendering
        local pixelX = ((btn.x + 1.0) * 0.5) * screenWidth + (btn.w * 0.15 * screenWidth)
        local pixelY = ((1.0 - (btn.y + btn.h * 0.65)) * 0.5) * screenHeight

        engine.draw_text(btn.label, pixelX, pixelY)
    end

    -- 4. Bottom Corner Version & Copyright Info
    engine.draw_text("Craft & Ruin 5.17.0-dev", 15.0, screenHeight - 25.0)
    engine.draw_text("(c) 2010-2026 Developer Studio", screenWidth - 260.0, screenHeight - 25.0)
end

-- Called on left mouse click when in GameState::MENU
function on_menu_click(mouseX, mouseY, screenWidth, screenHeight)
    for _, btn in ipairs(buttons) do
        local ndcX = (mouseX / screenWidth) * 2.0 - 1.0
        local ndcY = 1.0 - (mouseY / screenHeight) * 2.0
        local clicked = (ndcX >= btn.x and ndcX <= (btn.x + btn.w) and ndcY >= btn.y and ndcY <= (btn.y + btn.h))

        if clicked then
            if btn.id == "play" then
                engine.start_game()
            elseif btn.id == "cycle_game" then
                if #games > 0 then
                    selectedGameIndex = (selectedGameIndex % #games) + 1
                    buttons[2].label = "Subgame: " .. games[selectedGameIndex].title
                    engine.set_active_game(games[selectedGameIndex].folder)
                end
            elseif btn.id == "quit" then
                engine.quit()
            end
        end
    end
end

-- Auto-initialize on script load
on_menu_init()