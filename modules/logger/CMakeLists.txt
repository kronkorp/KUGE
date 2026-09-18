file(GLOB_RECURSE KUGE_LOGGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(kuge-logger OBJECT ${KUGE_LOGGER_SOURCES})

target_include_directories(kuge-logger PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(kuge-logger PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kuge-logger PRIVATE -Wall -Wextra)
