file(GLOB_RECURSE KUGE_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(kuge-core OBJECT ${KUGE_CORE_SOURCES})

target_include_directories(kuge-core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(kuge-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kuge-core PRIVATE -Wall -Wextra)

target_link_libraries(kuge-core PUBLIC kuge-logger)
