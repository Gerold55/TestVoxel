-- mapgen.lua - Minecraft-Style Biomes with Smooth Blending, Swamps & Coastal Cattails

local world_seed = 1337
local CHUNK_W = 16

-- Tree density per candidate column
local TREE_CHANCE_FOREST = 0.012
local TREE_CHANCE_SWAMP  = 0.030

-- Configure celestial textures
engine.set_sun_texture("cw_sun.png")

function set_world_seed(seed)
    if seed then world_seed = seed end
end

-- Terrain noise hash (sine-based). Used only for the smooth terrain shape.
function get_rand(x, z, seed_offset)
    local val = math.sin((x + world_seed * 0.1) * 12.9898 + (z + world_seed * 0.1) * 78.233 + seed_offset) * 43758.5453
    return val - math.floor(val)
end

-- Placement hash. Integer arithmetic (Park-Miller style), so there are no
-- diagonal bands or repeating rows. Use this for anything placed per block.
local HASH_M = 2147483647
function hash_rand(x, z, salt)
    local seed_term = ((world_seed % HASH_M) * 48271) % HASH_M
    local s = math.floor(salt * 100 + 0.5) % HASH_M
    local h = (math.floor(x) * 73856093 + math.floor(z) * 19349663 + s * 83492791 + seed_term) % HASH_M
    h = (h * 16807) % HASH_M
    h = (h * 16807) % HASH_M
    h = (h * 16807) % HASH_M
    return h / HASH_M
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
    return total / (maxValue > 0 and maxValue or 1)
end

function get_biome_parameters(world_x, world_z)
    local continent_scale = 0.001
    local biome_scale      = 0.002
    local river_scale      = 0.006

    local wx = world_x + (world_seed % 1000)
    local wz = world_z + (world_seed % 1000)

    local continentalness = octave_noise2d(wx, wz, 3, 0.5, continent_scale, 1.0) * 2.0 - 1.0
    local temperature      = octave_noise2d(wx, wz, 3, 0.5, biome_scale, 2.0)
    local humidity         = octave_noise2d(wx, wz, 3, 0.5, biome_scale, 4.0)
    local river_noise      = math.abs(octave_noise2d(wx, wz, 2, 0.5, river_scale, 3.0) * 2.0 - 1.0)

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

    -- Smooth swamp blend factor (warm + humid + low inland height)
    local swamp_factor = math.max(0.0, math.min(1.0, (humidity - 0.55) / 0.15)) *
                         math.max(0.0, math.min(1.0, (temperature - 0.35) / 0.15))

    -- Flatten swamps toward water level without step cuts
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

-- ---------------------------------------------------------------------------
-- Trees (Minecraft-Style Layered Canopies)
-- ---------------------------------------------------------------------------

local OAK_TREE = {
    log = "log_oak", leaves = "leaves_oak",
    log_fallback = 5, leaves_fallback = 6,
    trunk_min = 4, trunk_max = 6, seed = 21.3,
    canopy_start = 3, canopy_height = 3, radius = 2,
    vines = false,
}

local BIRCH_TREE = {
    log = "log_birch", leaves = "leaves_birch",
    log_fallback = 5, leaves_fallback = 6,
    trunk_min = 5, trunk_max = 7, seed = 33.7,
    canopy_start = 4, canopy_height = 4, radius = 2,
    vines = false,
}

local SWAMP_TREE = {
    log = "log_oak", leaves = "leaves_oak",
    log_fallback = 5, leaves_fallback = 6,
    trunk_min = 4, trunk_max = 5, seed = 45.1,
    canopy_start = 2, canopy_height = 3, radius = 3,
    vines = true,
}

local function id_or(name, fallback)
    local id = engine.get_block_id(name) or 0
    if id == 0 then return fallback end
    return id
end

-- Places a block only if the cell is inside this chunk and currently air.
local function place_if_air(chunk, bx, by, bz, block_id)
    if bx < 0 or bx >= CHUNK_W or bz < 0 or bz >= CHUNK_W or by < 0 or by >= 256 then
        return false
    end
    if chunk:get_block(bx, by, bz) == 0 then
        chunk:set_block(bx, by, bz, block_id)
        return true
    end
    return false
end

