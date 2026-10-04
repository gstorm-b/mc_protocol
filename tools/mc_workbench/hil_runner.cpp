#include "mc_workbench/hil_runner.h"

#include "hil_capture/bench_report.h"
#include "hil_capture/runner.h"
#include "hil_capture/tool.h"
#include "mc_workbench/capture_export.h"
#include "mc_workbench/hil_prepare.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QPointer>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <exception>
#include <functional>
#include <utility>

namespace mc::workbench {

void HilControl::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancel.store(false);
    m_answered = false;
}

void HilControl::requestCancel() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancel.store(true);
    }
    m_cv.notify_all();
}

void HilControl::answerPrompt() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_answered = true;
    }
    m_cv.notify_all();
}

bool HilControl::waitForAnswer() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return m_answered || m_cancel.load(); });
    if (m_cancel.load()) {
        return false;
    }
    m_answered = false;
    return true;
}

namespace {

// Thrown by the step hook to end a run between two steps; not a std::exception on purpose, so no
// containment layer mistakes it for a failure.
struct RunCancelled {};

constexpr qint64 kMaxFileBytes = 1024 * 1024;
constexpr qint64 kMaxReplayOutput = 256 * 1024;
constexpr int kReplayTimeoutMs = 120000;

// The console stream of the tool as a device: splits what Runner writes into lines and calls back.
class LineSink : public QIODevice {
public:
    using LineFn = std::function<void(const QString&)>;
    LineSink(LineFn onLine, LineFn onPrompt) : m_onLine(std::move(onLine)), m_onPrompt(std::move(onPrompt)) {
        open(QIODevice::WriteOnly);
    }

protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* data, qint64 size) override {
        m_buffer.append(data, static_cast<qsizetype>(size));
        qsizetype newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            QString line = QString::fromUtf8(m_buffer.left(newline));
            m_buffer.remove(0, newline + 1);
            if (line.endsWith(QLatin1Char('\r'))) {
                line.chop(1);
            }
            m_onLine(line);
        }
        // "<text> (press Enter to continue) " without a newline is the operator prompt.
        static const QByteArray marker("(press Enter to continue)");
        const qsizetype at = m_buffer.indexOf(marker);
        if (at >= 0) {
            m_onPrompt(QString::fromUtf8(m_buffer.left(at)).trimmed());
            m_buffer.clear();
        }
        return size;
    }

private:
    LineFn m_onLine;
    LineFn m_onPrompt;
    QByteArray m_buffer;
};

// The operator's input stream of the tool as a device: a read blocks (on the helper thread the
// Runner starts for it) until the GUI answers the prompt or the run is cancelled.
class PromptInput : public QIODevice {
public:
    explicit PromptInput(std::shared_ptr<HilControl> control) : m_control(std::move(control)) {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (maxSize <= 0) {
            return 0;
        }
        if (!m_control->waitForAnswer()) {
            return -1;
        }
        data[0] = '\n';
        return 1;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    std::shared_ptr<HilControl> m_control;
};

// A capture of something that is not a PLC says so in every record, so it cannot pass for
// hardware data if it is ever copied: the Runner writes "# source: plc" for every record.
// Returns the problem when a record did not carry the line this expects (the writer format changed):
// the run then ends as Failed instead of leaving a mock capture tagged "plc".
QString tagCaptureSource(const QString& folder, CaptureSource source) {
    if (source == CaptureSource::RealPlc || folder.isEmpty()) {
        return QString();
    }
    QString problem;
    const QString word = captureSourceName(source);
    for (const char* name : {"steps.vec", "session.vec"}) {
        QFile file(QDir(folder).filePath(QString::fromLatin1(name)));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QByteArray text = file.readAll();
        file.close();
        const QByteArray from = "# source: plc  profile:";
        if (text.contains("# source:") && !text.contains(from)) {
            problem = QStringLiteral("%1 has no \"# source: plc  profile:\" line to tag; the capture writer format changed")
                          .arg(QString::fromLatin1(name));
        }
        text.replace(from, ("# source: " + word + "  profile:").toUtf8());
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(text);
        }
    }
    QFile meta(QDir(folder).filePath(QStringLiteral("run.meta")));
    if (meta.open(QIODevice::Append)) {
        meta.write(("capture_source: " + word + "\ngui_run: mc_workbench\n").toUtf8());
    }
    return problem;
}

} // namespace

HilRunner::HilRunner(std::shared_ptr<HilControl> control) : m_control(std::move(control)) {}

void HilRunner::stopAfterFailure() noexcept {
    m_control->requestCancel();
}

void HilRunner::shutdown() {
    m_control->requestCancel();
    if (m_replay != nullptr) {
        m_replay->kill();
        m_replay->waitForFinished(500);
    }
}

void HilRunner::check(quint64 token, const HilCheckInput& input) {
    HilCheckResult result;
    if (m_running) {
        result.errorText = QStringLiteral("a run is in progress");
    } else {
        result = prepareHil(input).toResult();
    }
    result.token = token;
    emit checked(result);
}

