file(GLOB_RECURSE KUGE_NETWORK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(kuge-network OBJECT ${KUGE_NETWORK_SOURCES})

target_include_directories(kuge-network PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/vendor/kronknet/include"
)

set_target_properties(kuge-network PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kuge-network PRIVATE -Wall -Wextra)
