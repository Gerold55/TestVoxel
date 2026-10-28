print("[Craft & Ruin] Registering core block types...")

-- Non-rendered Air Block
engine.register_block("air", 0.0, 0.0, 0.0, nil, nil, {
    transparent = true,
    walkable = false,
    selectable = false
})

-- Standard Opaque Blocks
engine.register_block("stone", 1.0, 1.0, 1.0, "textures/cw_stone.png", nil, {
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("dirt", 1.0, 1.0, 1.0, "textures/cw_dirt.png", nil, {
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Grass with top, bottom, and side overlays
engine.register_block("grass", 0.35, 0.75, 0.25, {
    top    = "textures/cw_grass_top.png",
    bottom = "textures/cw_dirt.png",
    sides  = "textures/cw_dirt.png"
}, {
    sides  = "textures/cw_grass_side_overlay.png"
}, {
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Oak Log
engine.register_block("log_oak", 1.0, 1.0, 1.0, {
    top    = "textures/cw_oak_log_top.png",
    bottom = "textures/cw_oak_log_top.png",
    sides  = "textures/cw_oak_log.png"
}, nil, {
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Oak Leaves
engine.register_block("leaves_oak", 0.2, 0.8, 0.2, "textures/cw_oak_leaves.png", nil, {
    transparent = true,
    walkable = true,
    behavior = "solid",
    tint = true
})

-- Birch Log
engine.register_block("log_birch", 1.0, 1.0, 1.0, {
    top    = "textures/cw_birch_log_top.png",
    bottom = "textures/cw_birch_log_top.png",
    sides  = "textures/cw_birch_log.png"
}, nil, {
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Birch Leaves (Golden Autumn Tint)
engine.register_block("leaves_birch", 1.0, 0.85, 0.1, "textures/cw_birch_leaves.png", nil, {
    transparent = true,
    walkable = true,
    behavior = "solid",
    tint = true
})

-- Static Water Source Block
engine.register_block("water", 0.2, 0.4, 0.9, {
    top = "textures/ws_water_source_animated.png",
    bottom = "textures/ws_water_source_animated.png",
    sides = "textures/ws_water_source_animated.png"
}, nil, {
    transparent = true,
    walkable = false,
    selectable = true,
    behavior = "liquid",
    tint = false
})

-- Flowing Water Stream Block (Uses flowing textures)
engine.register_block("flowing_water", 0.2, 0.4, 0.9, {
    top = "textures/ws_water_flowing_animated.png",
    bottom = "textures/ws_water_flowing_animated.png",
    sides = "textures/ws_water_flowing_animated.png"
}, nil, {
    transparent = true,
    walkable = false,
    selectable = true,
    behavior = "liquid",
    tint = false,
    in_creative = false
})

-- Register Bedrock (Unbreakable bottom layer)
engine.register_block("bedrock", 0.2, 0.2, 0.2, "cw_bedrock.png", nil, {
    walkable = true,
    selectable = false
})

-- Register Sand (Desert / Beach surface layer)
engine.register_block("sand", 0.85, 0.82, 0.65, "cw_sand.png", nil, {
    walkable = true,
    tint = false,
    gravity = true
})

-- Register Gravel (Rough underground / cave patch blocks)
engine.register_block("gravel", 0.5, 0.5, 0.5, "cw_gravel.png", nil, {
    walkable = true,
    tint = false,
    gravity = true
})

-- --- NEW: Vegetation Blocks ---

-- Short Grass (Cross-plane plantlike pattern with biome tinting)
engine.register_block("short_grass", 0.35, 0.75, 0.25, "textures/cw_grass.png", nil, {
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = true
})

-- Reeds Lower/Middle Stalk
engine.register_block("reed", 0.8, 0.9, 0.8, {
    top    = "textures/cw_reeds.png",
    bottom = "textures/cw_reeds.png",
    sides  = "textures/cw_reeds.png"
}, nil, {
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

-- Reeds Top Tip Stalk
engine.register_block("reed_top", 0.8, 0.9, 0.8, {
    top    = "textures/cw_reeds_top.png",
    bottom = "textures/cw_reeds.png",
    sides  = "textures/cw_reeds_top.png"
}, nil, {
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false,
    in_creative = false
})

-- Register Cactus (Desert column block)
engine.register_block("cactus", 0.3, 0.7, 0.3, {
    top    = "textures/cw_cactus_top.png",
    bottom = "textures/cw_cactus_bottom.png",
    sides  = "textures/cw_cactus_side.png"
}, nil, {
    transparent = false,
    walkable = true,
    selectable = true,
    behavior = "solid",
    in_creative = true
})

-- Red Flower (Cross-plane plantlike pattern without biome tinting)
engine.register_block("poppy", 1.0, 1.0, 1.0, "textures/cw_flower_poppy.png", nil, {
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

engine.register_block("flower_daisy", 1.0, 1.0, 1.0, "textures/cw_flower_daisy.png", nil, {
    transparent = true, 
    walkable = false, 
    selectable = true, 
    gravity = false, 
    pattern = "plantlike", 
    tint = false
})

engine.register_block("flower_bluebell", 1.0, 1.0, 1.0, "textures/cw_flower_bluebell.png", nil, {
    transparent = true, walkable = false, selectable = true, gravity = false, pattern = "plantlike", tint = false
})

engine.register_block("flower_azure_bluet", 1.0, 1.0, 1.0, "textures/cw_flower_azure_bluet.png", nil, {
    transparent = true, walkable = false, selectable = true, gravity = false, pattern = "plantlike", tint = false
})

engine.register_block("flower_allium", 1.0, 1.0, 1.0, "textures/cw_flower_allium.png", nil, {
    transparent = true, walkable = false, selectable = true, gravity = false, pattern = "plantlike", tint = false
})

engine.register_block("flower_tulip_white", 1.0, 1.0, 1.0, "textures/cw_flower_tulip_white.png", nil, {
    transparent = true, walkable = false, selectable = true, gravity = false, pattern = "plantlike", tint = false
})

engine.register_block("flower_tulip_red", 1.0, 1.0, 1.0, "textures/cw_flower_tulip_red.png", nil, {
    transparent = true, walkable = false, selectable = true, gravity = false, pattern = "plantlike", tint = false
})

engine.register_block("flower_tulip_pink", 1.0, 1.0, 1.0, "textures/cw_flower_tulip_pink.png", nil, {
    transparent = true, walkable = false, selectable = true, gravity = false, pattern = "plantlike", tint = false
})

-- Load separate UI script
dofile(g_CurrentModPath .. "/ui.lua")
dofile(g_CurrentModPath .. "/pause_menu.lua")