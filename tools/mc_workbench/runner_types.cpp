#include "mc_workbench/runner_types.h"

#include "mc/device/meta_types.h"
#include "mc_workbench/capture_types.h"

#include <QMutex>
#include <QMutexLocker>

namespace mc::workbench {

void registerRunnerMetaTypes() {
    static QMutex mutex;
    static bool done = false;
    QMutexLocker lock(&mutex);
    if (done) {
        return;
    }
    mc::registerMetaTypes();
    qRegisterMetaType<CommandResult>("mc::workbench::CommandResult");
    qRegisterMetaType<ValueUpdate>("mc::workbench::ValueUpdate");
    qRegisterMetaType<ValueBatch>("mc::workbench::ValueBatch");
    qRegisterMetaType<FrameRecord>("mc::workbench::FrameRecord");
    qRegisterMetaType<QVector<FrameRecord>>("QVector<mc::workbench::FrameRecord>");
    qRegisterMetaType<LogLine>("mc::workbench::LogLine");
    qRegisterMetaType<QVector<LogLine>>("QVector<mc::workbench::LogLine>");
    qRegisterMetaType<RequestOutcome>("mc::workbench::RequestOutcome");
    qRegisterMetaType<FaultReport>("mc::workbench::FaultReport");
    qRegisterMetaType<ThreadReport>("mc::workbench::ThreadReport");
    qRegisterMetaType<MockStats>("mc::workbench::MockStats");
    registerCaptureMetaTypes();
    done = true;
}

} // namespace mc::workbench
