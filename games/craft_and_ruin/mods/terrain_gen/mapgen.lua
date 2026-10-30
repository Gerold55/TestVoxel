-- mapgen.lua - Balanced Biomes, Dense Short Grass, Sparse Reeds, & Sparse Cacti

local world_seed = 1337

-- Configure celestial textures
engine.set_sun_texture("cw_sun.png")

function set_world_seed(seed)
    if seed then world_seed = seed end
end

function get_rand(x, z, seed_offset)
    local val = math.sin((x + world_seed * 0.1) * 12.9898 + (z + world_seed * 0.1) * 78.233 + seed_offset) * 43758.5453
    return val - math.floor(val)
end

function get_biome_parameters(world_x, world_z)
    local continent_scale = 0.00025  
    local biome_scale = 0.0006      
    local river_scale = 0.002       

    local ox = world_x + (world_seed % 1000)
    local oz = world_z + (world_seed % 1000)

    local continentalness = math.sin(ox * continent_scale) * math.cos(oz * continent_scale * 0.7) 
        + (math.sin(ox * continent_scale * 2.1) * math.cos(oz * continent_scale * 2.1)) * 0.25
    continentalness = continentalness / 1.25

    local temperature = math.sin(ox * biome_scale) * math.cos(oz * biome_scale * 0.8) * 0.5 + 0.5
    local river_noise = math.abs(math.sin(ox * river_scale) * math.cos(oz * river_scale * 1.1))

    return continentalness, temperature, river_noise
end

function get_terrain_height(world_x, world_z, continentalness, temperature, river_val)
    local scale_hills = 0.005
    local scale_detail = 0.02

    local ox = world_x + (world_seed % 1000)
    local oz = world_z + (world_seed % 1000)

    local h1 = math.sin(ox * scale_hills) * math.cos(oz * scale_hills) * 16.0
    local h2 = math.sin(ox * scale_detail + 1.2) * math.sin(oz * scale_detail + 0.8) * 3.0
    
    local sea_level = 62.0
    local base_height = sea_level + (continentalness * 35.0)

    local temp_factor = math.max(0.0, math.min(1.0, (temperature - 0.58) / 0.15))
    if temp_factor > 0.0 then
        h1 = h1 * (1.0 - (0.5 * temp_factor))
        h2 = h2 * (1.0 - (0.3 * temp_factor))
    end

    local height = base_height + h1 + h2

    if continentalness < -0.15 then
        local depth_factor = (-0.15 - continentalness)
        local ocean_depth = (depth_factor * depth_factor * 120.0) + (depth_factor * 25.0)
        height = height - ocean_depth
    end

    if continentalness > -0.05 and river_val < 0.05 then
        local river_depth_factor = (0.05 - river_val) / 0.05
        height = height - (river_depth_factor * 9.0)
        if height < sea_level - 3.0 then
            height = sea_level - 3.0
        end
    end

    return math.floor(height)
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

    local short_grass_id = engine.get_block_id("short_grass")
    local sea_level = 62.0

    local heights = {}
    local water_mask = {}
    local desert_mask = {}

    for lx = 0, 15 do
        heights[lx] = {}
        water_mask[lx] = {}
        desert_mask[lx] = {}
        for lz = 0, 15 do
            local world_x = chunk_x * 16 + lx
            local world_z = chunk_z * 16 + lz
            local continentalness, temp, river_val = get_biome_parameters(world_x, world_z)
            local surface_y = get_terrain_height(world_x, world_z, continentalness, temp, river_val)
            heights[lx][lz] = surface_y
            water_mask[lx][lz] = (surface_y < sea_level)
            desert_mask[lx][lz] = (temp > 0.65)
        end
    end

    for lx = 0, 15 do
        for lz = 0, 15 do
            local world_x = chunk_x * 16 + lx
            local world_z = chunk_z * 16 + lz
            
            local surface_y = heights[lx][lz]
            local is_desert = desert_mask[lx][lz]
            local is_water_body = water_mask[lx][lz]

            local top_block = is_desert and sand_id or grass_id
            local sub_block = is_desert and sand_id or dirt_id

            if is_water_body then
                local bed_rand = get_rand(world_x, world_z, 5.5)
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

            -- --- SURFACE FLORA & FEATURES (Safe from chunk boundary cuts with margin [1 to 14]) ---
            if not is_water_body and surface_y >= math.floor(sea_level) and lx >= 1 and lx <= 14 and lz >= 1 and lz <= 14 then
                local flora_rand = get_rand(world_x, world_z, 1.1)

                if not is_desert and top_block ~= sand_id then
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

                    -- Shoreline Reeds Check (Sparse Minecraft-style)
                    if surface_y == math.floor(sea_level) then
                        local is_near_water = false
                        local neighbor_offsets = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} }
                        
                        for _, offset in ipairs(neighbor_offsets) do
                            local nx = lx + offset[1]
                            local nz = lz + offset[2]
                            if nx >= 0 and nx < 16 and nz >= 0 and nz < 16 then
                                if water_mask[nx][nz] then
                                    is_near_water = true
                                    break
                                end
                            end
                        end

                        if is_near_water then
                            local reed_rand = get_rand(world_x, world_z, 8.8)
                            if reed_rand < 0.12 and surface_y + 4 < 256 then
                                generate_reeds(chunk, lx, surface_y + 1, lz)
                            end
                        end
                    end

                    -- Trees (Restricted to inner chunk boundary [2 to 13])
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
                    -- Sparse cacti (1 to 3 blocks tall, rare frequency)
                    if flora_rand < 0.003 and surface_y + 4 < 256 then
                        generate_cactus(chunk, lx, surface_y + 1, lz)
                    end
                end
            end
        end
    end
end

print("[Mapgen] Loaded Complete Polished Mapgen successfully.")