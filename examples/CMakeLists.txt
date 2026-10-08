# Example programs (SPEC-build-packaging.md). Added from the top-level CMakeLists.txt under
# MC_BUILD_EXAMPLES; an example that links mc::mock is built only when MC_BUILD_MOCK is on.

if(MC_BUILD_MOCK)
    # session_loop (SPEC-core-session.md "Non-Qt usage"): Session against MockPlc through an
    # in-memory pipe, std::chrono::steady_clock, no Qt, no header from tests/.
    add_executable(session_loop session_loop/main.cpp)
    target_link_libraries(session_loop PRIVATE mc::mock mc::core)
    set_target_properties(session_loop PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/session_loop)
    mc_apply_warnings(session_loop)
    # Under MC_COVERAGE the instrumented libraries need libgcov at this link step too.
    mc_apply_coverage(session_loop)

    # Smoke test: the example must exit 0 (three rounds, no link fault). Registered here, next to
    # the target; the parent CMakeLists.txt enables testing before adding this directory.
    if(MC_BUILD_TESTS)
        add_test(NAME examples.session_loop COMMAND session_loop)
        set_tests_properties(examples.session_loop PROPERTIES
            LABELS "integration"
            PASS_REGULAR_EXPRESSION "done after 3 rounds"
            TIMEOUT 30)
    endif()
endif()

# Qt console demo (SPEC-qt-device.md success criterion 4): virtual_plc serves a MockPlc over TCP and
# qt_console_poller drives an McDevice against it. Both link mc::device and mc::mock (the demo PLC
# is the mock); mc_device itself never links mc::mock. Built only when both layers are on; the
# executables land in this directory (build/<dir>/examples/), as the spec's commands show.
if(MC_BUILD_DEVICE AND MC_BUILD_MOCK)
    # Imported Qt targets are directory-scoped, so this directory finds Qt itself.
    mc_find_qt(Core Network SerialPort)

    add_executable(virtual_plc virtual_plc/main.cpp)
    target_link_libraries(virtual_plc PRIVATE mc::device mc::mock)
    mc_apply_warnings(virtual_plc)
    mc_apply_coverage(virtual_plc)

    add_executable(qt_console_poller qt_console_poller/main.cpp)
    target_link_libraries(qt_console_poller PRIVATE mc::device mc::mock)
    mc_apply_warnings(qt_console_poller)
    mc_apply_coverage(qt_console_poller)
endif()
