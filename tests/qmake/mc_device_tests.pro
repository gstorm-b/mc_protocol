# tests/qmake/mc_device_tests.pro (BLD-03, T-037): builds the QtTest binary of
# tests/device/tst_mc_device.cpp and its loopback server (mock_plc_server.cpp) through
# mc_device.pri and mc_mock.pri (each pulls in mc_core.pri), mirroring tests/CMakeLists.txt's
# mc_device_tests. See mc_tcp_transport_tests.pro for the comments on QT and the intermediate dirs.
include(../../mc_device.pri)
include(../../mc_mock.pri)

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

HEADERS += \
    $$PWD/../device/mock_plc_server.h \
    $$PWD/../device/device_recorder.h \
    $$PWD/../device/device_test_support.h \
    $$PWD/../device/fake_transport.h \
    $$PWD/../device/smoke_check.h

SOURCES += \
    $$PWD/../device/tst_mc_device.cpp \
    $$PWD/../device/mock_plc_server.cpp
