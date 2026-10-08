# Helpers to declare KUGE's vendored libraries and modules.

# kuge_vendor(<name> [DEPENDS <targets>...])
#
# Builds vendor/<name>'s sources into the static library kuge-<name> (from the
# object library kuge-<name>-obj, which kuge.so reuses). Its public headers are
# visible to whoever links it, its private headers (src/) only to its own
# sources. kuge.so takes the objects of every target listed in KUGE_VENDORS.
function(kuge_vendor name)
    cmake_parse_arguments(V "" "" "DEPENDS" ${ARGN})
    set(dir "${PROJECT_SOURCE_DIR}/vendor/${name}")

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${dir}/src/*.cpp" "${dir}/src/*.c")
    add_library(kuge-${name}-obj OBJECT ${sources})
    add_library(kuge-${name} STATIC $<TARGET_OBJECTS:kuge-${name}-obj>)

    foreach(target kuge-${name}-obj kuge-${name})
        target_include_directories(${target} PUBLIC "${dir}/include")
        target_link_libraries(${target} PUBLIC ${V_DEPENDS})
    endforeach()
    target_include_directories(kuge-${name}-obj PRIVATE "${dir}/src")
    target_compile_options(kuge-${name}-obj PRIVATE -Wall -Wextra)

    set_property(GLOBAL APPEND PROPERTY KUGE_VENDORS kuge-${name}-obj)
endfunction()

# kuge_module(<name> [DEPENDS <targets>...])
#
# Declares modules/<name> as the static library kuge-<name> (alias kuge::<name>),
# built from the object library kuge-<name>-obj (which kuge.so reuses).
#
# A module only sees the headers of what it DEPENDS on: including a module that
# is not linked is a compile error. This is what keeps client and server apart.
function(kuge_module name)
    cmake_parse_arguments(M "" "" "DEPENDS" ${ARGN})

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    )

    add_library(kuge-${name}-obj OBJECT ${sources})
    add_library(kuge-${name} STATIC $<TARGET_OBJECTS:kuge-${name}-obj>)
    add_library(kuge::${name} ALIAS kuge-${name})

    foreach(target kuge-${name}-obj kuge-${name})
        target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${target} PUBLIC ${M_DEPENDS})
    endforeach()
    target_compile_options(kuge-${name}-obj PRIVATE -Wall -Wextra)

    set_property(GLOBAL APPEND PROPERTY KUGE_MODULES kuge-${name})
endfunction()

# kuge_add_test(<name> <executable-target>)
#
# Registers a kronklab executable in ctest, failing when a test inside it fails
# (see cmake/RunKronklab.cmake for why the exit code is not enough).
function(kuge_add_test name target)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} -DTEST_EXE=$<TARGET_FILE:${target}>
                -P ${PROJECT_SOURCE_DIR}/cmake/RunKronklab.cmake)
endfunction()
