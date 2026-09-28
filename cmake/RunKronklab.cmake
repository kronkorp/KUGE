# Runs a kronklab test executable and fails if a test failed or crashed.
#
# kronklab's main() always returns 0, so the exit code says nothing: the result
# is read from its report line instead.
#
#     cmake -DTEST_EXE=<path> -P RunKronklab.cmake

execute_process(
    COMMAND "${TEST_EXE}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE  errors
    RESULT_VARIABLE result
)
message("${output}${errors}")

string(ASCII 27 ESC)
string(REGEX REPLACE "${ESC}\\[[0-9;]*m" "" clean "${output}")

if(NOT result EQUAL 0)
    message(FATAL_ERROR "${TEST_EXE} exited with '${result}'")
endif()

if(NOT clean MATCHES "total \\[([0-9]+)\\] \\| passed \\[([0-9]+)\\] \\| failed \\[([0-9]+)\\] \\| crashed \\[([0-9]+)\\]")
    message(FATAL_ERROR "${TEST_EXE}: no kronklab report at the end of its output")
endif()
set(total   ${CMAKE_MATCH_1})
set(passed  ${CMAKE_MATCH_2})
set(failed  ${CMAKE_MATCH_3})
set(crashed ${CMAKE_MATCH_4})

if(total EQUAL 0)
    message(FATAL_ERROR "${TEST_EXE}: no test ran")
endif()
if(NOT failed EQUAL 0 OR NOT crashed EQUAL 0 OR NOT passed EQUAL total)
    message(FATAL_ERROR "${TEST_EXE}: ${passed}/${total} passed, ${failed} failed, ${crashed} crashed")
endif()
