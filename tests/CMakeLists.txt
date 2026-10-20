# What needs no test framework runs on every platform: the boundaries, and the games played for real.
add_subdirectory(boundaries)
add_subdirectory(dll_ids)

# Ticket 023: the same floats on every platform. A plain program (no kronklab), so Windows runs it too:
# the final state of a scripted 2D simulation is hashed, and Linux and Windows must reach the same constant
add_executable(kuge_float_determinism determinism/float_determinism.cpp)
target_link_libraries(kuge_float_determinism PRIVATE kuge-physics)
target_compile_options(kuge_float_determinism PRIVATE ${KUGE_WARNINGS})
add_test(NAME float_determinism COMMAND kuge_float_determinism)

# The real game, started for real (on SDL's dummy screen), plays a scripted match
if(TARGET kuge_pong)
    add_test(NAME pong_smoke
        COMMAND ${CMAKE_COMMAND} -DPONG=$<TARGET_FILE:kuge_pong> -DOUT=${CMAKE_CURRENT_BINARY_DIR}/pong.ppm
                -P ${PROJECT_SOURCE_DIR}/cmake/RunPong.cmake)
endif()

if(TARGET kuge_platformer)
    add_test(NAME platformer_smoke
        COMMAND ${CMAKE_COMMAND} -DGAME=$<TARGET_FILE:kuge_platformer> -DOUT=${CMAKE_CURRENT_BINARY_DIR}/platformer.ppm
                -P ${PROJECT_SOURCE_DIR}/cmake/RunPlatformer.cmake)
endif()

# R-Type as real programs: a server and a client over the network, then the host (both in one process)
# (a shell script: on Windows, the sh of Git for Windows)
find_program(KUGE_SH sh)
if(TARGET kuge_rtype_server AND TARGET kuge_rtype_client AND TARGET kuge_rtype_host)
    if(KUGE_SH)
        add_test(NAME rtype_smoke
            COMMAND ${KUGE_SH} ${PROJECT_SOURCE_DIR}/cmake/RunRType.sh
                    $<TARGET_FILE:kuge_rtype_server> $<TARGET_FILE:kuge_rtype_client> $<TARGET_FILE:kuge_rtype_host>
                    ${CMAKE_CURRENT_BINARY_DIR}/rtype)
    else()
        message(STATUS "No sh found: the R-Type smoke test (cmake/RunRType.sh) is not run")
    endif()
endif()

