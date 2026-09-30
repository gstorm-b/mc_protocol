# check_pri_sync.cmake — BLD-04: compares each mc_*.pri file's own HEADERS/SOURCES entries
# with the CMake source list of the matching target, exported at configure time to
# ${MC_SOURCES_FILE} by src/CMakeLists.txt (one "TARGET path" line per file, path
# repo-relative with forward slashes). A .pri whose target is absent from that file must list no
# files of its own; a layer that is switched off (mc_mock under -DMC_BUILD_MOCK=OFF, mc_device
# under -DMC_BUILD_DEVICE=OFF) still exports its source-list variable and is compared. Any other
# difference is reported by file name, .pri and target, and fails the check.
#
# Run as: cmake -D MC_SOURCE_DIR=<repo root> -D MC_SOURCES_FILE=<path> -P check_pri_sync.cmake

# cmake -P script mode does not pick up policies from a project()'s cmake_minimum_required();
# set it here too so IN_LIST (CMP0057) behaves as the modern operator, not the pre-3.3 default.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED MC_SOURCE_DIR)
    message(FATAL_ERROR "check_pri_sync: MC_SOURCE_DIR not set")
endif()
if(NOT DEFINED MC_SOURCES_FILE)
    message(FATAL_ERROR "check_pri_sync: MC_SOURCES_FILE not set")
endif()
if(NOT EXISTS "${MC_SOURCES_FILE}")
    message(FATAL_ERROR "check_pri_sync: sources file not found: ${MC_SOURCES_FILE} (configure the CMake build first)")
endif()

# ---- CMake side: TARGET -> sorted list of repo-relative paths, read from MC_SOURCES_FILE ----
file(STRINGS "${MC_SOURCES_FILE}" _mc_lines)
set(_mc_known_targets "")
foreach(_line IN LISTS _mc_lines)
    if(_line STREQUAL "")
        continue()
    endif()
    if(NOT _line MATCHES "^([A-Za-z0-9_]+) (.+)$")
        message(FATAL_ERROR "check_pri_sync: malformed line in ${MC_SOURCES_FILE}: '${_line}'")
    endif()
    set(_target "${CMAKE_MATCH_1}")
    set(_path "${CMAKE_MATCH_2}")
    list(APPEND "cmake_${_target}" "${_path}")
    if(NOT _target IN_LIST _mc_known_targets)
        list(APPEND _mc_known_targets "${_target}")
    endif()
endforeach()

# ---- .pri side: which file maps to which target ----
set(_pri_names "mc_core.pri" "mc_mock.pri" "mc_device.pri")
set(_pri_targets "mc_core" "mc_mock" "mc_device")

set(_mc_ok TRUE)

list(LENGTH _pri_names _mc_n)
math(EXPR _mc_last "${_mc_n} - 1")
foreach(_i RANGE 0 ${_mc_last})
    list(GET _pri_names ${_i} _pri_name)
    list(GET _pri_targets ${_i} _target)
    set(_pri_path "${MC_SOURCE_DIR}/${_pri_name}")

    if(NOT EXISTS "${_pri_path}")
        message(FATAL_ERROR "check_pri_sync: missing ${_pri_path}")
    endif()

    file(READ "${_pri_path}" _pri_content)
    # Join backslash-newline continuations into one logical line before scanning for entries.
    string(REGEX REPLACE "\\\\[ \t]*\r?\n" " " _pri_joined "${_pri_content}")

    set(_pri_paths "")
    foreach(_kind "HEADERS" "SOURCES")
        string(REGEX MATCHALL "${_kind}[ \t]*\\+=[^\r\n]*" _mc_blocks "${_pri_joined}")
        foreach(_block IN LISTS _mc_blocks)
            string(REGEX REPLACE "^${_kind}[ \t]*\\+=" "" _entries "${_block}")
            string(REPLACE "$$PWD/" "" _entries "${_entries}")
            separate_arguments(_entry_list UNIX_COMMAND "${_entries}")
            foreach(_e IN LISTS _entry_list)
                if(NOT _e STREQUAL "")
                    list(APPEND _pri_paths "${_e}")
                endif()
            endforeach()
        endforeach()
    endforeach()

    if(_pri_paths)
        list(REMOVE_DUPLICATES _pri_paths)
        list(SORT _pri_paths)
    endif()

    if(_target IN_LIST _mc_known_targets)
        set(_cmake_paths "${cmake_${_target}}")
        if(_cmake_paths)
            list(REMOVE_DUPLICATES _cmake_paths)
            list(SORT _cmake_paths)
        endif()

        if(NOT "${_pri_paths}" STREQUAL "${_cmake_paths}")
            set(_only_pri "${_pri_paths}")
            if(_only_pri AND _cmake_paths)
                list(REMOVE_ITEM _only_pri ${_cmake_paths})
            endif()
            set(_only_cmake "${_cmake_paths}")
            if(_only_cmake AND _pri_paths)
                list(REMOVE_ITEM _only_cmake ${_pri_paths})
            endif()

            message(STATUS "check_pri_sync: MISMATCH file=${_pri_name} target=${_target}")
            if(_only_pri)
                message(STATUS "  only in ${_pri_name}: ${_only_pri}")
            endif()
            if(_only_cmake)
                message(STATUS "  only in CMake target '${_target}': ${_only_cmake}")
            endif()
            set(_mc_ok FALSE)
        endif()

        unset(_cmake_paths)
    else()
        if(_pri_paths)
            message(STATUS "check_pri_sync: MISMATCH file=${_pri_name} target=${_target} (target not yet defined in CMake, but ${_pri_name} lists: ${_pri_paths})")
            set(_mc_ok FALSE)
        endif()
    endif()
endforeach()

if(NOT _mc_ok)
    message(FATAL_ERROR "check_pri_sync: pri/CMake source lists disagree (see above)")
endif()

message(STATUS "check_pri_sync: OK")
