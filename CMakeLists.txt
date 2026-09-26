cmake_minimum_required(VERSION 3.16)

include(cmake/mc_version.cmake)

project(mc_protocol VERSION ${MC_VERSION} LANGUAGES CXX)

include(cmake/mc_warnings.cmake)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MC_IS_TOP_LEVEL ON)
else()
    set(MC_IS_TOP_LEVEL OFF)
endif()

option(MC_BUILD_DEVICE "Build mc_device; when OFF, find_package(Qt6) is never called" ON)
option(MC_BUILD_MOCK "Build mc_mock" ON)
option(MC_BUILD_TESTS "Build test binaries and register them with ctest" ${MC_IS_TOP_LEVEL})
option(MC_BUILD_EXAMPLES "Build example programs" ${MC_IS_TOP_LEVEL})
option(MC_BUILD_BENCH "Build regression benchmarks" OFF)
option(MC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${MC_IS_TOP_LEVEL})
option(MC_COVERAGE "GCC/Clang only: --coverage on core targets and tests" OFF)

# MC_BUILD_TOOLS depends on top-level, MC_BUILD_DEVICE and MC_BUILD_MOCK rather than being
# independently toggled: developer tools under tools/ need both the device layer and the mock
# PLC, and only make sense when this folder is the top-level project.
if(MC_IS_TOP_LEVEL AND MC_BUILD_DEVICE AND MC_BUILD_MOCK)
    option(MC_BUILD_TOOLS "Build developer tools under tools/" ON)
else()
    option(MC_BUILD_TOOLS "Build developer tools under tools/" OFF)
endif()

add_subdirectory(src)

if(MC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

