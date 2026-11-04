-- mapgen.lua - Minecraft-Style Biomes with Smooth Blending

local world_seed = 1337

-- Configure celestial textures
engine.set_sun_texture("cw_sun.png")

function set_world_seed(seed)
    if seed then world_seed = seed end
end

-- Hash function for pseudo-random values at integer coordinates
function get_rand(x, z, seed_offset)
    local val = math.sin((x + world_seed * 0.1) * 12.9898 + (z + world_seed * 0.1) * 78.233 + seed_offset) * 43758.5453
    return val - math.floor(val)
end

-- Smooth Interpolation Helpers
local function fade(t)
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
end

local function lerp(t, a, b)
    return a + t * (b - a)
end

local function smooth_noise2d(x, z, seed_offset)
    local X = math.floor(x)
    local Z = math.floor(z)
    local fx = x - X
    local fz = z - Z

    local u = fade(fx)
    local v = fade(fz)

    local n00 = get_rand(X, Z, seed_offset)
    local n10 = get_rand(X + 1, Z, seed_offset)
    local n01 = get_rand(X, Z + 1, seed_offset)
    local n11 = get_rand(X + 1, Z + 1, seed_offset)

    local nx0 = lerp(u, n00, n10)
    local nx1 = lerp(u, n01, n11)

    return lerp(v, nx0, nx1)
end

-- Octave noise for continuous terrain shapes
local function octave_noise2d(x, z, octaves, persistence, scale, seed_offset)
    local total = 0
    local frequency = scale
    local amplitude = 1
    local maxValue = 0
    for i = 1, octaves do
        total = total + smooth_noise2d(x * frequency, z * frequency, seed_offset + i * 1.5) * amplitude
        maxValue = maxValue + amplitude
        amplitude = amplitude * persistence
        frequency = frequency * 2
    end
    return total / maxValue
end

function get_biome_parameters(world_x, world_z)
    local continent_scale = 0.001
    local biome_scale     = 0.002
    local river_scale     = 0.006

    local wx = world_x + (world_seed % 1000)
    local wz = world_z + (world_seed % 1000)

    local continentalness = octave_noise2d(wx, wz, 3, 0.5, continent_scale, 1.0) * 2.0 - 1.0
    local temperature     = octave_noise2d(wx, wz, 3, 0.5, biome_scale, 2.0)
    local humidity        = octave_noise2d(wx, wz, 3, 0.5, biome_scale, 4.0)
    local river_noise     = math.abs(octave_noise2d(wx, wz, 2, 0.5, river_scale, 3.0) * 2.0 - 1.0)

    return continentalness, temperature, humidity, river_noise
end

function get_terrain_height(world_x, world_z, continentalness, temperature, humidity, river_val)
    local wx = world_x + (world_seed % 1000)
    local wz = world_z + (world_seed % 1000)

    local sea_level = 62.0
    
    -- Base inland height
    local base_height = sea_level + (continentalness * 28.0)

    -- Continuous detail noise
    local h1 = (octave_noise2d(wx, wz, 4, 0.5, 0.008, 5.0) * 2.0 - 1.0) * 14.0
    local h2 = (octave_noise2d(wx, wz, 2, 0.5, 0.03, 6.0) * 2.0 - 1.0) * 3.0

    -- Smooth swamp terrain blending factor (Warm + High Humidity + Low Inland Height)
    local swamp_factor = math.max(0.0, math.min(1.0, (humidity - 0.55) / 0.15)) *
                         math.max(0.0, math.min(1.0, (temperature - 0.35) / 0.15))

    -- Smoothly flatten swamp areas toward water level without step cuts
    if swamp_factor > 0.0 then
        h1 = lerp(swamp_factor, h1, h1 * 0.15)
        h2 = lerp(swamp_factor, h2, h2 * 0.2)
        base_height = lerp(swamp_factor, base_height, sea_level - 0.5)
    end

    local height = base_height + h1 + h2

    -- Oceans
    if continentalness < -0.2 then
        local depth_factor = (-0.2 - continentalness)
        local ocean_depth = (depth_factor * depth_factor * 70.0) + (depth_factor * 15.0)
        height = height - ocean_depth
    end

    -- Rivers
    if continentalness > -0.05 and river_val < 0.05 then
        local river_depth_factor = (0.05 - river_val) / 0.05
        height = height - (river_depth_factor * 7.0)
        if height < sea_level - 3.0 then
            height = sea_level - 3.0
        end
    end

    return math.floor(height)
end

-- World water check helper
local function is_world_water(world_x, world_z)
    local c, t, h, r = get_biome_parameters(world_x, world_z)
    local height = get_terrain_height(world_x, world_z, c, t, h, r)
    return height < 62.0
end

