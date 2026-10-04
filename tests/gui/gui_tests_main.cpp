// mc_workbench_tests (SPEC-gui-tool.md "Testing", label gui): one binary, several QtTest classes,
// each in its own tst_*.cpp. QtTest allows one test object per QTEST_MAIN, so this main runs them
// one after another and adds up the failures. Every argument is handed to QtTest unchanged.
//
// The program is exercised offscreen: main() sets QT_QPA_PLATFORM before the application exists, so
// the tests need no display and behave the same under ctest, qmake `check` and by hand.
//
// This file is not called main.cpp on purpose: the qmake build compiles it next to the tool
// sources, and nmake/jom inference rules would pick the tool's main.cpp for main.obj.
#include "gui_suites.h"

#include <QApplication>
#include <QTest>

#include <memory>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    int failures = 0;
    for (QObject* (*make)() : {mc::workbench::test::makeSmokeSuite,
                               mc::workbench::test::makeRunnerSuite,
                               mc::workbench::test::makeDeviceTabSuite,
                               mc::workbench::test::makeMockTabSuite,
                               mc::workbench::test::makeTraceSuite,
                               mc::workbench::test::makeBackPressureSuite,
                               mc::workbench::test::makeHilSuite,
                               mc::workbench::test::makeWorkspaceSuite,
                               mc::workbench::test::makeVirtualPlcSuite,
                               mc::workbench::test::makeTesterProbesSuite}) {
        const std::unique_ptr<QObject> suite(make());
        failures += QTest::qExec(suite.get(), argc, argv);
    }
    return failures;
}
