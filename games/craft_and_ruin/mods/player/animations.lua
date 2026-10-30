-- Load the model
local playerModel = engine.load_model("models/player_model.json")

-- Grab specific bones
local rightArm = playerModel:get_bone("right_arm")
local leftArm = playerModel:get_bone("left_arm")
local head = playerModel:get_bone("head")

-- Called every frame by the C++ engine
function update_player_animation(dt, is_moving, camera_pitch)
    -- Point the head up/down based on camera view
    head:set_rotation(camera_pitch, 0, 0)

    if is_moving then
        -- Swinging arms dynamically using math
        local swing = math.sin(os.clock() * 12) * 35
        rightArm:set_rotation(swing, 0, 0)
        leftArm:set_rotation(-swing, 0, 0)
    else
        -- Return to idle smoothly
        rightArm:set_rotation(0, 0, 0)
        leftArm:set_rotation(0, 0, 0)
    end
end