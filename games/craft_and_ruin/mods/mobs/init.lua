-- Load the .b3d cow model
g_CowModel = engine.load_model("mods/mobs/models/mobs_mc_cow.b3d")

-- Example animation/update function called every frame from C++
function update_player_animation(dt, isMoving, pitch)
    -- You can manipulate cow bones or transform them here!
    local headBone = g_CowModel:get_bone("head")
    if headBone ~= nil then
        headBone:set_rotation(pitch * 0.5, 0, 0)
    end
end