-- Minecraft-style layered tree generator
local function build_tree(chunk, lx, ly, lz, wx, wz, spec)
    local wood_id   = id_or(spec.log, spec.log_fallback)
    local leaves_id = id_or(spec.leaves, spec.leaves_fallback)

    local span = spec.trunk_max - spec.trunk_min + 1
    local trunk_h = spec.trunk_min + math.floor(hash_rand(wx, wz, spec.seed) * span)
    if trunk_h > spec.trunk_max then trunk_h = spec.trunk_max end

    for h = 0, trunk_h - 1 do
        if ly + h < 256 then
            chunk:set_block(lx, ly + h, lz, wood_id)
        end
    end

    local leaf_base_y = ly + spec.canopy_start
    local total_layers = spec.canopy_height

    for layer = 0, total_layers - 1 do
        local current_y = leaf_base_y + layer
        local current_radius = spec.radius
        if layer == total_layers - 1 then
            current_radius = spec.radius - 1
            if current_radius < 1 then current_radius = 1 end
        end

        for dx = -current_radius, current_radius do
            for dz = -current_radius, current_radius do
                local is_corner = (math.abs(dx) == current_radius and math.abs(dz) == current_radius)
                
                if not (is_corner and layer == 0 and spec.radius <= 2) then
                    local target_x = lx + dx
                    local target_z = lz + dz
                    if not (dx == 0 and dz == 0 and current_y < ly + trunk_h) then
                        place_if_air(chunk, target_x, current_y, target_z, leaves_id)
                    end
                end
            end
        end
    end

    local top_leaf_y = leaf_base_y + total_layers
    if top_leaf_y < 256 then
        place_if_air(chunk, lx, top_leaf_y, lz, leaves_id)
    end

    if spec.vines then
        local vine_id = id_or("vine", 0)
        if vine_id ~= 0 then
            local R = spec.radius
            for dx = -R, R do
                for dz = -R, R do
                    local vx, vy, vz = lx + dx, leaf_base_y, lz + dz
                    if vx >= 0 and vx < CHUNK_W and vz >= 0 and vz < CHUNK_W then
                        if chunk:get_block(vx, vy, vz) == leaves_id and hash_rand(wx + dx, wz + dz, spec.seed + 9.1) < 0.4 then
                            local len = 1 + math.floor(hash_rand(wx + dx, wz + dz, spec.seed + 17.3) * 3)
                            for h = 1, len do
                                if not place_if_air(chunk, vx, vy - h, vz, vine_id) then
                                    break
                                end
                            end
                        end
                    end
                end
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Flora
-- ---------------------------------------------------------------------------

function generate_cactus(chunk, x, y, z)
    local cactus_id = (engine.get_block_id("cactus") ~= 0 and engine.get_block_id("cactus")) or 12
    local cactus_height = 1 + math.floor(hash_rand(x, z, 9.9) * 3)
    if cactus_height > 3 then cactus_height = 3 end

    for h = 0, cactus_height - 1 do
        local ty = y + h
        if ty < 256 then chunk:set_block(x, ty, z, cactus_id) end
    end
end

function generate_reeds(chunk, x, y, z)
    local reed_id = (engine.get_block_id("reed") ~= 0 and engine.get_block_id("reed")) or (engine.get_block_id("short_grass") or 1)
    local reed_top_id = (engine.get_block_id("reed_top") ~= 0 and engine.get_block_id("reed_top")) or reed_id

    local target_height = 2 + math.floor(hash_rand(x, z, 7.7) * 2)
    if target_height > 3 then target_height = 3 end

    for h = 0, target_height - 1 do
        local ty = y + h
        if ty < 256 then
            local block_to_place = (h == target_height - 1) and reed_top_id or reed_id
            chunk:set_block(x, ty, z, block_to_place)
        end
    end
end

function generate_cattails(chunk, lx, start_y, lz)
    local cattail_id = (engine.get_block_id("cattail") ~= 0 and engine.get_block_id("cattail")) or (engine.get_block_id("reed") or 1)
    local cattail_top_id = (engine.get_block_id("cattail_top") ~= 0 and engine.get_block_id("cattail_top")) or cattail_id

    local bottom_y = start_y
    local top_y = start_y + 1

    if top_y < 256 then
        if chunk.set_block_with_meta then
            chunk:set_block_with_meta(lx, bottom_y, lz, cattail_id, 1)
        else
            chunk:set_block(lx, bottom_y, lz, cattail_id)
            if chunk.set_metadata then
                chunk:set_metadata(lx, bottom_y, lz, 1)
            end
        end

        chunk:set_block(lx, top_y, lz, cattail_top_id)
    end
