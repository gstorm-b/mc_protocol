# mc_bench_session (spec SPEC-core-session.md, Project Structure: "tests/bench/bench_session.cpp
# -- encode-free round cost vs subscribed points, regression tracking only"; T-027): a plain
# executable, not a ctest test -- no add_test() here, matching the spec's own "no threshold".
# Only reachable when MC_BUILD_BENCH is ON (see the top-level CMakeLists.txt's own option() and
# tests/CMakeLists.txt's own guard around add_subdirectory(bench)); CMake-only, no qmake .pro
# (spec: "tests/bench is CMake-only").
add_executable(mc_bench_session
    bench_session.cpp
)
# Reuses tests/core/session/harness.h/.cpp directly (FakeClock/ScriptedPeer/PeerScript): it has no
# doctest dependency of its own, so it compiles cleanly into this plain (non-doctest) executable
# too. Its own include path (for #include "harness.h") mirrors mc_core_session_tests's own.
target_sources(mc_bench_session PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/session/harness.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/session/harness.cpp
)
target_include_directories(mc_bench_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/session)
target_link_libraries(mc_bench_session PRIVATE mc::core)
mc_apply_warnings(mc_bench_session)
