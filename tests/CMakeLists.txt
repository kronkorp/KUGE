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
)
target_link_libraries(kuge_tests PRIVATE kuge-core kronklab-static)
target_compile_options(kuge_tests PRIVATE -Wall -Wextra)
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
target_compile_options(kuge_physics_tests PRIVATE -Wall -Wextra)
kuge_add_test(kuge_physics_tests kuge_physics_tests)

add_subdirectory(boundaries)

add_executable(kuge_logger_tests logger/logger_test.cpp)
target_link_libraries(kuge_logger_tests PRIVATE kuge-logger kronklab-static)
target_compile_options(kuge_logger_tests PRIVATE -Wall -Wextra)
kuge_add_test(kuge_logger_tests kuge_logger_tests)

add_executable(kuge_logger_init_tests logger/first_use_test.cpp)
target_link_libraries(kuge_logger_init_tests PRIVATE kuge-logger kronklab-static)
target_compile_options(kuge_logger_init_tests PRIVATE -Wall -Wextra)
kuge_add_test(kuge_logger_init_tests kuge_logger_init_tests)

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
    )
    target_link_libraries(kuge_client_tests PRIVATE kuge-client kronklab-static)
    target_include_directories(kuge_client_tests PRIVATE ${PROJECT_SOURCE_DIR}/example/pong ${PROJECT_SOURCE_DIR}/example/platformer)
    target_link_libraries(kuge_client_tests PRIVATE kuge-physics)
    target_compile_options(kuge_client_tests PRIVATE -Wall -Wextra)
    kuge_add_test(kuge_client_tests kuge_client_tests)
endif()

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
