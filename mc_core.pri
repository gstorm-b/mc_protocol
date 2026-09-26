# mc_core.pri — the core library only: no Qt, no mock, no device layer.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_core.pri)
!defined(MC_CORE_PRI_INCLUDED, var) {
MC_CORE_PRI_INCLUDED = 1

CONFIG += c++17

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/src

HEADERS += \
    $$PWD/include/mc/version.h \
    $$PWD/include/mc/core/types.h \
    $$PWD/include/mc/core/result.h

SOURCES += \
    $$PWD/src/core/version.cpp

}
