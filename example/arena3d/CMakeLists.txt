# Arena: the same 3D game as a server with no window, a client with one, and both in one process.
# Its three programs share common/ (the vocabulary, the level, the rules), and none of them changes the engine.

add_library(arena-common STATIC common/Arena.cpp common/Map.cpp)
target_include_directories(arena-common PUBLIC common)
target_link_libraries(arena-common PUBLIC kuge-replication kuge-physics3d)
target_compile_options(arena-common PRIVATE ${KUGE_WARNINGS})

add_library(arena-room STATIC server/ArenaRoom.cpp)
target_include_directories(arena-room PUBLIC server)
target_link_libraries(arena-room PUBLIC arena-common kuge-server)
target_compile_options(arena-room PRIVATE ${KUGE_WARNINGS})

add_executable(kuge_arena3d_server server/main.cpp)
target_link_libraries(kuge_arena3d_server PRIVATE arena-room)
target_compile_options(kuge_arena3d_server PRIVATE ${KUGE_WARNINGS})

if(TARGET kuge-client AND TARGET kuge-render3d)
    add_library(arena-client INTERFACE)
    target_include_directories(arena-client INTERFACE client)
    target_link_libraries(arena-client INTERFACE arena-common kuge-render3d)
    # Where the models and textures are (--assets says another place): the folder of the sources
    target_compile_definitions(arena-client INTERFACE ARENA_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

    add_executable(kuge_arena3d_client client/main.cpp)
    target_link_libraries(kuge_arena3d_client PRIVATE arena-client)
    target_compile_options(kuge_arena3d_client PRIVATE ${KUGE_WARNINGS})

    add_executable(kuge_arena3d_host host/main.cpp)
    target_link_libraries(kuge_arena3d_host PRIVATE arena-client arena-room)
    target_compile_options(kuge_arena3d_host PRIVATE ${KUGE_WARNINGS})
endif()
