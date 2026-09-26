# mc_protocol.pri — core + device in one include; the qmake consumer entry point.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_protocol.pri)
!defined(MC_PROTOCOL_PRI_INCLUDED, var) {
MC_PROTOCOL_PRI_INCLUDED = 1

include($$PWD/mc_core.pri)
include($$PWD/mc_device.pri)

}
