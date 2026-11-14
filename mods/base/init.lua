-- init.lua: entry point for the base mod.
-- The engine runs this file instead of loading the other scripts directly.
-- Load order is set here.

local modPath = g_CurrentModPath or "."

local function loadScript(name)
    if not name or type(name) ~= "string" then
        return false, "Invalid script name"
    end

    -- Security Check: Block path traversal sequences (".."),
    -- absolute paths, or Windows drive letters.
    if name:find("%.%.") or name:match("^/") or name:match("^[a-zA-Z]:") then
        print("[Security Warning] Path traversal attempt detected in loadScript: " .. name)
        return false, "Access denied: Unsafe path"
    end

    local fullPath = modPath .. "/" .. name
    local ok, err = pcall(dofile, fullPath)
    if not ok then
        -- Never fail silently: say which script broke and why
        print("[Mod] FAILED to load " .. fullPath .. ": " .. tostring(err))
        return false, err
    end
    print("[Mod] Loaded " .. fullPath)
    return true
end

loadScript("fog.lua")           -- fog settings
-- menu.lua is no longer loaded: the title screen is drawn by MainMenu.cpp
loadScript("chat.lua")          -- chat commands
loadScript("mods_menu.lua")     -- mods menu
loadScript("texture_packs.lua") -- texture pack picker
loadScript("settings_menu.lua") -- settings (controls, video)
loadScript("world_select.lua")  -- world selection
loadScript("multiplayer.lua")   -- multiplayer server list
loadScript("player_list.lua")   -- player list (hold Tab)
