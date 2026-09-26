# tests/consumer_cmake/CMakeLists.txt — BLD-06: exercises the CMake consumption path exactly
# as SPEC-build-packaging.md documents it: add_subdirectory() + target_link_libraries(mc::core).
# Configured and built as its OWN project (its CMAKE_SOURCE_DIR is this folder), so inside
# mc_protocol's own CMakeLists.txt, CMAKE_SOURCE_DIR != CMAKE_CURRENT_SOURCE_DIR and
# MC_IS_TOP_LEVEL is OFF: MC_BUILD_TESTS and MC_BUILD_EXAMPLES default OFF there, so this
# build tree gets no library test or example target.
cmake_minimum_required(VERSION 3.16)
project(mc_consumer_cmake LANGUAGES CXX)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. mc_protocol)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE mc::core)
