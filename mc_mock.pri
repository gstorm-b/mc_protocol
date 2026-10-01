# mc_mock.pri — the mock PLC layer. Pulls in mc_core.pri; no Qt.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_mock.pri)
!defined(MC_MOCK_PRI_INCLUDED, var) {
MC_MOCK_PRI_INCLUDED = 1

include($$PWD/mc_core.pri)

HEADERS += \
    $$PWD/include/mc/mock/mock_plc.h \
    $$PWD/src/mock/memory_image.h \
    $$PWD/src/mock/mock_internal.h

SOURCES += \
    $$PWD/src/mock/memory_image.cpp \
    $$PWD/src/mock/request_decode_ethernet.cpp \
    $$PWD/src/mock/request_decode_serial.cpp \
    $$PWD/src/mock/command_exec.cpp \
    $$PWD/src/mock/response_build.cpp \
    $$PWD/src/mock/corruption.cpp \
    $$PWD/src/mock/mock_plc.cpp

}
