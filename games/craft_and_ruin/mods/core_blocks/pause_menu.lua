-- pause_menu.lua - Fully Functional Interactive Lua Pause & Settings Menu
print("[UI] pause_menu.lua loaded successfully!")

local pause_open = false
local menu_state = "main" -- "main" or "settings"

-- Developer-defined settings values
local settings = {
    fov = 70,
    render_distance = 8
}

function set_pause_open(isOpen)
    pause_open = isOpen
    menu_state = "main"
end

function toggle_pause_menu()
    pause_open = not pause_open
    menu_state = "main"
end

function is_pause_open()
    return pause_open
end

function render_pause_menu(width, height)
    if not pause_open then return end

    if ui and ui.draw_rect then
        -- 1. Dim background overlay
        ui.draw_rect(0, 0, width, height, 0.0, 0.0, 0.0, 0.65)

        -- 2. Window Panel Dimensions (Centered)
        local win_w = 420
        local win_h = 340
        local win_x = (width - win_w) / 2
        local win_y = (height - win_h) / 2

        -- Main Panel Background
        ui.draw_rect(win_x, win_y, win_w, win_h, 0.18, 0.18, 0.18, 0.95)

        local btn_w = 320
        local btn_h = 44
        local btn_x = win_x + 50

        if menu_state == "main" then
            -- Title
            if ui.draw_text then
                ui.draw_text("GAME PAUSED", win_x + 125, win_y + 25, 1.0, 1.0, 1.0)
            end

            -- Resume Button
            local res_y = win_y + 80
            ui.draw_rect(btn_x, res_y, btn_w, btn_h, 0.28, 0.28, 0.28, 1.0)
            if ui.draw_text then ui.draw_text("Resume Game", btn_x + 110, res_y + 14, 1.0, 1.0, 1.0) end

            -- Settings Button
            local set_y = win_y + 140
            ui.draw_rect(btn_x, set_y, btn_w, btn_h, 0.28, 0.28, 0.28, 1.0)
            if ui.draw_text then ui.draw_text("Settings", btn_x + 130, set_y + 14, 1.0, 1.0, 1.0) end

            -- Quit Button
            local quit_y = win_y + 200
            ui.draw_rect(btn_x, quit_y, btn_w, btn_h, 0.35, 0.15, 0.15, 1.0)
            if ui.draw_text then ui.draw_text("Quit to Desktop", btn_x + 100, quit_y + 14, 1.0, 1.0, 1.0) end

        elseif menu_state == "settings" then
            -- Title
            if ui.draw_text then
                ui.draw_text("SETTINGS", win_x + 155, win_y + 25, 1.0, 1.0, 1.0)
            end

            -- FOV Button
            local fov_y = win_y + 80
            ui.draw_rect(btn_x, fov_y, btn_w, btn_h, 0.28, 0.28, 0.28, 1.0)
            if ui.draw_text then ui.draw_text("FOV: " .. tostring(settings.fov), btn_x + 110, fov_y + 14, 1.0, 1.0, 1.0) end

            -- Render Distance Button
            local rd_y = win_y + 140
            ui.draw_rect(btn_x, rd_y, btn_w, btn_h, 0.28, 0.28, 0.28, 1.0)
            if ui.draw_text then ui.draw_text("Render Distance: " .. tostring(settings.render_distance) .. " Chunks", btn_x + 45, rd_y + 14, 1.0, 1.0, 1.0) end

            -- Back Button
            local back_y = win_y + 200
            ui.draw_rect(btn_x, back_y, btn_w, btn_h, 0.28, 0.28, 0.28, 1.0)
            if ui.draw_text then ui.draw_text("Back to Menu", btn_x + 110, back_y + 14, 1.0, 1.0, 1.0) end
        end
    end
end

function handle_pause_click(mx, my, width, height)
    if not pause_open then return end

    local win_w = 420
    local win_h = 340
    local win_x = (width - win_w) / 2
    local win_y = (height - win_h) / 2

    local btn_w = 320
    local btn_h = 44
    local btn_x = win_x + 50

    if menu_state == "main" then
        local res_y = win_y + 80
        local set_y = win_y + 140
        local quit_y = win_y + 200

        -- Resume Click
        if mx >= btn_x and mx <= (btn_x + btn_w) and my >= res_y and my <= (res_y + btn_h) then
            pause_open = false
            return "resume"
        end

        -- Settings Click
        if mx >= btn_x and mx <= (btn_x + btn_w) and my >= set_y and my <= (set_y + btn_h) then
            menu_state = "settings"
            return "settings"
        end

        -- Quit Click
        if mx >= btn_x and mx <= (btn_x + btn_w) and my >= quit_y and my <= (quit_y + btn_h) then
            if engine and engine.quit then engine.quit() end
            return "quit"
        end

    elseif menu_state == "settings" then
        local fov_y = win_y + 80
        local rd_y = win_y + 140
        local back_y = win_y + 200

        -- FOV Click
        if mx >= btn_x and mx <= (btn_x + btn_w) and my >= fov_y and my <= (fov_y + btn_h) then
            settings.fov = settings.fov + 10
            if settings.fov > 110 then settings.fov = 60 end
            if engine and engine.set_fov then engine.set_fov(settings.fov) end
            return "fov"
        end

        -- Render Distance Click
        if mx >= btn_x and mx <= (btn_x + btn_w) and my >= rd_y and my <= (rd_y + btn_h) then
            settings.render_distance = settings.render_distance + 2
            if settings.render_distance > 16 then settings.render_distance = 4 end
            if world and world.set_render_distance then world.set_render_distance(settings.render_distance) end
            return "render_distance"
        end

        -- Back Click
        if mx >= btn_x and mx <= (btn_x + btn_w) and my >= back_y and my <= (back_y + btn_h) then
            menu_state = "main"
            return "back"
        end
    end
    return nil
end