#include "mc_workbench/hil_types.h"

namespace mc::workbench {

int HilRunResult::exitCode() const {
    // The numbers of mc::hil::ExitCode (options.h): 0 ok, 1 a step failed, 2 bad input, 3 gate.
    switch (status) {
    case HilRunStatus::Finished:
        return failed == 0 ? 0 : 1;
    case HilRunStatus::GateRefused:
        return 3;
    case HilRunStatus::BadInput:
    case HilRunStatus::NotConfirmed:
    case HilRunStatus::OutputRefused:
    case HilRunStatus::Busy:
        return 2;
    case HilRunStatus::Cancelled:
    case HilRunStatus::Failed:
        return 1;
    }
    return 1;
}

void registerHilMetaTypes() {
    static const int once = []() {
        qRegisterMetaType<HilCheckInput>("mc::workbench::HilCheckInput");
        qRegisterMetaType<HilGateFrame>("mc::workbench::HilGateFrame");
        qRegisterMetaType<HilCheckResult>("mc::workbench::HilCheckResult");
        qRegisterMetaType<HilRunRequest>("mc::workbench::HilRunRequest");
        qRegisterMetaType<HilStepLine>("mc::workbench::HilStepLine");
        qRegisterMetaType<HilRunResult>("mc::workbench::HilRunResult");
        qRegisterMetaType<HilFileText>("mc::workbench::HilFileText");
        qRegisterMetaType<HilReplayResult>("mc::workbench::HilReplayResult");
        qRegisterMetaType<HilBenchText>("mc::workbench::HilBenchText");
        return 0;
    }();
    (void)once;
    registerCaptureMetaTypes();
}

} // namespace mc::workbench
