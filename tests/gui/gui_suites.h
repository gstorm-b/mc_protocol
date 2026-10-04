// Factories of the QtTest suites of mc_workbench_tests; each is defined in its own tst_*.cpp.
#pragma once

#include <QObject>

namespace mc::workbench::test {

/// GUI-09: the main window opens offscreen with ADS and qpb.
QObject* makeSmokeSuite();

/// GUI-01, GUI-03, GUI-04: runner threads, containment and shutdown.
QObject* makeRunnerSuite();

/// GUI-06 and the device tab: the config grid, connect, subscriptions, trend, ad-hoc console.
QObject* makeDeviceTabSuite();

/// GUI-02 and the mock tab: parallel mocks and devices, COM serving, memory editor, faults, request log.
QObject* makeMockTabSuite();

/// T-071: the trace ring, frame decoder, debug log, capture and export.
QObject* makeTraceSuite();

/// GUI-05: back-pressure under a flood and under a stalled GUI thread.
QObject* makeBackPressureSuite();

/// GUI-07: the HIL runner view on mc_hil_tool: gate equality, confirmation, runs against a mock.
QObject* makeHilSuite();

/// GUI-08: workspace save / load, damaged and unknown-key files, recent files, off-thread file work.
QObject* makeWorkspaceSuite();

/// T-074: the GUI against examples/virtual_plc (a device tab; a full HIL run with E-10 and replay).
QObject* makeVirtualPlcSuite();

/// T-074 phase tester probes: gate and confirmation bypass attempts, export rule, load, workspace.
QObject* makeTesterProbesSuite();

} // namespace mc::workbench::test
