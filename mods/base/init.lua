-- init.lua: entry point for the base mod.
-- The engine runs this file instead of loading the other scripts directly.
-- Load order is set here.

local modPath = g_CurrentModPath or "."

local function loadScript(name)
    local ok, err = pcall(dofile, modPath .. "/" .. name)
    if ok then
        print("[init] loaded " .. name)
    else
        print("[init] ERROR in " .. name .. ": " .. tostring(err))
    end
end

loadScript("fog.lua")      -- fog settings
loadScript("menu.lua")     -- main menu
