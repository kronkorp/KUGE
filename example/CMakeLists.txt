add_executable(kuge_example src/Main.cpp)
target_link_libraries(kuge_example PRIVATE kuge-core)
target_compile_options(kuge_example PRIVATE ${KUGE_WARNINGS})

# Pong: the first game made with the client module
if(TARGET kuge-client)
    add_executable(kuge_pong pong/main.cpp)
    target_link_libraries(kuge_pong PRIVATE kuge-client)
    target_include_directories(kuge_pong PRIVATE pong)
    target_compile_options(kuge_pong PRIVATE ${KUGE_WARNINGS})
endif()

# A platformer: tilemap, animations, physics, interface, sounds, saves
if(TARGET kuge-client AND TARGET kuge-physics)
    add_executable(kuge_platformer platformer/main.cpp)
    target_link_libraries(kuge_platformer PRIVATE kuge-client kuge-physics)
    target_include_directories(kuge_platformer PRIVATE platformer)
    target_compile_options(kuge_platformer PRIVATE ${KUGE_WARNINGS})
endif()

# Catch: a tiny game with a mod next to it (data and a Lua script)
if(TARGET kuge-client AND TARGET kuge-script)
    add_executable(kuge_modding modding/main.cpp)
    target_link_libraries(kuge_modding PRIVATE kuge-client kuge-script)
    target_include_directories(kuge_modding PRIVATE modding)
    target_compile_definitions(kuge_modding PRIVATE CATCH_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/modding")
    target_compile_options(kuge_modding PRIVATE ${KUGE_WARNINGS})
endif()

# A chat: a server with no window (lobby and rooms), and a client for the terminal
if(TARGET kuge-server)
    add_executable(kuge_chat_server chat/server.cpp)
    target_link_libraries(kuge_chat_server PRIVATE kuge-server)
    target_include_directories(kuge_chat_server PRIVATE chat)
    target_compile_options(kuge_chat_server PRIVATE ${KUGE_WARNINGS})

    add_executable(kuge_chat_client chat/client.cpp)
    target_link_libraries(kuge_chat_client PRIVATE kuge-net)
    target_include_directories(kuge_chat_client PRIVATE chat)
    target_compile_options(kuge_chat_client PRIVATE ${KUGE_WARNINGS})
endif()

# R-Type: a server, a client and both in one process, from the same code
if(TARGET kuge-server AND TARGET kuge-replication)
    add_subdirectory(rtype)
endif()

# Arena: a 3D game (render, physics, replication, prediction): a server, a client and both in one process
if(TARGET kuge-server AND TARGET kuge-replication AND TARGET kuge-physics3d)
    add_subdirectory(arena3d)
endif()
