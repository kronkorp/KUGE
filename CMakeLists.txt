cmake_minimum_required(VERSION 3.16)
project(kuge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Create a build directory (e.g. 'build/').")
endif()

# kronkworld depends on kronkflow (headers + objects)
include(FetchContent)
FetchContent_Declare(
    kronkflow
    GIT_REPOSITORY https://github.com/kronkorp/kronkflow
    GIT_TAG        main
)
FetchContent_MakeAvailable(kronkflow)

# Vendored libraries, compiled straight into kuge
set(KUGE_VENDORS kronk3d kronknet kronkpool kronkworld)

set(VENDOR_SOURCES "")
foreach(vendor ${KUGE_VENDORS})
    file(GLOB_RECURSE vendor_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${vendor}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${vendor}/src/*.c"
    )
    list(APPEND VENDOR_SOURCES ${vendor_srcs})
endforeach()

add_library(kuge-vendor OBJECT ${VENDOR_SOURCES})
set_target_properties(kuge-vendor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kuge-vendor PRIVATE -Wall -Wextra)

foreach(vendor ${KUGE_VENDORS})
    # Public headers are visible to the kuge modules and to kuge's consumers,
    # private headers (e.g. server.h) only to the vendor sources
    target_include_directories(kuge-vendor
        PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${vendor}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${vendor}/src"
    )
endforeach()

# kronkworld's headers include kronkflow's
target_link_libraries(kuge-vendor PUBLIC kronkflow-obj)

# Kuge modules: each one is an object library defined in its own CMakeLists.txt
set(KUGE_MODULES core logger network)
foreach(module ${KUGE_MODULES})
    add_subdirectory(modules/${module})
    target_link_libraries(kuge-${module} PUBLIC kuge-vendor)
endforeach()

add_library(kuge SHARED)

# Produce "kuge.so" rather than "libkuge.so"
set_target_properties(kuge PROPERTIES
    PREFIX ""
    OUTPUT_NAME kuge
    SUFFIX ".so"
)

foreach(target vendor ${KUGE_MODULES})
    target_sources(kuge PRIVATE $<TARGET_OBJECTS:kuge-${target}>)
    # PUBLIC usage requirements (include dirs) propagate to kuge's consumers
    target_link_libraries(kuge PUBLIC kuge-${target})
endforeach()
target_sources(kuge PRIVATE $<TARGET_OBJECTS:kronkflow-obj>)
target_link_libraries(kuge PUBLIC kronkflow pthread)

option(KUGE_BUILD_EXAMPLE "Build the example/ executable" ON)
if(KUGE_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
