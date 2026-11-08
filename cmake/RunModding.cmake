# Plays Catch for 400 frames on SDL's dummy screen, with its mod and without, and checks what the mod
# changed: a gold coin (the mod's script made it) is in the picture with the mod, and not without it.
#
#     cmake -DGAME=<path> -DOUT=<folder> -P RunModding.cmake

set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
file(MAKE_DIRECTORY "${OUT}")

# The first gold coin falls at 5 s (frame 300) and is still in the air 100 frames later; nobody moves the paddle
foreach(variant with without)
    set(picture "${OUT}/modding_${variant}.ppm")
    file(REMOVE "${picture}")
    if(variant STREQUAL "with")
        set(flags "")
    else()
        set(flags --no-mods)
    endif()
    execute_process(
        COMMAND "${GAME}" ${flags} --frames 400 --screenshot "${picture}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE  errors
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "catch (${variant} the mod) exited with '${result}':\n${output}${errors}")
    endif()
    if(NOT EXISTS "${picture}")
        message(FATAL_ERROR "catch (${variant} the mod) left no picture")
    endif()
    file(READ "${picture}" pixels HEX)
    string(FIND "${pixels}" "ffffff" white)
    if(white EQUAL -1)
        message(FATAL_ERROR "nothing white was drawn (the paddle): the game did not render")
    endif()
    string(FIND "${pixels}" "ffc800" gold)
    set(gold_${variant} ${gold})
    set(log_${variant} "${output}${errors}")
endforeach()

if(gold_with EQUAL -1)
    message(FATAL_ERROR "the mod's gold coin is not in the picture:\n${log_with}")
endif()
if(NOT gold_without EQUAL -1)
    message(FATAL_ERROR "a gold coin is drawn without the mod")
endif()
if(NOT log_with MATCHES "golden")
    message(FATAL_ERROR "the log does not say that the mod was loaded:\n${log_with}")
endif()
