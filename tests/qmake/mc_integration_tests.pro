# tests/qmake/mc_integration_tests.pro (BLD-03, T-032): builds the integration test binary
# (tests/mock/integration/*.cpp; Session against MockPlc) through mc_mock.pri alone (which pulls in
# mc_core.pri), mirroring tests/CMakeLists.txt's mc_integration_tests target. Like the CMake
# target it reuses tests/mock/main.cpp for the doctest main. See mc_mock_tests.pro for the long
# version of the comments below.
include(../../mc_mock.pri)

QT =

CONFIG += console testcase warn_on

# Mirrors the mc_doctest INTERFACE define in tests/CMakeLists.txt (T-006).
DEFINES += DOCTEST_CONFIG_USE_STD_HEADERS

# Own $$TARGET-named intermediate directories (T-019 tester finding #3b).
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
    $$PWD/..

HEADERS += \
    $$PWD/../mock/integration/pipe.h \
    $$PWD/../mock/integration/rig.h

SOURCES += \
    $$PWD/../mock/main.cpp \
    $$PWD/../mock/integration/rig.cpp \
    $$PWD/../mock/integration/test_integration.cpp
