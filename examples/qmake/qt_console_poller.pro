# examples/qmake/qt_console_poller.pro (T-039): the Qt console poller (McDevice), built through mc_device.pri and
# mc_mock.pri (each pulls in mc_core.pri). Mirrors examples/CMakeLists.txt's qt_console_poller target.
# Not a `testcase`: it needs a running PLC (virtual_plc).
include(../../mc_device.pri)
include(../../mc_mock.pri)

QT = core network serialport

CONFIG += console warn_on
CONFIG -= app_bundle

TARGET = qt_console_poller

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
    $$PWD/../qt_console_poller/main.cpp
