# check_include_hygiene.cmake — BLD-05: the public-surface and no-Qt-in-core/mock rules over
# include/mc/** and src/**.
#
#   (a) every quoted #include in include/mc/** is "mc/..." and resolves under include/;
#       in src/** it resolves under include/ ("mc/...") or under src/.
#   (b) no "#include <Q", "QT_" token, or "Q_OBJECT" token anywhere under include/mc/** or
#       src/**, except include/mc/device/** and src/device/** — Qt is confined to the device
#       layer. This is wider than the four sub-folders SPEC-build-packaging.md names
#       (include/mc/core, include/mc/mock, src/core, src/mock): those sub-folders do not exist
#       yet, and a root-level header such as include/mc/version.h must be covered too (proven
#       by the acceptance test that adds "#include <QString>" to it).
#   (c) nothing under include/mc/ resolves into src/.
#   (d) nothing under include/mc/device or src/device includes mc/mock/...; and the
#       LINK_LIBRARIES of the mc_device CMake target (exported next to mc_sources.txt as
#       mc_device_link_libraries.txt, only once that target exists) must not contain mc_mock.
#       Reported as "not applicable" when mc_device is not defined (MC_BUILD_DEVICE=OFF).
#
# Run as: cmake -D MC_SOURCE_DIR=<repo root> -D MC_BINARY_DIR=<build dir> -P check_include_hygiene.cmake

# cmake -P script mode does not pick up policies from a project()'s cmake_minimum_required();
# set it here too so this script's behavior does not depend on the caller's CMake defaults.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED MC_SOURCE_DIR)
    message(FATAL_ERROR "check_include_hygiene: MC_SOURCE_DIR not set")
endif()
if(NOT DEFINED MC_BINARY_DIR)
    message(FATAL_ERROR "check_include_hygiene: MC_BINARY_DIR not set")
endif()

file(GLOB_RECURSE _mc_include_files
    "${MC_SOURCE_DIR}/include/mc/*.h"
    "${MC_SOURCE_DIR}/include/mc/*.hpp"
)
file(GLOB_RECURSE _mc_src_files
    "${MC_SOURCE_DIR}/src/*.h"
    "${MC_SOURCE_DIR}/src/*.hpp"
    "${MC_SOURCE_DIR}/src/*.cpp"
    "${MC_SOURCE_DIR}/src/*.cc"
)

set(_mc_ok TRUE)

function(mc_report _file _line _rule _msg)
    message(STATUS "check_include_hygiene: ${_file}:${_line}: rule (${_rule}): ${_msg}")
    set(_mc_ok FALSE PARENT_SCOPE)
endfunction()

foreach(_f IN LISTS _mc_include_files _mc_src_files)
    file(RELATIVE_PATH _rel "${MC_SOURCE_DIR}" "${_f}")
    get_filename_component(_dir "${_f}" DIRECTORY)

    set(_under_include_mc FALSE)
    set(_under_src FALSE)
    set(_under_device FALSE)

    if(_rel MATCHES "^include/mc/")
        set(_under_include_mc TRUE)
    endif()
    if(_rel MATCHES "^src/")
        set(_under_src TRUE)
    endif()
    if(_rel MATCHES "^include/mc/device/" OR _rel MATCHES "^src/device/")
        set(_under_device TRUE)
    endif()

    file(STRINGS "${_f}" _lines)
    list(LENGTH _lines _mc_n)
    if(_mc_n GREATER 0)
        math(EXPR _mc_last "${_mc_n} - 1")
        foreach(_idx RANGE 0 ${_mc_last})
            list(GET _lines ${_idx} _line)
            math(EXPR _lineno "${_idx} + 1")

            # ---- rule (b): Qt confined to the device layer ----
            if(NOT _under_device)
                if(_line MATCHES "#include[ \t]*<Q")
                    mc_report("${_rel}" "${_lineno}" "b" "angle-bracket Qt include ('${_line}')")
                endif()
                if(_line MATCHES "QT_")
                    mc_report("${_rel}" "${_lineno}" "b" "QT_ token")
                endif()
                if(_line MATCHES "Q_OBJECT")
                    mc_report("${_rel}" "${_lineno}" "b" "Q_OBJECT token")
                endif()
            endif()

            # ---- rule (d), include side: device must not reach into mock ----
            if(_under_device AND _line MATCHES "#include" AND _line MATCHES "mc/mock/")
                mc_report("${_rel}" "${_lineno}" "d" "device-layer file includes mc/mock/...")
            endif()

            # ---- rules (a) / (c): quoted include resolution ----
            if(_line MATCHES "^[ \t]*#include[ \t]+\"([^\"]+)\"")
                set(_inc "${CMAKE_MATCH_1}")

                if(_under_include_mc)
                    if(NOT _inc MATCHES "^mc/")
                        mc_report("${_rel}" "${_lineno}" "a" "quoted include '${_inc}' does not start with mc/")
                    else()
                        set(_target_path "${MC_SOURCE_DIR}/include/${_inc}")
                        if(NOT EXISTS "${_target_path}")
                            mc_report("${_rel}" "${_lineno}" "a" "quoted include '${_inc}' does not resolve under include/")
                        else()
                            get_filename_component(_resolved "${_target_path}" ABSOLUTE)
                            string(FIND "${_resolved}" "${MC_SOURCE_DIR}/src/" _pos)
                            if(_pos EQUAL 0)
                                mc_report("${_rel}" "${_lineno}" "c" "quoted include '${_inc}' resolves into src/")
                            endif()
                        endif()
                    endif()
                elseif(_under_src)
                    set(_resolved_ok FALSE)
                    if(_inc MATCHES "^mc/" AND EXISTS "${MC_SOURCE_DIR}/include/${_inc}")
                        set(_resolved_ok TRUE)
                    elseif(EXISTS "${_dir}/${_inc}")
                        set(_resolved_ok TRUE)
                    elseif(EXISTS "${MC_SOURCE_DIR}/src/${_inc}")
                        set(_resolved_ok TRUE)
                    endif()
                    if(NOT _resolved_ok)
                        mc_report("${_rel}" "${_lineno}" "a" "quoted include '${_inc}' does not resolve under include/ or src/")
                    endif()
                endif()
            endif()
        endforeach()
    endif()
endforeach()

# ---- rule (d), link side ----
set(_mc_link_file "${MC_BINARY_DIR}/mc_device_link_libraries.txt")
if(EXISTS "${_mc_link_file}")
    file(READ "${_mc_link_file}" _mc_device_links)
    # LINK_LIBRARIES keeps a target as it was written: the target name (mc_mock) or the alias
    # this project uses everywhere (mc::mock). Both must be caught.
    if(_mc_device_links MATCHES "mc(_|::)mock")
        message(STATUS "check_include_hygiene: rule (d): mc_device LINK_LIBRARIES contains ${CMAKE_MATCH_0}")
        set(_mc_ok FALSE)
    endif()
else()
    message(STATUS "check_include_hygiene: rule (d) LINK_LIBRARIES check: not applicable (mc_device target does not exist)")
endif()

if(NOT _mc_ok)
    message(FATAL_ERROR "check_include_hygiene: violations found (see above)")
endif()

message(STATUS "check_include_hygiene: OK")
