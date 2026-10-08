cmake_minimum_required(VERSION 3.24)
# Before project(): it picks vcpkg, which installs SDL2 & co. (see cmake/Vcpkg.cmake)
include(cmake/Vcpkg.cmake)
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

# The warnings of KUGE's own code
if(MSVC)
    set(KUGE_WARNINGS /W4)
    # The sources are UTF-8 (whatever the code page of the machine), the biggest ones have more
    # sections than an object file holds by default, and the C runtime's "use the _s version"
    # warnings are not for portable code
    add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    set(KUGE_WARNINGS -Wall -Wextra)
endif()

option(KUGE_BUILD_SHARED  "Also build kuge.so, an aggregate of every module" ON)
option(KUGE_BUILD_CLIENT  "Build the client module (window, inputs, 2D rendering: needs SDL2)" ON)
option(BUILD_EXAMPLES     "Build the example/ executables" OFF)
option(BUILD_TESTS        "Build the tests (run them with ctest)" OFF)
set(KUGE_SANITIZE "" CACHE STRING "Build with a sanitizer: address (+ undefined) or thread")
set_property(CACHE KUGE_SANITIZE PROPERTY STRINGS "" address thread)

# Set before anything is fetched, so that the dependencies are instrumented too
if(KUGE_SANITIZE AND MSVC)
    message(FATAL_ERROR "KUGE_SANITIZE is for GCC and Clang (the flags below are theirs)")
