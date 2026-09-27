# tests/qmake/mc_core_model_tests.pro — BLD-03 gap closed at Checkpoint B (T-019): builds the
# whole core-model test binary (every tests/core/model/*.cpp) through mc_core.pri alone, mirroring
# tests/CMakeLists.txt's mc_core_model_tests target so `nmake check` / `mingw32-make check` prove
# the qmake story for core-model too, not just tests/build/test_version.cpp (build_version.pro).
include(../../mc_core.pri)

QT =

CONFIG += console testcase warn_on

# MSVC only forward-declares std::ostream unless told otherwise; doctest's fallback
# stringification of a type with no StringMaker (e.g. std::string_view, used throughout
# core-model's public API) then instantiates the standard library's own operator<< against an
# incomplete std::basic_ostream and fails to compile (T-006). DOCTEST_CONFIG_USE_STD_HEADERS makes
# doctest include the real standard headers instead of forward-declaring them. Mirrors the
# mc_doctest INTERFACE define in tests/CMakeLists.txt.
DEFINES += DOCTEST_CONFIG_USE_STD_HEADERS

# Checkpoint B rework (T-019 tester finding #3b): see build_version.pro for why every target
# here needs its own $$TARGET-named intermediate directory (this block is repeated verbatim,
# only $$TARGET differs, in build_version.pro and mc_core_protocol_tests.pro).
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

SOURCES += \
    $$PWD/../core/model/main.cpp \
    $$PWD/../core/model/test_result.cpp \
    $$PWD/../core/model/test_device.cpp \
    $$PWD/../core/model/test_frame_config.cpp \
    $$PWD/../core/model/test_limits.cpp \
    $$PWD/../core/model/test_chunk.cpp \
    $$PWD/../core/model/test_convert.cpp \
    $$PWD/../core/model/test_log.cpp \
    $$PWD/../core/model/test_alloc.cpp
