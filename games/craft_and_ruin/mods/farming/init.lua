-- Craft & Ruin Farming Mod (Minecraft-Style + Scythe Harvesting)

print("[Mod] Loading Advanced Farming System with Scythe Support...")

-- 1. Register Farmland Blocks
engine.register_block("farmland_dry", 0.5, 0.4, 0.3, {
    top = "farmland_dry.png",
    sides = "dirt.png",
    bottom = "dirt.png"
}, nil, {
    transparent = false,
    walkable = true,
    pattern = "cube",
    in_creative = true
})

engine.register_block("farmland_wet", 0.35, 0.28, 0.18, {
    top = "farmland_wet.png",
    sides = "dirt.png",
    bottom = "dirt.png"
}, nil, {
    transparent = false,
    walkable = true,
    pattern = "cube",
    in_creative = true
})

-- 2. Register Crops across Growth Stages (Hidden from Creative Inventory)
engine.register_block("wheat_stage_0", 0.4, 0.8, 0.2, "cw_farming_wheat_1.png", nil, {
    transparent = true, walkable = false, selectable = true, pattern = "plantlike", in_creative = false
})
engine.register_block("wheat_stage_1", 0.5, 0.9, 0.2, "cw_farming_wheat_2.png", nil, {
    transparent = true, walkable = false, selectable = true, pattern = "plantlike", in_creative = false
})
engine.register_block("wheat_stage_2", 0.7, 0.95, 0.15, "cw_farming_wheat_3.png", nil, {
    transparent = true, walkable = false, selectable = true, pattern = "plantlike", in_creative = false
})
engine.register_block("wheat_ripe", 0.95, 0.9, 0.1, "cw_farming_wheat_4.png", nil, {
    transparent = true, walkable = false, selectable = true, pattern = "plantlike", in_creative = false
})

-- Register Seeds and Harvested Wheat Item (Visible in Creative)
engine.register_block("wheat_seeds", 0.4, 0.8, 0.2, "wheat_seeds.png", nil, {
    transparent = true, pattern = "plantlike", walkable = false, in_creative = true
})
engine.register_block("wheat", 0.9, 0.8, 0.1, "wheat_item.png", nil, {
    transparent = true, pattern = "plantlike", walkable = false, in_creative = true
})

-- Register Scythe Tool (Visible in Creative)
engine.register_block("scythe", 0.7, 0.7, 0.7, "scythe.png", nil, {
    transparent = true, pattern = "plantlike", walkable = false, creative = true
})

-- 3. Hoe Tilling Interaction
function on_use_hoe(x, y, z, block_name)
    if block_name == "grass" or block_name == "dirt" then
        engine.set_block(x, y, z, "farmland_dry")
        return true 
    end
    return false
end

-- 4. Right-Click Interactions (Planting & Hand Harvesting)
function on_right_click_block(x, y, z, block_name, held_item)
    -- Planting seeds on farmland
    if block_name == "farmland_dry" or block_name == "farmland_wet" then
        local above_block = engine.get_block_name(x, y + 1, z)
        if held_item == "wheat_seeds" and above_block == "air" then
            engine.set_block(x, y + 1, z, "wheat_stage_0")
            return true
        end
    end

    -- Normal right-click harvesting of mature wheat (resets to stage 0 automatically)
    if block_name == "wheat_ripe" then
        engine.set_block(x, y, z, "wheat_stage_0")
        print("[Farming] Harvested wheat! Automatically left seeds in place.")
        return true
    end

    return false
end

-- 5. Scythe Interaction Hook (Called when left or right-clicking with a scythe)
-- You can wire this into your C++ block break / left-click handler if checking held items!
function on_use_scythe(x, y, z, block_name)
    if block_name == "wheat_ripe" then
        -- Instantly harvest the mature crop and reset it back to stage 0
        -- Without destroying the underlying farmland or requiring manual replanting!
        engine.set_block(x, y, z, "wheat_stage_0")
        print("[Scythe] Reaped mature wheat and auto-replanted stage 0!")
        return true
    end
    return false
end

-- 6. Helper function for water hydration check
function check_water_nearby(cx, cy, cz)
    for dx = -4, 4 do
        for dy = -1, 1 do
            for dz = -4, 4 do
                local neighbor = engine.get_block_name(cx + dx, cy + dy, cz + dz)
                if neighbor == "water" or neighbor == "flowing_water" then
                    return true
                end
            end
        end
    end
    return false
end