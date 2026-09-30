# check_mock_includes.cmake -- MCK-HYG (SPEC-mock-plc.md, "Independence rule"): the files under
# src/mock may include mc/core/*.h, their own headers, the standard library, and exactly four
# private core headers (core/protocol/hexascii.h, sumcheck.h, field_codec.h, device_encode.h).
# They must not include frame_*.h, command_*.h, serial_parser.h or any other private core header,
# nor name McProtocol or Parser (the client direction of core-protocol).
#
# Every violation is printed as "MCK-HYG <file>:<line>: <reason>"; any violation fails the script.
#
# Run as: cmake -D MC_MOCK_DIR=<directory to scan> -P check_mock_includes.cmake
# (tests/CMakeLists.txt passes src/mock for the real check, and tests/mock/hyg_fixture/ for the
# negative controls, so the real sources are never edited to prove that the check bites.)

# cmake -P script mode does not pick up policies from a project(); see check_pri_sync.cmake.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED MC_MOCK_DIR)
    message(FATAL_ERROR "check_mock_includes: MC_MOCK_DIR not set")
endif()
if(NOT IS_DIRECTORY "${MC_MOCK_DIR}")
    message(FATAL_ERROR "check_mock_includes: not a directory: ${MC_MOCK_DIR}")
endif()

file(GLOB_RECURSE _mc_files
    "${MC_MOCK_DIR}/*.h" "${MC_MOCK_DIR}/*.hpp" "${MC_MOCK_DIR}/*.cpp" "${MC_MOCK_DIR}/*.cc")
if(NOT _mc_files)
    message(FATAL_ERROR "check_mock_includes: no source files under ${MC_MOCK_DIR}")
endif()

set(_mc_allowed_private
    "core/protocol/hexascii.h"
    "core/protocol/sumcheck.h"
    "core/protocol/field_codec.h"
    "core/protocol/device_encode.h")

set(_mc_ok TRUE)
set(_mc_scanned 0)

foreach(_f IN LISTS _mc_files)
    math(EXPR _mc_scanned "${_mc_scanned} + 1")
    get_filename_component(_name "${_f}" NAME)
    get_filename_component(_dir "${_f}" DIRECTORY)
    file(STRINGS "${_f}" _lines)
    set(_lineno 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _lineno "${_lineno} + 1")

        # The angle-bracket form of a private core header is the same violation as the quoted one.
        if(_line MATCHES "^[ \t]*#[ \t]*include[ \t]+<(core/[^>]+)>")
            set(_angle "${CMAKE_MATCH_1}")
            if(NOT _angle IN_LIST _mc_allowed_private)
                message(STATUS "MCK-HYG ${_name}:${_lineno}: include <${_angle}> is not allowed by the independence rule")
                set(_mc_ok FALSE)
            endif()
        endif()

        if(_line MATCHES "^[ \t]*#[ \t]*include[ \t]+\"([^\"]+)\"")
            set(_inc "${CMAKE_MATCH_1}")
            set(_allowed FALSE)
            if(_inc MATCHES "^mc/core/[^/]+\.h$")
                set(_allowed TRUE)                    # the public core surface
            elseif(_inc MATCHES "^mc/mock/[^/]+\.h$")
                set(_allowed TRUE)                    # the mock's own public header
            elseif(_inc MATCHES "^mock/[^/]+\.h$")
                set(_allowed TRUE)                    # the mock's own private headers
            elseif(NOT _inc MATCHES "/" AND EXISTS "${_dir}/${_inc}")
                set(_allowed TRUE)                    # a sibling header of the same folder
            elseif(_inc IN_LIST _mc_allowed_private)
                set(_allowed TRUE)                    # the four allowed private primitives
            endif()
            if(NOT _allowed)
                message(STATUS "MCK-HYG ${_name}:${_lineno}: include \"${_inc}\" is not allowed by the independence rule")
                set(_mc_ok FALSE)
            endif()
        endif()

        if(_line MATCHES "McProtocol")
            message(STATUS "MCK-HYG ${_name}:${_lineno}: uses McProtocol (client direction)")
            set(_mc_ok FALSE)
        endif()
        if(_line MATCHES "(^|[^A-Za-z0-9_])Parser([^A-Za-z0-9_]|$)")
            message(STATUS "MCK-HYG ${_name}:${_lineno}: uses Parser (client direction)")
            set(_mc_ok FALSE)
        endif()
    endforeach()
endforeach()

if(NOT _mc_ok)
    message(FATAL_ERROR "MCK-HYG: independence rule violated (see above)")
endif()
message(STATUS "MCK-HYG: OK (${_mc_scanned} files scanned under ${MC_MOCK_DIR})")
