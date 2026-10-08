# Internal helpers for qpb/CMakeLists.txt. Everything here only affects qpb's own
# targets; nothing may change global state of the host project.

include_guard(DIRECTORY)

# qpb_add_library(<target> EXPORT_DEFINE <macro> OUTPUT_NAME <name> SOURCES <files...>)
#
# Creates a static (default) or shared (QPB_BUILD_SHARED=ON) library with qpb's
# standard settings: C++17 minimum, public include directories, private warning
# flags and Qt hygiene defines, AUTOMOC on the target only.
function(qpb_add_library target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPORT_DEFINE;OUTPUT_NAME" "SOURCES")

    if(QPB_BUILD_SHARED)
        set(type SHARED)
    else()
        set(type STATIC)
    endif()

    add_library(${target} ${type} ${arg_SOURCES})

    target_include_directories(${target}
        PUBLIC
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
    )

    # A minimum only: consumers may compile with a newer standard.
    target_compile_features(${target} PUBLIC cxx_std_17)

    if(NOT QPB_BUILD_SHARED)
        target_compile_definitions(${target} PUBLIC QPB_STATIC)
    endif()

    target_compile_definitions(${target}
        PRIVATE
            ${arg_EXPORT_DEFINE}
            QT_NO_CAST_FROM_ASCII
            QT_NO_CAST_TO_ASCII
            QT_NO_URL_CAST_FROM_STRING
    )
    # API deprecated before the minimum Qt version must not be used.
    if(QPB_QT_MAJOR EQUAL 5)
        target_compile_definitions(${target} PRIVATE QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
    else()
        target_compile_definitions(${target} PRIVATE QT_DISABLE_DEPRECATED_UP_TO=0x060500)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${arg_OUTPUT_NAME}
        VERSION ${QPB_VERSION}
        SOVERSION ${QPB_VERSION_MAJOR}
        AUTOMOC ON
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        # Static qpb must be linkable into consumers' shared libraries too.
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()
