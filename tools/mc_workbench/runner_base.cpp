#include "mc_workbench/runner_base.h"

#include <QThread>

#include <atomic>

namespace mc::workbench {

namespace {

std::atomic<int> g_liveRunners{0};
std::atomic<int> g_foreignDeletes{0};

} // namespace

RunnerBase::RunnerBase(QObject* parent) : QObject(parent) {
    ++g_liveRunners;
}

RunnerBase::~RunnerBase() {
    // The object's own thread is the only one allowed to delete it (SPEC-qt-device.md: one
    // object tree, one thread). A thread whose loop already ended counts as "own" through
    // thread(), which is still the runner thread here.
    if (QThread::currentThread() != thread()) {
        ++g_foreignDeletes;
    }
    --g_liveRunners;
}

int RunnerBase::liveRunners() noexcept {
    return g_liveRunners.load();
}

int RunnerBase::foreignDeletes() noexcept {
    return g_foreignDeletes.load();
}

void RunnerBase::fail(const char* what, const QString& text) {
    const bool first = !m_failed;
    m_failed = true;
    if (first) {
        stopAfterFailure();
    }
    emit failed(QStringLiteral("%1: %2").arg(QString::fromLatin1(what), text));
}

} // namespace mc::workbench
