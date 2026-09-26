# doctest v2.5.3 (MIT), vendored single header from
# https://github.com/doctest/doctest/releases/tag/v2.5.3 — see third_party/doctest/LICENSE.txt.
add_library(mc_doctest INTERFACE)
target_include_directories(mc_doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_executable(mc_build_tests
    build/test_version.cpp
)
target_link_libraries(mc_build_tests PRIVATE mc::core mc_doctest)
mc_apply_warnings(mc_build_tests)

include(CTest)
add_test(NAME BLD-08.version_agrees COMMAND mc_build_tests)
set_tests_properties(BLD-08.version_agrees PROPERTIES LABELS "build")

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
