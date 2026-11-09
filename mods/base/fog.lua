-- fog.lua: Minecraft-style distance fog for the base mod.
-- Loaded by init.lua. Restart the game after editing.

-- The world loads 3 chunks out (16 blocks each), so the loaded edge is 48 blocks.
-- Fog reaches full density just inside that edge, so chunks fade in instead of popping.
local RENDER_DISTANCE_CHUNKS = 6
local LOADED_EDGE = RENDER_DISTANCE_CHUNKS * 16

fog.set_enabled(true)
fog.set_exponential(false)   -- linear fog, like Minecraft's
fog.set_range(LOADED_EDGE * 0.55, LOADED_EDGE - 2)   -- haze from about 26 to 46 blocks
fog.set_rain_scale(0.4)

-- Fog color should match the sky near the horizon, or a seam shows.
fog.set_day_color(0.55, 0.78, 0.92)
fog.set_sunset_color(0.82, 0.48, 0.30)
fog.set_night_color(0.04, 0.06, 0.12)

-- Weather narrows the fog a little, the way Minecraft's rain and storms do.
local fogPresets = {
    clear  = { near = LOADED_EDGE * 0.55, far = LOADED_EDGE - 2 },
    cloudy = { near = LOADED_EDGE * 0.50, far = LOADED_EDGE - 2 },
    rain   = { near = LOADED_EDGE * 0.38, far = LOADED_EDGE - 6 },
    storm  = { near = LOADED_EDGE * 0.25, far = LOADED_EDGE - 14 },
}

local originalSetWeather = clouds.set_weather
clouds.set_weather = function(name, seconds)
    originalSetWeather(name, seconds)
    local p = fogPresets[name]
    if p then
        fog.set_range(p.near, p.far)
    end
end

print("[fog.lua] Fog range " .. string.format("%.0f", LOADED_EDGE * 0.55) .. " to " .. (LOADED_EDGE - 2) .. " blocks")