void HilRunner::run(quint64 token, const HilRunRequest& request) {
    QPointer<HilRunner> self(this);
    const std::shared_ptr<HilControl> control = m_control;

    HilRunResult result;
    result.token = token;
    result.threadName = QThread::currentThread()->objectName();
    result.onGuiThread = QCoreApplication::instance() != nullptr &&
                         QThread::currentThread() == QCoreApplication::instance()->thread();
    const auto end = [&](HilRunStatus status, const QString& reason) {
        result.status = status;
        result.reason = reason;
        if (!self.isNull()) {
            emit self->runFinished(result);
        }
    };

    if (m_running) {
        end(HilRunStatus::Busy, QStringLiteral("a run is already in progress"));
        return;
    }
    if (control->cancelRequested()) {
        end(HilRunStatus::Cancelled, QStringLiteral("cancelled before the run started"));
        return;
    }

    // 1. The whole pipeline of the check again, on this thread: the gate of hil_capture, nothing
    //    else. A refused plan stops here, before any transport exists.
    const HilPrepared prepared = prepareHil(request.input);
    if (prepared.exitCode == 3) {
        end(HilRunStatus::GateRefused, prepared.refusalText);
        return;
    }
    if (!prepared.ok()) {
        end(HilRunStatus::BadInput, prepared.errorText);
        return;
    }
    const HilCheckResult check = prepared.toResult();

    // 2. What the operator looked at is what runs.
    if (check.digest != request.expectedDigest) {
        end(HilRunStatus::NotConfirmed,
            QStringLiteral("the profile, the plan or the options changed since the check; check "
                           "again and confirm what the dry run shows"));
        return;
    }

    // 3. The confirmation of hil_capture: the profile id is typed, always when the run holds
    //    read-only frames.
    QString why;
    if (!confirmationAccepted(check, request.typedId, request.skipTyping, &why)) {
        end(HilRunStatus::NotConfirmed, why);
        return;
    }

    // 4. Where the capture goes.
    if (request.source == CaptureSource::RealPlc && check.loopbackTcp) {
        end(HilRunStatus::OutputRefused,
            QStringLiteral("this profile talks to this computer (a mock), not to a real PLC: choose "
                           "the source 'mock PLC' or 'virtual_plc'"));
        return;
    }
    const QString outputRoot = QDir(request.outputRoot).absolutePath();
    // The rule is applied to the folder the capture is written to, not to the root (a profile id
    // such as "captured" turns the root `tests/vectors` into the protected folder).
    const QString inputProblem = checkOutputInputs(request.outputRoot, check.profileId);
    if (!inputProblem.isEmpty()) {
        end(HilRunStatus::OutputRefused, inputProblem);
        return;
    }
    const QString folder = QDir(outputRoot).filePath(check.profileId);
    const ExportDecision decision = checkOutputTarget(folder, request.source, request.capturedRoot);
    if (!decision.allowed) {
        end(HilRunStatus::OutputRefused, decision.reason);
        return;
    }
    if (!request.overwrite && QFileInfo::exists(folder)) {
        end(HilRunStatus::OutputRefused,
            QStringLiteral("%1 exists; choose another folder or allow replacing it").arg(folder));
        return;
    }

    // 5. Run, with the console of the tool turned into signals.
    const auto onLine = [self](const QString& line) {
        if (self.isNull()) {
            return;
        }
        emit self->outputLine(line);
        static const QRegularExpression outcome(
            QStringLiteral("^(PASS|UNSUPPORTED|DIVERGED|FAIL|SKIP)\\s+(\\S+)\\s*(.*)$"));
        const QRegularExpressionMatch m = outcome.match(line);
        if (m.hasMatch()) {
            emit self->stepOutcome({m.captured(1), m.captured(2), m.captured(3).trimmed()});
        }
    };
    const auto onPrompt = [self](const QString& text) {
        if (!self.isNull()) {
            emit self->promptRequested(text);
        }
    };
    LineSink outSink(onLine, onPrompt);
    LineSink errSink(onLine, onPrompt);
    PromptInput promptIn(control);
    QTextStream out(&outSink);
    QTextStream err(&errSink);
    QTextStream in(&promptIn);
    mc::hil::ToolIo io;
    io.out = &out;
    io.err = &err;
    io.in = &in;
    io.stepStarted = [self, control](const QString& id) {
        if (control->cancelRequested()) {
            throw RunCancelled{};
        }
        if (!self.isNull()) {
            emit self->stepStarted(id);
        }
    };

    mc::hil::RunnerSettings settings;
    settings.outputRoot = outputRoot;
    settings.plcState = request.input.plcState.toUpper();
    settings.benchReps = request.benchReps;
    settings.reconnectBudgetMs = request.reconnectSeconds * 1000;
    settings.operatorNote = request.note;
    if (request.source != CaptureSource::RealPlc) {
        settings.operatorNote =
            QStringLiteral("MC Workbench HIL run against %1, not hardware%2")
                .arg(request.source == CaptureSource::MockPlc ? QStringLiteral("a mock PLC")
                                                               : QStringLiteral("virtual_plc"),
                     request.note.isEmpty() ? QString() : QStringLiteral("; ") + request.note);
    }

    const bool folderExisted = QFileInfo::exists(folder);
    m_running = true;
    mc::hil::RunSummary summary;
    bool cancelled = false;
    QString problem;
    try {
        mc::hil::Runner runner(prepared.profile, prepared.resolved, settings, io);
        summary = runner.run();
    } catch (const RunCancelled&) {
        cancelled = true;
    } catch (const std::exception& e) {
        problem = QString::fromUtf8(e.what());
    } catch (...) {
        problem = QStringLiteral("unknown exception");
    }
    out.flush();
    if (self.isNull()) {
        return; // the runner was deleted by a shutdown that ran inside the run's event loop
    }
    m_running = false;

    result.passed = summary.passed;
    result.failed = summary.failed;
    result.diverged = summary.diverged;
    result.notSupported = summary.notSupported;
    result.skipped = summary.skipped;
    result.lines = summary.lines;
    if ((cancelled || !problem.isEmpty()) && !folderExisted) {
        // The writer creates the folder when the run starts; a run that ended without a capture
        // leaves nothing behind.
        QDir(folder).removeRecursively();
    }
    result.folder = summary.folder;
    if (cancelled) {
        end(HilRunStatus::Cancelled,
            QStringLiteral("stopped between two steps; no capture was written"));
    } else if (!problem.isEmpty()) {
        end(HilRunStatus::Failed, problem);
    } else if (!summary.error.isEmpty()) {
        end(HilRunStatus::Failed, summary.error);
    } else {
        const QString tagProblem = tagCaptureSource(summary.folder, request.source);
        end(tagProblem.isEmpty() ? HilRunStatus::Finished : HilRunStatus::Failed, tagProblem);
    }
}