end

local function try_generate_reeds(chunk, lx, lz, world_x, world_z, surface_y)
    if surface_y + 4 >= 256 then return end

    local is_near_water = false
    local neighbor_offsets = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} }

    for _, offset in ipairs(neighbor_offsets) do
        if is_world_water(world_x + offset[1], world_z + offset[2]) then
            is_near_water = true
            break
        end
    end

    if is_near_water then
        local reed_rand = hash_rand(world_x, world_z, 8.8)
        if reed_rand < 0.15 then
            generate_reeds(chunk, lx, surface_y + 1, lz)
        end
    end
end

local function try_generate_cattails(chunk, lx, lz, world_x, world_z, surface_y, sea_level, continentalness)
    local water_depth = math.floor(sea_level) - surface_y

    if water_depth ~= 1 then return end
    if continentalness < -0.3 then return end

    local is_near_bank = false
    local neighbor_offsets = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} }

    for _, offset in ipairs(neighbor_offsets) do
        local n_wx = world_x + offset[1]
        local n_wz = world_z + offset[2]

        local c, t, h, r = get_biome_parameters(n_wx, n_wz)
        local n_height = get_terrain_height(n_wx, n_wz, c, t, h, r)
        if n_height >= math.floor(sea_level) then
            is_near_bank = true
            break
        end
    end

    if is_near_bank then
        local cattail_rand = hash_rand(world_x, world_z, 11.2)
        if cattail_rand < 0.25 then
            generate_cattails(chunk, lx, surface_y + 1, lz)
        end
    end
end

-- ---------------------------------------------------------------------------
-- Chunk generation
-- ---------------------------------------------------------------------------

