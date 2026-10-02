// mc_hil_tool_tests (SPEC-hil-capture.md "Testing the tool itself", label hil_tool): one binary,
// several QtTest classes, each in its own tst_*.cpp. QtTest allows one test object per
// QTEST_MAIN, so this main runs them one after another and adds up the failures.
//
// This file is not called main.cpp on purpose: the qmake build compiles it next to the tool
// sources, and nmake/jom inference rules would pick the tool's main.cpp for main.obj.
//
//   mc_hil_tool_tests                  every suite except `serial`
//   mc_hil_tool_tests --suite serial   only the named suite (the serial one needs the COM pair
//                                      and has its own ctest entry with RESOURCE_LOCK)
//
// Every other argument is handed to QtTest unchanged (-o, function names, -v2 ...).
#include "hil_suites.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTest>

#include <cstdio>
#include <memory>
#include <vector>

namespace {

struct Suite {
    const char* name;
    QObject* (*make)();
    bool inDefaultRun;
};

const Suite kSuites[] = {
    {"profile", mc::hil::test::makeProfileSuite, true},
    {"gate", mc::hil::test::makeGateSuite, true},
    {"capture", mc::hil::test::makeCaptureSuite, true},
    {"plans", mc::hil::test::makePlansSuite, true},
    {"run", mc::hil::test::makeRunSuite, true},
    {"serial", mc::hil::test::makeSerialRunSuite, false},
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QString only;
    std::vector<char*> forwarded;
    for (int i = 0; i < argc; ++i) {
        if (QLatin1String(argv[i]) == QLatin1String("--suite") && i + 1 < argc) {
            only = QString::fromLocal8Bit(argv[i + 1]);
            ++i;
        } else {
            forwarded.push_back(argv[i]);
        }
    }

    int failures = 0;
    bool ran = false;
    for (const Suite& suite : kSuites) {
        const bool selected =
            only.isEmpty() ? suite.inDefaultRun : only == QLatin1String(suite.name);
        if (!selected) {
            continue;
        }
        ran = true;
        const std::unique_ptr<QObject> test(suite.make());
        failures += QTest::qExec(test.get(), static_cast<int>(forwarded.size()), forwarded.data());
    }
    if (!ran) {
        std::fprintf(stderr, "mc_hil_tool_tests: no suite named '%s'\n", qPrintable(only));
        return 2;
    }
    return failures;
}
