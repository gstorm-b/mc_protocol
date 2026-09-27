# tests/qmake/build_version.pro — builds tests/build/test_version.cpp (BLD-08) through
# mc_core.pri alone, with no Qt module linked, proving the qmake .pri contract works standalone.
# Named build_version, not core_version (tasks/todo.md T02's original name): the test file has
# lived in tests/build/ since T-002, not tests/core/ (core-model/-protocol/-session own that
# folder per SPEC-build-packaging.md's ownership rule).
include(../../mc_core.pri)

QT =

CONFIG += console testcase warn_on

# Checkpoint B rework (T-019 tester finding #3b): every tests/qmake/*.pro subproject builds in
# this SAME physical directory (TEMPLATE = subdirs with .file=, not .subdir=, per the T-003
# gotcha above -- there is no per-target subdirectory qmake would otherwise give it), so the
# default OBJECTS_DIR ("debug"/"release", identical for every subproject) let one target's
# compiled .obj silently satisfy another target's link step whenever two targets compiled a
# same-named source file (mc_core.pri's own sources, or two test binaries' own main.cpp /
# test_alloc.cpp) -- confirmed reproducible: the qmake mc_core_protocol_tests binary linked in
# mc_core_model_tests' stale test_alloc.o and silently ran core-model's ALC-01 case instead of
# its own. Giving every target its own $$TARGET-named intermediate directory isolates them; this
# exact block is repeated verbatim (only $$TARGET differs) in mc_core_model_tests.pro and
# mc_core_protocol_tests.pro.
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

INCLUDEPATH += $$PWD/../third_party

SOURCES += \
    $$PWD/../build/test_version.cpp