function is_cave(world_x, world_y, world_z)
    local scale_a = 0.035
    local scale_b = 0.07
    local ox = world_x + (world_seed % 1000)
    local oz = world_z + (world_seed % 1000)

    local n1 = math.sin(ox * scale_a) * math.cos(world_y * scale_a) * math.sin(oz * scale_a)
    local n2 = math.cos(ox * scale_b + 1.2) * math.sin(world_y * scale_b + 0.8) * math.cos(oz * scale_b + 2.1)
    
    local combined = (n1 + n2) * 0.5
    return (combined > 0.38 or combined < -0.38)
end

function generate_oak_tree(chunk, x, y, z)
    local wood_id = engine.get_block_id("log_oak") ~= 0 and engine.get_block_id("log_oak") or 5
    local leaves_id = engine.get_block_id("leaves_oak") ~= 0 and engine.get_block_id("leaves_oak") or 6

    local trunk_height = 5
    for h = 0, trunk_height - 1 do
        local ty = y + h
        if ty < 256 then chunk:set_block(x, ty, z, wood_id) end
    end
    
    local leaf_start = y + 3
    for ly = 0, 3 do
        local current_y = leaf_start + ly
        local radius = (ly == 3) and 1 or 2

        for lx = -radius, radius do
            for lz = -radius, radius do
                if not (ly == 0 and math.abs(lx) == 2 and math.abs(lz) == 2) then
                    local bx, by, bz = x + lx, current_y, z + lz
                    if bx >= 0 and bx < 16 and by >= 0 and by < 256 and bz >= 0 and bz < 16 then
                        if chunk:get_block(bx, by, bz) == 0 then
                            chunk:set_block(bx, by, bz, leaves_id)
                        end
                    end
                end
            end
        end
    end

    local top_crown_y = leaf_start + 4
    if top_crown_y < 256 and x >= 0 and x < 16 and z >= 0 and z < 16 then
        if chunk:get_block(x, top_crown_y, z) == 0 then
            chunk:set_block(x, top_crown_y, z, leaves_id)
        end
    end
end

function generate_swamp_tree(chunk, x, y, z)
    local wood_id = engine.get_block_id("log_oak") ~= 0 and engine.get_block_id("log_oak") or 5
    local leaves_id = engine.get_block_id("leaves_oak") ~= 0 and engine.get_block_id("leaves_oak") or 6

    local trunk_height = 5 + math.floor(get_rand(x, z, 3.1) * 2)
    for h = 0, trunk_height - 1 do
        local ty = y + h
        if ty < 256 then chunk:set_block(x, ty, z, wood_id) end
    end

    local leaf_start = y + trunk_height - 3
    for ly = 0, 3 do
        local current_y = leaf_start + ly
        local radius = (ly == 0 or ly == 3) and 2 or 3

        for lx = -radius, radius do
            for lz = -radius, radius do
                if not (math.abs(lx) == radius and math.abs(lz) == radius) then
                    local bx, by, bz = x + lx, current_y, z + lz
                    if bx >= 0 and bx < 16 and by >= 0 and by < 256 and bz >= 0 and bz < 16 then
                        if chunk:get_block(bx, by, bz) == 0 then
                            chunk:set_block(bx, by, bz, leaves_id)
                        end
                    end
                end
            end
        end
    end
end

function generate_birch_tree(chunk, x, y, z)
    local wood_id = engine.get_block_id("log_birch") ~= 0 and engine.get_block_id("log_birch") or 5
    local leaves_id = engine.get_block_id("leaves_birch") ~= 0 and engine.get_block_id("leaves_birch") or 6

    local trunk_height = 6
    for h = 0, trunk_height - 1 do
        local ty = y + h
        if ty < 256 then chunk:set_block(x, ty, z, wood_id) end
    end
    
    local leaf_start = y + 4
    for ly = 0, 3 do
        local current_y = leaf_start + ly
        local radius = (ly == 0 or ly == 3) and 1 or 2

        for lx = -radius, radius do
            for lz = -radius, radius do
                if not (ly == 1 and math.abs(lx) == 2 and math.abs(lz) == 2) then
                    local bx, by, bz = x + lx, current_y, z + lz
                    if bx >= 0 and bx < 16 and by >= 0 and by < 256 and bz >= 0 and bz < 16 then
                        if chunk:get_block(bx, by, bz) == 0 then
                            chunk:set_block(bx, by, bz, leaves_id)
                        end
                    end
                end
            end
        end
    end

    local top_crown_y = leaf_start + 4
    if top_crown_y < 256 and x >= 0 and x < 16 and z >= 0 and z < 16 then
        if chunk:get_block(x, top_crown_y, z) == 0 then
            chunk:set_block(x, top_crown_y, z, leaves_id)
        end
    end
