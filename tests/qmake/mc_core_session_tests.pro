# tests/qmake/mc_core_session_tests.pro (BLD-03, T-020): builds the whole core-session test
# binary (every tests/core/session/*.cpp) through mc_core.pri alone, mirroring
# tests/CMakeLists.txt's mc_core_session_tests target so `nmake check` / `mingw32-make check`
# prove the qmake story for core-session too. Mirrors mc_core_model_tests.pro /
# mc_core_protocol_tests.pro (see those files for the long version of the comments below).
include(../../mc_core.pri)

QT =

CONFIG += console testcase warn_on

# MSVC only forward-declares std::ostream unless told otherwise; doctest's fallback
# stringification of a type with no StringMaker then instantiates the standard library's own
# operator<< against an incomplete std::basic_ostream and fails to compile (T-006). Mirrors the
# mc_doctest INTERFACE define in tests/CMakeLists.txt.
DEFINES += DOCTEST_CONFIG_USE_STD_HEADERS

# Every target here needs its own $$TARGET-named intermediate directory (T-019 tester finding
# #3b): a shared debug/ directory lets one qmake test binary silently reuse another's .obj files
# while still reporting green.
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

HEADERS += \
    $$PWD/../core/session/harness.h

SOURCES += \
    $$PWD/../core/session/main.cpp \
    $$PWD/../core/session/harness.cpp \
    $$PWD/../core/session/test_range_set.cpp \
    $$PWD/../core/session/test_value_store.cpp \
    $$PWD/../core/session/test_session_poll.cpp \
    $$PWD/../core/session/test_session_adhoc.cpp \
    $$PWD/../core/session/test_session_fault.cpp \
    $$PWD/../core/session/test_session_heartbeat.cpp \
    $$PWD/../core/session/test_alloc.cpp
