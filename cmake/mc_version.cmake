# mc_version.cmake — reads MC_VERSION_MAJOR/MINOR/PATCH/STRING from include/mc/version.h
# into CMake variables, and fails the configure when MAJOR.MINOR.PATCH disagrees with
# MC_VERSION_STRING. Included by the root CMakeLists.txt before project(), so
# project(mc_protocol VERSION ${MC_VERSION} ...) can use the result.

set(_mc_version_header "${CMAKE_CURRENT_LIST_DIR}/../include/mc/version.h")

if(NOT EXISTS "${_mc_version_header}")
    message(FATAL_ERROR "mc_protocol: version header not found at '${_mc_version_header}'.")
endif()

file(STRINGS "${_mc_version_header}" _mc_version_major_line REGEX "^#define MC_VERSION_MAJOR ")
file(STRINGS "${_mc_version_header}" _mc_version_minor_line REGEX "^#define MC_VERSION_MINOR ")
file(STRINGS "${_mc_version_header}" _mc_version_patch_line REGEX "^#define MC_VERSION_PATCH ")
file(STRINGS "${_mc_version_header}" _mc_version_string_line REGEX "^#define MC_VERSION_STRING ")

string(REGEX REPLACE "^#define MC_VERSION_MAJOR[ \t]+([0-9]+)$" "\\1" MC_VERSION_MAJOR "${_mc_version_major_line}")
string(REGEX REPLACE "^#define MC_VERSION_MINOR[ \t]+([0-9]+)$" "\\1" MC_VERSION_MINOR "${_mc_version_minor_line}")
string(REGEX REPLACE "^#define MC_VERSION_PATCH[ \t]+([0-9]+)$" "\\1" MC_VERSION_PATCH "${_mc_version_patch_line}")
string(REGEX REPLACE "^#define MC_VERSION_STRING[ \t]+\"(.*)\"$" "\\1" MC_VERSION_STRING "${_mc_version_string_line}")

set(MC_VERSION "${MC_VERSION_MAJOR}.${MC_VERSION_MINOR}.${MC_VERSION_PATCH}")

if(NOT MC_VERSION STREQUAL MC_VERSION_STRING)
    message(FATAL_ERROR
        "mc_protocol: include/mc/version.h is inconsistent: MC_VERSION_MAJOR.MINOR.PATCH "
        "is '${MC_VERSION}' but MC_VERSION_STRING is '${MC_VERSION_STRING}'.")
endif()

unset(_mc_version_header)
unset(_mc_version_major_line)
unset(_mc_version_minor_line)
unset(_mc_version_patch_line)
unset(_mc_version_string_line)