end

function generate_cactus(chunk, x, y, z)
    local cactus_id = engine.get_block_id("cactus") ~= 0 and engine.get_block_id("cactus") or 12
    local cactus_height = 1 + math.floor(get_rand(x, z, 9.9) * 3)
    if cactus_height > 3 then cactus_height = 3 end

    for h = 0, cactus_height - 1 do
        local ty = y + h
        if ty < 256 then chunk:set_block(x, ty, z, cactus_id) end
    end
end

function generate_reeds(chunk, x, y, z)
    local reed_id = engine.get_block_id("reed") ~= 0 and engine.get_block_id("reed") or engine.get_block_id("short_grass")
    local reed_top_id = engine.get_block_id("reed_top") ~= 0 and engine.get_block_id("reed_top")
    if reed_top_id == 0 then reed_top_id = reed_id end

    local target_height = 2 + math.floor(get_rand(x, z, 7.7) * 2)
    if target_height > 3 then target_height = 3 end

    for h = 0, target_height - 1 do
        local ty = y + h
        if ty < 256 then
            local block_to_place = (h == target_height - 1) and reed_top_id or reed_id
            chunk:set_block(x, ty, z, block_to_place)
        end
    end
end

function generate_chunk(chunk, chunk_x, chunk_z)
    local stone_id = engine.get_block_id("stone") ~= 0 and engine.get_block_id("stone") or 2
    local bedrock_id = engine.get_block_id("bedrock") ~= 0 and engine.get_block_id("bedrock") or 2

    local grass_id = engine.get_block_id("grass") ~= 0 and engine.get_block_id("grass") or 4
    local dirt_id = engine.get_block_id("dirt") ~= 0 and engine.get_block_id("dirt") or 3
    local sand_id = engine.get_block_id("sand") ~= 0 and engine.get_block_id("sand") or 7
    local gravel_id = engine.get_block_id("gravel") ~= 0 and engine.get_block_id("gravel") or 13
    local water_id = engine.get_block_id("water") ~= 0 and engine.get_block_id("water") or 9

    local mushroom_red_id = engine.get_block_id("mushroom_red") ~= 0 and engine.get_block_id("mushroom_red") or engine.get_block_id("flower_poppy")
    local mushroom_brown_id = engine.get_block_id("mushroom_brown") ~= 0 and engine.get_block_id("mushroom_brown") or engine.get_block_id("flower_daisy")
    local blue_orchid_id = engine.get_block_id("flower_bluebell")

    local short_grass_id = engine.get_block_id("short_grass")
    local sea_level = 62.0

    local heights = {}
    local water_mask = {}

    for lx = 0, 15 do
        heights[lx] = {}
        water_mask[lx] = {}
        for lz = 0, 15 do
            local world_x = chunk_x * 16 + lx
            local world_z = chunk_z * 16 + lz
            local c, temp, hum, river_val = get_biome_parameters(world_x, world_z)
            local surface_y = get_terrain_height(world_x, world_z, c, temp, hum, river_val)
            
            heights[lx][lz] = surface_y
            water_mask[lx][lz] = (surface_y < sea_level)
        end
    end

    for lx = 0, 15 do
        for lz = 0, 15 do
            local world_x = chunk_x * 16 + lx
            local world_z = chunk_z * 16 + lz
            
            local c, temp, hum, river_val = get_biome_parameters(world_x, world_z)
            local surface_y = heights[lx][lz]
            local is_water_body = water_mask[lx][lz]

            -- Continuous Minecraft Biome Classification
            local is_desert = (temp > 0.68 and hum < 0.35)
            local is_swamp = (hum > 0.55 and temp >= 0.35 and c > -0.25)

            local top_block = is_desert and sand_id or grass_id
            local sub_block = is_desert and sand_id or dirt_id

            if is_swamp then
                top_block = (surface_y < sea_level) and dirt_id or grass_id
                sub_block = dirt_id
            end

            if is_water_body then
                local bed_rand = get_rand(world_x, world_z, 5.5)
                if is_swamp then
                    top_block = (bed_rand < 0.80) and dirt_id or gravel_id
                    sub_block = dirt_id
                else
                    if bed_rand < 0.6 then
                        top_block = sand_id
                        sub_block = sand_id
                    elseif bed_rand < 0.85 then
                        top_block = gravel_id
                        sub_block = dirt_id
                    else
                        top_block = dirt_id
                        sub_block = dirt_id
                    end
                end
            end

            for ly = 0, surface_y do
                if ly == 0 then
                    chunk:set_block(lx, ly, lz, bedrock_id)
                elseif ly == surface_y then
                    chunk:set_block(lx, ly, lz, top_block)
                elseif ly > surface_y - 4 then
                    chunk:set_block(lx, ly, lz, sub_block)
                else
                    if ly > 4 and ly < surface_y - 6 and is_cave(world_x, ly, world_z) then
                        -- Air for cave
                    else
                        chunk:set_block(lx, ly, lz, stone_id)
                    end
                end
            end

            if is_water_body then
                for wy = surface_y + 1, math.floor(sea_level) do
                    if wy < 256 then
                        chunk:set_block(lx, wy, lz, water_id)
                    end
                end
            end

            -- --- SURFACE FLORA ---
            if not is_water_body and surface_y >= math.floor(sea_level) and lx >= 1 and lx <= 14 and lz >= 1 and lz <= 14 then
                local flora_rand = get_rand(world_x, world_z, 1.1)

                if is_swamp then
                    if flora_rand < 0.22 and surface_y + 1 < 256 then
                        local type_rand = get_rand(world_x, world_z, 2.2)
                        local selected_id = short_grass_id

                        if type_rand < 0.12 and blue_orchid_id ~= 0 then
                            selected_id = blue_orchid_id
                        elseif type_rand < 0.20 then
                            selected_id = mushroom_brown_id
                        elseif type_rand < 0.28 then
                            selected_id = mushroom_red_id
                        end

                        if selected_id ~= 0 then
                            chunk:set_block(lx, surface_y + 1, lz, selected_id)
                        end
                    end

                    -- Shoreline Reeds Check
                    if surface_y == math.floor(sea_level) then
                        local is_near_water = false
                        local neighbor_offsets = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} }
                        
                        for _, offset in ipairs(neighbor_offsets) do
                            local n_wx = world_x + offset[1]
                            local n_wz = world_z + offset[2]
                            if is_world_water(n_wx, n_wz) then
                                is_near_water = true
                                break
                            end
                        end

                        if is_near_water then
                            local reed_rand = get_rand(world_x, world_z, 8.8)
                            if reed_rand < 0.15 and surface_y + 4 < 256 then
                                generate_reeds(chunk, lx, surface_y + 1, lz)
                            end
                        end
                    end

                    -- Swamp Oak Trees
                    if lx >= 2 and lx <= 13 and lz >= 2 and lz <= 13 then
                        local tree_rand = get_rand(world_x, world_z, 3.3)
                        if tree_rand < 0.018 and surface_y + 8 < 256 then
                            generate_swamp_tree(chunk, lx, surface_y + 1, lz)
                        end
                    end

                elseif not is_desert and top_block ~= sand_id then
                    if flora_rand < 0.22 and surface_y + 1 < 256 then
                        local type_rand = get_rand(world_x, world_z, 2.2)
                        local selected_id = short_grass_id

                        if type_rand < 0.04 then
                            selected_id = engine.get_block_id("flower_poppy")
                        elseif type_rand < 0.08 then
                            selected_id = engine.get_block_id("flower_daisy")
                        elseif type_rand < 0.12 then
                            selected_id = engine.get_block_id("flower_bluebell")
                        elseif type_rand < 0.16 then
                            selected_id = engine.get_block_id("flower_tulip_red")
                        end

                        if selected_id ~= 0 then
                            chunk:set_block(lx, surface_y + 1, lz, selected_id)
                        end
                    end

                    if surface_y == math.floor(sea_level) then
                        local is_near_water = false
                        local neighbor_offsets = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} }
                        
                        for _, offset in ipairs(neighbor_offsets) do
                            local n_wx = world_x + offset[1]
                            local n_wz = world_z + offset[2]
                            if is_world_water(n_wx, n_wz) then
                                is_near_water = true
                                break
                            end
                        end

                        if is_near_water then
                            local reed_rand = get_rand(world_x, world_z, 8.8)
                            if reed_rand < 0.12 and surface_y + 4 < 256 then
                                generate_reeds(chunk, lx, surface_y + 1, lz)
                            end
                        end
                    end

                    if lx >= 2 and lx <= 13 and lz >= 2 and lz <= 13 then
                        local tree_rand = get_rand(world_x, world_z, 3.3)
                        if tree_rand < 0.012 and surface_y + 9 < 256 then
                            local tree_type_rand = get_rand(world_x, world_z, 4.4)
                            if tree_type_rand < 0.4 then
                                generate_birch_tree(chunk, lx, surface_y + 1, lz)
                            else
                                generate_oak_tree(chunk, lx, surface_y + 1, lz)
                            end
                        end
                    end
                elseif is_desert then
                    if flora_rand < 0.003 and surface_y + 4 < 256 then
                        generate_cactus(chunk, lx, surface_y + 1, lz)
                    end
                end
            end
        end
    end
end

print("[Mapgen] Loaded Minecraft-Style Blended Biomes successfully.")