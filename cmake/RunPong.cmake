# Plays a scripted game of Pong on SDL's dummy screen, and checks that it ran and
# left a picture of the right size, with something drawn on it.
#
#     cmake -DPONG=<path> -DOUT=<file.ppm> -P RunPong.cmake

set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
file(REMOVE "${OUT}")

execute_process(
    COMMAND "${PONG}" --frames 90 --screenshot "${OUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE  errors
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "pong exited with '${result}':\n${output}${errors}")
endif()
if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "pong left no picture")
endif()

# "P6\n800 600\n255\n" then 800 * 600 pixels of 3 bytes
file(SIZE "${OUT}" size)
math(EXPR expected "15 + 800 * 600 * 3")
if(NOT size EQUAL expected)
    message(FATAL_ERROR "the picture is ${size} bytes, expected ${expected}")
endif()

# The background is dark: there must be white in the picture (paddles, digits)
file(READ "${OUT}" pixels HEX)
string(FIND "${pixels}" "ffffff" white)
if(white EQUAL -1)
    message(FATAL_ERROR "nothing white was drawn: the game did not render")
endif()
