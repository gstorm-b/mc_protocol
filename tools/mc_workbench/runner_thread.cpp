#include "mc_workbench/runner_thread.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <exception>
#include <utility>

namespace mc::workbench {

// Shared between the handle and the lambdas that run on the runner thread, so a handle that is
// destroyed while its thread is stuck leaves nothing dangling for them. `runner` is touched on
// the runner thread only.
struct RunnerThread::State {
    Factory factory;
    RunnerBase* runner{nullptr};
    QObject* context{nullptr}; // lives on the runner thread; receives the queued commands
    QThread* thread{nullptr};
};

RunnerThread::RunnerThread(const QString& name, Factory factory, QObject* parent)
    : QObject(parent), m_thread(new QThread), m_state(std::make_shared<State>()) {
    m_thread->setObjectName(name);
    m_state->factory = std::move(factory);
    m_state->thread = m_thread;
    m_state->context = new QObject;
    // The thread is not running yet, so the context may move to it.
    m_state->context->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, this, &RunnerThread::finished);

    std::shared_ptr<State> state = m_state;
    QPointer<RunnerThread> self(this);
    // `started` is emitted on the runner thread, where the context lives: the lambda runs there,
    // before the event loop takes the first queued command.
    QObject::connect(m_thread, &QThread::started, state->context, [state, self]() {
        QString problem;
        try {
            state->runner = state->factory();
            if (state->runner == nullptr) {
                problem = QStringLiteral("the runner factory returned no object");
            }
        } catch (const std::exception& e) {
            problem = QStringLiteral("runner factory: %1").arg(QString::fromUtf8(e.what()));
        } catch (...) {
            problem = QStringLiteral("runner factory: unknown exception");
        }
        if (self.isNull()) {
            return;
        }
        if (state->runner != nullptr) {
            QObject::connect(state->runner, &RunnerBase::failed, self.data(),
                             &RunnerThread::failed, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(self.data(), "failed", Qt::QueuedConnection,
                                      Q_ARG(QString, problem));
        }
    });
    m_thread->start();
}

RunnerThread::~RunnerThread() {
    if (m_thread->isRunning() && !stop(kDefaultStopTimeoutMs)) {
        // Abandon: the thread deletes its objects when it gets there and then deletes itself.
        m_thread->setParent(nullptr);
        QObject::connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        return;
    }
    delete m_thread;
}

void RunnerThread::post(Command command) {
    if (m_stopRequested) {
        return;
    }
    std::shared_ptr<State> state = m_state;
    QMetaObject::invokeMethod(
        state->context,
        [state, command = std::move(command)]() {
            RunnerBase* runner = state->runner;
            if (runner == nullptr) {
                return;
            }
            runner->guarded("command", [&]() { command(*runner); });
        },
        Qt::QueuedConnection);
}

void RunnerThread::requestStop() {
    if (m_stopRequested) {
        return;
    }
    m_stopRequested = true;
    std::shared_ptr<State> state = m_state;
    QMetaObject::invokeMethod(
        state->context, [state]() { shutdownOnThread(state); }, Qt::QueuedConnection);
}

// Runs on the runner thread. A runner that is inside a blocking job (a nested event loop, like the
// HIL run) is not deleted from under it: the shutdown is tried again a little later, and quit() is
// not called until the job is over, so the nested loop is never cut short.
void RunnerThread::shutdownOnThread(const std::shared_ptr<State>& state) {
    if (state->runner != nullptr && state->runner->shutdownDeferred()) {
        QTimer::singleShot(20, state->context, [state]() { shutdownOnThread(state); });
        return;
    }
    if (state->runner != nullptr) {
        state->runner->guarded("shutdown", [&]() { state->runner->shutdown(); });
        // The runner dies here, on its own thread.
        delete state->runner;
        state->runner = nullptr;
    }
    state->context->deleteLater();
    state->thread->quit();
}

bool RunnerThread::waitFinished(int timeoutMs) {
    return m_thread->wait(timeoutMs);
}

bool RunnerThread::stop(int timeoutMs) {
    requestStop();
    if (waitFinished(timeoutMs)) {
        m_stuck = false;
        return true;
    }
    m_stuck = true;
    emit stuck(QStringLiteral("runner thread \"%1\" did not finish within %2 ms")
                   .arg(m_thread->objectName())
                   .arg(timeoutMs));
    return false;
}

bool RunnerThread::isRunning() const {
    return m_thread->isRunning();
}

} // namespace mc::workbench
