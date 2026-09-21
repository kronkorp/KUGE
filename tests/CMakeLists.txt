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
