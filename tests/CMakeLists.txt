# doctest v2.5.3 (MIT), vendored single header from
# https://github.com/doctest/doctest/releases/tag/v2.5.3 — see third_party/doctest/LICENSE.txt.
add_library(mc_doctest INTERFACE)
target_include_directories(mc_doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
# MSVC only forward-declares std::ostream unless told otherwise; doctest's fallback
# stringification of a type with no StringMaker (e.g. std::string_view, used throughout
# core-model's public API) then instantiates the standard library's own operator<< against an
# incomplete std::basic_ostream and fails to compile (T-006). DOCTEST_CONFIG_USE_STD_HEADERS
# makes doctest include the real standard headers instead of forward-declaring them.
target_compile_definitions(mc_doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)

add_executable(mc_build_tests
    build/test_version.cpp
)
target_link_libraries(mc_build_tests PRIVATE mc::core mc_doctest)
mc_apply_warnings(mc_build_tests)

include(CTest)
add_test(NAME BLD-08.version_agrees COMMAND mc_build_tests)
set_tests_properties(BLD-08.version_agrees PROPERTIES LABELS "build")

# mc_core_model_tests (SPEC-core-model.md): one binary for the whole core-model module; later
# tasks (T06 onward) add more test_*.cpp sources here without adding another main.cpp (T-006).
add_executable(mc_core_model_tests
    core/model/main.cpp
    core/model/test_result.cpp
    core/model/test_device.cpp
    core/model/test_frame_config.cpp
    core/model/test_limits.cpp
    core/model/test_chunk.cpp
    core/model/test_convert.cpp
    core/model/test_log.cpp
    core/model/test_alloc.cpp
)
target_link_libraries(mc_core_model_tests PRIVATE mc::core mc_doctest)
# LIM-03 (test_limits.cpp) reads mc::detail's random-access limit rows through the private
# src/core/model/limits_table.h; mc::core only exports include/ publicly (PRIVATE src/), so the
# test binary gets its own PRIVATE include path to src/ rather than widening mc_core's own.
target_include_directories(mc_core_model_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
# ALC-01 (test_alloc.cpp) includes tests/common/alloc_counter.h as "common/alloc_counter.h"; give
# the binary its own include path to the tests/ root for that (mirrors the src/ path above).
target_include_directories(mc_core_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
mc_apply_warnings(mc_core_model_tests)
# Coverage data (.gcda) is only written when the instrumented process exits normally; this
# binary is what actually runs mc_core's instrumented code, so it needs --coverage at its own
# link step too (which pulls in libgcov), not just mc_core at its link step.
mc_apply_coverage(mc_core_model_tests)

# mc_coverage_report (T-012): runs cmake/mc_coverage_report.cmake over mc_core's own object
# files under src/core/model once MC_COVERAGE has produced .gcda data (i.e. after `ctest` has
# actually run mc_core_model_tests). Only defined under MC_COVERAGE=ON with GCC (Clang would
# need llvm-cov's gcov-compatible mode instead; not implemented, since this machine's only
# GCC/Clang compiler is MinGW GCC 13.1 — see build-env.md).
if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(_mc_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(MC_GCOV_EXECUTABLE NAMES gcov HINTS "${_mc_compiler_dir}")

    add_custom_target(mc_coverage_report
        COMMAND ${CMAKE_COMMAND}
            -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -D MC_OBJECT_BASE_DIR=${CMAKE_BINARY_DIR}/src/CMakeFiles/mc_core.dir/core/model
            -D MC_GCOV_EXECUTABLE=${MC_GCOV_EXECUTABLE}
            -D MC_MIN_COVERAGE=95
            -P ${PROJECT_SOURCE_DIR}/cmake/mc_coverage_report.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS mc_core_model_tests
        VERBATIM
    )
endif()

add_test(NAME core_model.mc_core_model_tests COMMAND mc_core_model_tests)
set_tests_properties(core_model.mc_core_model_tests PROPERTIES LABELS "core_model")

# tests/common/vectors.* (T-013, SPEC-core-protocol.md "Golden vector files"): the `.vec` loader,
# as its own small static library so mc_core_protocol_tests and later tests/mock, tests/replay
# binaries (SPEC-mock-plc.md) share one parser instead of each compiling its own copy.
add_library(mc_test_vectors STATIC
    common/vectors.h
    common/vectors.cpp
)
target_include_directories(mc_test_vectors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
mc_apply_warnings(mc_test_vectors)

# mc_core_protocol_tests (SPEC-core-protocol.md): one binary for the whole core-protocol module;
# later tasks (T-014 onward) add more test_*.cpp sources here without adding another main.cpp,
# mirroring mc_core_model_tests above (T-006).
add_executable(mc_core_protocol_tests
    core/protocol/main.cpp
    core/protocol/test_vectors_format.cpp
    core/protocol/test_primitives.cpp
    core/protocol/test_device_encode.cpp
    core/protocol/test_commands.cpp
    core/protocol/test_frame_3e.cpp
    core/protocol/test_frame_1e.cpp
    core/protocol/test_serial_parser.cpp
    core/protocol/test_frame_serial.cpp
    core/protocol/test_parser_stream.cpp
    core/protocol/test_alloc.cpp
)
target_link_libraries(mc_core_protocol_tests PRIVATE mc::core mc_doctest mc_test_vectors)
# test_vectors_format.cpp (VEC-02) walks tests/vectors/*.vec from the source tree, not the test
# binary's own working directory: ctest runs each binary from its own build tree, and the MSVC
# (build/cmake-debug) and MinGW (build/cmake-mingw) trees sit at different depths under the
# repository root, so a relative path from the executable would not be portable between them.
target_compile_definitions(mc_core_protocol_tests PRIVATE
    MC_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
# test_primitives.cpp (PRIM-*) reaches mc::detail's private hexascii/sumcheck/field_codec headers
# under src/core/protocol/; mc::core only exports include/ publicly (PRIVATE src/), so this
# binary gets its own PRIVATE include path to src/, mirroring mc_core_model_tests's LIM-03 include
# above (T-006).
target_include_directories(mc_core_protocol_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
mc_apply_warnings(mc_core_protocol_tests)
# See the matching comment on mc_core_model_tests above: this binary is what runs mc_core's
# instrumented protocol code once T-014 onward add it.
mc_apply_coverage(mc_core_protocol_tests)

add_test(NAME core_protocol.mc_core_protocol_tests COMMAND mc_core_protocol_tests)
set_tests_properties(core_protocol.mc_core_protocol_tests PROPERTIES LABELS "core_protocol")

# mc_coverage_report_protocol (T-019): the same script as mc_coverage_report above, pointed at
# src/core/protocol's own object files instead (built by this same mc_core target; mc_test_vectors
# is test-only code under tests/common/, not part of src/core/protocol, so it needs no coverage
# instrumentation and is not part of this report).
if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_custom_target(mc_coverage_report_protocol
        COMMAND ${CMAKE_COMMAND}
            -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -D MC_OBJECT_BASE_DIR=${CMAKE_BINARY_DIR}/src/CMakeFiles/mc_core.dir/core/protocol
            -D MC_GCOV_EXECUTABLE=${MC_GCOV_EXECUTABLE}
            -D MC_MIN_COVERAGE=95
            -D MC_REPORT_LABEL=src/core/protocol
            -P ${PROJECT_SOURCE_DIR}/cmake/mc_coverage_report.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS mc_core_protocol_tests
        VERBATIM
    )
endif()

# mc_core_session_tests (SPEC-core-session.md): one binary for the whole core-session module;
# later tasks (T18 onward) add more test_*.cpp sources here without adding another main.cpp,
# mirroring mc_core_model_tests/mc_core_protocol_tests above.
add_executable(mc_core_session_tests
    core/session/main.cpp
    core/session/harness.h
    core/session/harness.cpp
    core/session/test_range_set.cpp
    core/session/test_value_store.cpp
    core/session/test_session_poll.cpp
    core/session/test_session_adhoc.cpp
    core/session/test_session_fault.cpp
    core/session/test_session_heartbeat.cpp
    core/session/test_alloc.cpp
)
target_link_libraries(mc_core_session_tests PRIVATE mc::core mc_doctest mc_test_vectors)
# test_session_fault.cpp (serial rows, T-051) reads the 3C golden vectors tests/vectors/3c_f*.vec
# from the source tree, as mc_core_protocol_tests does above (the spec's Testing Strategy: serial
# behaviour "uses the golden vectors directly").
target_compile_definitions(mc_core_session_tests PRIVATE
    MC_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
# test_alloc.cpp (ALC-01/02) includes tests/common/alloc_counter.h as "common/alloc_counter.h";
# give the binary its own include path to the tests/ root for that, mirroring
# mc_core_model_tests/mc_core_protocol_tests above.
target_include_directories(mc_core_session_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# test_session_fault.cpp (SES-25, T-026 rework) reaches mc::detail's private
# src/core/session/drain_violation.h to install a recording handler; mc::core only exports
# include/ publicly (PRIVATE src/), so this binary gets its own PRIVATE include path to src/,
# mirroring mc_core_model_tests's LIM-03 / mc_core_protocol_tests's PRIM-* include above (T-006).
# tests/qmake/mc_core_session_tests.pro already has this path (it predates this addition).
target_include_directories(mc_core_session_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
mc_apply_warnings(mc_core_session_tests)
mc_apply_coverage(mc_core_session_tests)

add_test(NAME core_session.mc_core_session_tests COMMAND mc_core_session_tests)
set_tests_properties(core_session.mc_core_session_tests PROPERTIES LABELS "core_session")

# mc_coverage_report_session (T-028, Checkpoint C): the same script as mc_coverage_report /
# mc_coverage_report_protocol above, pointed at src/core/session's own object files instead.
if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_custom_target(mc_coverage_report_session
        COMMAND ${CMAKE_COMMAND}
            -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -D MC_OBJECT_BASE_DIR=${CMAKE_BINARY_DIR}/src/CMakeFiles/mc_core.dir/core/session
            -D MC_GCOV_EXECUTABLE=${MC_GCOV_EXECUTABLE}
            -D MC_MIN_COVERAGE=95
            -D MC_REPORT_LABEL=src/core/session
            -P ${PROJECT_SOURCE_DIR}/cmake/mc_coverage_report.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS mc_core_session_tests
        VERBATIM
    )
endif()

# mc_mock_tests (SPEC-mock-plc.md, label `mock`): one binary for the whole mock-plc module; a new
# test_*.cpp source of the module is added here without another main.cpp, mirroring
# mc_core_model_tests / mc_core_protocol_tests / mc_core_session_tests above.
if(MC_BUILD_MOCK)
    add_executable(mc_mock_tests
        mock/main.cpp
        mock/test_mock_memory.cpp
        mock/test_mock_vectors.cpp
        mock/test_mock_stream.cpp
        mock/test_mock_streams.cpp
        mock/test_mock_faults.cpp
    )
    target_link_libraries(mc_mock_tests PRIVATE mc::mock mc_doctest mc_test_vectors)
    # test_mock_memory.cpp reaches the private src/mock/memory_image.h; mc::mock only exports
    # include/ publicly (PRIVATE src/), so this binary gets its own PRIVATE include path to src/,
    # mirroring the other test binaries above (T-006).
    target_include_directories(mc_mock_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    # The vector-driven tests (T-030) read tests/vectors from the source tree, exactly like
    # mc_core_protocol_tests (T-013): never through the working directory.
    target_compile_definitions(mc_mock_tests PRIVATE
        MC_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    mc_apply_warnings(mc_mock_tests)
    mc_apply_coverage(mc_mock_tests)

    add_test(NAME mock.mc_mock_tests COMMAND mc_mock_tests)
    set_tests_properties(mock.mc_mock_tests PROPERTIES LABELS "mock")

    # mc_integration_tests (SPEC-mock-plc.md "Integration", T-032, label `integration`): Session
    # against MockPlc over seeded fragmenting pipes and a fake clock. Built here because both
    # MC_BUILD_MOCK and MC_BUILD_TESTS are on (this directory is only added under MC_BUILD_TESTS).
    # Public headers only: no private src/ path. The doctest main is the one mc_mock_tests uses.
    add_executable(mc_integration_tests
        mock/main.cpp
        mock/integration/pipe.h
        mock/integration/rig.h
        mock/integration/rig.cpp
        mock/integration/test_integration.cpp
    )
    target_link_libraries(mc_integration_tests PRIVATE mc::mock mc::core mc_doctest)
    mc_apply_warnings(mc_integration_tests)
    mc_apply_coverage(mc_integration_tests)

    add_test(NAME integration.mc_integration_tests COMMAND mc_integration_tests)
    set_tests_properties(integration.mc_integration_tests PROPERTIES LABELS "integration")

    # MCK-HYG (SPEC-mock-plc.md "Independence rule"): src/mock includes only what the rule allows.
    # The negative controls run the same script over tests/mock/hyg_fixture/, which holds one file
    # per kind of violation, so the check is proved to bite without editing the real sources.
    set(_mc_hyg_script ${CMAKE_CURRENT_SOURCE_DIR}/mock/check_mock_includes.cmake)
    add_test(NAME MCK-HYG.mock_includes
        COMMAND ${CMAKE_COMMAND} -D MC_MOCK_DIR=${PROJECT_SOURCE_DIR}/src/mock -P ${_mc_hyg_script})
    set_tests_properties(MCK-HYG.mock_includes PROPERTIES LABELS "mock"
        PASS_REGULAR_EXPRESSION "MCK-HYG: OK")
    add_test(NAME MCK-HYG.negative_control_exit_status
        COMMAND ${CMAKE_COMMAND} -D MC_MOCK_DIR=${CMAKE_CURRENT_SOURCE_DIR}/mock/hyg_fixture
            -P ${_mc_hyg_script})
    set_tests_properties(MCK-HYG.negative_control_exit_status PROPERTIES LABELS "mock" WILL_FAIL TRUE)
    foreach(_mc_case IN ITEMS
            "frame_include:bad_frame_include\\.cpp:[0-9]+: include"
            "command_include:bad_command_include\\.cpp:[0-9]+: include"
            "serial_parser_include:bad_serial_parser_include\\.cpp:[0-9]+: include"
            "angle_include:bad_angle_include\\.cpp:[0-9]+: include <core/protocol/frame_3e\\.h>"
            "client_use:bad_client_use\\.cpp:[0-9]+: uses McProtocol")
        string(REGEX MATCH "^([^:]+):(.*)$" _mc_unused "${_mc_case}")
        set(_mc_case_name "${CMAKE_MATCH_1}")
        set(_mc_case_regex "MCK-HYG ${CMAKE_MATCH_2}")
        add_test(NAME MCK-HYG.negative_control_${_mc_case_name}
            COMMAND ${CMAKE_COMMAND} -D MC_MOCK_DIR=${CMAKE_CURRENT_SOURCE_DIR}/mock/hyg_fixture
                -P ${_mc_hyg_script})
        set_tests_properties(MCK-HYG.negative_control_${_mc_case_name} PROPERTIES LABELS "mock"
            PASS_REGULAR_EXPRESSION "${_mc_case_regex}")
    endforeach()

    # mc_coverage_report_mock (T-029): the same script as the reports above, pointed at
    # src/mock's own object files (built by the mc_mock target), bar 90 % (SPEC-mock-plc.md).
    if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_custom_target(mc_coverage_report_mock
            COMMAND ${CMAKE_COMMAND}
                -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
                -D MC_OBJECT_BASE_DIR=${CMAKE_BINARY_DIR}/src/CMakeFiles/mc_mock.dir/mock
                -D MC_GCOV_EXECUTABLE=${MC_GCOV_EXECUTABLE}
                -D MC_MIN_COVERAGE=90
                -D MC_REPORT_LABEL=src/mock
                -P ${PROJECT_SOURCE_DIR}/cmake/mc_coverage_report.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            DEPENDS mc_mock_tests
            VERBATIM
        )
    endif()
endif()

# mc_replay_tests (SPEC-hil-capture.md "Replay tests", label `replay`): RPL-01..06 on capture
# folders, no hardware. Std-only: links mc::mock (and through it mc::core) and the .vec loader,
# never Qt. The committed fixtures (tests/hil/fixtures/, captures of examples/virtual_plc, not
# hardware) keep the checks running on every build; MC_REPLAY_SCRATCH_DIR is where the negative
# tests damage their copies.
if(MC_BUILD_MOCK)
    add_executable(mc_replay_tests
        hil/replay/replay_main.cpp
        hil/replay/replay_options.h
        hil/replay/replay.h
        hil/replay/replay.cpp
        hil/replay/replay_session.cpp
        hil/replay/replay_util.h
        hil/replay/replay_util.cpp
        hil/test_replay.cpp
    )
    target_link_libraries(mc_replay_tests PRIVATE mc::mock mc::core mc_doctest mc_test_vectors)
    target_include_directories(mc_replay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hil)
    target_compile_definitions(mc_replay_tests PRIVATE
        MC_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        MC_REPLAY_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/replay_scratch"
    )
    mc_apply_warnings(mc_replay_tests)
    mc_apply_coverage(mc_replay_tests)

    add_test(NAME replay.mc_replay_tests COMMAND mc_replay_tests)
    set_tests_properties(replay.mc_replay_tests PROPERTIES LABELS "replay")
endif()

# Device layer tests (SPEC-qt-device.md, label `device`): QtTest binaries, one per tests/device/
# tst_*.cpp (each has its own QTEST_GUILESS_MAIN), all loopback on 127.0.0.1. Built only when the
# device layer is, so a core-only configure (-DMC_BUILD_DEVICE=OFF) never searches for Qt.
if(MC_BUILD_DEVICE)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test Network)

    # A QtTest binary links the Qt DLLs; ctest runs it from the build tree, where the loader finds
    # them only through PATH. Prepend the directory of the Qt kit's own DLLs (the one the imported
    # Qt6::Core points at) to the test's PATH, so ctest works from any shell with no Qt on PATH.
    # ENVIRONMENT_MODIFICATION needs CMake 3.22; an older CMake leaves PATH untouched (the
    # developer then has the kit's bin directory on PATH, as before).
    function(mc_add_qt_test target)
        add_executable(${target} ${ARGN})
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
        target_link_libraries(${target} PRIVATE mc::device Qt6::Test)
        mc_apply_warnings(${target})
        mc_apply_coverage(${target})

        # MC_QT_TEST_GROUP (set by the caller before the call) names the ctest prefix and label;
        # the device tests keep the default, the tool tests (tests/hil) use hil_tool.
        set(_mc_group device)
        if(DEFINED MC_QT_TEST_GROUP)
            set(_mc_group ${MC_QT_TEST_GROUP})
        endif()
        add_test(NAME ${_mc_group}.${target} COMMAND ${target})
        set_tests_properties(${_mc_group}.${target} PROPERTIES LABELS "${_mc_group}" TIMEOUT 120)
        # QT_ASSUME_STDERR_HAS_CONSOLE: without it QtTest prints nothing when its output is a
        # pipe (ctest, an IDE, an agent shell) on Windows, so a failing test would say nothing.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            set(_mc_qt_env "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            set_tests_properties(${_mc_group}.${target} PROPERTIES ENVIRONMENT_MODIFICATION
                "${_mc_qt_env};QT_ASSUME_STDERR_HAS_CONSOLE=set:1")
        else()
            set_tests_properties(${_mc_group}.${target} PROPERTIES
                ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1")
        endif()
    endfunction()

    # T-035: TcpTransport contract, QDV-06 / QDV-12 at transport level, meta-type registration.
    mc_add_qt_test(mc_tcp_transport_tests device/tst_tcp_transport.cpp)

    # T-036: McDeviceConfig validate() and its JSON mapping (QDV-10, QDV-16 at validate level).
    mc_add_qt_test(mc_config_json_tests device/tst_config_json.cpp)

    # T-037/T-038: McDevice against a loopback MockPlcServer. These binaries need the mock, so
    # they exist only when MC_BUILD_MOCK is on as well; mc_device itself never links mc::mock.
    if(MC_BUILD_MOCK)
        mc_add_qt_test(mc_device_tests
            device/tst_mc_device.cpp
            device/mock_plc_server.cpp
            device/mock_plc_server.h
            device/device_recorder.h
            device/device_test_support.h
            device/smoke_check.h
            device/fake_transport.h)
        target_link_libraries(mc_device_tests PRIVATE mc::mock)

        # QDV-09: the device on a QThread the test owns (the library starts none).
        mc_add_qt_test(mc_device_thread_tests
            device/tst_mc_device_thread.cpp
            device/mock_plc_server.cpp
            device/mock_plc_server.h
            device/device_recorder.h
            device/device_test_support.h
            device/fake_transport.h)
        target_link_libraries(mc_device_thread_tests PRIVATE mc::mock)

        # QDV-14 (T-054): SerialTransport and McDevice over a COM port. The SER-nn tests need no
        # hardware; the ones that need a virtual COM pair QSKIP unless MC_TEST_SERIAL_PAIR names
        # one (e.g. "COM52,COM53"). The bridge hosting the MockPlc is why this binary links the
        # mock.
        mc_add_qt_test(mc_serial_tests
            device/tst_serial.cpp
            device/serial_bridge.cpp
            device/serial_bridge.h
            device/smoke_check.h
            device/device_recorder.h
            device/device_test_support.h)
        target_link_libraries(mc_serial_tests PRIVATE mc::mock)
        # Only this entry opens the COM pair; the lock keeps it from running next to any other
        # entry that ever does (a virtual port is opened exclusively, one process at a time).
        set_tests_properties(device.mc_serial_tests PROPERTIES RESOURCE_LOCK mc_serial_pair)

        # mc_hil_tool_tests (SPEC-hil-capture.md "Testing the tool itself", label hil_tool): the
        # logic of tools/hil_capture, no hardware. Built only with the tool, because it links the
        # tool's own static library (mc_hil_tool, which holds every source except main.cpp). One
        # binary, one QtTest class per tests/hil/tst_*.cpp, run in turn by
        # tests/hil/hil_tests_main.cpp.
        if(MC_BUILD_TOOLS)
            set(MC_QT_TEST_GROUP hil_tool)
            mc_add_qt_test(mc_hil_tool_tests
                hil/hil_tests_main.cpp
                hil/hil_suites.h
                hil/hil_test_support.h
                hil/tst_hil_profile.cpp
                hil/tst_hil_gate.cpp
                hil/tst_hil_capture.cpp
                hil/tst_hil_plans.cpp
                hil/tst_hil_run.cpp)
            unset(MC_QT_TEST_GROUP)
            target_link_libraries(mc_hil_tool_tests PRIVATE mc_hil_tool mc::mock mc_test_vectors)
            target_include_directories(mc_hil_tool_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hil)
            # Profiles and plans are read from the source tree, never through the working directory.
            # What a test writes goes under the build tree, never to tests/vectors/captured/.
            target_compile_definitions(mc_hil_tool_tests PRIVATE
                MC_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                MC_HIL_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/hil_out")
            # The end-to-end tests start examples/virtual_plc (a separate program, built when the
            # examples are); without it they skip. The serial suite is its own ctest entry: it opens
            # the virtual COM pair, so it holds the same RESOURCE_LOCK as every other entry that
            # does.
            if(MC_BUILD_EXAMPLES)
                target_compile_definitions(mc_hil_tool_tests PRIVATE
                    "MC_VIRTUAL_PLC_PATH=\"$<TARGET_FILE:virtual_plc>\"")
                add_dependencies(mc_hil_tool_tests virtual_plc)
            else()
                target_compile_definitions(mc_hil_tool_tests PRIVATE MC_VIRTUAL_PLC_PATH="")
            endif()
            # HIL-04, replay half: the end-to-end test runs mc_replay_tests on the capture it wrote.
            if(TARGET mc_replay_tests)
                target_compile_definitions(mc_hil_tool_tests PRIVATE
                    "MC_REPLAY_TESTS_PATH=\"$<TARGET_FILE:mc_replay_tests>\"")
                add_dependencies(mc_hil_tool_tests mc_replay_tests)
            else()
                target_compile_definitions(mc_hil_tool_tests PRIVATE MC_REPLAY_TESTS_PATH="")
            endif()
            add_test(NAME hil_tool.mc_hil_tool_serial COMMAND mc_hil_tool_tests --suite serial)
            set_tests_properties(hil_tool.mc_hil_tool_serial PROPERTIES
                LABELS "hil_tool" TIMEOUT 120 RESOURCE_LOCK mc_serial_pair)
            if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
                set(_mc_qt_env "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
                set_tests_properties(hil_tool.mc_hil_tool_serial PROPERTIES
                    ENVIRONMENT_MODIFICATION "${_mc_qt_env};QT_ASSUME_STDERR_HAS_CONSOLE=set:1")
            else()
                set_tests_properties(hil_tool.mc_hil_tool_serial PROPERTIES
                    ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1")
            endif()

            # hil_fault_proxy: a fixture-generation helper (tests/hil/fixtures/make), not a test. It
            # makes a link fault on purpose when a replay fixture has to be captured again. Built so
            # it cannot rot; no ctest entry and no qmake twin.
            add_executable(hil_fault_proxy hil/fixtures/make/fault_proxy.cpp)
            target_link_libraries(hil_fault_proxy PRIVATE Qt6::Core Qt6::Network)
            mc_apply_warnings(hil_fault_proxy)

            # mc_workbench_tests (SPEC-gui-tool.md "Testing", label gui): the GUI cases, offscreen.
            # Exists only when tools/ enabled the GUI (Qt >= 6.5, docking library found). The
            # docking library is a DLL the test finds through PATH; QT_QPA_PLATFORM makes the run
            # headless (the test's own main() sets it too, for runs outside ctest).
            if(TARGET mc_workbench_ui)
                find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
                set(MC_QT_TEST_GROUP gui)
                mc_add_qt_test(mc_workbench_tests
                    gui/gui_tests_main.cpp
                    gui/gui_suites.h
                    gui/tst_gui_smoke.cpp
                    gui/tst_gui_runners.cpp
                    gui/tst_gui_device_tab.cpp
                    gui/tst_gui_mock_tab.cpp
                    gui/gui_test_support.h
                    gui/tst_gui_trace.cpp
                    gui/tst_gui_backpressure.cpp
                    gui/tst_gui_hil.cpp
                    gui/tst_gui_workspace.cpp
                    gui/tst_gui_virtual_plc.cpp
                    gui/tst_gui_mock_streams.cpp
                    gui/tst_gui_tester_probes.cpp)
                unset(MC_QT_TEST_GROUP)
                target_link_libraries(mc_workbench_tests PRIVATE mc_workbench_ui Qt6::Widgets)
                target_include_directories(mc_workbench_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
                # What a test writes goes under the build tree, never to tests/vectors/captured/.
                target_compile_definitions(mc_workbench_tests PRIVATE
                    MC_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                    MC_GUI_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/gui_out")
                # T-074: the GUI is also run against examples/virtual_plc, started as a process
                # (built when the examples are; without it those cases skip).
                if(MC_BUILD_EXAMPLES)
                    target_compile_definitions(mc_workbench_tests PRIVATE
                        "MC_VIRTUAL_PLC_PATH=\"$<TARGET_FILE:virtual_plc>\"")
                    add_dependencies(mc_workbench_tests virtual_plc)
                else()
                    target_compile_definitions(mc_workbench_tests PRIVATE MC_VIRTUAL_PLC_PATH="")
                endif()
                # T-074: the program itself is started once (GUI_09_theProgramStartsAndStaysUp).
                target_compile_definitions(mc_workbench_tests PRIVATE
                    "MC_WORKBENCH_PATH=\"$<TARGET_FILE:mc_workbench>\"")
                add_dependencies(mc_workbench_tests mc_workbench)
                # A capture made in the GUI is replayed with mc_replay_tests (T-071).
                if(TARGET mc_replay_tests)
                    target_compile_definitions(mc_workbench_tests PRIVATE
                        "MC_REPLAY_TESTS_PATH=\"$<TARGET_FILE:mc_replay_tests>\"")
                    add_dependencies(mc_workbench_tests mc_replay_tests)
                else()
                    target_compile_definitions(mc_workbench_tests PRIVATE MC_REPLAY_TESTS_PATH="")
                endif()
                set_tests_properties(gui.mc_workbench_tests PROPERTIES ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ads::qtadvanceddocking-qt6>;QT_ASSUME_STDERR_HAS_CONSOLE=set:1;QT_QPA_PLATFORM=set:offscreen")
                # GUI-03 holds the first port of MC_TEST_SERIAL_PAIR open (when it is set), like every
                # other entry that opens the pair.
                set_tests_properties(gui.mc_workbench_tests PROPERTIES RESOURCE_LOCK mc_serial_pair)
                # GUI-05 floods a mock and a device on four threads and measures the GUI event gap: it must
                # not share the machine with the timing-sensitive cases of the other binaries (a loaded run
                # showed a GUI gap of 370 ms). RUN_SERIAL runs it alone.
                set_tests_properties(gui.mc_workbench_tests PROPERTIES RUN_SERIAL TRUE)

                # mc_coverage_report_gui_core / _gui_ui (T-074, Checkpoint H): the same script as the
                # reports above, pointed at the object files of tools/mc_workbench's two libraries.
                # REPORT ONLY: the spec sets no bar for the GUI tool (MC_MIN_COVERAGE 0). Needs .gcda
                # data, i.e. `ctest -L gui` must have run in the MC_COVERAGE=ON tree first.
                if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                    foreach(_mc_gui_lib core ui)
                        add_custom_target(mc_coverage_report_gui_${_mc_gui_lib}
                            COMMAND ${CMAKE_COMMAND}
                                -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
                                -D MC_OBJECT_BASE_DIR=${CMAKE_BINARY_DIR}/tools/mc_workbench/CMakeFiles/mc_workbench_${_mc_gui_lib}.dir
                                -D MC_GCOV_EXECUTABLE=${MC_GCOV_EXECUTABLE}
                                -D MC_MIN_COVERAGE=0
                                -D MC_REPORT_LABEL=tools/mc_workbench_${_mc_gui_lib}
                                -P ${PROJECT_SOURCE_DIR}/cmake/mc_coverage_report.cmake
                            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                            DEPENDS mc_workbench_tests
                            VERBATIM
                        )
                    endforeach()
                endif()
            endif()
        endif()
    endif()

    # mc_coverage_report_device (T-040, Checkpoint D): the same script as the reports above,
    # pointed at src/device's own object files. REPORT ONLY: the spec sets no bar for the device
    # layer, so MC_MIN_COVERAGE is 0 and the target never fails on the figure; it prints every
    # file and the total. Needs .gcda data, i.e. `ctest -L device` must have run in the
    # MC_COVERAGE=ON tree first (the QtTest binaries are instrumented through mc_add_qt_test).
    if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_mc_device_coverage_deps mc_tcp_transport_tests mc_config_json_tests)
        if(MC_BUILD_MOCK)
            list(APPEND _mc_device_coverage_deps mc_device_tests mc_device_thread_tests
                 mc_serial_tests)
        endif()
        add_custom_target(mc_coverage_report_device
            COMMAND ${CMAKE_COMMAND}
                -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
                -D MC_OBJECT_BASE_DIR=${CMAKE_BINARY_DIR}/src/CMakeFiles/mc_device.dir/device
                -D MC_GCOV_EXECUTABLE=${MC_GCOV_EXECUTABLE}
                -D MC_MIN_COVERAGE=0
                -D MC_REPORT_LABEL=src/device
                -P ${PROJECT_SOURCE_DIR}/cmake/mc_coverage_report.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            DEPENDS ${_mc_device_coverage_deps}
            VERBATIM
        )
    endif()

    # QDV-HYG (SPEC-qt-device.md, success criterion 2): src/device holds no blocking wait, nested
    # event loop, thread or mutex token. Negative controls run the same script over
    # tests/device/hyg_fixture/, one file per kind of violation (never compiled), so the check is
    # proved to bite without editing the real sources; same pattern as MCK-HYG.
    set(_mc_qdv_hyg_script ${CMAKE_CURRENT_SOURCE_DIR}/device/check_device_tokens.cmake)
    add_test(NAME QDV-HYG.device_tokens
        COMMAND ${CMAKE_COMMAND} -D MC_DEVICE_DIR=${PROJECT_SOURCE_DIR}/src/device
            -P ${_mc_qdv_hyg_script})
    set_tests_properties(QDV-HYG.device_tokens PROPERTIES LABELS "device"
        PASS_REGULAR_EXPRESSION "QDV-HYG: OK")
    # The public headers are scanned too: an inline wait in a header would run in the consumer.
    add_test(NAME QDV-HYG.device_tokens_headers
        COMMAND ${CMAKE_COMMAND} -D MC_DEVICE_DIR=${PROJECT_SOURCE_DIR}/include/mc/device
            -P ${_mc_qdv_hyg_script})
    set_tests_properties(QDV-HYG.device_tokens_headers PROPERTIES LABELS "device"
        PASS_REGULAR_EXPRESSION "QDV-HYG: OK")
    add_test(NAME QDV-HYG.negative_control_exit_status
        COMMAND ${CMAKE_COMMAND} -D MC_DEVICE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/device/hyg_fixture
            -P ${_mc_qdv_hyg_script})
    set_tests_properties(QDV-HYG.negative_control_exit_status PROPERTIES LABELS "device"
        WILL_FAIL TRUE)
    foreach(_mc_case IN ITEMS
            "wait_for:bad_wait_for\\.cpp:[0-9]+: uses waitFor"
            "event_loop:bad_event_loop\\.cpp:[0-9]+: uses QEventLoop"
            "thread:bad_thread\\.cpp:[0-9]+: uses QThread"
            "mutex:bad_mutex\\.cpp:[0-9]+: uses QMutex")
        string(REGEX MATCH "^([^:]+):(.*)$" _mc_unused "${_mc_case}")
        set(_mc_case_name "${CMAKE_MATCH_1}")
        set(_mc_case_regex "QDV-HYG ${CMAKE_MATCH_2}")
        add_test(NAME QDV-HYG.negative_control_${_mc_case_name}
            COMMAND ${CMAKE_COMMAND} -D MC_DEVICE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/device/hyg_fixture
                -P ${_mc_qdv_hyg_script})
        set_tests_properties(QDV-HYG.negative_control_${_mc_case_name} PROPERTIES LABELS "device"
            PASS_REGULAR_EXPRESSION "${_mc_case_regex}")
    endforeach()
endif()

# BLD-04 pri_sync and BLD-05 include_hygiene: cmake -P guard scripts run as ctest entries so
# `ctest -L build` alone keeps the two build systems and the public surface honest. Neither
# depends on a compiled binary, so they run even when nothing else has been built yet.
add_test(NAME BLD-04.pri_sync
    COMMAND ${CMAKE_COMMAND}
        -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -D MC_SOURCES_FILE=${CMAKE_BINARY_DIR}/mc_sources.txt
        -P ${PROJECT_SOURCE_DIR}/cmake/check_pri_sync.cmake
)
set_tests_properties(BLD-04.pri_sync PROPERTIES LABELS "build")

add_test(NAME BLD-05.include_hygiene
    COMMAND ${CMAKE_COMMAND}
        -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -D MC_BINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/check_include_hygiene.cmake
)
set_tests_properties(BLD-05.include_hygiene PROPERTIES LABELS "build")

# BLD-05 rule (d), link side: the same script fed a fixture mc_device_link_libraries.txt instead of
# the real one (MC_BINARY_DIR points at tests/build/link_fixture/<case>), so the guard is proved to
# bite without editing the real mc_device link line. The alias spelling (mc::mock) is the one this
# project uses; a guard that matched only the target name once let it through (T-040 rework).
foreach(_mc_case IN ITEMS alias plain)
    add_test(NAME BLD-05.negative_control_link_${_mc_case}
        COMMAND ${CMAKE_COMMAND}
            -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -D MC_BINARY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/build/link_fixture/${_mc_case}
            -P ${PROJECT_SOURCE_DIR}/cmake/check_include_hygiene.cmake)
    set_tests_properties(BLD-05.negative_control_link_${_mc_case} PROPERTIES LABELS "build"
        PASS_REGULAR_EXPRESSION "rule \\(d\\): mc_device LINK_LIBRARIES contains mc(_|::)mock")
endforeach()
# ... and a link list without the mock passes (the fixture is not what makes the others fail).
add_test(NAME BLD-05.positive_control_link_clean
    COMMAND ${CMAKE_COMMAND}
        -D MC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -D MC_BINARY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/build/link_fixture/clean
        -P ${PROJECT_SOURCE_DIR}/cmake/check_include_hygiene.cmake)
set_tests_properties(BLD-05.positive_control_link_clean PROPERTIES LABELS "build"
    PASS_REGULAR_EXPRESSION "check_include_hygiene: OK")

# tests/bench (T-027, MC_BUILD_BENCH): mc_bench_session is a plain executable, not a ctest test
# (spec: "no threshold") -- nested here (rather than added directly from the top-level
# CMakeLists.txt) since it is physically under tests/, and the option's own default (OFF) means
# this add_subdirectory() is a no-op for every other target/task's own build.
if(MC_BUILD_BENCH)
    add_subdirectory(bench)
endif()
