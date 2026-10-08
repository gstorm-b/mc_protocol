# mc_device.pri — the Qt device layer. Pulls in mc_core.pri and the Qt modules it needs.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_device.pri)
!defined(MC_DEVICE_PRI_INCLUDED, var) {
MC_DEVICE_PRI_INCLUDED = 1

include($$PWD/mc_core.pri)

QT += core network serialport

# Qt 5.15 with MSVC needs a toolset older than 14.50 (VS 2026 removed the stdext checked iterators
# the Qt 5.15 headers use); the older toolsets deprecate them, so their warning is silenced with
# the standard library's own switch. cmake/mc_qt.cmake does the same.
msvc:lessThan(QT_MAJOR_VERSION, 6): DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
msvc:lessThan(QT_MAJOR_VERSION, 6):greaterThan(QMAKE_MSC_VER, 1949) {
    warning(Qt $$QT_VERSION with MSVC $$QMAKE_MSC_VER: the Qt 5.15 headers need an MSVC toolset \
            older than 14.50 - run qmake from a shell set up with scripts/vsdev.ps1 -VcVarsVer 14.44)
}

HEADERS += \
    $$PWD/include/mc/device/transport.h \
    $$PWD/include/mc/device/tcp_transport.h \
    $$PWD/include/mc/device/serial_transport.h \
    $$PWD/include/mc/device/mc_device_config.h \
    $$PWD/include/mc/device/mc_device.h \
    $$PWD/include/mc/device/meta_types.h

SOURCES += \
    $$PWD/src/device/tcp_transport.cpp \
    $$PWD/src/device/serial_transport.cpp \
    $$PWD/src/device/meta_types.cpp \
    $$PWD/src/device/mc_device_config.cpp \
    $$PWD/src/device/mc_device.cpp

}
