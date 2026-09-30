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
