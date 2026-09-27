# mc_coverage_report.cmake — T-012: runs gcov over mc_core's own object files under
# MC_OBJECT_BASE_DIR (originally always src/core/model; T-019 reuses the same script for
# src/core/protocol too, as a second cmake target -- see tests/CMakeLists.txt's
# mc_coverage_report_protocol) and reports per-file and total line coverage, failing if the
# total is below MC_MIN_COVERAGE. No Python on this machine (see build-env.md), hence gcov +
# this script instead of gcovr.
#
# Requires .gcda data to already exist next to the .gcno/.obj files under MC_OBJECT_BASE_DIR —
# i.e. the coverage-instrumented test binary for that directory must have already been run once
# (`ctest`), not just built. Building the target this script is wired to (tests/CMakeLists.txt)
# does not itself run ctest; that is a separate, explicit step.
#
# Run as: cmake -D MC_SOURCE_DIR=<repo root> -D MC_OBJECT_BASE_DIR=<obj dir>
#               -D MC_GCOV_EXECUTABLE=<path to gcov> [-D MC_MIN_COVERAGE=95]
#               [-D MC_REPORT_LABEL=<display name, default "src/core/model">]
#               -P mc_coverage_report.cmake

# cmake -P script mode does not pick up policies from a project()'s cmake_minimum_required(); set
# it here too, and because get_filename_component's NAME_WLE keyword needs 3.14+.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED MC_SOURCE_DIR)
    message(FATAL_ERROR "mc_coverage_report: MC_SOURCE_DIR not set")
endif()
if(NOT DEFINED MC_OBJECT_BASE_DIR)
    message(FATAL_ERROR "mc_coverage_report: MC_OBJECT_BASE_DIR not set")
endif()
if(NOT MC_GCOV_EXECUTABLE OR NOT EXISTS "${MC_GCOV_EXECUTABLE}")
    message(FATAL_ERROR "mc_coverage_report: MC_GCOV_EXECUTABLE not found "
                         "('${MC_GCOV_EXECUTABLE}'); this script needs GCC's gcov (a Clang build "
                         "would need llvm-cov instead, not implemented here — see Dev notes, "
                         "T-012).")
endif()
if(NOT DEFINED MC_MIN_COVERAGE)
    set(MC_MIN_COVERAGE 95)
endif()
# T-019: which source directory MC_OBJECT_BASE_DIR's .gcda files belong to, for the report's own
# banner line only (MC_OBJECT_BASE_DIR itself already told this script exactly where to find
# them; this is display text, so a second target -- core-protocol's own coverage, alongside
# core-model's original one -- prints an accurate label instead of a misleading copy-paste one).
# Default unchanged: omitting it reproduces exactly T-012's original "src/core/model" banner.
if(NOT DEFINED MC_REPORT_LABEL)
    set(MC_REPORT_LABEL "src/core/model")
endif()

file(GLOB _mc_gcda_files "${MC_OBJECT_BASE_DIR}/*.cpp.gcda")
if(NOT _mc_gcda_files)
    message(FATAL_ERROR "mc_coverage_report: no .gcda files under ${MC_OBJECT_BASE_DIR} — build "
                         "with -DMC_COVERAGE=ON and run ctest (not just cmake --build) first.")
endif()
list(SORT _mc_gcda_files)

# gcov writes "<source-file-name>.gcov" (here: "<name>.cpp.gcov") into its working directory;
# run it in a dedicated scratch folder next to the objects so nothing lands in the repo itself,
# and remove that folder again once every summary below has been read out of it.
set(_mc_report_dir "${MC_OBJECT_BASE_DIR}/mc_coverage_report_gcov")
file(REMOVE_RECURSE "${_mc_report_dir}")
file(MAKE_DIRECTORY "${_mc_report_dir}")

set(_mc_total_covered 0)
set(_mc_total_lines 0)
set(_mc_report_lines "")

