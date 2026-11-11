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
        return false, err
    end
    return true
end

loadScript("fog.lua")      -- fog settings
loadScript("menu.lua")     -- main menu
loadScript("chat.lua")     -- chat commands