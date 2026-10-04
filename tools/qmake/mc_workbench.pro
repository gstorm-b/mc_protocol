# tools/qmake/mc_workbench.pro: the MC Workbench program, built through mc_workbench.pri. Mirrors
# tools/mc_workbench/CMakeLists.txt's mc_workbench target. Listed by tools.pro only when
# mc_gui_deps.pri found a docking library for this kit (mc_local.pri). The docking DLL is copied
# next to the program after the link (mc_gui_deps.pri).
include(../mc_workbench/mc_workbench.pri)

QT += core network serialport widgets

CONFIG += warn_on
CONFIG -= app_bundle

TARGET = mc_workbench

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
    $$PWD/../mc_workbench/workbench_main.cpp