# Arena, a 3D game, as real programs: a server and a client over the network, then the host (both in one process)
if(TARGET kuge_arena3d_server AND TARGET kuge_arena3d_client AND TARGET kuge_arena3d_host)
    if(KUGE_SH)
        add_test(NAME arena3d_smoke
            COMMAND ${KUGE_SH} ${PROJECT_SOURCE_DIR}/cmake/RunArena3D.sh
                    $<TARGET_FILE:kuge_arena3d_server> $<TARGET_FILE:kuge_arena3d_client> $<TARGET_FILE:kuge_arena3d_host>
                    ${CMAKE_CURRENT_BINARY_DIR}/arena3d)
        # The same on the GPU, with SDL's offscreen video driver (an OpenGL context and no window): skipped (77) where
        # there is no OpenGL 3.3
        add_test(NAME arena3d_smoke_gl
            COMMAND ${KUGE_SH} ${PROJECT_SOURCE_DIR}/cmake/RunArena3D.sh
                    $<TARGET_FILE:kuge_arena3d_server> $<TARGET_FILE:kuge_arena3d_client> $<TARGET_FILE:kuge_arena3d_host>
                    ${CMAKE_CURRENT_BINARY_DIR}/arena3d_gl)
        set_tests_properties(arena3d_smoke_gl PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;ARENA_ARGS=--opengl" SKIP_RETURN_CODE 77)
    else()
        message(STATUS "No sh found: the Arena smoke tests (cmake/RunArena3D.sh) are not run")
    endif()
endif()

# The unit tests run on kronklab, which runs each test in a fork(): Windows does not have it
if(WIN32)
    return()
endif()

add_executable(kuge_tests
    core/asset_async_test.cpp
    core/asset_manager_test.cpp
    core/asset_reload_test.cpp
    core/config_file_test.cpp
    core/engine_test.cpp
    core/fixed_timestep_test.cpp
    core/mailbox_test.cpp
    core/math2d_test.cpp
    core/module_test.cpp
    core/save_test.cpp
    core/scene_test.cpp
    core/serializer_test.cpp
    core/snapshot_test.cpp
    core/spawn_test.cpp
    core/tilemap_test.cpp
    core/transform3d_test.cpp
)
target_link_libraries(kuge_tests PRIVATE kuge-core kronklab-static)
target_compile_options(kuge_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_tests kuge_tests)

# The physics only needs the core
add_executable(kuge_physics_tests
    physics/physics_property_test.cpp
    physics/physics_test.cpp
    physics/queries_test.cpp
    physics/shapes_test.cpp
    physics/spatial_grid_test.cpp
    physics/tilemap_collision_test.cpp
)
target_link_libraries(kuge_physics_tests PRIVATE kuge-physics kronklab-static)
target_include_directories(kuge_physics_tests PRIVATE physics)
target_compile_options(kuge_physics_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_physics_tests kuge_physics_tests)

# R-Type: the game of example/rtype, run as a host and as a dedicated server (needs the client module for its scene)
if(TARGET kuge-client AND TARGET rtype-room)
    add_executable(kuge_rtype_tests rtype/rtype_test.cpp)
    target_link_libraries(kuge_rtype_tests PRIVATE rtype-client rtype-room kronklab-static)
    target_compile_options(kuge_rtype_tests PRIVATE ${KUGE_WARNINGS})
    kuge_add_test(kuge_rtype_tests kuge_rtype_tests)
endif()

add_executable(kuge_replication_tests
    replication/datagram_test.cpp
    replication/prediction_test.cpp
    replication/rates_test.cpp
    replication/replication3d_test.cpp
    replication/replication_test.cpp
    replication/room_test.cpp
)
target_link_libraries(kuge_replication_tests PRIVATE kuge-replication kuge-server kronklab-static)
target_compile_options(kuge_replication_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_replication_tests kuge_replication_tests)

add_executable(kuge_server_tests server/server_test.cpp)
target_link_libraries(kuge_server_tests PRIVATE kuge-server kronklab-static)
target_compile_options(kuge_server_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_server_tests kuge_server_tests)

add_executable(kuge_net_tests
    net/auth_test.cpp
    net/endpoint_test.cpp
    net/handshake_test.cpp
    net/loopback_test.cpp
    net/net_scene_test.cpp
    net/socket_test.cpp
    net/reliable_test.cpp
    net/unreliable_network_test.cpp
    net/wire_test.cpp
)
target_link_libraries(kuge_net_tests PRIVATE kuge-net kronklab-static)
target_compile_options(kuge_net_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_net_tests kuge_net_tests)

# A socket whose send buffer is full: send() and sendto() are wrapped, so that a test can make them say "try again"
add_executable(kuge_net_fault_tests net/udp_backpressure_test.cpp)
target_link_libraries(kuge_net_fault_tests PRIVATE kuge-net kronklab-static)
target_link_options(kuge_net_fault_tests PRIVATE -Wl,--wrap=send -Wl,--wrap=sendto)
target_compile_options(kuge_net_fault_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_net_fault_tests kuge_net_fault_tests)

add_executable(kuge_logger_tests logger/logger_test.cpp)
target_link_libraries(kuge_logger_tests PRIVATE kuge-logger kronklab-static)
target_compile_options(kuge_logger_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_logger_tests kuge_logger_tests)

add_executable(kuge_logger_init_tests logger/first_use_test.cpp)
target_link_libraries(kuge_logger_init_tests PRIVATE kuge-logger kronklab-static)
target_compile_options(kuge_logger_init_tests PRIVATE ${KUGE_WARNINGS})
kuge_add_test(kuge_logger_init_tests kuge_logger_init_tests)

if(TARGET kuge-physics3d)
    add_executable(kuge_animation_tests animation/animation_test.cpp)
    target_link_libraries(kuge_animation_tests PRIVATE kuge-animation kuge-physics3d kronklab-static)
    target_compile_options(kuge_animation_tests PRIVATE ${KUGE_WARNINGS})
    kuge_add_test(kuge_animation_tests kuge_animation_tests)
endif()

if(TARGET kuge-client)
    add_executable(kuge_client_tests
        client/animation_test.cpp
        client/async_load_test.cpp
        client/audio_test.cpp
        client/client_module_test.cpp
        client/hot_reload_test.cpp
        client/image_test.cpp
        client/input_map_test.cpp
        client/keys_test.cpp
        client/platformer_test.cpp
        client/pong_test.cpp
        client/sdl_audio_test.cpp
        client/sdl_font_test.cpp
        client/sdl_backend_test.cpp
        client/spritesheet_test.cpp
        client/sprites_test.cpp
        client/text_test.cpp
        client/tilemap_render_test.cpp
        client/ui_interaction_test.cpp
        client/ui_layout_test.cpp
        client/ui_render_test.cpp
        client/ui_textfield_test.cpp
        client/ui_widgets_test.cpp
    )
    target_link_libraries(kuge_client_tests PRIVATE kuge-client kronklab-static)
    target_include_directories(kuge_client_tests PRIVATE ${PROJECT_SOURCE_DIR}/example/pong ${PROJECT_SOURCE_DIR}/example/platformer)
    target_link_libraries(kuge_client_tests PRIVATE kuge-physics)
    target_compile_options(kuge_client_tests PRIVATE ${KUGE_WARNINGS})
    kuge_add_test(kuge_client_tests kuge_client_tests)
endif()

if(TARGET kuge-render3d)
    add_executable(kuge_render3d_tests render3d/animator3d_test.cpp render3d/camera_test.cpp render3d/effects_test.cpp render3d/gl_test.cpp render3d/models_test.cpp render3d/render3d_test.cpp)
    target_link_libraries(kuge_render3d_tests PRIVATE kuge-render3d kronklab-static)
    target_include_directories(kuge_render3d_tests PRIVATE render3d)
    target_compile_options(kuge_render3d_tests PRIVATE ${KUGE_WARNINGS})
    kuge_add_test(kuge_render3d_tests kuge_render3d_tests)
endif()

if(TARGET kuge-physics3d)
    add_executable(kuge_physics3d_tests physics3d/physics3d_test.cpp physics3d/physics3d_net_test.cpp
        physics3d/character_test.cpp physics3d/hitbox_test.cpp)
    target_link_libraries(kuge_physics3d_tests PRIVATE kuge-physics3d kuge-replication kronklab-static)
    target_include_directories(kuge_physics3d_tests PRIVATE replication)
    target_compile_options(kuge_physics3d_tests PRIVATE ${KUGE_WARNINGS})
    kuge_add_test(kuge_physics3d_tests kuge_physics3d_tests)
endif()

# The rules of Arena, with no network and no screen (only when the examples are built: its libraries are theirs)
if(TARGET arena-common)
    add_executable(kuge_arena3d_tests arena3d/arena3d_test.cpp)
    target_link_libraries(kuge_arena3d_tests PRIVATE arena-common kronklab-static)
    target_compile_definitions(kuge_arena3d_tests PRIVATE ARENA_ASSETS_DIR="${PROJECT_SOURCE_DIR}/example/arena3d/assets")
    target_include_directories(kuge_arena3d_tests PRIVATE ${PROJECT_SOURCE_DIR}/example/arena3d/client)
    target_compile_options(kuge_arena3d_tests PRIVATE ${KUGE_WARNINGS})
    kuge_add_test(kuge_arena3d_tests kuge_arena3d_tests)
endif()
