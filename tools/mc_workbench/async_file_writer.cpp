#include "mc_workbench/async_file_writer.h"

#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

namespace mc::workbench {

namespace {
// How long a destructor waits for a file-writing thread (the stop bound of RunnerThread).
constexpr int kWorkerBoundMs = 3000;
} // namespace

AsyncFileWriter::AsyncFileWriter(QObject* parent) : QObject(parent) {}

AsyncFileWriter::~AsyncFileWriter() {
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

void AsyncFileWriter::write(const QString& path, const QString& text) {
    QThread* worker = QThread::create([self = QPointer<AsyncFileWriter>(this), path, text]() {
        QString message;
        bool ok = false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            message = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        } else {
            const QByteArray bytes = text.toUtf8();
            if (file.write(bytes) != bytes.size()) {
                message = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
            } else {
                ok = true;
            }
        }
        QMetaObject::invokeMethod(
            self.data(), [self, path, ok, message]() { if (self) { emit self->finished(path, ok, message); } },
            Qt::QueuedConnection);
    });
    m_workers.push_back(worker);
    connect(worker, &QThread::finished, this, [this, worker]() {
        m_workers.removeOne(worker);
        worker->deleteLater();
    });
    worker->start();
}

} // namespace mc::workbench
