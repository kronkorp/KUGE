# A module only sees the headers of what it depends on. Each case is built by
# ctest itself: "ok" must build, "bad" must fail because the header is not
# there (not for any other reason, hence the expected message).

function(kuge_boundary name source link)
    add_executable(boundary_${name} EXCLUDE_FROM_ALL ${source})
    target_link_libraries(boundary_${name} PRIVATE ${link})
    add_test(NAME boundary_${name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target boundary_${name})
endfunction()

# Positive control: core sees the logger it depends on
kuge_boundary(core_sees_logger core_sees_logger.cpp kuge-core)

# The logger does not depend on core, and core does not depend on net
kuge_boundary(logger_not_core logger_sees_core.cpp kuge-logger)
set_tests_properties(boundary_logger_not_core PROPERTIES
    PASS_REGULAR_EXPRESSION "Engine.hpp: No such file or directory")

kuge_boundary(core_not_net core_sees_net.cpp kuge-core)
set_tests_properties(boundary_core_not_net PROPERTIES
    PASS_REGULAR_EXPRESSION "server/Server.hpp: No such file or directory")