function generate_chunk(chunk, chunk_x, chunk_z)
    local stone_id = (engine.get_block_id("stone") ~= 0 and engine.get_block_id("stone")) or 2
    local bedrock_id = (engine.get_block_id("bedrock") ~= 0 and engine.get_block_id("bedrock")) or 2

    local grass_id = (engine.get_block_id("grass") ~= 0 and engine.get_block_id("grass")) or 4
    local dirt_id = (engine.get_block_id("dirt") ~= 0 and engine.get_block_id("dirt")) or 3
    local sand_id = (engine.get_block_id("sand") ~= 0 and engine.get_block_id("sand")) or 7
    local gravel_id = (engine.get_block_id("gravel") ~= 0 and engine.get_block_id("gravel")) or 13
    local water_id = (engine.get_block_id("water") ~= 0 and engine.get_block_id("water")) or 9

    local mushroom_red_id = (engine.get_block_id("mushroom_red") ~= 0 and engine.get_block_id("mushroom_red")) or (engine.get_block_id("flower_poppy") or 0)
    local mushroom_brown_id = (engine.get_block_id("mushroom_brown") ~= 0 and engine.get_block_id("mushroom_brown")) or (engine.get_block_id("flower_daisy") or 0)
    local blue_orchid_id = engine.get_block_id("flower_bluebell") or 0

    local short_grass_id = engine.get_block_id("short_grass") or 0
    local lily_pad_id = engine.get_block_id("lily_pad") or 0
    local sea_level = 62.0

    local pending_trees = {}
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

            local is_desert = (temp > 0.68 and hum < 0.35)
            local is_swamp = (hum > 0.55 and temp >= 0.35 and c > -0.25)

            local top_block = is_desert and sand_id or grass_id
            local sub_block = is_desert and sand_id or dirt_id

            if is_swamp then
                top_block = (surface_y < sea_level) and dirt_id or grass_id
                sub_block = dirt_id
            end

            if is_water_body then
                local depth = math.floor(sea_level) - surface_y
                local bed_noise = octave_noise2d(world_x * 0.05, world_z * 0.05, 2, 0.5, 1.0, 99.0)

                if is_swamp then
                    if bed_noise < 0.6 then
                        top_block = dirt_id
                        sub_block = dirt_id
                    elseif bed_noise < 0.85 then
                        top_block = gravel_id
                        sub_block = dirt_id
                    else
                        top_block = (engine.get_block_id("clay") ~= 0 and engine.get_block_id("clay")) or dirt_id
                        sub_block = dirt_id
                    end
                else
                    if depth <= 3 then
                        top_block = sand_id
                        sub_block = sand_id
                    else
                        if bed_noise < 0.45 then
                            top_block = gravel_id
                            sub_block = dirt_id
                        elseif bed_noise < 0.75 then
                            top_block = sand_id
                            sub_block = sand_id
                        else
                            top_block = stone_id
                            sub_block = stone_id
                        end
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

                if lx >= 1 and lx <= 14 and lz >= 1 and lz <= 14 then
                    try_generate_cattails(chunk, lx, lz, world_x, world_z, surface_y, sea_level, c)

                    if is_swamp and lily_pad_id ~= 0 then
                        local lily_rand = hash_rand(world_x, world_z, 12.7)
                        if lily_rand < 0.06 and math.floor(sea_level) + 1 < 256 then
                            chunk:set_block(lx, math.floor(sea_level) + 1, lz, lily_pad_id)
                        end
                    end
                end
            end

            if not is_water_body and surface_y >= math.floor(sea_level) and lx >= 1 and lx <= 14 and lz >= 1 and lz <= 14 then
                local flora_rand = hash_rand(world_x, world_z, 1.1)

                if is_swamp then
                    if flora_rand < 0.22 and surface_y + 1 < 256 then
                        local type_rand = hash_rand(world_x, world_z, 2.2)
                        local selected_id = short_grass_id

                        if type_rand < 0.12 and blue_orchid_id and blue_orchid_id ~= 0 then
                            selected_id = blue_orchid_id
                        elseif type_rand < 0.20 and mushroom_brown_id and mushroom_brown_id ~= 0 then
                            selected_id = mushroom_brown_id
                        elseif type_rand < 0.28 and mushroom_red_id and mushroom_red_id ~= 0 then
                            selected_id = mushroom_red_id
                        end

                        if selected_id and selected_id ~= 0 then
                            chunk:set_block(lx, surface_y + 1, lz, selected_id)
                        end
                    end

                    if surface_y == math.floor(sea_level) then
                        try_generate_reeds(chunk, lx, lz, world_x, world_z, surface_y)
                    end

                    if lx >= 3 and lx <= 12 and lz >= 3 and lz <= 12 and surface_y + 9 < 256 then
                        if hash_rand(world_x, world_z, 6.6) < TREE_CHANCE_SWAMP then
                            pending_trees[#pending_trees + 1] = {
                                lx = lx, ly = surface_y + 1, lz = lz,
                                wx = world_x, wz = world_z,
                                spec = SWAMP_TREE,
                            }
                        end
                    end

                elseif not is_desert and top_block ~= sand_id then
                    if flora_rand < 0.22 and surface_y + 1 < 256 then
                        local type_rand = hash_rand(world_x, world_z, 2.2)
                        local selected_id = short_grass_id

                        if type_rand < 0.04 then
                            selected_id = engine.get_block_id("flower_poppy") or 0
                        elseif type_rand < 0.08 then
                            selected_id = engine.get_block_id("flower_daisy") or 0
                        elseif type_rand < 0.12 then
                            selected_id = engine.get_block_id("flower_bluebell") or 0
                        elseif type_rand < 0.16 then
                            selected_id = engine.get_block_id("flower_tulip_red") or 0
                        end

                        if selected_id and selected_id ~= 0 then
                            chunk:set_block(lx, surface_y + 1, lz, selected_id)
                        end
                    end

                    if surface_y == math.floor(sea_level) then
                        try_generate_reeds(chunk, lx, lz, world_x, world_z, surface_y)
                    end

                    if lx >= 3 and lx <= 12 and lz >= 3 and lz <= 12 and surface_y + 9 < 256 then
                        if hash_rand(world_x, world_z, 3.3) < TREE_CHANCE_FOREST then
                            local tree_type_rand = hash_rand(world_x, world_z, 4.4)
                            pending_trees[#pending_trees + 1] = {
                                lx = lx, ly = surface_y + 1, lz = lz,
                                wx = world_x, wz = world_z,
                                spec = (tree_type_rand < 0.4) and BIRCH_TREE or OAK_TREE,
                            }
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

    for _, t in ipairs(pending_trees) do
        build_tree(chunk, t.lx, t.ly, t.lz, t.wx, t.wz, t.spec)
    end
end

print("[Mapgen] Loaded Minecraft-Style Blended Biomes with swamps successfully.")