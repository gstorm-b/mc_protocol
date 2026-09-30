# examples/qmake/session_loop.pro (T-033): the non-Qt Session <-> MockPlc example, built through
# mc_mock.pri alone (which pulls in mc_core.pri). Mirrors examples/CMakeLists.txt's session_loop
# target. `testcase` makes `nmake check` / `mingw32-make check` run it as a smoke test (it exits 0
# after three rounds), like the ctest entry examples.session_loop.
include(../../mc_mock.pri)

QT =

CONFIG += console testcase warn_on
CONFIG -= app_bundle

TARGET = session_loop

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
    $$PWD/../session_loop/main.cpp
