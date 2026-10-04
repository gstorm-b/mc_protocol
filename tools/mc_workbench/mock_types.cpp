#include "mc_workbench/mock_types.h"

#include "mc_workbench/runner_types.h"

#include <QMutex>
#include <QMutexLocker>

namespace mc::workbench {

mc::MockOptions MockSettings::toOptions(mc::LogSink* sink) const {
    mc::MockOptions options;
    options.unsupportedQna = unsupportedQna;
    options.unsupported1e = unsupported1e;
    options.unsupported1c = unsupported1c;
    options.sumErrorQna = sumErrorQna;
    options.sumError1c = sumError1c;
    options.outOfRangeQna = outOfRangeQna;
    options.outOfRange1e = outOfRange1e;
    options.outOfRange1eAbnormal = outOfRange1eAbnormal;
    options.outOfRange1c = outOfRange1c;
    options.log = sink;
    return options;
}

void registerMockMetaTypes() {
    static QMutex mutex;
    static bool done = false;
    QMutexLocker lock(&mutex);
    if (done) {
        return;
    }
    registerRunnerMetaTypes();
    qRegisterMetaType<MockRequestEntry>("mc::workbench::MockRequestEntry");
    qRegisterMetaType<MockRequestBatch>("mc::workbench::MockRequestBatch");
    qRegisterMetaType<MemoryBlock>("mc::workbench::MemoryBlock");
    done = true;
}

} // namespace mc::workbench
