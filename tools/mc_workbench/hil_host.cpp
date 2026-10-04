#include "mc_workbench/hil_host.h"

#include <QPointer>

namespace mc::workbench {

HilHost::HilHost(const QString& name, QObject* parent)
    : QObject(parent), m_control(std::make_shared<HilControl>()) {
    registerHilMetaTypes();
    QPointer<HilHost> self(this);
    const std::shared_ptr<HilControl> control = m_control;
    // Runs on the runner thread: the runner is born there, and so is every McDevice the run makes.
    m_thread = new RunnerThread(name, [control, self]() -> RunnerBase* {
        auto* runner = new HilRunner(control);
        if (!self.isNull()) {
            HilHost* host = self.data();
            const auto queued = Qt::QueuedConnection;
            QObject::connect(runner, &HilRunner::checked, host, &HilHost::checked, queued);
            QObject::connect(runner, &HilRunner::stepStarted, host, &HilHost::stepStarted, queued);
            QObject::connect(runner, &HilRunner::stepOutcome, host, &HilHost::stepOutcome, queued);
            QObject::connect(runner, &HilRunner::outputLine, host, &HilHost::outputLine, queued);
            QObject::connect(runner, &HilRunner::promptRequested, host, &HilHost::promptRequested,
                             queued);
            QObject::connect(runner, &HilRunner::runFinished, host, &HilHost::runFinished, queued);
            QObject::connect(runner, &HilRunner::fileRead, host, &HilHost::fileRead, queued);
            QObject::connect(runner, &HilRunner::replayDone, host, &HilHost::replayDone, queued);
            QObject::connect(runner, &HilRunner::benchReady, host, &HilHost::benchReady, queued);
        }
        return runner;
    });
    connect(m_thread, &RunnerThread::failed, this, &HilHost::failed);
}

HilHost::~HilHost() {
    // The run is only stopped at a step boundary and the thread's queue must not reach the
    // shutdown while a plan runs, so cancel first; the bounded stop below does the rest.
    m_control->requestCancel();
    delete m_thread;
}

quint64 HilHost::check(const HilCheckInput& input) {
    const quint64 token = nextToken();
    m_thread->post([token, input](RunnerBase& base) {
        static_cast<HilRunner&>(base).check(token, input);
    });
    return token;
}

quint64 HilHost::run(const HilRunRequest& request) {
    const quint64 token = nextToken();
    m_control->reset();
    m_thread->post([token, request](RunnerBase& base) {
        static_cast<HilRunner&>(base).run(token, request);
    });
    return token;
}

void HilHost::cancelRun() {
    m_control->requestCancel();
}

void HilHost::answerPrompt() {
    m_control->answerPrompt();
}

quint64 HilHost::readFile(const QString& path, qint64 maxBytes) {
    const quint64 token = nextToken();
    m_thread->post([token, path, maxBytes](RunnerBase& base) {
        static_cast<HilRunner&>(base).readFile(token, path, maxBytes);
    });
    return token;
}

quint64 HilHost::runReplay(const QString& program, const QString& root) {
    const quint64 token = nextToken();
    m_thread->post([token, program, root](RunnerBase& base) {
        static_cast<HilRunner&>(base).runReplay(token, program, root);
    });
    return token;
}

quint64 HilHost::benchReport(const QString& root) {
    const quint64 token = nextToken();
    m_thread->post([token, root](RunnerBase& base) {
        static_cast<HilRunner&>(base).benchReport(token, root);
    });
    return token;
}

} // namespace mc::workbench
