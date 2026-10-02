# Developer tools (SPEC-hil-capture.md, SPEC-build-packaging.md "Who may link mc::mock").
# Added from the top-level CMakeLists.txt under MC_BUILD_TOOLS only, which is ON when this folder
# is the top-level project and both the device layer and the mock are built.

# Imported Qt targets are directory-scoped, so this directory finds Qt itself.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network SerialPort)

# The tool's logic is a static library of its own so the program (hil_capture) and the QtTest
# binary mc_hil_tool_tests (tests/hil) compile the very same sources once. The test binary links
# this library instead of repeating a source list. Headers are included as "hil_capture/x.h";
# this directory is the include root.
set(MC_HIL_TOOL_SOURCES
    hil_capture/json_reader.h
    hil_capture/profile.h
    hil_capture/profile.cpp
    hil_capture/plan.h
    hil_capture/plan.cpp
    hil_capture/resolve.h
    hil_capture/resolve.cpp
    hil_capture/safety_gate.h
    hil_capture/safety_gate.cpp
    hil_capture/dry_run.h
    hil_capture/dry_run.cpp
    hil_capture/recording_transport.h
    hil_capture/recording_transport.cpp
    hil_capture/capture_writer.h
    hil_capture/capture_writer.cpp
    hil_capture/bench_report.h
    hil_capture/bench_report.cpp
    hil_capture/runner.h
    hil_capture/runner.cpp
    hil_capture/options.h
    hil_capture/options.cpp
    hil_capture/tool.h
    hil_capture/tool.cpp
)

add_library(mc_hil_tool STATIC ${MC_HIL_TOOL_SOURCES})
set_target_properties(mc_hil_tool PROPERTIES AUTOMOC ON)
target_include_directories(mc_hil_tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mc_hil_tool PUBLIC mc::device mc::mock Qt6::Core)
mc_apply_warnings(mc_hil_tool)
mc_apply_coverage(mc_hil_tool)

add_executable(hil_capture hil_capture/main.cpp)
target_link_libraries(hil_capture PRIVATE mc_hil_tool)
mc_apply_warnings(hil_capture)
mc_apply_coverage(hil_capture)
