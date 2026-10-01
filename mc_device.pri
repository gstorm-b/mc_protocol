# mc_device.pri — the Qt device layer. Pulls in mc_core.pri and the Qt modules it needs.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_device.pri)
!defined(MC_DEVICE_PRI_INCLUDED, var) {
MC_DEVICE_PRI_INCLUDED = 1

include($$PWD/mc_core.pri)

QT += core network serialport

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
