# tests/qmake/mc_mock_tests.pro (BLD-03, T-029): builds the whole mock-plc test binary (every
# tests/mock/*.cpp) through mc_mock.pri alone (which pulls in mc_core.pri), with the shared `.vec`
# loader (tests/common/vectors.h/.cpp) bundled directly as ordinary SOURCES/HEADERS, exactly as
# mc_core_protocol_tests.pro does. Mirrors tests/CMakeLists.txt's mc_mock_tests + mc_test_vectors;
# see mc_core_protocol_tests.pro / mc_core_session_tests.pro for the long version of the comments
# below.
include(../../mc_mock.pri)

QT =

CONFIG += console testcase warn_on

# MSVC only forward-declares std::ostream unless told otherwise (T-006); mirrors the mc_doctest
# INTERFACE define in tests/CMakeLists.txt.
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

# tests/vectors, resolved from the source tree at runtime: mirrors tests/CMakeLists.txt's own
# MC_TESTS_SOURCE_DIR compile definition.
DEFINES += MC_TESTS_SOURCE_DIR=\\\"$$PWD/..\\\"

HEADERS += \
    $$PWD/../common/vectors.h

SOURCES += \
    $$PWD/../common/vectors.cpp \
    $$PWD/../mock/main.cpp \
    $$PWD/../mock/test_mock_memory.cpp \
    $$PWD/../mock/test_mock_vectors.cpp \
    $$PWD/../mock/test_mock_faults.cpp
