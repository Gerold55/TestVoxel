-- pause_menu.lua - Fully Moddable Lua Pause & Settings Menu

print("[Craft & Ruin] Loading Lua Pause Menu System...")

-- State management for tabs (e.g., "main", "settings")
local menu_state = "main"

-- Developer-defined settings values
local settings = {
    fov = 70,
    render_distance = 8,
    vsync = true
}

-- Return pause menu render elements to C++ frame-by-frame
function get_pause_menu_layout(mouse_x, mouse_y, screen_w, screen_h)
    local elements = {}

    -- 1. Dark Backdrop Overlay
    table.insert(elements, {
        type = "backdrop",
        color = {0.0, 0.0, 0.0, 0.65}
    })

    if menu_state == "main" then
        -- Title
        table.insert(elements, {
            type = "label",
            text = "GAME PAUSED",
            x = -0.2, y = 0.35, w = 0.4, h = 0.1
        })

        -- Main Menu Buttons
        table.insert(elements, {
            type = "button",
            id = "resume",
            label = "Resume Game",
            x = -0.25, y = 0.10, w = 0.5, h = 0.12,
            action = function()
                engine.resume_game()
            end
        })

        table.insert(elements, {
            type = "button",
            id = "settings",
            label = "Settings",
            x = -0.25, y = -0.06, w = 0.5, h = 0.12,
            action = function()
                menu_state = "settings"
            end
        })

        table.insert(elements, {
            type = "button",
            id = "quit",
            label = "Quit to Desktop",
            x = -0.25, y = -0.22, w = 0.5, h = 0.12,
            action = function()
                engine.quit_game()
            end
        })

    elseif menu_state == "settings" then
        -- Settings Title
        table.insert(elements, {
            type = "label",
            text = "SETTINGS",
            x = -0.15, y = 0.35, w = 0.3, h = 0.1
        })

        -- FOV Control Option
        table.insert(elements, {
            type = "button",
            id = "fov_toggle",
            label = "FOV: " .. tostring(settings.fov),
            x = -0.3, y = 0.12, w = 0.6, h = 0.10,
            action = function()
                settings.fov = settings.fov + 10
                if settings.fov > 110 then settings.fov = 60 end
                engine.set_fov(settings.fov)
            end
        })

        -- Render Distance Option
        table.insert(elements, {
            type = "button",
            id = "render_dist",
            label = "Render Distance: " .. tostring(settings.render_distance) .. " Chunks",
            x = -0.3, y = -0.02, w = 0.6, h = 0.10,
            action = function()
                settings.render_distance = settings.render_distance + 2
                if settings.render_distance > 16 then settings.render_distance = 4 end
                engine.set_render_distance(settings.render_distance)
            end
        })

        -- Back Button
        table.insert(elements, {
            type = "button",
            id = "back",
            label = "Back to Menu",
            x = -0.25, y = -0.22, w = 0.5, h = 0.10,
            action = function()
                menu_state = "main"
            end
        })
    end

    return elements
end

-- Reset state when pausing
function on_game_paused()
    menu_state = "main"
end

print("[Craft & Ruin] Pause menu registered successfully.")