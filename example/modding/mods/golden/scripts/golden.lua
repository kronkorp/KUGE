-- The "golden" mod: you are faster (data/catch.json) and, every 5 seconds, a gold coin falls twice as fast and is worth 5 points.
--
-- A script sees only what the game described: Transform2D and Faller (see makeSetup in Catch.hpp). The game gives
-- every Faller its look, so the script does not draw anything.

local EVERY = 5.0
local timer = 0.0

function update(dt)
    timer = timer + dt
    if timer >= EVERY then
        timer = timer - EVERY
        world:spawn{
            Transform2D = {position = {math.random(-280, 280), -250}},
            Faller = {speed = 240, value = 5},
        }
    end
end
