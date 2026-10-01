# tests/qmake/mc_serial_tests.pro (BLD-03, T-054): builds the QtTest binary of
# tests/device/tst_serial.cpp and its serial bridge (serial_bridge.cpp, which hosts a MockPlc)
# through mc_device.pri and mc_mock.pri (each pulls in mc_core.pri), mirroring
# tests/CMakeLists.txt's mc_serial_tests. The tests that need a virtual COM pair QSKIP unless
# MC_TEST_SERIAL_PAIR names one. See mc_tcp_transport_tests.pro for the comments on QT and the
# intermediate dirs.
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
    $$PWD/../device/serial_bridge.h \
    $$PWD/../device/smoke_check.h \
    $$PWD/../device/device_recorder.h \
    $$PWD/../device/device_test_support.h

SOURCES += \
    $$PWD/../device/tst_serial.cpp \
    $$PWD/../device/serial_bridge.cpp
