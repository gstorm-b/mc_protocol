// Helpers of the trace / capture / back-pressure tests (tst_gui_trace.cpp, tst_gui_backpressure.cpp):
// a mock on a runner thread, a device configuration for it, the replay run, the output folder and
// the process memory. Test code only.
#pragma once

#include "mc/device/mc_device_config.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/runner_types.h"

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QTimer>

#ifdef Q_OS_WIN
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#endif

namespace mc::workbench::test {

inline constexpr int kWait = 8000;

// Measures how long the GUI thread's event loop goes without running a 10 ms timer.
class Ticker : public QObject {
public:
    Ticker() {
        m_timer.setInterval(10);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            const qint64 now = m_clock.elapsed();
            m_maxGapMs = qMax(m_maxGapMs, now - m_last);
            m_last = now;
        });
        restart();
    }
    void restart() {
        m_clock.start();
        m_last = 0;
        m_maxGapMs = 0;
        m_timer.start();
    }
    qint64 maxGapMs() const { return m_maxGapMs; }

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last{0};
    qint64 m_maxGapMs{0};
};

// The first CommandResult with this token, waiting for it; a timeout gives ok = false.
template <class Spy> CommandResult resultOf(const Spy& spy, quint64 token) {
    CommandResult found;
    found.ok = false;
    found.errorCode = -1;
    found.message = QStringLiteral("timeout");
    const auto look = [&]() {
        for (const QList<QVariant>& args : spy) {
            const auto result = args.at(0).template value<CommandResult>();
            if (result.token == token) {
                found = result;
                return true;
            }
        }
        return false;
    };
    (void)QTest::qWaitFor(look, kWait);
    return found;
}

inline bool sawLink(const QSignalSpy& spy, mc::LinkState state) {
    for (const QList<QVariant>& args : spy) {
        if (args.at(0).value<mc::LinkState>() == state) {
            return true;
        }
    }
    return false;
}

// A mock on a runner thread that listens on a system-chosen port, with D100..D103 = 10..40.
struct MockRig {
    explicit MockRig(const QString& name, const mc::FrameConfig& frame = mc::FrameConfig::frame3E())
        : host(name, frame), done(&host, &MockHost::commandDone) {
        host.setWords(QStringLiteral("D100"), {10, 20, 30, 40});
        const quint64 token = host.listen(0);
        const CommandResult listening = resultOf(done, token);
        ok = listening.ok;
        port = static_cast<quint16>(listening.value);
    }
    MockHost host;
    QSignalSpy done;
    bool ok{false};
    quint16 port{0};
};

// A device configuration for a mock on loopback; cycleMs 0 polls back to back.
inline mc::McDeviceConfig deviceConfig(quint16 port, quint32 cycleMs = 20) {
    mc::McDeviceConfig cfg;
    cfg.session.cycleIntervalMs = cycleMs;
    cfg.tcp.host = QStringLiteral("127.0.0.1");
    cfg.tcp.port = port;
    cfg.tcp.connectTimeoutMs = 500;
    cfg.subscriptions = {{QStringLiteral("D100"), 4}, {QStringLiteral("M0"), 16}};
    return cfg;
}

// The folder a test writes to: a fresh sub-folder of the build tree, never tests/vectors/captured/.
inline QString freshOutputDir(const QString& name) {
    QDir dir(QStringLiteral(MC_GUI_OUTPUT_DIR));
    dir.mkpath(QStringLiteral("."));
    QDir sub(dir.absoluteFilePath(name));
    if (sub.exists()) {
        sub.removeRecursively();
    }
    dir.mkpath(name);
    return sub.absolutePath();
}

// Runs mc_replay_tests on the capture folders under @p root; exit 0 is green.
inline QString runReplay(const QString& root, int* exitCode) {
    const QString path = QStringLiteral(MC_REPLAY_TESTS_PATH);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        *exitCode = -1;
        return QStringLiteral("mc_replay_tests is not built here (%1)").arg(path);
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(path, {QStringLiteral("--replay-root=") + root, QStringLiteral("-tc=RPL-sweep: every*")});
    if (!p.waitForFinished(60000)) {
        p.kill();
        *exitCode = -2;
        return QStringLiteral("mc_replay_tests did not finish");
    }
    *exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -3;
    return QString::fromUtf8(p.readAll());
}

// The working set of this process in bytes (0 when it cannot be read).
inline qint64 workingSetBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<qint64>(counters.WorkingSetSize);
    }
#endif
    return 0;
}

} // namespace mc::workbench::test
