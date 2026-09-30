# examples/qmake/virtual_plc.pro (T-039): the Qt console demo PLC, built through mc_device.pri and
# mc_mock.pri (each pulls in mc_core.pri). Mirrors examples/CMakeLists.txt's virtual_plc target.
# Not a `testcase`: it runs until stopped.
include(../../mc_device.pri)
include(../../mc_mock.pri)

QT = core network serialport

CONFIG += console warn_on
CONFIG -= app_bundle

TARGET = virtual_plc

# Own $$TARGET-named intermediate directories, as every test .pro has (T-019 tester finding #3b).
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
    $$PWD/../virtual_plc/main.cpp
