add_executable(kuge_example src/Main.cpp)
target_link_libraries(kuge_example PRIVATE kuge-core)
target_compile_options(kuge_example PRIVATE -Wall -Wextra)

# Pong: the first game made with the client module
if(TARGET kuge-client)
    add_executable(kuge_pong pong/main.cpp)
    target_link_libraries(kuge_pong PRIVATE kuge-client)
    target_include_directories(kuge_pong PRIVATE pong)
    target_compile_options(kuge_pong PRIVATE -Wall -Wextra)
endif()

# A platformer: tilemap, animations, physics, interface, sounds, saves
if(TARGET kuge-client AND TARGET kuge-physics)
    add_executable(kuge_platformer platformer/main.cpp)
    target_link_libraries(kuge_platformer PRIVATE kuge-client kuge-physics)
    target_include_directories(kuge_platformer PRIVATE platformer)
    target_compile_options(kuge_platformer PRIVATE -Wall -Wextra)
endif()
