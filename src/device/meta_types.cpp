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
    // requestFinished() names the alias, not uint64_t. Qt 5 queues an argument only under the
    // name the signal spells, so the alias is registered by name (Qt 6 resolves it by itself).
    qRegisterMetaType<mc::RequestId>("mc::RequestId");
}

} // namespace mc
