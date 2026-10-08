# tests/consumer_cmake/CMakeLists.txt — BLD-06: exercises the CMake consumption path exactly
# as SPEC-build-packaging.md documents it: add_subdirectory() + target_link_libraries(mc::device).
# Configured and built as its OWN project (its CMAKE_SOURCE_DIR is this folder), so inside
# mc_protocol's own CMakeLists.txt, CMAKE_SOURCE_DIR != CMAKE_CURRENT_SOURCE_DIR and
# MC_IS_TOP_LEVEL is OFF: MC_BUILD_TESTS and MC_BUILD_EXAMPLES default OFF there, so this
# build tree gets no library test or example target.
cmake_minimum_required(VERSION 3.16)
project(mc_consumer_cmake LANGUAGES CXX)

# Imported Qt targets are directory-scoped, so the consumer finds Qt itself: Qt 6.2+ or 5.15,
# whichever CMAKE_PREFIX_PATH points at. The library's own search then reuses the cached QT_DIR,
# so both sides use the same Qt.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if(QT_VERSION_MAJOR EQUAL 5)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network SerialPort)
else()
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network SerialPort)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. mc_protocol)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE mc::device)
