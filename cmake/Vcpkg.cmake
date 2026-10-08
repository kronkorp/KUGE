# Where the third-party libraries come from: vcpkg, in manifest mode. vcpkg.json lists them (SDL2,
# SDL2_mixer, SDL2_ttf, stb) and pins their versions with its builtin-baseline, so a machine with
# a compiler, CMake and git builds the same ones as the CI, and nothing is installed on the system.
#
# Included before project(), which is when vcpkg installs what vcpkg.json asks for. The vcpkg used:
#   1. the toolchain file given with -DCMAKE_TOOLCHAIN_FILE, if any;
#   2. otherwise the one in $VCPKG_ROOT, if it is set (the developer's own, or Visual Studio's);
#   3. otherwise vcpkg cloned at the baseline into KUGE_VCPKG_DIR (.vcpkg/ by default): once, then
#      again only when the baseline changes. vcpkg's toolchain builds its program the first time.
#
# Only for KUGE built on its own: a game that fetches KUGE as a subproject is the one that chooses
# its toolchain, before its own project(), and lists SDL2 & co. in its own vcpkg.json.

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

# The client's libraries are a feature of the manifest: a server (KUGE_BUILD_CLIENT=OFF) builds
# without SDL, as it links without it
if(NOT DEFINED KUGE_BUILD_CLIENT OR KUGE_BUILD_CLIENT)
    list(APPEND VCPKG_MANIFEST_FEATURES client)
endif()

set(KUGE_VCPKG_DIR "${CMAKE_SOURCE_DIR}/.vcpkg" CACHE PATH "Where vcpkg is cloned when VCPKG_ROOT is not set")
set(kuge_vcpkg_toolchain "${KUGE_VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")

# (A toolchain file in the cache that is the clone's own is checked again: the baseline may have moved)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL kuge_vcpkg_toolchain)
    return()
endif()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    message(STATUS "vcpkg: the one in VCPKG_ROOT ($ENV{VCPKG_ROOT})")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
    return()
endif()

file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" kuge_vcpkg_manifest)
string(JSON kuge_vcpkg_baseline GET "${kuge_vcpkg_manifest}" builtin-baseline)
find_package(Git REQUIRED)

set(kuge_vcpkg_head "")
if(EXISTS "${KUGE_VCPKG_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${KUGE_VCPKG_DIR}"
        OUTPUT_VARIABLE kuge_vcpkg_head
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    file(MAKE_DIRECTORY "${KUGE_VCPKG_DIR}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init -q WORKING_DIRECTORY "${KUGE_VCPKG_DIR}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" remote add origin https://github.com/microsoft/vcpkg.git
        WORKING_DIRECTORY "${KUGE_VCPKG_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

# Only the baseline's commit is fetched (about 100 MB, not vcpkg's whole history): it is all that
# vcpkg needs to build the versions it names
if(NOT kuge_vcpkg_head STREQUAL kuge_vcpkg_baseline)
    message(STATUS "vcpkg: fetching ${kuge_vcpkg_baseline} into ${KUGE_VCPKG_DIR}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" fetch -q --depth 1 origin ${kuge_vcpkg_baseline}
        WORKING_DIRECTORY "${KUGE_VCPKG_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false checkout -q FETCH_HEAD
        WORKING_DIRECTORY "${KUGE_VCPKG_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    # The vcpkg program of the old baseline may not read the new one's scripts: it is built again
    file(REMOVE "${KUGE_VCPKG_DIR}/vcpkg" "${KUGE_VCPKG_DIR}/vcpkg.exe")
endif()

set(CMAKE_TOOLCHAIN_FILE "${kuge_vcpkg_toolchain}" CACHE FILEPATH "")
