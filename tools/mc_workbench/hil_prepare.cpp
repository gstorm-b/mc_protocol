#include "mc_workbench/hil_prepare.h"

#include "hil_capture/dry_run.h"
#include "hil_capture/plan.h"
#include "mc_workbench/capture_export.h"

#include <QCryptographicHash>

namespace mc::workbench {

namespace {

QString digestOf(const QString& dryRun, const QString& confirmation, const QString& profileId) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(profileId.toUtf8());
    hash.addData(QByteArrayView("\n--\n"));
    hash.addData(dryRun.toUtf8());
    hash.addData(QByteArrayView("\n--\n"));
    hash.addData(confirmation.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

HilCheckResult HilPrepared::toResult() const {
    HilCheckResult r;
    r.exitCode = exitCode;
    r.errorText = errorText;
    r.refusalText = refusalText;
    r.dryRunText = dryRunText;
    r.confirmationText = confirmationText;
    r.profileId = profile.id;
    r.planId = planId;
    r.stepCount = static_cast<int>(resolved.steps.size());
    for (const mc::hil::UndecodedFrame& f : gate.readOnlyFrames) {
        r.readOnlyFrames.push_back({f.stepId, f.what, f.hex});
    }
    r.loopbackTcp = profile.device.transport == mc::TransportKind::Tcp &&
                    isLoopbackHost(profile.device.tcp.host);
    if (ok()) {
        r.digest = digestOf(dryRunText, confirmationText, profile.id);
    }
    return r;
}

HilPrepared prepareHil(const HilCheckInput& input) {
    HilPrepared p;
    const QString state = input.plcState.toUpper();
    if (state != QLatin1String("RUN") && state != QLatin1String("STOP")) {
        p.errorText = QStringLiteral("the PLC state is RUN or STOP");
        return p;
    }
    const mc::hil::ProfileLoad profile = mc::hil::loadProfileFile(input.profilePath);
    if (!profile.ok()) {
        p.errorText = QStringLiteral("profile %1: %2").arg(input.profilePath, profile.error.text());
        return p;
    }
    p.profile = *profile.profile;
    const mc::hil::PlanLoad plan = mc::hil::loadPlanFile(input.planPath);
    if (!plan.ok()) {
        p.errorText = QStringLiteral("plan %1: %2").arg(input.planPath, plan.error.text());
        return p;
    }
    p.planId = plan.plan->id;
    // A group nobody has is a typo, not "run nothing" (as in runTool()).
    for (const QString& group : input.only) {
        bool known = false;
        for (const mc::hil::Step& step : plan.plan->steps) {
            known = known || step.group.compare(group, Qt::CaseInsensitive) == 0;
        }
        if (!known) {
            p.errorText = QStringLiteral("group %1: no step of plan %2 belongs to this group")
                              .arg(group, plan.plan->id);
            return p;
        }
    }
    p.resolved = mc::hil::resolvePlan(*plan.plan, p.profile, input.only);
    if (!p.resolved.ok()) {
        QStringList lines;
        for (const mc::hil::StepError& e : p.resolved.errors) {
            lines << e.text();
        }
        p.errorText = lines.join(QLatin1Char('\n'));
        return p;
    }

    // The safety gate, the one of hil_capture (mc_hil_tool). It has no switch.
    p.gate = mc::hil::checkGate(p.resolved, p.profile);
    if (p.gate.refused()) {
        p.refusalText = mc::hil::refusalText(p.gate);
        p.exitCode = 3;
        return p;
    }
    p.dryRunText = mc::hil::dryRunText(p.resolved, p.profile);
    p.confirmationText = mc::hil::confirmationSummary(p.profile, p.gate, p.resolved, state);
    p.exitCode = 0;
    return p;
}

bool skipTypingAllowed(const HilCheckResult& check) {
    // runTool(): `--yes` counts only when `gate.readOnlyFrames.isEmpty()`. The GUI adds: only for a
    // profile that talks TCP to this computer (owner decision C, 2026-10-04). Another host or any
    // COM port always needs the typed id. `loopbackTcp` is derived from the profile on the thread
    // that runs the check, never taken from the request.
    return check.loopbackTcp && check.readOnlyFrames.isEmpty();
}

bool mustTypeProfileId(const HilCheckResult& check, bool skipTyping) {
    return !skipTyping || !skipTypingAllowed(check);
}

bool confirmationAccepted(const HilCheckResult& check, const QString& typedId, bool skipTyping,
                          QString* why) {
    const auto no = [&](const QString& text) {
        if (why != nullptr) {
            *why = text;
        }
        return false;
    };
    if (!check.ok()) {
        return no(QStringLiteral("the gate did not pass; nothing was sent"));
    }
    if (mustTypeProfileId(check, skipTyping) && typedId.trimmed() != check.profileId) {
        if (!check.readOnlyFrames.isEmpty()) {
            return no(QStringLiteral("not confirmed: the run holds read-only frames the gate "
                                     "could not decode, so the profile id (%1) must be typed")
                          .arg(check.profileId));
        }
        if (skipTyping && !check.loopbackTcp) {
            return no(QStringLiteral("not confirmed: only a profile that talks to this computer "
                                     "(a loopback TCP host) may be confirmed without typing; "
                                     "type the profile id (%1)")
                          .arg(check.profileId));
        }
        return no(QStringLiteral("not confirmed: type the profile id (%1)").arg(check.profileId));
    }
    return true;
}

} // namespace mc::workbench
