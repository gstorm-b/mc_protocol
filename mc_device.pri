# mc_device.pri — the Qt device layer. Pulls in mc_core.pri and the Qt modules it needs.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_device.pri)
!defined(MC_DEVICE_PRI_INCLUDED, var) {
MC_DEVICE_PRI_INCLUDED = 1

include($$PWD/mc_core.pri)

QT += core network serialport

# src/device arrives with T30; no HEADERS/SOURCES yet.

}
