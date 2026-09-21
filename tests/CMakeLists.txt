add_executable(kuge_tests
    core/asset_manager_test.cpp
    core/config_file_test.cpp
    core/engine_test.cpp
    core/fixed_timestep_test.cpp
    core/math2d_test.cpp
    core/module_test.cpp
    core/scene_test.cpp
    core/serializer_test.cpp
)
target_link_libraries(kuge_tests PRIVATE kuge-core kronklab-static)
target_compile_options(kuge_tests PRIVATE -Wall -Wextra)
kuge_add_test(kuge_tests kuge_tests)

add_subdirectory(boundaries)

if(TARGET kuge-client)
    add_executable(kuge_client_tests
        client/client_module_test.cpp
        client/image_test.cpp
        client/input_map_test.cpp
        client/keys_test.cpp
        client/pong_test.cpp
        client/sdl_backend_test.cpp
        client/sprites_test.cpp
    )
    target_link_libraries(kuge_client_tests PRIVATE kuge-client kronklab-static)
    target_include_directories(kuge_client_tests PRIVATE ${PROJECT_SOURCE_DIR}/example/pong)
    target_compile_options(kuge_client_tests PRIVATE -Wall -Wextra)
    kuge_add_test(kuge_client_tests kuge_client_tests)
endif()

# The real game, started for real (on SDL's dummy screen), plays a scripted match
if(TARGET kuge_pong)
    add_test(NAME pong_smoke
        COMMAND ${CMAKE_COMMAND} -DPONG=$<TARGET_FILE:kuge_pong> -DOUT=${CMAKE_CURRENT_BINARY_DIR}/pong.ppm
                -P ${PROJECT_SOURCE_DIR}/cmake/RunPong.cmake)
endif()