foreach(_mc_gcda IN LISTS _mc_gcda_files)
    # "device_table.cpp.gcda" -> "device_table.cpp" (NAME_WLE strips only the last extension;
    # NAME_WE would also strip ".cpp", which would not match the .gcov file gcov actually writes).
    get_filename_component(_mc_source_stem "${_mc_gcda}" NAME_WLE)

    execute_process(
        COMMAND "${MC_GCOV_EXECUTABLE}" "${_mc_gcda}"
        WORKING_DIRECTORY "${_mc_report_dir}"
        OUTPUT_QUIET
        ERROR_QUIET
        RESULT_VARIABLE _mc_gcov_result
    )
    if(NOT _mc_gcov_result EQUAL 0)
        message(FATAL_ERROR
                "mc_coverage_report: gcov failed (exit ${_mc_gcov_result}) on ${_mc_gcda}")
    endif()

    set(_mc_gcov_file "${_mc_report_dir}/${_mc_source_stem}.gcov")
    if(NOT EXISTS "${_mc_gcov_file}")
        message(FATAL_ERROR
                "mc_coverage_report: expected ${_mc_gcov_file} after running gcov, not found")
    endif()

    # Annotated-file line counting (exact) rather than parsing gcov's own "Lines executed:XX.XX%"
    # summary text (which would need reconstructing an integer count from a rounded percentage):
    # each line is "<marker>:<lineno>:<source text>"; marker is "-" (non-executable), "#####"
    # (executable, never hit) or an execution count. Lineno 0 marks gcov's own header lines
    # ("-:0:Source:...", "-:0:Graph:...", ...), not source lines, and is skipped.
    #
    # T-028 (Checkpoint C): reading the whole file with file(STRINGS) into one line-per-item list
    # (as this used to do) is not safe here -- a source file's own gcov annotation carries that
    # line's full text along with the marker/lineno prefix, and CMake's list representation is
    # just a semicolon-joined string; an unescaped ';' inside some *other* line's own text can
    # make file(STRINGS)'s result list silently merge that line with neighbouring ones (observed
    # on src/core/session/read_plan.cpp.gcov: file(STRINGS) returned 26 "lines" for a 267-line
    # file, most of it swallowed into one giant merged item, so this whole file's own
    # total/covered count came out 0/0 -- silently *passing* as "100%" instead of reporting its
    # real, still-under-95%-for-the-module total). Matching only the short "<marker>:<lineno>:"
    # prefix directly out of the raw file content (never materializing a line's own free-text
    # portion as a list item at all) sidesteps the whole class of bug regardless of what any
    # particular line's own text contains.
    file(READ "${_mc_gcov_file}" _mc_gcov_content)
    string(REGEX MATCHALL "\n[ \t]*[^:\n]+:[ \t]*[0-9]+:" _mc_gcov_matches "${_mc_gcov_content}")
    set(_mc_file_covered 0)
    set(_mc_file_total 0)
    foreach(_mc_match IN LISTS _mc_gcov_matches)
        if(_mc_match MATCHES "^\n[ \t]*([^:]+):[ \t]*([0-9]+):")
            set(_mc_marker "${CMAKE_MATCH_1}")
            set(_mc_lineno "${CMAKE_MATCH_2}")
            string(STRIP "${_mc_marker}" _mc_marker)
            if(NOT _mc_lineno EQUAL 0 AND NOT _mc_marker STREQUAL "-")
                math(EXPR _mc_file_total "${_mc_file_total} + 1")
                if(NOT _mc_marker STREQUAL "#####")
                    math(EXPR _mc_file_covered "${_mc_file_covered} + 1")
                endif()
            endif()
        endif()
    endforeach()

    math(EXPR _mc_total_covered "${_mc_total_covered} + ${_mc_file_covered}")
    math(EXPR _mc_total_lines "${_mc_total_lines} + ${_mc_file_total}")

    if(_mc_file_total GREATER 0)
        math(EXPR _mc_pct_x100 "(${_mc_file_covered} * 10000) / ${_mc_file_total}")
    else()
        set(_mc_pct_x100 10000)
    endif()
    math(EXPR _mc_pct_int "${_mc_pct_x100} / 100")
    math(EXPR _mc_pct_frac "${_mc_pct_x100} % 100")
    if(_mc_pct_frac LESS 10)
        set(_mc_pct_frac "0${_mc_pct_frac}")
    endif()
    set(_mc_pct_text "${_mc_pct_int}.${_mc_pct_frac}%")
    list(APPEND _mc_report_lines
         "  ${_mc_source_stem}: ${_mc_file_covered}/${_mc_file_total} (${_mc_pct_text})")
endforeach()

file(REMOVE_RECURSE "${_mc_report_dir}")

if(_mc_total_lines GREATER 0)
    math(EXPR _mc_total_pct_x100 "(${_mc_total_covered} * 10000) / ${_mc_total_lines}")
else()
    set(_mc_total_pct_x100 10000)
endif()
math(EXPR _mc_total_pct_int "${_mc_total_pct_x100} / 100")
math(EXPR _mc_total_pct_frac "${_mc_total_pct_x100} % 100")
if(_mc_total_pct_frac LESS 10)
    set(_mc_total_pct_frac "0${_mc_total_pct_frac}")
endif()

set(_mc_total_pct_text "${_mc_total_pct_int}.${_mc_total_pct_frac}%")
message(STATUS "mc_coverage_report: ${MC_REPORT_LABEL} line coverage")
foreach(_mc_line IN LISTS _mc_report_lines)
    message(STATUS "${_mc_line}")
endforeach()
message(STATUS "  TOTAL: ${_mc_total_covered}/${_mc_total_lines} (${_mc_total_pct_text})")

math(EXPR _mc_min_x100 "${MC_MIN_COVERAGE} * 100")
if(_mc_total_pct_x100 LESS _mc_min_x100)
    message(FATAL_ERROR "mc_coverage_report: total line coverage ${_mc_total_pct_text} is "
                         "below the required ${MC_MIN_COVERAGE}%")
endif()
