#include "mc_workbench/workspace_io.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QPointer>
#include <QThread>

namespace mc::workbench {

namespace {
// How long a destructor waits for a file-writing thread (the stop bound of RunnerThread).
constexpr int kWorkerBoundMs = 3000;
} // namespace

WorkspaceIo::WorkspaceIo(QObject* parent) : QObject(parent) {
    qRegisterMetaType<mc::workbench::Workspace>();
    qRegisterMetaType<mc::workbench::WorkspaceProblem>();
    qRegisterMetaType<QVector<mc::workbench::WorkspaceProblem>>();
}

WorkspaceIo::~WorkspaceIo() {
    const QVector<QThread*> workers = m_workers;
    m_workers.clear();
    for (QThread* worker : workers) {
        if (worker->wait(kWorkerBoundMs)) {
            delete worker;
        } else {
            // A write that does not end in time: a running QThread is never deleted. The thread object
            // deletes itself when the thread ends (and is leaked when its owner thread is gone).
            QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        }
    }
}

void WorkspaceIo::run(std::function<void()> job) {
    QThread* worker = QThread::create(std::move(job));
    m_workers.push_back(worker);
    connect(worker, &QThread::finished, this, [this, worker]() {
        m_workers.removeOne(worker);
        worker->deleteLater();
    });
    worker->start();
}

void WorkspaceIo::save(const QString& path, const Workspace& workspace) {
    run([self = QPointer<WorkspaceIo>(this), path, workspace]() {
        QString message;
        bool ok = false;
        const QByteArray bytes = workspaceToBytes(workspace);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            message = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        } else if (file.write(bytes) != bytes.size()) {
            message = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
            file.cancelWriting();
        } else if (!file.commit()) {
            message = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        } else {
            ok = true;
        }
        QMetaObject::invokeMethod(
            self.data(), [self, path, ok, message]() { if (self) { emit self->saved(path, ok, message); } },
            Qt::QueuedConnection);
    });
}

void WorkspaceIo::load(const QString& path) {
    run([self = QPointer<WorkspaceIo>(this), path]() {
        Workspace workspace;
        QVector<WorkspaceProblem> problems;
        QFile file(path);
        if (!file.exists()) {
            problems.push_back({QString(), QStringLiteral("%1 does not exist").arg(path)});
        } else if (file.size() > kWorkspaceMaxBytes) {
            problems.push_back({QString(), QStringLiteral("%1 is too large for a workspace (%2 bytes, "
                                                          "the limit is %3)")
                                               .arg(path)
                                               .arg(file.size())
                                               .arg(kWorkspaceMaxBytes)});
        } else if (!file.open(QIODevice::ReadOnly)) {
            problems.push_back(
                {QString(), QStringLiteral("cannot read %1: %2").arg(path, file.errorString())});
        } else {
            const QByteArray bytes = file.read(kWorkspaceMaxBytes + 1);
            if (bytes.size() > kWorkspaceMaxBytes) {
                problems.push_back({QString(), QStringLiteral("%1 is too large for a workspace").arg(path)});
            } else {
                workspaceFromBytes(bytes, workspace, problems);
            }
        }
        const bool ok = problems.isEmpty();
        QMetaObject::invokeMethod(
            self.data(),
            [self, path, ok, workspace, problems]() {
                if (self) {
                    emit self->loaded(path, ok, ok ? workspace : Workspace{}, problems);
                }
            },
            Qt::QueuedConnection);
    });
}

} // namespace mc::workbench