void HilRunner::readFile(quint64 token, const QString& path, qint64 maxBytes) {
    HilFileText file;
    file.token = token;
    file.path = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        file.text = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
    } else {
        const qint64 limit = maxBytes > 0 ? maxBytes : kMaxFileBytes;
        const QByteArray data = f.read(limit + 1);
        file.truncated = data.size() > limit;
        file.text = QString::fromUtf8(data.left(limit));
        file.ok = true;
    }
    emit fileRead(file);
}

void HilRunner::runReplay(quint64 token, const QString& program, const QString& root) {
    HilReplayResult result;
    result.token = token;
    if (m_replay != nullptr) {
        result.output = QStringLiteral("a replay is already running");
        emit replayDone(result);
        return;
    }
    if (program.trimmed().isEmpty() || !QFileInfo::exists(program)) {
        result.output = QStringLiteral("mc_replay_tests was not found (%1); set its path")
                            .arg(program.isEmpty() ? QStringLiteral("no path") : program);
        emit replayDone(result);
        return;
    }
    auto* process = new QProcess(this);
    m_replay = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto* output = new QByteArray;
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(process, &QProcess::readyRead, process, [process, output]() {
        output->append(process->readAll());
        if (output->size() > kMaxReplayOutput) {
            output->remove(0, output->size() - kMaxReplayOutput);
        }
    });
    const auto finish = [this, process, output, token](bool started, int code) {
        output->append(process->readAll());
        HilReplayResult done;
        done.token = token;
        done.started = started;
        done.exitCode = code;
        done.output = QString::fromUtf8(*output);
        delete output;
        m_replay = nullptr;
        process->deleteLater();
        emit replayDone(done);
    };
    connect(process, &QProcess::errorOccurred, process, [process, output, finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            output->append(process->errorString().toUtf8());
            finish(false, -1);
        }
    });
    connect(process, &QProcess::finished, process,
            [process, finish](int code, QProcess::ExitStatus status) {
                finish(true, status == QProcess::NormalExit ? code : -3);
            });
    connect(timer, &QTimer::timeout, process, [process, output]() {
        output->append("\nreplay did not finish in time; stopped\n");
        process->kill();
    });
    process->start(program, {QStringLiteral("--replay-root=") + root,
                             QStringLiteral("-tc=RPL-sweep: every*")});
    timer->start(kReplayTimeoutMs);
}

void HilRunner::benchReport(quint64 token, const QString& root) {
    HilBenchText result;
    result.token = token;
    QString error;
    result.text = mc::hil::benchReportText(root, &error);
    result.ok = error.isEmpty() && !result.text.isEmpty();
    if (!result.ok) {
        result.text = error.isEmpty() ? QStringLiteral("no bench report") : error;
    }
    emit benchReady(result);
}

} // namespace mc::workbench
