#include "hil_capture/tool.h"

#include "hil_capture/bench_report.h"
#include "hil_capture/dry_run.h"
#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/runner.h"
#include "hil_capture/safety_gate.h"

namespace mc::hil {

ExitCode runTool(const Options& options, const ToolIo& io) {
    QTextStream& out = *io.out;
    QTextStream& err = *io.err;

    if (options.report) {
        QString error;
        if (!writeBenchReport(options.outputRoot, options.reportOut, &error)) {
            err << "hil_capture: " << error << "\n";
            return ExitCode::BadInput;
        }
        out << "bench report written to " << options.reportOut << "\n";
        return ExitCode::Ok;
    }

    const ProfileLoad profile = loadProfileFile(options.profilePath);
    if (!profile.ok()) {
        err << "hil_capture: profile " << options.profilePath << ": " << profile.error.text()
            << "\n";
        return ExitCode::BadInput;
    }
    const PlanLoad plan = loadPlanFile(options.planPath);
    if (!plan.ok()) {
        err << "hil_capture: plan " << options.planPath << ": " << plan.error.text() << "\n";
        return ExitCode::BadInput;
    }
    // A group nobody has is a typo, not "run nothing": a capture of zero steps would look like a
    // clean run.
    for (const QString& group : options.only) {
        bool known = false;
        for (const Step& step : plan.plan->steps) {
            known = known || step.group.compare(group, Qt::CaseInsensitive) == 0;
        }
        if (!known) {
            err << "hil_capture: --only " << group << ": no step of plan " << plan.plan->id
                << " belongs to this group\n";
            return ExitCode::BadInput;
        }
    }
    const ResolveResult resolved = resolvePlan(*plan.plan, *profile.profile, options.only);
    if (!resolved.ok()) {
        for (const StepError& e : resolved.errors) {
            err << "hil_capture: " << e.text() << "\n";
        }
        return ExitCode::BadInput;
    }

    // The safety gate runs before anything else can happen: before the dry-run text, before the
    // prompt, and whatever `--yes` says. It has no switch.
    const GateReport gate = checkGate(resolved, *profile.profile);
    if (gate.refused()) {
        err << refusalText(gate);
        return ExitCode::GateRefused;
    }

    if (options.dryRun) {
        out << dryRunText(resolved, *profile.profile);
        return ExitCode::Ok;
    }

    out << confirmationSummary(*profile.profile, gate, resolved, options.plcState);
    // `--yes` skips the confirmation only when the gate decoded every frame: a run with read-only
    // frames (frames the gate could not decode and takes the plan's word for) is always confirmed
    // by typing the profile id.
    const bool mustType = !options.yes || !gate.readOnlyFrames.isEmpty();
    if (mustType) {
        if (options.yes) {
            out << "--yes does not apply: the run holds read-only frames the gate could not "
                   "decode.\n";
        }
        out << "Type the profile id (" << profile.profile->id
            << ") to confirm, anything else aborts: ";
        out.flush();
        const QString answer = io.in != nullptr ? io.in->readLine().trimmed() : QString();
        if (answer != profile.profile->id) {
            err << "hil_capture: not confirmed; nothing was sent\n";
            return ExitCode::BadInput;
        }
    }

    RunnerSettings settings;
    settings.outputRoot = options.outputRoot;
    settings.plcState = options.plcState;
    settings.operatorNote = options.note;
    settings.benchReps = options.benchReps;
    settings.reconnectBudgetMs = options.reconnectSeconds * 1000;
    Runner runner(*profile.profile, resolved, settings, io);
    const RunSummary summary = runner.run();
    if (!summary.error.isEmpty()) {
        err << "hil_capture: " << summary.error << "\n";
        return ExitCode::RunFailures;
    }
    return summary.clean() ? ExitCode::Ok : ExitCode::RunFailures;
}

} // namespace mc::hil
