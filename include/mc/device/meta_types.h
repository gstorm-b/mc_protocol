/**
 * @file meta_types.h
 * @brief Qt meta-type declarations for the value types McDevice signals carry, and
 * registerMetaTypes().
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/result.h"
#include "mc/core/session.h"
#include "mc/core/value_store.h"
#include "mc/device/mc_device.h"

#include <QMetaType>
#include <QVector>

Q_DECLARE_METATYPE(mc::DeviceType)
Q_DECLARE_METATYPE(mc::Error)
Q_DECLARE_METATYPE(mc::Change)
Q_DECLARE_METATYPE(mc::CycleInfo)
Q_DECLARE_METATYPE(mc::LinkState)
Q_DECLARE_METATYPE(mc::LinkReason)
Q_DECLARE_METATYPE(mc::LinkFaultInfo)
Q_DECLARE_METATYPE(mc::SnapshotSegment)
Q_DECLARE_METATYPE(mc::ChunkStatus)
Q_DECLARE_METATYPE(mc::DeviceSnapshot)

namespace mc {

/**
 * @brief Registers every signal type of the device layer with Qt's meta-type system.
 *
 * Makes queued connections between threads work without application code: McDevice's
 * constructors call it. Idempotent and safe to call from any thread. The list grows with the
 * signal types the device layer gains.
 *
 * @see McDevice
 */
void registerMetaTypes();

} // namespace mc
