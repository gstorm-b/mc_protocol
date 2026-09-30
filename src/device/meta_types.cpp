#include "mc/device/meta_types.h"

namespace mc {

void registerMetaTypes() {
    // qRegisterMetaType() is idempotent and thread-safe: a repeated call returns the same id.
    qRegisterMetaType<mc::DeviceType>();
    qRegisterMetaType<mc::Error>();
    qRegisterMetaType<mc::Change>();
    qRegisterMetaType<mc::CycleInfo>();
    qRegisterMetaType<QVector<mc::Change>>();
    qRegisterMetaType<mc::LinkState>();
    qRegisterMetaType<mc::LinkReason>();
    qRegisterMetaType<mc::LinkFaultInfo>();
    qRegisterMetaType<mc::SnapshotSegment>();
    qRegisterMetaType<mc::ChunkStatus>();
    qRegisterMetaType<mc::DeviceSnapshot>();
}

} // namespace mc
