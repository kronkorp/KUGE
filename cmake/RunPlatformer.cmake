# Plays the platformer (scripted, on SDL's dummy screen) and checks that it ran
# and drew the level: sky, ground and the hero.
#
#     cmake -DGAME=<path> -DOUT=<file.ppm> -P RunPlatformer.cmake

set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
file(REMOVE "${OUT}")

execute_process(
    COMMAND "${GAME}" --frames 400 --screenshot "${OUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE  errors
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the platformer exited with '${result}':\n${output}${errors}")
endif()
if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "the platformer left no picture")
endif()

# "P6\n640 360\n255\n" then 640 * 360 pixels of 3 bytes
file(SIZE "${OUT}" size)
math(EXPR expected "15 + 640 * 360 * 3")
if(NOT size EQUAL expected)
    message(FATAL_ERROR "the picture is ${size} bytes, expected ${expected}")
endif()

file(READ "${OUT}" pixels HEX)
foreach(colour 6eaae6 aa463c 56aa46 e1c8b4)   # sky, bricks, grass, the hero's face
    string(FIND "${pixels}" "${colour}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "the colour ${colour} is not in the picture: the level was not drawn")
    endif()
endforeach()
