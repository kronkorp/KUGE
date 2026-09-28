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
    PASS_REGULAR_EXPRESSION "Endpoint.hpp: No such file or directory")

# The physics needs the core, and nothing of the client: a server has it too
kuge_boundary(physics_sees_core physics_sees_core.cpp kuge-physics)

kuge_boundary(physics_not_client physics_sees_client.cpp kuge-physics)
set_tests_properties(boundary_physics_not_client PROPERTIES
    PASS_REGULAR_EXPRESSION "ClientModule.hpp: No such file or directory")

# A ByteReader reads bytes it does not own: on a temporary vector it would read
# freed memory, so that does not compile
kuge_boundary(reader_on_variable reader_on_variable.cpp kuge-core)

kuge_boundary(reader_on_temporary reader_on_temporary.cpp kuge-core)
set_tests_properties(boundary_reader_on_temporary PROPERTIES
    PASS_REGULAR_EXPRESSION "deleted")

# The network needs the core (scenes, serializer) and nothing of the client: a server has it too
kuge_boundary(net_sees_core net_sees_core.cpp kuge-net)

kuge_boundary(net_not_client net_sees_client.cpp kuge-net)
set_tests_properties(boundary_net_not_client PROPERTIES
    PASS_REGULAR_EXPRESSION "ClientModule.hpp: No such file or directory")

# The server needs the core and the network, and nothing of the client (it has no window);
# the network knows nothing of the server (a client links it without the rooms)
kuge_boundary(server_sees_net server_sees_net.cpp kuge-server)

kuge_boundary(server_not_client server_sees_client.cpp kuge-server)
set_tests_properties(boundary_server_not_client PROPERTIES
    PASS_REGULAR_EXPRESSION "ClientModule.hpp: No such file or directory")

kuge_boundary(net_not_server net_sees_server.cpp kuge-net)
set_tests_properties(boundary_net_not_server PROPERTIES
    PASS_REGULAR_EXPRESSION "GameServer.hpp: No such file or directory")

# Replication is used by the rooms and by the clients: it needs the network and the physics, and knows neither of them
kuge_boundary(replication_sees_net replication_sees_net.cpp kuge-replication)

kuge_boundary(replication_not_client replication_sees_client.cpp kuge-replication)
set_tests_properties(boundary_replication_not_client PROPERTIES
    PASS_REGULAR_EXPRESSION "ClientModule.hpp: No such file or directory")

kuge_boundary(replication_not_server replication_sees_server.cpp kuge-replication)
set_tests_properties(boundary_replication_not_server PROPERTIES
    PASS_REGULAR_EXPRESSION "GameServer.hpp: No such file or directory")
