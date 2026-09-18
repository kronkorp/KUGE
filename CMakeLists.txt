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

# Kuge modules
# Modules with their own CMakeLists.txt (network) are object libraries,
# the others (core) are globbed directly
add_subdirectory(modules/network)

file(GLOB_RECURSE KUGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/core/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/core/src/*.c"
)

# Vendored libraries, compiled straight into kuge
set(KUGE_VENDORS kronk3d kronknet kronkpool kronkworld)

set(VENDOR_SOURCES "")
set(VENDOR_INCLUDES "")
foreach(vendor ${KUGE_VENDORS})
    set(vendor_dir "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${vendor}")
    file(GLOB_RECURSE vendor_srcs CONFIGURE_DEPENDS
        "${vendor_dir}/src/*.cpp"
        "${vendor_dir}/src/*.c"
    )
    list(APPEND VENDOR_SOURCES ${vendor_srcs})
    list(APPEND VENDOR_INCLUDES "${vendor_dir}/include")
endforeach()

add_library(kuge SHARED ${KUGE_SOURCES} ${VENDOR_SOURCES})

# Produce "kuge.so" rather than "libkuge.so"
set_target_properties(kuge PROPERTIES
    PREFIX ""
    OUTPUT_NAME kuge
    SUFFIX ".so"
)

file(GLOB KUGE_MODULE_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/modules/*")
foreach(dir ${KUGE_MODULE_DIRS})
    if(IS_DIRECTORY "${dir}/src")
        target_include_directories(kuge PUBLIC "${dir}/src")
    endif()
endforeach()

# Vendor sources include their own headers privately (e.g. server.h), so
# expose each src root as well
foreach(vendor ${KUGE_VENDORS})
    target_include_directories(kuge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${vendor}/src")
endforeach()
target_include_directories(kuge PUBLIC ${VENDOR_INCLUDES})

target_sources(kuge PRIVATE
    $<TARGET_OBJECTS:kronkflow-obj>
    $<TARGET_OBJECTS:kuge-network>
)
target_link_libraries(kuge PUBLIC kuge-network)
target_link_libraries(kuge PUBLIC kronkflow)
target_link_libraries(kuge PUBLIC pthread)

target_compile_options(kuge PRIVATE -Wall -Wextra)

option(KUGE_BUILD_EXAMPLE "Build the example/ executable" ON)
if(KUGE_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
