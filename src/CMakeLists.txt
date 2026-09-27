add_library(mc_core STATIC
    core/version.cpp
    core/model/device_table.cpp
    core/model/device_parse.cpp
    core/model/frame_config.cpp
    core/model/validate.cpp
    core/model/limits_table.cpp
    core/model/limits_table.h
    core/model/chunk.cpp
    core/model/convert.cpp
    core/model/log.cpp
    core/protocol/hexascii.cpp
    core/protocol/hexascii.h
    core/protocol/sumcheck.cpp
    core/protocol/sumcheck.h
    core/protocol/field_codec.h
    core/protocol/device_encode.cpp
    core/protocol/device_encode.h
    core/protocol/command_qna.cpp
    core/protocol/command_qna.h
    core/protocol/frame_3e.cpp
    core/protocol/frame_3e.h
    core/protocol/protocol.cpp
    ${PROJECT_SOURCE_DIR}/include/mc/version.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/types.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/result.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/device.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/frame_config.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/request.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/limits.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/convert.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/log.h
    ${PROJECT_SOURCE_DIR}/include/mc/core/protocol.h
)
add_library(mc::core ALIAS mc_core)

target_include_directories(mc_core
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

mc_apply_warnings(mc_core)
mc_apply_coverage(mc_core)

# ---- BLD-04 pri_sync: export each library target's own source list, one "TARGET path" line
# per file, to ${CMAKE_BINARY_DIR}/mc_sources.txt. cmake/check_pri_sync.cmake compares this
# against the HEADERS/SOURCES each mc_*.pri lists for the matching target. Extend
# _mc_pri_sync_targets with mc_mock (T25) and mc_device (T30) as those targets are added below.
set(_mc_pri_sync_targets mc_core)
set(_mc_sources_lines "")
foreach(_mc_target IN LISTS _mc_pri_sync_targets)
    get_target_property(_mc_target_sources ${_mc_target} SOURCES)
    foreach(_mc_src IN LISTS _mc_target_sources)
        if(IS_ABSOLUTE "${_mc_src}")
            set(_mc_src_abs "${_mc_src}")
        else()
            set(_mc_src_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_mc_src}")
        endif()
        file(RELATIVE_PATH _mc_src_rel "${PROJECT_SOURCE_DIR}" "${_mc_src_abs}")
        string(APPEND _mc_sources_lines "${_mc_target} ${_mc_src_rel}\n")
    endforeach()
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mc_sources.txt" CONTENT "${_mc_sources_lines}")

# ---- BLD-05 rule (d), link side: export mc_device's LINK_LIBRARIES once that target exists
# (T30) so cmake/check_include_hygiene.cmake can assert it never contains mc_mock. No file at
# all == "not applicable yet", which the check script reports rather than failing on.
if(TARGET mc_device)
    get_target_property(_mc_device_links mc_device LINK_LIBRARIES)
    if(NOT _mc_device_links)
        set(_mc_device_links "")
    endif()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mc_device_link_libraries.txt" CONTENT "${_mc_device_links}")
endif()
