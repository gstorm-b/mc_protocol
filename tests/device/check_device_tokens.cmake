# check_device_tokens.cmake -- QDV-HYG (SPEC-qt-device.md, success criterion 2 and Boundaries,
# "Never"): the library's device layer never blocks, starts a thread or takes a mutex. No file
# under src/device or include/mc/device may contain the token waitFor*, QEventLoop, QThread, QMutex (QMutexLocker
# included), std::mutex or std::thread. Every token is matched as a whole word, comments
# included: a comment that needs the word is reworded.
#
# Every violation is printed as "QDV-HYG <file>:<line>: <reason>"; any violation fails the script.
#
# Run as: cmake -D MC_DEVICE_DIR=<directory to scan> -P check_device_tokens.cmake
# (tests/CMakeLists.txt passes src/device and include/mc/device for the real checks, and tests/device/hyg_fixture/ for
# the negative controls, so the real sources are never edited to prove that the check bites.)

# cmake -P script mode does not pick up policies from a project(); see check_pri_sync.cmake.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED MC_DEVICE_DIR)
    message(FATAL_ERROR "check_device_tokens: MC_DEVICE_DIR not set")
endif()
if(NOT IS_DIRECTORY "${MC_DEVICE_DIR}")
    message(FATAL_ERROR "check_device_tokens: not a directory: ${MC_DEVICE_DIR}")
endif()

file(GLOB_RECURSE _mc_files
    "${MC_DEVICE_DIR}/*.h" "${MC_DEVICE_DIR}/*.hpp" "${MC_DEVICE_DIR}/*.cpp" "${MC_DEVICE_DIR}/*.cc")
if(NOT _mc_files)
    message(FATAL_ERROR "check_device_tokens: no source files under ${MC_DEVICE_DIR}")
endif()

# One entry per token: "<regex of the whole word>|<name shown in the report>".
set(_mc_tokens
    "waitFor[A-Za-z]*|waitFor*"
    "QEventLoop|QEventLoop"
    "QThread|QThread"
    "QMutex|QMutex"
    "QMutexLocker|QMutexLocker"
    "std::mutex|std::mutex"
    "std::thread|std::thread")

set(_mc_ok TRUE)
set(_mc_scanned 0)

foreach(_f IN LISTS _mc_files)
    math(EXPR _mc_scanned "${_mc_scanned} + 1")
    get_filename_component(_name "${_f}" NAME)
    # file(READ), not file(STRINGS): the latter splits a line at every ';'. Line numbers are
    # recovered by counting the newlines in front of a match.
    file(READ "${_f}" _text)
    foreach(_entry IN LISTS _mc_tokens)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _regex)
        list(GET _parts 1 _shown)
        set(_rest "${_text}")
        set(_offset 0)
        while(TRUE)
            if(NOT _rest MATCHES "(^|[^A-Za-z0-9_])(${_regex})([^A-Za-z0-9_]|$)")
                break()
            endif()
            string(FIND "${_rest}" "${CMAKE_MATCH_2}" _pos)
            string(LENGTH "${CMAKE_MATCH_2}" _len)
            # Count the newlines before this match in the full text.
            math(EXPR _abs "${_offset} + ${_pos}")
            string(SUBSTRING "${_text}" 0 ${_abs} _before)
            string(REGEX MATCHALL "\n" _newlines "${_before}")
            list(LENGTH _newlines _lineno)
            math(EXPR _lineno "${_lineno} + 1")
            message(STATUS "QDV-HYG ${_name}:${_lineno}: uses ${_shown}")
            set(_mc_ok FALSE)
            math(EXPR _next "${_pos} + ${_len}")
            string(SUBSTRING "${_rest}" ${_next} -1 _rest)
            math(EXPR _offset "${_offset} + ${_next}")
        endwhile()
    endforeach()
endforeach()

if(NOT _mc_ok)
    message(FATAL_ERROR "QDV-HYG: forbidden token found (see above)")
endif()
message(STATUS "QDV-HYG: OK (${_mc_scanned} files scanned under ${MC_DEVICE_DIR})")
