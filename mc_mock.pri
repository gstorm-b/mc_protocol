# mc_mock.pri — the mock PLC layer. Pulls in mc_core.pri; no Qt.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_mock.pri)
!defined(MC_MOCK_PRI_INCLUDED, var) {
MC_MOCK_PRI_INCLUDED = 1

include($$PWD/mc_core.pri)

# src/mock arrives with T25; no HEADERS/SOURCES yet.

}
