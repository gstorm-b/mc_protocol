# hil_capture.pri -- the sources of tools/hil_capture other than main.cpp, for qmake. The twin of
# the mc_hil_tool static library in tools/CMakeLists.txt; the tool program
# (tools/qmake/hil_capture.pro) and the tool tests (tests/qmake/mc_hil_tool_tests.pro) both include
# it, as the CMake targets both link mc_hil_tool. Pulls in mc_device.pri and mc_mock.pri (each pulls
# in mc_core.pri).
!defined(MC_HIL_CAPTURE_PRI_INCLUDED, var) {
MC_HIL_CAPTURE_PRI_INCLUDED = 1

include($$PWD/../../mc_device.pri)
include($$PWD/../../mc_mock.pri)

QT += core network serialport

# The headers are included as "hil_capture/x.h", so tools/ is the include root.
INCLUDEPATH += $$PWD/..

HEADERS += \
    $$PWD/json_reader.h \
    $$PWD/profile.h \
    $$PWD/plan.h \
    $$PWD/resolve.h \
    $$PWD/safety_gate.h \
    $$PWD/dry_run.h \
    $$PWD/recording_transport.h \
    $$PWD/capture_writer.h \
    $$PWD/bench_report.h \
    $$PWD/runner.h \
    $$PWD/options.h \
    $$PWD/tool.h

SOURCES += \
    $$PWD/profile.cpp \
    $$PWD/plan.cpp \
    $$PWD/resolve.cpp \
    $$PWD/safety_gate.cpp \
    $$PWD/dry_run.cpp \
    $$PWD/recording_transport.cpp \
    $$PWD/capture_writer.cpp \
    $$PWD/bench_report.cpp \
    $$PWD/runner.cpp \
    $$PWD/options.cpp \
    $$PWD/tool.cpp

}
