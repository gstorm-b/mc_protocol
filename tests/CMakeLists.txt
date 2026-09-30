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
target_link_libraries(mc_core_session_tests PRIVATE mc::core mc_doctest)
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

        add_test(NAME device.${target} COMMAND ${target})
        set_tests_properties(device.${target} PROPERTIES LABELS "device" TIMEOUT 120)
        # QT_ASSUME_STDERR_HAS_CONSOLE: without it QtTest prints nothing when its output is a
        # pipe (ctest, an IDE, an agent shell) on Windows, so a failing test would say nothing.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            set_tests_properties(device.${target} PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_ASSUME_STDERR_HAS_CONSOLE=set:1")
        else()
            set_tests_properties(device.${target} PROPERTIES
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
    endif()

    # mc_coverage_report_device (T-040, Checkpoint D): the same script as the reports above,
    # pointed at src/device's own object files. REPORT ONLY: the spec sets no bar for the device
    # layer, so MC_MIN_COVERAGE is 0 and the target never fails on the figure; it prints every
    # file and the total. Needs .gcda data, i.e. `ctest -L device` must have run in the
    # MC_COVERAGE=ON tree first (the QtTest binaries are instrumented through mc_add_qt_test).
    if(MC_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_mc_device_coverage_deps mc_tcp_transport_tests mc_config_json_tests)
        if(MC_BUILD_MOCK)
            list(APPEND _mc_device_coverage_deps mc_device_tests mc_device_thread_tests)
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
