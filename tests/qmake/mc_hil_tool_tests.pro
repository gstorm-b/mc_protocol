# tests/qmake/mc_hil_tool_tests.pro (BLD-03): builds the QtTest binary of tests/hil (every
# tst_*.cpp and its main) and the tool's own sources through hil_capture.pri (which pulls in
# mc_device.pri and mc_mock.pri), mirroring tests/CMakeLists.txt's mc_hil_tool_tests. The shared
# `.vec` loader (tests/common/vectors.h/.cpp) is bundled directly, as mc_mock_tests.pro does. See
# mc_tcp_transport_tests.pro for the comments on QT and the intermediate dirs.
include(../../tools/hil_capture/hil_capture.pri)

QT = core network serialport testlib

CONFIG += console testcase warn_on

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

# Profiles and plans are read from the source tree: mirrors tests/CMakeLists.txt's own
# MC_TESTS_SOURCE_DIR compile definition.
DEFINES += MC_TESTS_SOURCE_DIR=\\\"$$PWD/..\\\"
# What a test writes goes under the build tree, never to tests/vectors/captured/.
DEFINES += MC_HIL_OUTPUT_DIR=\\\"$$OUT_PWD/hil_out\\\"

# The end-to-end tests start examples/virtual_plc, built by examples/qmake into its own folder; when
# it is not there they skip.
win32: VIRTUAL_PLC_SUFFIX = .exe
CONFIG(debug, debug|release) {
    VIRTUAL_PLC_DIR = $$OUT_PWD/../../examples/qmake/debug
} else {
    VIRTUAL_PLC_DIR = $$OUT_PWD/../../examples/qmake/release
}
DEFINES += MC_VIRTUAL_PLC_PATH=\\\"$$VIRTUAL_PLC_DIR/virtual_plc$$VIRTUAL_PLC_SUFFIX\\\"

# HIL-04, replay half: mc_replay_tests is built by tests/qmake/mc_replay_tests.pro into the same
# folder.
CONFIG(debug, debug|release) {
    DEFINES += MC_REPLAY_TESTS_PATH=\\\"$$OUT_PWD/debug/mc_replay_tests$$VIRTUAL_PLC_SUFFIX\\\"
} else {
    DEFINES += MC_REPLAY_TESTS_PATH=\\\"$$OUT_PWD/release/mc_replay_tests$$VIRTUAL_PLC_SUFFIX\\\"
}

INCLUDEPATH += $$PWD/../hil $$PWD/..

HEADERS += \
    $$PWD/../common/vectors.h \
    $$PWD/../hil/hil_suites.h \
    $$PWD/../hil/hil_test_support.h

SOURCES += \
    $$PWD/../common/vectors.cpp \
    $$PWD/../hil/hil_tests_main.cpp \
    $$PWD/../hil/tst_hil_profile.cpp \
    $$PWD/../hil/tst_hil_gate.cpp \
    $$PWD/../hil/tst_hil_capture.cpp \
    $$PWD/../hil/tst_hil_plans.cpp \
    $$PWD/../hil/tst_hil_run.cpp

