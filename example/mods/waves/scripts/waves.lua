-- A mod that adds a behaviour with no C++ and no rebuild: every two seconds an enemy comes in from the right at a random
-- height and drifts to the left; it is removed once it is out of the screen.
--
-- It needs a game that describes these components to its ComponentRegistry:
--   Transform2D  {position = {x, y}}   (kuge::describeCore does it)
--   Enemy        {speed = <number>}    (the game's own)

local EVERY = 2.0       -- seconds between two enemies
local LEFT = -10.0      -- where an enemy is gone
local timer = 0.0

function update(dt)
    timer = timer + dt
    if timer >= EVERY then
        timer = timer - EVERY
        -- math.random is KUGE's, seeded from the tick: the room and a replay get the same heights
        world:spawn{
            Transform2D = {position = {10.0, math.random(-5, 5)}},
            Enemy = {speed = 2.0 + math.random()},
        }
    end

    for _, e in ipairs(world:view{"Transform2D", "Enemy"}) do
        local position = world:get(e, "Transform2D", "position")
        local x = position[1] - world:get(e, "Enemy", "speed") * dt

        if x < LEFT then
            world:remove(e)
        else
            world:set(e, "Transform2D", "position", {x, position[2]})
        end
    end
end
