# mc_coverage.cmake — mc_apply_coverage(target): adds gcov-style --coverage instrumentation to
# `target` when MC_COVERAGE is ON, on GCC or Clang only (CMakeLists.txt's own option help:
# "GCC/Clang only: --coverage on core targets and tests"). No effect when MC_COVERAGE is OFF.
# On MSVC (no gcov equivalent this project uses), prints one clear message instead of silently
# doing nothing, so an MSVC build with -DMC_COVERAGE=ON does not look like it measured anything.
function(mc_apply_coverage target)
    if(NOT MC_COVERAGE)
        return()
    endif()

    if(MSVC)
        if(NOT MC_COVERAGE_MSVC_WARNED)
            message(STATUS "MC_COVERAGE=ON has no effect under MSVC (GCC/Clang only, spec "
                            "SPEC-build-packaging.md); ${target} and later targets build "
                            "without coverage instrumentation.")
            set(MC_COVERAGE_MSVC_WARNED TRUE CACHE INTERNAL "mc_apply_coverage MSVC notice shown")
        endif()
        return()
    endif()

    # -O0: coverage counts must reflect the source as written; inlining/dead-code elimination at
    # higher optimization levels can merge or drop lines gcov would otherwise count separately.
    # --coverage covers both the compile step (-fprofile-arcs -ftest-coverage) and the link step
    # (pulls in libgcov); passing it to both option kinds is the standard, documented usage.
    target_compile_options(${target} PRIVATE --coverage -O0)
    target_link_options(${target} PRIVATE --coverage)
endfunction()
