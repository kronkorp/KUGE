# R-Type: the same game as a server with no window, a client with one, and both in one process.
# Its three programs share common/ (the vocabulary, the arena, the rules), and none of them changes the engine.

add_library(rtype-common STATIC common/Rules.cpp)
target_include_directories(rtype-common PUBLIC common)
target_link_libraries(rtype-common PUBLIC kuge-replication)
target_compile_options(rtype-common PRIVATE -Wall -Wextra)

add_library(rtype-room STATIC server/RTypeRoom.cpp)
target_include_directories(rtype-room PUBLIC server)
target_link_libraries(rtype-room PUBLIC rtype-common kuge-server)
target_compile_options(rtype-room PRIVATE -Wall -Wextra)

add_executable(kuge_rtype_server server/main.cpp)
target_link_libraries(kuge_rtype_server PRIVATE rtype-room)
target_compile_options(kuge_rtype_server PRIVATE -Wall -Wextra)

if(TARGET kuge-client)
    add_library(rtype-client INTERFACE)
    target_include_directories(rtype-client INTERFACE client)
    target_link_libraries(rtype-client INTERFACE rtype-common kuge-client)

    add_executable(kuge_rtype_client client/main.cpp)
    target_link_libraries(kuge_rtype_client PRIVATE rtype-client)
    target_compile_options(kuge_rtype_client PRIVATE -Wall -Wextra)

    add_executable(kuge_rtype_host host/main.cpp)
    target_link_libraries(kuge_rtype_host PRIVATE rtype-client rtype-room)
    target_compile_options(kuge_rtype_host PRIVATE -Wall -Wextra)
endif()
