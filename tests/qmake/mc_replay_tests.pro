# tests/qmake/mc_replay_tests.pro (SPEC-hil-capture.md "Replay tests", label `replay`): the
# std-only replay test binary RPL-01..06, built through mc_mock.pri alone (which pulls in
# mc_core.pri), with the shared `.vec` loader (tests/common/vectors.h/.cpp) bundled directly as
# ordinary SOURCES/HEADERS, exactly as mc_mock_tests.pro does. Mirrors tests/CMakeLists.txt's
# mc_replay_tests; see mc_mock_tests.pro for the long version of the comments below. No Qt: QT is
# empty.
include(../../mc_mock.pri)

QT =

CONFIG += console testcase warn_on

# MSVC only forward-declares std::ostream unless told otherwise; mirrors the mc_doctest
# INTERFACE define in tests/CMakeLists.txt.
DEFINES += DOCTEST_CONFIG_USE_STD_HEADERS

# Own $$TARGET-named intermediate directory.
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

INCLUDEPATH += \
    $$PWD/../third_party \
    $$PWD/.. \
    $$PWD/../hil

# tests/ (the vectors, the fixtures) resolved from the source tree at runtime, and the folder the
# negative tests damage their copies in, under the build tree.
DEFINES += MC_TESTS_SOURCE_DIR=\\\"$$PWD/..\\\"
DEFINES += MC_REPLAY_SCRATCH_DIR=\\\"$$OUT_PWD/replay_scratch\\\"

HEADERS += \
    $$PWD/../common/vectors.h \
    $$PWD/../hil/replay/replay.h \
    $$PWD/../hil/replay/replay_options.h \
    $$PWD/../hil/replay/replay_util.h

SOURCES += \
    $$PWD/../common/vectors.cpp \
    $$PWD/../hil/replay/replay_main.cpp \
    $$PWD/../hil/replay/replay.cpp \
    $$PWD/../hil/replay/replay_session.cpp \
    $$PWD/../hil/replay/replay_util.cpp \
    $$PWD/../hil/test_replay.cpp
