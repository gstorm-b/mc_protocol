# tests/qmake/mc_workbench_tests.pro (BLD-03): builds the QtTest binary of tests/gui and the tool's
# sources through mc_workbench.pri, mirroring tests/CMakeLists.txt's mc_workbench_tests. Listed by
# tests.pro only when mc_gui_deps.pri found a docking library for this kit (mc_local.pri). The test
# sets QT_QPA_PLATFORM=offscreen itself. See mc_tcp_transport_tests.pro for the comments on the
# intermediate dirs.
include(../../tools/mc_workbench/mc_workbench.pri)

QT += core network serialport widgets testlib

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

# Profiles and the captured root are read from the source tree; what a test writes goes under the
# build tree, never to tests/vectors/captured/ (mirrors tests/CMakeLists.txt).
DEFINES += MC_TESTS_SOURCE_DIR=\\\"$$PWD/..\\\"
DEFINES += MC_GUI_OUTPUT_DIR=\\\"$$OUT_PWD/gui_out\\\"

# A capture made in the GUI is replayed with mc_replay_tests, built by mc_replay_tests.pro into the
# same folder (T-071).
win32: GUI_EXE_SUFFIX = .exe
CONFIG(debug, debug|release) {
    DEFINES += MC_REPLAY_TESTS_PATH=\\\"$$OUT_PWD/debug/mc_replay_tests$$GUI_EXE_SUFFIX\\\"
} else {
    DEFINES += MC_REPLAY_TESTS_PATH=\\\"$$OUT_PWD/release/mc_replay_tests$$GUI_EXE_SUFFIX\\\"
}

# T-074: the GUI is also run against examples/virtual_plc, built by examples/qmake into its own
# folder; when it is not there those cases skip.
CONFIG(debug, debug|release) {
    DEFINES += MC_VIRTUAL_PLC_PATH=\\\"$$OUT_PWD/../../examples/qmake/debug/virtual_plc$$GUI_EXE_SUFFIX\\\"
} else {
    DEFINES += MC_VIRTUAL_PLC_PATH=\\\"$$OUT_PWD/../../examples/qmake/release/virtual_plc$$GUI_EXE_SUFFIX\\\"
}

# T-074: the program itself is started once; tools/qmake builds it into its own folder.
CONFIG(debug, debug|release) {
    DEFINES += MC_WORKBENCH_PATH=\\\"$$OUT_PWD/../../tools/qmake/debug/mc_workbench$$GUI_EXE_SUFFIX\\\"
} else {
    DEFINES += MC_WORKBENCH_PATH=\\\"$$OUT_PWD/../../tools/qmake/release/mc_workbench$$GUI_EXE_SUFFIX\\\"
}

INCLUDEPATH += $$PWD/../gui

HEADERS += \
    $$PWD/../gui/gui_suites.h \
    $$PWD/../gui/gui_test_support.h

SOURCES += \
    $$PWD/../gui/gui_tests_main.cpp \
    $$PWD/../gui/tst_gui_smoke.cpp \
    $$PWD/../gui/tst_gui_runners.cpp \
    $$PWD/../gui/tst_gui_device_tab.cpp \
    $$PWD/../gui/tst_gui_mock_tab.cpp \
    $$PWD/../gui/tst_gui_trace.cpp \
    $$PWD/../gui/tst_gui_backpressure.cpp \
    $$PWD/../gui/tst_gui_hil.cpp \
    $$PWD/../gui/tst_gui_workspace.cpp \
    $$PWD/../gui/tst_gui_virtual_plc.cpp \
    $$PWD/../gui/tst_gui_mock_streams.cpp \
    $$PWD/../gui/tst_gui_tester_probes.cpp
