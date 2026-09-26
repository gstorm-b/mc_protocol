# tests/qmake/build_version.pro — builds tests/build/test_version.cpp (BLD-08) through
# mc_core.pri alone, with no Qt module linked, proving the qmake .pri contract works standalone.
# Named build_version, not core_version (tasks/todo.md T02's original name): the test file has
# lived in tests/build/ since T-002, not tests/core/ (core-model/-protocol/-session own that
# folder per SPEC-build-packaging.md's ownership rule).
include(../../mc_core.pri)

QT =

CONFIG += console testcase warn_on

INCLUDEPATH += $$PWD/../third_party

SOURCES += \
    $$PWD/../build/test_version.cpp
