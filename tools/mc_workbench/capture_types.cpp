#include "mc_workbench/capture_types.h"

namespace mc::workbench {

QString captureSourceName(CaptureSource source) {
    switch (source) {
    case CaptureSource::RealPlc:
        return QStringLiteral("plc");
    case CaptureSource::MockPlc:
        return QStringLiteral("mock");
    case CaptureSource::VirtualPlc:
        return QStringLiteral("virtual_plc");
    }
    return QStringLiteral("plc");
}

void registerCaptureMetaTypes() {
    qRegisterMetaType<CaptureSettings>("mc::workbench::CaptureSettings");
    qRegisterMetaType<CaptureStatus>("mc::workbench::CaptureStatus");
    qRegisterMetaType<CaptureSaveRequest>("mc::workbench::CaptureSaveRequest");
    qRegisterMetaType<CaptureSaveResult>("mc::workbench::CaptureSaveResult");
}

} // namespace mc::workbench
