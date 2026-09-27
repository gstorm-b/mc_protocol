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
