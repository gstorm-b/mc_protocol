# tools/qmake/hil_capture.pro: the capture tool, built through hil_capture.pri (which pulls in
# mc_device.pri and mc_mock.pri, each of which pulls in mc_core.pri). Mirrors tools/CMakeLists.txt's
# hil_capture target. Not a `testcase`: it needs a PLC (virtual_plc for a trial run).
include(../hil_capture/hil_capture.pri)

QT = core network serialport

CONFIG += console warn_on
CONFIG -= app_bundle

TARGET = hil_capture

# Own $$TARGET-named intermediate directories, as every test .pro has.
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

SOURCES += \
    $$PWD/../hil_capture/main.cpp
