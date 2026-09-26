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
