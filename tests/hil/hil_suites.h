// Factories of the QtTest suites of mc_hil_tool_tests; each is defined in its own tst_*.cpp.
#pragma once

#include <QObject>

namespace mc::hil::test {

/// HIL-01: profile and plan loading, device reference resolution, the command line.
QObject* makeProfileSuite();

/// HIL-02, HIL-03: the safety gate, the confirmation prompt and --dry-run.
QObject* makeGateSuite();

/// HIL-05: the capture writer and RecordingTransport.
QObject* makeCaptureSuite();

/// HIL-04, capture half: the tool end to end against examples/virtual_plc over loopback TCP.
QObject* makeRunSuite();

/// HIL-04, serial: the same over the virtual COM pair (needs MC_TEST_SERIAL_PAIR).
QObject* makeSerialRunSuite();

/// The four plans against the catalogue and the example profiles.
QObject* makePlansSuite();

} // namespace mc::hil::test
