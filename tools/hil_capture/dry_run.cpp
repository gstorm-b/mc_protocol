#include "hil_capture/dry_run.h"

#include "mc/core/poll_plan.h"
#include "mc/core/protocol.h"

#include <QStringList>

namespace mc::hil {

namespace {

QString frameLabel(const FrameConfig& f) {
    QString text = frameName(f);
    if (f.isSerial()) {
        text += QStringLiteral(" format %1").arg(static_cast<int>(f.format));
    } else {
        text += f.code == DataCode::Binary ? QStringLiteral(" Binary") : QStringLiteral(" ASCII");
    }
    return text;
}

QString subText(const ResolvedSub& s) {
    return QStringLiteral("%1 x%2%3")
        .arg(deviceText(s.head))
        .arg(s.count)
        .arg(s.input ? QStringLiteral(" (input)") : QString());
}

void tx(QString& out, const ByteBuf& frame) {
    out += QStringLiteral("    tx ") + hexText(frame) + QLatin1Char('\n');
}

void printOp(QString& out, const ResolvedOp& op) {
    out += QStringLiteral("  OP %1: %2%3\n")
               .arg(op.recordId, op.description,
                    op.readBack ? QStringLiteral(" (read-back)") : QString());
    if (!op.notSent.isEmpty()) {
        out += QStringLiteral("    not sent: %1\n").arg(op.notSent);
        return;
    }
    for (const ByteBuf& frame : op.frames) {
        tx(out, frame);
    }
    if (op.via != Via::Api && op.readOnly) {
        out += QStringLiteral("    (declared readOnly)\n");
    }
}

void printPlan(QString& out, const QVector<ResolvedSub>& subs, const FrameConfig& frame,
               const Profile& profile, const std::optional<bool>& bitsAsWords,
               const QString& when) {
    RangeSet set;
    for (const ResolvedSub& s : subs) {
        const Expected<SubscriptionId> id = set.add(s.head, s.count);
        if (!id) {
            out += QStringLiteral("  PLAN %1: subscription %2 refused: %3\n")
                       .arg(when, s.name, describeError(id.error()));
            return;
        }
    }
    PlanOptions options = profile.device.session.plan;
    if (bitsAsWords) {
        options.bitsAsWords = *bitsAsWords;
    }
    const Expected<ReadPlan> plan = ReadPlan::build(set, frame, options);
    if (!plan) {
        out += QStringLiteral("  PLAN %1: cannot be built: %2\n")
                   .arg(when, describeError(plan.error()));
        return;
    }
    out += QStringLiteral("  PLAN %1: %2 read chunk(s) per round\n")
               .arg(when)
               .arg(plan.value().size());
    const McProtocol codec(frame);
    for (size_t i = 0; i < plan.value().size(); ++i) {
        const Request& r = plan.value().chunk(i).request;
        out += QStringLiteral("  CHUNK %1 %2 x%3\n")
                   .arg(r.op == Op::ReadBits ? QStringLiteral("ReadBits")
                                             : QStringLiteral("ReadWords"),
                        deviceText(r.head))
                   .arg(r.count);
        const Expected<ByteBuf> bytes = codec.encode(r);
        if (bytes) {
            tx(out, bytes.value());
        } else {
            out += QStringLiteral("    not sent: %1\n").arg(describeError(bytes.error()));
        }
    }
}

void printPoll(QString& out, const ResolvedPoll& poll, const FrameConfig& frame,
               const Profile& profile) {
    const McProtocol codec(frame);
    if (poll.heartbeat) {
        for (const uint8_t value : {uint8_t(1), uint8_t(0)}) {
            Request w = Request::writeBits(*poll.heartbeat, ByteView{&value, 1});
            out +=
                QStringLiteral("  HEARTBEAT %1 = %2\n").arg(deviceText(*poll.heartbeat)).arg(value);
            const Expected<ByteBuf> bytes = codec.encode(w);
            if (bytes) {
                tx(out, bytes.value());
            } else {
                out += QStringLiteral("    not sent: %1\n").arg(describeError(bytes.error()));
            }
        }
    }
    QVector<ResolvedSub> subs = poll.subs;
    printPlan(out, subs, frame, profile, poll.bitsAsWords, QStringLiteral("at start"));
    for (const ResolvedAction& a : poll.actions) {
        switch (a.kind) {
        case PollAction::Kind::Write:
            out += QStringLiteral("  AFTER ROUND %1\n").arg(a.after);
            printOp(out, a.write);
            break;
        case PollAction::Kind::Subscribe:
            subs.push_back(a.sub);
            out += QStringLiteral("  AFTER ROUND %1: subscribe %2 = %3\n")
                       .arg(a.after)
                       .arg(a.sub.name, subText(a.sub));
            printPlan(out, subs, frame, profile, poll.bitsAsWords,
                      QStringLiteral("after round %1").arg(a.after));
            break;
        case PollAction::Kind::Unsubscribe:
            for (int i = 0; i < subs.size(); ++i) {
                if (subs[i].name == a.name) {
                    subs.remove(i);
                    break;
                }
            }
            out += QStringLiteral("  AFTER ROUND %1: unsubscribe %2\n").arg(a.after).arg(a.name);
            printPlan(out, subs, frame, profile, poll.bitsAsWords,
                      QStringLiteral("after round %1").arg(a.after));
            break;
        case PollAction::Kind::Prompt:
            out +=
                QStringLiteral("  AFTER ROUND %1: operator prompt: %2\n").arg(a.after).arg(a.text);
            break;
        }
    }
}

QString transportText(const Profile& p) {
    if (p.device.transport == TransportKind::Serial) {
        const SerialSettings& s = p.device.serial;
        return QStringLiteral("serial %1, %2 baud").arg(s.portName).arg(s.baudRate);
    }
    return QStringLiteral("tcp %1:%2").arg(p.device.tcp.host).arg(p.device.tcp.port);
}

} // namespace

QString dryRunText(const ResolveResult& resolved, const Profile& profile) {
    QString out;
    out += QStringLiteral("DRY RUN: profile %1, plan %2 (nothing is connected or sent)\n")
               .arg(profile.id, resolved.planId);
    int frames = 0;
    for (const ResolvedStep& step : resolved.steps) {
        const QString kind = [&]() {
            switch (step.kind) {
            case StepKind::Write:
                return QStringLiteral("write");
            case StepKind::Read:
                return QStringLiteral("read");
            case StepKind::Poll:
                return QStringLiteral("poll");
            case StepKind::Mutate:
                return QStringLiteral("mutate");
            case StepKind::Raw:
                return QStringLiteral("raw");
            case StepKind::Bench:
                return QStringLiteral("bench");
            }
            return QString();
        }();
        if (step.skipped()) {
            out += QStringLiteral("STEP %1 [%2] %3\n").arg(step.id, kind, step.skipReason);
            continue;
        }
        out += QStringLiteral("STEP %1 [%2] %3%4\n")
                   .arg(step.id, kind, frameLabel(step.frame),
                        step.title.isEmpty() ? QString() : QStringLiteral(": ") + step.title);
        switch (step.kind) {
        case StepKind::Write:
        case StepKind::Read:
        case StepKind::Mutate:
        case StepKind::Raw:
            for (const ResolvedOp& op : step.ops) {
                printOp(out, op);
            }
            break;
        case StepKind::Poll:
            printPoll(out, step.poll, step.frame, profile);
            break;
        case StepKind::Bench:
            if (step.bench.isPollSet) {
                out += QStringLiteral("  BENCH of the subscription set of %1\n")
                           .arg(step.bench.pollSetId);
                printPoll(out, step.bench.poll, step.frame, profile);
            } else {
                out += QStringLiteral("  BENCH repeats:\n");
                printOp(out, step.bench.request);
            }
            break;
        }
    }
    frames = out.count(QStringLiteral("    tx "));
    out += QStringLiteral("DRY RUN: %1 frame(s) listed, safety gate OK\n").arg(frames);
    return out;
}

QString confirmationSummary(const Profile& profile, const GateReport& gate,
                            const ResolveResult& resolved, const QString& plcState) {
    QString out;
    out += QStringLiteral("About to run against a PLC.\n");
    out += QStringLiteral("  PLC:        %1 (module %2, firmware %3)\n")
               .arg(profile.plc, profile.module.isEmpty() ? QStringLiteral("-") : profile.module,
                    profile.firmware.isEmpty() ? QStringLiteral("-") : profile.firmware);
    out += QStringLiteral("  Adapter:    %1\n")
               .arg(profile.adapter.isEmpty() ? QStringLiteral("-") : profile.adapter);
    out += QStringLiteral("  PLC state:  %1\n")
               .arg(profile.plcState.isEmpty() ? QStringLiteral("-") : profile.plcState);
    out += QStringLiteral("  Operator-set state: %1 (confirm the PLC really is in %1; the tool "
                          "never changes it)\n")
               .arg(plcState);
    out += QStringLiteral("  Transport:  %1, frame %2\n")
               .arg(transportText(profile), frameLabel(profile.device.frame));
    QStringList ranges;
    for (const ScratchRange& r : profile.scratch) {
        ranges << QStringLiteral("%1%2-%1%3")
                      .arg(deviceSymbol(r.type), formatDeviceNumber(r.type, r.first),
                           formatDeviceNumber(r.type, r.last));
    }
    out += QStringLiteral("  Scratch:    %1\n")
               .arg(ranges.isEmpty() ? QStringLiteral("(none: no write can pass the gate)")
                                     : ranges.join(QStringLiteral(", ")));
    int active = 0;
    int skipped = 0;
    for (const ResolvedStep& s : resolved.steps) {
        (s.skipped() ? skipped : active) += 1;
    }
    out +=
        QStringLiteral("  Steps:      %1 to run, %2 skipped; every write passed the safety gate\n")
            .arg(active)
            .arg(skipped);
    if (gate.readOnlyFrames.isEmpty()) {
        out += QStringLiteral("  Read-only frames the gate could not decode: none\n");
    } else {
        out += QStringLiteral(
            "  Read-only frames the gate could not decode (the plan declares them harmless):\n");
        for (const UndecodedFrame& f : gate.readOnlyFrames) {
            out += QStringLiteral("    %1  %2  %3\n").arg(f.stepId, f.what, f.hex);
        }
    }
    return out;
}

} // namespace mc::hil
