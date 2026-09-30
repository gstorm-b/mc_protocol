# tests/qmake/mc_core_protocol_tests.pro — BLD-03 gap closed at Checkpoint B (T-019): builds the
# whole core-protocol test binary (every tests/core/protocol/*.cpp) through mc_core.pri alone,
# with the shared `.vec` loader (tests/common/vectors.h/.cpp) bundled directly as ordinary
# SOURCES/HEADERS rather than its own qmake subproject -- only this one binary needs it, unlike
# CMake's mc_test_vectors (a separate static library there because it is a separate CMake target
# either way; qmake has no such target to split it out of). Mirrors tests/CMakeLists.txt's
# mc_core_protocol_tests + mc_test_vectors.
include(../../mc_core.pri)

QT =

CONFIG += console testcase warn_on

# MSVC only forward-declares std::ostream unless told otherwise; doctest's fallback
# stringification of a type with no StringMaker then instantiates the standard library's own
# operator<< against an incomplete std::basic_ostream and fails to compile (T-006). Mirrors the
# mc_doctest INTERFACE define in tests/CMakeLists.txt; see mc_core_model_tests.pro for the long
# version of this comment.
DEFINES += DOCTEST_CONFIG_USE_STD_HEADERS

# Checkpoint B rework (T-019 tester finding #3b): see build_version.pro for why every target
# here needs its own $$TARGET-named intermediate directory (this block is repeated verbatim,
# only $$TARGET differs, in build_version.pro and mc_core_model_tests.pro).
CONFIG(debug, debug|release) {
    OBJECTS_DIR = $$TARGET/debug
    MOC_DIR     = $$TARGET/debug
    RCC_DIR     = $$TARGET/debug
    UI_DIR      = $$TARGET/debug
} else {
    OBJECTS_DIR = $$TARGET/release
    MOC_DIR     = $$TARGET/release
    RCC_DIR     = $$TARGET/release
    UI_DIR      = $$TARGET/release
}

INCLUDEPATH += \
    $$PWD/../third_party \
    $$PWD/.. \
    $$PWD/../../src

# tests/vectors, resolved from the source tree at runtime: mirrors tests/CMakeLists.txt's own
# MC_TESTS_SOURCE_DIR compile definition (test_vectors_format.cpp, test_primitives.cpp,
# test_device_encode.cpp, test_commands.cpp, test_frame_3e.cpp, test_frame_1e.cpp and
# test_parser_stream.cpp all read this macro the same way regardless of which build system
# produced the binary).
DEFINES += MC_TESTS_SOURCE_DIR=\\\"$$PWD/..\\\"

HEADERS += \
    $$PWD/../common/vectors.h

SOURCES += \
    $$PWD/../common/vectors.cpp \
    $$PWD/../core/protocol/main.cpp \
    $$PWD/../core/protocol/test_vectors_format.cpp \
    $$PWD/../core/protocol/test_primitives.cpp \
    $$PWD/../core/protocol/test_device_encode.cpp \
    $$PWD/../core/protocol/test_commands.cpp \
    $$PWD/../core/protocol/test_frame_3e.cpp \
    $$PWD/../core/protocol/test_frame_1e.cpp \
    $$PWD/../core/protocol/test_parser_stream.cpp \
    $$PWD/../core/protocol/test_alloc.cpp
