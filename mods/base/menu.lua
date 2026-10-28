print("[DEBUG] menu.lua is executing!")

function render_main_menu(width, height)
    -- Center text using pixel coordinates (width / 2, height / 2)
    if engine.draw_text then
        -- Convert center coordinates or use approximate pixel positions
        engine.draw_text("CRAFT AND RUIN", width * 0.4, height * 0.4)
        engine.draw_text("PRESS ENTER TO PLAY", width * 0.38, height * 0.5)
        engine.draw_text("PRESS ESC TO QUIT", width * 0.39, height * 0.6)
    end
end

function handle_menu_click(x, y, width, height)
    print("[Menu] Clicked at: " .. x .. ", " .. y)
    -- If clicked anywhere near the center, start the game!
    if engine.start_game then
        engine.start_game()
    end
end