elseif(KUGE_SANITIZE STREQUAL "address")
    set(KUGE_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(KUGE_SANITIZE STREQUAL "thread")
    set(KUGE_SANITIZE_FLAGS -fsanitize=thread)
elseif(NOT KUGE_SANITIZE STREQUAL "")
    message(FATAL_ERROR "KUGE_SANITIZE must be empty, 'address' or 'thread' (got '${KUGE_SANITIZE}')")
endif()
if(KUGE_SANITIZE_FLAGS)
    add_compile_options(${KUGE_SANITIZE_FLAGS})
    add_link_options(${KUGE_SANITIZE_FLAGS})
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# The first declaration of a name wins: declaring kronklab here pins the version
# that kronkpool would otherwise take from its "main".
FetchContent_Declare(
    kronklab
    GIT_REPOSITORY https://github.com/kronkorp/kronklab.git
    GIT_TAG        5584149a3c9ccbb12baaf71cfd591db3b37541ad
)

# kronkflow depends on kronkpool, which KUGE also uses directly: both must be
# the same copy, the vendored one. It can only be used once kronkpool guards its
# "project_options" target (kronkflow defines the same one).
set(KRONKPOOL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KUGE_KRONKPOOL_DIR "${PROJECT_SOURCE_DIR}/vendor/kronkpool")
file(READ "${KUGE_KRONKPOOL_DIR}/cmake/CompilerOptions.cmake" KUGE_KRONKPOOL_OPTIONS)
if(KUGE_KRONKPOOL_OPTIONS MATCHES "NOT TARGET project_options")
    set(FETCHCONTENT_SOURCE_DIR_KRONKPOOL "${KUGE_KRONKPOOL_DIR}" CACHE PATH "" FORCE)
else()
    message(WARNING
        "vendor/kronkpool does not guard its 'project_options' target yet: "
        "kronkflow will fetch its own kronkpool instead of using the vendored one. "
        "Commit the guard in kronkpool and bump the submodule.")
endif()

# The client's libraries, from vcpkg (vcpkg.json). Its triplets build them shared on Windows and
# static on Linux, and the targets are not named the same: KUGE_SDL_TARGETS holds whichever exist.
# SDL2 comes last: the static SDL2_mixer and SDL2_ttf use it, and their targets do not say so.
if(KUGE_BUILD_CLIENT)
    # (A game that fetches KUGE must give its own build the vcpkg toolchain, and list them in its
    # own vcpkg.json: without vcpkg, stb is nowhere to be found)
    if(NOT VCPKG_TOOLCHAIN)
        message(FATAL_ERROR
            "The client module's libraries (SDL2, SDL2_mixer, SDL2_ttf, stb) come from vcpkg, and this "
            "build has no vcpkg toolchain. Built on its own, KUGE finds one (cmake/Vcpkg.cmake). "
            "A game that fetches KUGE: pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake "
            "and list sdl2, sdl2-mixer, sdl2-ttf and stb in the game's vcpkg.json (see KUGE's). "
            "A server: -DKUGE_BUILD_CLIENT=OFF needs none of them.")
    endif()
    # SDL2_mixer's config turns CMAKE_FIND_PACKAGE_PREFER_CONFIG on for whoever finds it, when it is
    # not set. zlib's config would then win over CMake's FindZLIB, and it has no ZLIB::ZLIB, which
    # freetype (SDL2_ttf's) links to. Set, it is left alone.
    if(NOT DEFINED CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
    endif()
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)
    find_package(Stb REQUIRED)   # header-only image loader: Stb_INCLUDE_DIR

    set(KUGE_SDL_TARGETS "")
    foreach(target SDL2_mixer::SDL2_mixer SDL2_ttf::SDL2_ttf SDL2::SDL2)
        if(TARGET ${target})
            list(APPEND KUGE_SDL_TARGETS ${target})
        elseif(TARGET ${target}-static)
            list(APPEND KUGE_SDL_TARGETS ${target}-static)
        else()
            message(FATAL_ERROR "SDL was found, but neither ${target} nor ${target}-static is a target")
        endif()
    endforeach()
endif()

FetchContent_Declare(
    kronkflow
    GIT_REPOSITORY https://github.com/kronkorp/kronkflow
    GIT_TAG        b4ae0a6e578bd0684633a4a753a62032cfb512c7
)
# Only the modules are built by default: kronkflow reads BUILD_TESTS too, and
# kronkutils (which it fetches) includes CTest, whose BUILD_TESTING is ON. Both
# are hidden from them, whatever KUGE's own BUILD_TESTS is.
set(BUILD_TESTS OFF)
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(kronkflow)
unset(BUILD_TESTS)
unset(BUILD_TESTING)
# What the modules link (kronkflow, kronkutils, kronkpool) is still built; the
# rest (their shared libraries, kronklab) only when something needs it.
set_property(DIRECTORY "${kronkflow_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL ON)

include(cmake/KugeModule.cmake)

# Vendored libraries, compiled from the submodules. kronk3d is only needed by
# the 3D module.
#
# kronknet is built by its own CMake: only it knows which of its sources are
# for which platform. Its tests are hidden from it, like kronkflow's.
set(BUILD_TESTS OFF)
add_subdirectory(vendor/kronknet EXCLUDE_FROM_ALL)
unset(BUILD_TESTS)
set_property(GLOBAL APPEND PROPERTY KUGE_VENDORS kronknet-static)
kuge_vendor(kronkworld DEPENDS kronkflow)

# ---------------------------------------------------------------------------
# Modules: one library each, linked only to what they need
# ---------------------------------------------------------------------------
add_subdirectory(modules/logger)
add_subdirectory(modules/core)
add_subdirectory(modules/net)
add_subdirectory(modules/server)
add_subdirectory(modules/replication)
add_subdirectory(modules/physics)
if(KUGE_BUILD_CLIENT)
    add_subdirectory(modules/client)
endif()

# kuge.so: every module in one shared library (games can also link the modules
# they use directly, which is what the example does)
if(KUGE_BUILD_SHARED)
    add_library(kuge SHARED cmake/aggregate.cpp)
    set_target_properties(kuge PROPERTIES
        PREFIX ""
        OUTPUT_NAME kuge
    )
    if(WIN32)
        # kuge.dll: nothing in the code is marked for export, so everything is
        set_target_properties(kuge PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    else()
        set_target_properties(kuge PROPERTIES SUFFIX ".so")
    endif()
    get_property(KUGE_MODULE_TARGETS GLOBAL PROPERTY KUGE_MODULES)
    get_property(KUGE_VENDOR_TARGETS GLOBAL PROPERTY KUGE_VENDORS)
    foreach(target ${KUGE_VENDOR_TARGETS})
        target_sources(kuge PRIVATE $<TARGET_OBJECTS:${target}>)
    endforeach()
    foreach(target ${KUGE_MODULE_TARGETS})
        target_sources(kuge PRIVATE $<TARGET_OBJECTS:${target}-obj>)
    endforeach()
    # The objects are already in: the libraries only bring what they depend on
    target_link_libraries(kuge PUBLIC ${KUGE_MODULE_TARGETS})
endif()

# ---------------------------------------------------------------------------
# Example and tests
# ---------------------------------------------------------------------------
if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(BUILD_TESTS)
    enable_testing()
    # kronklab runs each test in a fork(), which Windows does not have: there, only the tests
    # that need no kronklab are built (see tests/CMakeLists.txt)
    if(NOT WIN32)
        FetchContent_MakeAvailable(kronklab)
    endif()
    add_subdirectory(tests)
endif()
