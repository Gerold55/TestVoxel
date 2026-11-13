-- init.lua - Central Engine Entry Point & Script Loader
print("[Lua] Initializing Voxel Engine Lua Environment...")

-- Load dependent UI modules safely using the mod's absolute path
local mod_path = g_CurrentModPath or "../../games/craft_and_ruin/mods/inventory"
dofile(mod_path .. "/inventory.lua")

-- Global event handlers called by C++ main loop
function on_engine_init()
    print("[Lua] Engine initialized successfully.")
end

function on_frame_render(width, height)
    if render_creative_inventory then
        render_creative_inventory(width, height)
    end
end

function on_mouse_click(button, action, mods, width, height)
    if action == 1 then
        local mx, my = -1, -1
        if ui and ui.get_mouse_pos then
            mx, my = ui.get_mouse_pos()
        end
        if handle_creative_click then
            handle_creative_click(mx, my, width, height)
        end
    end
end