# tests/qmake/mc_config_json_tests.pro (BLD-03, T-036): builds the QtTest binary of
# tests/device/tst_config_json.cpp through mc_device.pri alone (which pulls in mc_core.pri and
# adds QT += core network serialport), mirroring tests/CMakeLists.txt's mc_config_json_tests.
# See mc_mock_tests.pro for the long version of the comments below.
include(../../mc_device.pri)

# mc_device.pri adds core, network and serialport on top of the default QT (which includes gui);
# a test needs no gui, so the list is set outright.
QT = core network serialport testlib

CONFIG += console testcase warn_on

# Own $$TARGET-named intermediate directories (T-019 tester finding #3b): moc output included.
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
    $$PWD/../device/tst_config_json.cpp
