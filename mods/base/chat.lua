-- chat.lua: chat commands for the base mod.

function on_chat_command(cmd)
    local name, args = cmd:match("^(%S+)%s*(.*)$")
    if not name then return false end

    if name == "time" then
        if args == "day" then
            sky.set_time(0.25)
            chat.send("Set time to day")
            return true
        elseif args == "night" then
            sky.set_time(0.75)
            chat.send("Set time to night")
            return true
        end
        chat.send("Usage: /time day or /time night", 1.0, 0.55, 0.55)
        return true
    end

    if name == "rain" then
        clouds.set_weather("rain", 10)
        chat.send("Rain starting")
        return true
    end

    return false   -- not ours; the engine shows "Unknown command"
end