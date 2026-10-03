#include "hil_capture/resolve.h"

#include "mc/core/limits.h"
#include "mc/core/protocol.h"

#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace mc::hil {

const char* opName(Op op) {
    switch (op) {
    case Op::ReadBits:
        return "ReadBits";
    case Op::ReadWords:
        return "ReadWords";
    case Op::WriteBits:
        return "WriteBits";
    case Op::WriteWords:
        return "WriteWords";
    }
    return "?";
}

namespace {

// Everything a resolution function needs about the step it works for.
struct Ctx {
    const Profile& profile;
    const Plan& plan;
    QString stepId;
    FrameConfig frame;
};

uint16_t limitValue(Limit l, const FrameConfig& f) {
    switch (l) {
    case Limit::Wmax:
        return maxPoints(f, Op::ReadWords, DeviceKind::Word);
    case Limit::BRmax:
        return maxPoints(f, Op::ReadBits, DeviceKind::Bit);
    case Limit::BWmax:
        return maxPoints(f, Op::WriteBits, DeviceKind::Bit);
    case Limit::WBRmax:
        return maxPoints(f, Op::ReadWords, DeviceKind::Bit);
    case Limit::WBWmax:
        return maxPoints(f, Op::WriteWords, DeviceKind::Bit);
    case Limit::None:
        break;
    }
    return 0;
}

bool resolveCountValue(const CountExpr& c, const FrameConfig& f, int64_t& out, QString& why) {
    if (c.a == Limit::None) {
        out = c.value;
    } else {
        const int64_t a = limitValue(c.a, f);
        if (a == 0) {
            why = QStringLiteral("the frame has no point limit for '%1'").arg(c.text);
            return false;
        }
        if (c.isMin) {
            const int64_t b = limitValue(c.b, f);
            if (b == 0) {
                why = QStringLiteral("the frame has no point limit for '%1'").arg(c.text);
                return false;
            }
            out = std::min(a, b);
        } else {
            out = a + c.delta;
        }
    }
    if (out < 1 || out > 65535) {
        why = QStringLiteral("count %1 of '%2' is outside 1..65535").arg(out).arg(c.text);
        return false;
    }
    return true;
}

bool resolveRef(const DeviceRef& ref, const Profile& p, Device& out, bool& scratchRelative,
                QString& why) {
    scratchRelative = false;
    int64_t number = 0;
    switch (ref.base) {
    case DeviceRef::Base::Literal: {
        const QByteArray bytes = ref.text.toLatin1();
        const Expected<Device> d =
            parseDevice(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())),
                        p.device.frame.xyNotation);
        if (!d) {
            why = QStringLiteral("'%1' is not a device number under the profile's X/Y notation")
                      .arg(ref.text);
            return false;
        }
        out = d.value();
        return true;
    }
    case DeviceRef::Base::ScratchStart:
    case DeviceRef::Base::ScratchAligned: {
        const ScratchRange* r = p.firstScratch(ref.type);
        if (r == nullptr) {
            why = QStringLiteral("'%1' needs a scratch range of %2, the profile has none")
                      .arg(ref.text, deviceSymbol(ref.type));
            return false;
        }
        int64_t first = r->first;
        if (ref.base == DeviceRef::Base::ScratchAligned) {
            first = ((first + ref.align - 1) / ref.align) * ref.align;
            if (first > static_cast<int64_t>(r->last)) {
                why = QStringLiteral("'%1': no number aligned to %2 inside the first scratch range "
                                     "of %3")
                          .arg(ref.text)
                          .arg(ref.align)
                          .arg(deviceSymbol(ref.type));
                return false;
            }
        }
        number = first + ref.offset;
        scratchRelative = true;
        break;
    }
    case DeviceRef::Base::End: {
        const std::optional<uint32_t> end = p.end(ref.type);
        if (!end) {
            why = QStringLiteral("'%1' needs deviceEnd of %2, the profile has none")
                      .arg(ref.text, deviceSymbol(ref.type));
            return false;
        }
        number = static_cast<int64_t>(*end) + ref.offset;
        out.type = ref.type;
        break;
    }
    case DeviceRef::Base::SpecialBit:
        out.type = p.specialBit.type;
        number = static_cast<int64_t>(p.specialBit.number) + ref.offset;
        break;
    case DeviceRef::Base::SpecialWord:
        out.type = p.specialWord.type;
        number = static_cast<int64_t>(p.specialWord.number) + ref.offset;
        break;
    case DeviceRef::Base::ScanTime:
        if (!p.scanTime) {
            why = QStringLiteral("'@scan' needs scanTimeDevice, the profile has none");
            return false;
        }
        out = *p.scanTime;
        return true;
    }
    if (ref.base == DeviceRef::Base::ScratchStart || ref.base == DeviceRef::Base::ScratchAligned) {
        out.type = ref.type;
    }
    if (number < 0 || number > 0xFFFFFFFFLL) {
        why = QStringLiteral("'%1' resolves outside the device number range").arg(ref.text);
        return false;
    }
    out.number = static_cast<uint32_t>(number);
    return true;
}

QString substitute(const QString& text, const FrameConfig& f) {
    QString out = text;
    out.replace(QStringLiteral("{x}"),
                f.code == DataCode::Binary ? QStringLiteral("B") : QStringLiteral("A"));
    out.replace(QStringLiteral("{n}"), QString::number(static_cast<int>(f.format)));
    return out;
}

// The words or bits of a write, one byte per bit point or two bytes per word.
bool buildData(const ValuesSpec& v, bool wordUnit, uint16_t count, bool countFromValues,
               ByteBuf& out, QString& why) {
    out.clear();
    const auto putWord = [&](uint32_t w) {
        out.push_back(static_cast<uint8_t>(w & 0xFF));
        out.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
    };
    switch (v.mode) {
    case ValuesSpec::Mode::None:
        why = QStringLiteral("a write needs values");
        return false;
    case ValuesSpec::Mode::List:
        if (!countFromValues && v.list.size() != count) {
            why = QStringLiteral("%1 values for a count of %2").arg(v.list.size()).arg(count);
            return false;
        }
        for (const uint16_t w : v.list) {
            if (wordUnit) {
                putWord(w);
            } else {
                if (w > 1) {
                    why = QStringLiteral("a bit value must be 0 or 1, got %1").arg(w);
                    return false;
                }
                out.push_back(static_cast<uint8_t>(w));
            }
        }
        return true;
    case ValuesSpec::Mode::Index:
        if (!wordUnit) {
            why = QStringLiteral("gen index writes words, not bits");
            return false;
        }
        for (uint32_t k = 0; k < count; ++k) {
            putWord((k * v.mul) & 0xFFFF);
        }
        return true;
    case ValuesSpec::Mode::Alternating:
        if (wordUnit) {
            why = QStringLiteral("gen alt writes bits, not words");
            return false;
        }
        for (uint32_t k = 0; k < count; ++k) {
            out.push_back(static_cast<uint8_t>((k % 2 == 0) ? v.first : 1 - v.first));
        }
        return true;
    case ValuesSpec::Mode::Fill:
        if (!wordUnit && v.fill > 1) {
            why = QStringLiteral("a bit value must be 0 or 1, got %1").arg(v.fill);
            return false;
        }
        for (uint32_t k = 0; k < count; ++k) {
            if (wordUnit) {
                putWord(v.fill);
            } else {
                out.push_back(static_cast<uint8_t>(v.fill));
            }
        }
        return true;
    }
    return false;
}

QString opDescription(const ResolvedRequest& r, XyNumbering xy) {
    return QStringLiteral("%1 %2 x%3")
        .arg(QLatin1String(opName(r.op)), deviceText(r.head, xy))
        .arg(r.count);
}

// Encodes the request the way McDevice would send it: split with mc::chunk(), one frame each.
void encodeApi(ResolvedOp& op) {
    const Request whole = [&]() {
        Request r;
        r.op = op.request.op;
        r.head = op.request.head;
        r.count = op.request.count;
        r.data = ByteView{op.request.data.data(), op.request.data.size()};
        return r;
    }();
    const Expected<size_t> n = chunkCount(whole, op.frame);
    if (!n) {
        op.notSent = describeError(n.error());
        return;
    }
    std::vector<Chunk> chunks(n.value());
    const Expected<size_t> made = chunk(whole, op.frame, chunks.data(), chunks.size());
    if (!made) {
        op.notSent = describeError(made.error());
        return;
    }
    const McProtocol codec(op.frame);
    for (const Chunk& c : chunks) {
        Request r = whole;
        r.head.number = c.headNumber;
        r.count = c.count;
        if (whole.isWrite()) {
            const size_t perPoint = whole.op == Op::WriteWords ? 2 : 1;
            const size_t bytes = static_cast<size_t>(c.count) * perPoint;
            r.data = ByteView{op.request.data.data() + c.dataOffset, bytes};
        }
        const Expected<ByteBuf> frame = codec.encode(r);
        if (!frame) {
            op.notSent = describeError(frame.error());
            op.frames.clear();
            return;
        }
        op.frames.push_back(frame.value());
        op.frameMeta.push_back(FrameMeta{r.op, r.head, r.count});
    }
}

// The SUM character pair of a serial frame.
bool sumPosition(const ByteBuf& frame, const FrameConfig& cfg, size_t& pos, QString& why) {
    if (!cfg.isSerial() || !cfg.sumCheck) {
        why = QStringLiteral("replaceSum needs a serial frame with sumCheck on");
        return false;
    }
    const size_t tail = cfg.format == SerialFormat::Format4 ? 4 : 2;
    if (frame.size() < tail + 1) {
        why = QStringLiteral("frame too short for a SUM");
        return false;
    }
    pos = frame.size() - tail;
    return true;
}

int hexValue(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

bool conditionHolds(const QString& textIn, const Profile& p) {
    QString text = textIn.trimmed();
    bool negate = false;
    if (text.startsWith(QLatin1Char('!'))) {
        negate = true;
        text = text.mid(1);
    }
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    const QString key = colon < 0 ? text : text.left(colon);
    const QString arg = colon < 0 ? QString() : text.mid(colon + 1);
    const FrameConfig& f = p.device.frame;
    bool holds = false;
    if (key == QLatin1String("scanTime")) {
        holds = p.scanTime.has_value();
    } else if (key == QLatin1String("supports")) {
        holds = p.supportsType(*deviceTypeFromSymbol(arg));
    } else if (key == QLatin1String("scratch")) {
        holds = p.firstScratch(*deviceTypeFromSymbol(arg)) != nullptr;
    } else if (key == QLatin1String("endNotMultiple16")) {
        const std::optional<uint32_t> end = p.end(*deviceTypeFromSymbol(arg));
        holds = end && ((*end + 1) % 16) != 0;
    } else if (key == QLatin1String("frame")) {
        holds = frameName(f) == arg;
    } else if (key == QLatin1String("code")) {
        holds = f.code == (arg == QLatin1String("Binary") ? DataCode::Binary : DataCode::Ascii);
    } else if (key == QLatin1String("format")) {
        holds = f.isSerial() && static_cast<int>(f.format) == arg.toInt();
    } else if (key == QLatin1String("sumCheck")) {
        holds = f.sumCheck == (arg == QLatin1String("on"));
    } else if (key == QLatin1String("profile")) {
        holds = QRegularExpression(QRegularExpression::wildcardToRegularExpression(arg))
                    .match(p.id)
                    .hasMatch();
    }
    return negate ? !holds : holds;
}

// Resolves one OP into one or more operations (the implicit read-back follows a write).
bool resolveOp(const OpSpec& spec, const Ctx& ctx, ResolvedOp& op, QString& why) {
    op = ResolvedOp{};
    op.via = Via::Api;
    op.frame = ctx.frame;
    Device head;
    bool scratchRelative = false;
    if (spec.device.hasTemplate) {
        why = QStringLiteral("device '%1' still holds {T}: use foreach").arg(spec.device.text);
        return false;
    }
    if (!resolveRef(spec.device, ctx.profile, head, scratchRelative, why)) {
        return false;
    }
    const DeviceKind kind = deviceInfo(head.type).kind;
    if (kind == DeviceKind::DWord) {
        why = QStringLiteral("double word devices are not supported");
        return false;
    }
    const bool wordUnit =
        spec.unit == QLatin1String("word") || (spec.unit.isEmpty() && kind == DeviceKind::Word);
    if (!wordUnit && kind != DeviceKind::Bit) {
        why = QStringLiteral("unit bit on word device %1")
                  .arg(deviceText(head, ctx.profile.device.frame.xyNotation));
        return false;
    }
    int64_t count = 1;
    const bool countFromValues =
        !spec.count.present && spec.write && spec.values.mode == ValuesSpec::Mode::List;
    if (spec.count.present) {
        if (!resolveCountValue(spec.count, ctx.frame, count, why)) {
            return false;
        }
    } else if (countFromValues) {
        count = spec.values.list.size();
        if (count < 1 || count > 65535) {
            why = QStringLiteral("count %1 is outside 1..65535").arg(count);
            return false;
        }
    }
    op.request.op = spec.write ? (wordUnit ? Op::WriteWords : Op::WriteBits)
                               : (wordUnit ? Op::ReadWords : Op::ReadBits);
    op.request.head = head;
    op.request.count = static_cast<uint16_t>(count);
    op.request.scratchRelative = scratchRelative;
    if (spec.write && !buildData(spec.values, wordUnit, op.request.count, countFromValues,
                                 op.request.data, why)) {
        return false;
    }
    op.expect = spec.expect;
    op.metaKey = spec.metaKey;
    op.scratchMisfit = scratchRelative &&
                       !ctx.profile.inScratch(head.type, head.number, op.request.numbersCovered());
    op.description = opDescription(op.request, ctx.profile.device.frame.xyNotation);
    encodeApi(op);
    return true;
}

bool resolveSub(const PollSub& s, const Ctx& ctx, ResolvedSub& out, QString& why) {
    out = ResolvedSub{};
    out.name = s.name;
    out.input = s.input;
    if (!resolveRef(s.device, ctx.profile, out.head, out.scratchRelative, why)) {
        return false;
    }
    int64_t count = 1;
    if (s.count.present && !resolveCountValue(s.count, ctx.frame, count, why)) {
        return false;
    }
    out.count = static_cast<uint32_t>(count);
    return true;
}

bool resolvePoll(const PollSpec& spec, const Ctx& ctx, ResolvedPoll& out, QString& why) {
    out = ResolvedPoll{};
    out.rounds = spec.rounds;
    out.bitsAsWords = spec.bitsAsWords;
    if (spec.hasHeartbeat) {
        Device hb;
        bool rel = false;
        if (!resolveRef(spec.heartbeat, ctx.profile, hb, rel, why)) {
            return false;
        }
        if (deviceInfo(hb.type).kind != DeviceKind::Bit) {
            why = QStringLiteral("the heartbeat must be a bit device, got %1")
                      .arg(deviceText(hb, ctx.profile.device.frame.xyNotation));
            return false;
        }
        out.heartbeat = hb;
    }
    for (const PollSub& s : spec.subs) {
        ResolvedSub rs;
        if (!resolveSub(s, ctx, rs, why)) {
            return false;
        }
        out.subs.push_back(rs);
    }
    for (const PollAction& a : spec.actions) {
        ResolvedAction ra;
        ra.kind = a.kind;
        ra.after = a.after;
        switch (a.kind) {
        case PollAction::Kind::Write:
            if (!resolveOp(a.write, ctx, ra.write, why)) {
                return false;
            }
            ra.write.recordId = ctx.stepId;
            break;
        case PollAction::Kind::Subscribe:
            if (!resolveSub(a.sub, ctx, ra.sub, why)) {
                return false;
            }
            break;
        case PollAction::Kind::Unsubscribe:
            ra.name = a.name;
            break;
        case PollAction::Kind::Prompt:
            ra.text = a.text;
            break;
        }
        out.actions.push_back(ra);
    }
    return true;
}

// The steps a foreach step stands for.
bool expandStep(const Step& step, const Profile& profile, QVector<Step>& out, QString& why) {
    if (step.expandOver.isEmpty()) {
        out.push_back(step);
        return true;
    }
    if (step.kind != StepKind::Read && step.kind != StepKind::Write) {
        why = QStringLiteral("foreach is for read and write steps");
        return false;
    }
    QVector<DeviceType> types;
    if (step.expandOver == QLatin1String("supports")) {
        types = profile.supports;
    } else {
        const size_t n = static_cast<size_t>(DeviceType::Count);
        for (size_t i = 0; i < n; ++i) {
            const auto t = static_cast<DeviceType>(i);
            if (frameHasCode(profile.device.frame, t) && !profile.supportsType(t)) {
                types.push_back(t);
            }
        }
    }
    for (const DeviceType t : types) {
        Step s = step;
        s.expandOver.clear();
        s.id = step.id + QLatin1Char('-') + deviceSymbol(t);
        const auto sub = [&](OpSpec& op) -> bool {
            if (!op.device.hasTemplate) {
                return true;
            }
            QString text = op.device.text;
            text.replace(QStringLiteral("{T}"), deviceSymbol(t));
            return parseDeviceRef(text, op.device, why);
        };
        if (!sub(s.op)) {
            return false;
        }
        for (OpSpec& o : s.then) {
            if (!sub(o)) {
                return false;
            }
        }
        out.push_back(s);
    }
    return true;
}

bool resolveStep(const Step& step, const Ctx& ctxIn, const Profile& profile, ResolvedStep& rs,
                 QString& why) {
    rs.id = step.id;
    rs.group = step.group;
    rs.title = step.title;
    rs.kind = step.kind;
    rs.frameOverride = step.frameOverride;
    Ctx ctx = ctxIn;
    ctx.stepId = step.id;

    for (const QString& cond : step.conditions) {
        if (!conditionHolds(cond, profile)) {
            rs.frame = profile.device.frame;
            rs.skipReason = QStringLiteral("skipped: requires %1").arg(cond);
            return true;
        }
    }
    const std::optional<FrameConfig> frame = applyFrameOverride(profile, step.frameOverride, why);
    if (!frame) {
        return false;
    }
    rs.frame = *frame;
    ctx.frame = *frame;
    rs.expect = step.expect;

    const QString mirrors = substitute(step.mirrors, *frame);
    const auto finishOp = [&](ResolvedOp& op, int index) {
        op.recordId = index == 0 ? step.id : QStringLiteral("%1.%2").arg(step.id).arg(index + 1);
        op.mirrors = mirrors;
        op.overrideText = overrideTextOf(step.frameOverride);
    };

    switch (step.kind) {
    case StepKind::Write:
    case StepKind::Read:
    case StepKind::Mutate:
    case StepKind::Raw: {
        int index = 0;
        // An API operation, followed by its implicit read-back when the plan asks for one.
        const auto addApi = [&](const OpSpec& spec) -> bool {
            ResolvedOp op;
            if (!resolveOp(spec, ctx, op, why)) {
                return false;
            }
            finishOp(op, index++);
            rs.ops.push_back(op);
            if (!(spec.write && spec.readBack && spec.expect.kind == ExpectKind::Ok)) {
                return true;
            }
            OpSpec back = spec;
            back.write = false;
            // The read-back covers exactly what was written (the count may have come from the
            // values).
            back.count = CountExpr{};
            back.count.present = true;
            back.count.value = rs.ops.back().request.count;
            back.count.text = QString::number(back.count.value);
            back.values = ValuesSpec{};
            back.readBack = false;
            back.expect = Expect{};
            back.expect.kind = ExpectKind::Ok;
            back.hasExpect = true;
            ResolvedOp rb;
            if (!resolveOp(back, ctx, rb, why)) {
                return false;
            }
            rb.readBack = true;
            // Expected values: the bytes just written, as words or as bits.
            rb.expect.hasValues = true;
            const ResolvedRequest written = rs.ops.back().request;
            if (written.op == Op::WriteWords) {
                for (size_t i = 0; i + 1 < written.data.size(); i += 2) {
                    rb.expect.values.push_back(
                        static_cast<uint16_t>(written.data[i] | (written.data[i + 1] << 8)));
                }
            } else {
                for (const uint8_t b : written.data) {
                    rb.expect.values.push_back(b);
                }
            }
            finishOp(rb, index++);
            rs.ops.push_back(rb);
            return true;
        };
        if (step.kind == StepKind::Write || step.kind == StepKind::Read) {
            if (!addApi(step.op)) {
                return false;
            }
        } else if (step.kind == StepKind::Mutate) {
            ResolvedOp op;
            if (!resolveOp(step.op, ctx, op, why)) {
                return false;
            }
            // A mutate sends one frame, exactly as McProtocol encodes the base request.
            const McProtocol codec(*frame);
            Request r;
            r.op = op.request.op;
            r.head = op.request.head;
            r.count = op.request.count;
            r.data = ByteView{op.request.data.data(), op.request.data.size()};
            const Expected<ByteBuf> base = codec.encode(r);
            if (!base) {
                why = QStringLiteral("the base request cannot be encoded: %1")
                          .arg(describeError(base.error()));
                return false;
            }
            ByteBuf edited;
            if (!applyEdits(base.value(), *frame, step.edits, edited, why)) {
                return false;
            }
            op.via = Via::Mutate;
            op.frames.clear();
            op.frameMeta.clear();
            op.frames.push_back(edited);
            op.frameMeta.push_back(FrameMeta{r.op, r.head, r.count});
            op.notSent.clear();
            op.expect = step.expect;
            op.readOnly = step.readOnly;
            op.recover = step.recover;
            op.description = QStringLiteral("mutate %1, %2 edit(s)")
                                 .arg(opDescription(op.request, profile.device.frame.xyNotation))
                                 .arg(step.edits.size());
            finishOp(op, index++);
            rs.ops.push_back(op);
        } else {
            ResolvedOp op;
            op.via = Via::Raw;
            op.frame = *frame;
            ByteBuf frameBytes;
            if (!step.rawRequests.isEmpty()) {
                // Several encoded requests, one write.
                const McProtocol codec(*frame);
                for (const OpSpec& spec : step.rawRequests) {
                    ResolvedOp one;
                    if (!resolveOp(spec, ctx, one, why)) {
                        return false;
                    }
                    Request r;
                    r.op = one.request.op;
                    r.head = one.request.head;
                    r.count = one.request.count;
                    r.data = ByteView{one.request.data.data(), one.request.data.size()};
                    const Expected<ByteBuf> encoded = codec.encode(r);
                    if (!encoded) {
                        why = QStringLiteral("a request cannot be encoded: %1")
                                  .arg(describeError(encoded.error()));
                        return false;
                    }
                    frameBytes.insert(frameBytes.end(), encoded.value().begin(),
                                      encoded.value().end());
                }
                op.description = QStringLiteral("raw %1 request(s), %2 byte(s) in one write")
                                     .arg(step.rawRequests.size())
                                     .arg(frameBytes.size());
            } else {
                QString text = step.rawHex;
                if (text.contains(QStringLiteral("{EOT}"))) {
                    if (!frame->isSerial()) {
                        why = QStringLiteral("{EOT} is for serial frames");
                        return false;
                    }
                    text.replace(QStringLiteral("{EOT}"), frame->format == SerialFormat::Format4
                                                              ? QStringLiteral("04 0D 0A")
                                                              : QStringLiteral("04"));
                }
                const QByteArray bytes =
                    QByteArray::fromHex(text.simplified().remove(QLatin1Char(' ')).toLatin1());
                frameBytes.assign(bytes.begin(), bytes.end());
                op.description = QStringLiteral("raw %1 byte(s)").arg(frameBytes.size());
            }
            op.frames.push_back(frameBytes);
            op.expect = step.expect;
            op.readOnly = step.readOnly;
            op.recover = step.recover;
            finishOp(op, index++);
            rs.ops.push_back(op);
        }
        for (const OpSpec& spec : step.then) {
            if (!addApi(spec)) {
                return false;
            }
        }
        break;
    }
    case StepKind::Poll:
        if (!resolvePoll(step.poll, ctx, rs.poll, why)) {
            return false;
        }
        break;
    case StepKind::Bench:
        rs.bench.reps = step.bench.reps;
        rs.bench.warmup = step.bench.warmup;
        if (step.bench.hasRequest) {
            if (!resolveOp(step.bench.request, ctx, rs.bench.request, why)) {
                return false;
            }
            finishOp(rs.bench.request, 0);
        } else {
            rs.bench.isPollSet = true;
            rs.bench.pollSetId = step.bench.pollSet;
            for (const Step& other : ctx.plan.steps) {
                if (other.id == step.bench.pollSet && other.kind == StepKind::Poll) {
                    if (!resolvePoll(other.poll, ctx, rs.bench.poll, why)) {
                        return false;
                    }
                }
            }
        }
        break;
    }

    if (step.scratchSkip) {
        bool misfit = false;
        for (const ResolvedOp& op : rs.ops) {
            misfit = misfit || op.scratchMisfit;
        }
        misfit = misfit || rs.bench.request.scratchMisfit;
        if (misfit) {
            rs.skipReason = QStringLiteral("skipped: scratch too small");
        }
    }
    return true;
}

} // namespace

uint64_t ResolvedRequest::numbersCovered() const {
    const bool bitDevice = deviceInfo(head.type).kind == DeviceKind::Bit;
    const bool wordOp = op == Op::ReadWords || op == Op::WriteWords;
    return wordOp && bitDevice ? static_cast<uint64_t>(count) * 16 : count;
}

QString overrideTextOf(const QJsonObject& o) {
    QStringList parts;
    QStringList keys = o.keys();
    keys.sort();
    for (const QString& k : keys) {
        const QJsonValue v = o.value(k);
        const QString text = v.isString() ? v.toString()
                             : v.isBool()
                                 ? (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                                 : QString::number(v.toDouble(), 'g', 12);
        parts << k + QLatin1Char('=') + text;
    }
    return parts.join(QLatin1Char(' '));
}

QString describeError(const Error& e) {
    QString text = QString::fromLatin1(e.message);
    if (e.code == ErrorCode::PlcError) {
        text += QStringLiteral(" (end code 0x%1)").arg(e.plcCode, 4, 16, QLatin1Char('0'));
    } else {
        text += QStringLiteral(" (error %1)").arg(static_cast<int>(e.code));
    }
    return text;
}

QString frameName(const FrameConfig& f) {
    switch (f.frame) {
    case FrameType::F3E:
        return QStringLiteral("3E");
    case FrameType::F1E:
        return QStringLiteral("1E");
    case FrameType::F3C:
        return QStringLiteral("3C");
    case FrameType::F1C:
        return QStringLiteral("1C");
    case FrameType::F4E:
        return QStringLiteral("4E");
    case FrameType::F4C:
        return QStringLiteral("4C");
    }
    return QString();
}

bool frameHasCode(const FrameConfig& f, DeviceType t) {
    const DeviceInfo& i = deviceInfo(t);
    switch (f.frame) {
    case FrameType::F3E:
        if (f.code == DataCode::Binary) {
            return (f.series == PlcSeries::IqR ? i.qnaBinIqr : i.qnaBinQL) != kNoCode;
        }
        return (f.series == PlcSeries::IqR ? i.qnaAsciiIqr[0] : i.qnaAsciiQL[0]) != '\0';
    case FrameType::F3C:
        return (f.series == PlcSeries::IqR ? i.qnaAsciiIqr[0] : i.qnaAsciiQL[0]) != '\0';
    case FrameType::F1E:
        return i.e1Code != kNoCode;
    case FrameType::F1C:
        return i.c1Code[0] != '\0';
    default:
        return false;
    }
}

QString hexText(const ByteBuf& bytes) {
    QString text;
    text.reserve(static_cast<qsizetype>(bytes.size()) * 3);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            text += QLatin1Char(' ');
        }
        text += QStringLiteral("%1")
                    .arg(static_cast<uint>(bytes[i]), 2, 16, QLatin1Char('0'))
                    .toUpper();
    }
    return text;
}

std::optional<FrameConfig> applyFrameOverride(const Profile& profile,
                                              const QJsonObject& overrideObj, QString& why) {
    if (overrideObj.isEmpty()) {
        return profile.device.frame;
    }
    QJsonObject root = profile.device.toJson();
    QJsonObject frame = root.value(QStringLiteral("frame")).toObject();
    static const QRegularExpression relative(QStringLiteral(R"(^[+-]\d+$)"));
    for (auto it = overrideObj.begin(); it != overrideObj.end(); ++it) {
        QJsonValue v = it.value();
        if (v.isString() && relative.match(v.toString()).hasMatch()) {
            const QJsonValue base = frame.value(it.key());
            if (!base.isDouble()) {
                why = QStringLiteral("frameOverride.%1: a relative value needs a numeric key")
                          .arg(it.key());
                return std::nullopt;
            }
            v = QJsonValue(base.toDouble() + v.toString().toDouble());
        }
        frame.insert(it.key(), v);
    }
    root.insert(QStringLiteral("frame"), frame);
    QString where;
    Expected<McDeviceConfig> cfg = McDeviceConfig::fromJson(root, &where);
    if (!cfg) {
        why = QStringLiteral("frameOverride.%1: %2")
                  .arg(where.startsWith(QStringLiteral("frame.")) ? where.mid(6) : where,
                       QString::fromLatin1(cfg.error().message));
        return std::nullopt;
    }
    const Expected<void> ok = cfg.value().frame.validate();
    if (!ok) {
        why = QStringLiteral("frameOverride: %1").arg(QString::fromLatin1(ok.error().message));
        return std::nullopt;
    }
    return cfg.value().frame;
}

bool applyEdits(const ByteBuf& frame, const FrameConfig& cfg, const QVector<Edit>& edits,
                ByteBuf& out, QString& why) {
    out = frame;
    for (const Edit& e : edits) {
        const auto index = [&](int64_t at, size_t& i) {
            const int64_t n = static_cast<int64_t>(out.size());
            const int64_t pos = at < 0 ? n + at : at;
            if (pos < 0 || pos >= n) {
                why =
                    QStringLiteral("edit position %1 is outside the %2-byte frame").arg(at).arg(n);
                return false;
            }
            i = static_cast<size_t>(pos);
            return true;
        };
        size_t i = 0;
        switch (e.kind) {
        case Edit::Kind::SetByte:
            if (!index(e.at, i)) {
                return false;
            }
            out[i] = static_cast<uint8_t>(e.value);
            break;
        case Edit::Kind::SetNibble:
            if (!index(e.at, i)) {
                return false;
            }
            out[i] = e.high ? static_cast<uint8_t>((out[i] & 0x0F) | (e.value << 4))
                            : static_cast<uint8_t>((out[i] & 0xF0) | e.value);
            break;
        case Edit::Kind::Append:
            out.insert(out.end(), e.bytes.begin(), e.bytes.end());
            break;
        case Edit::Kind::Truncate:
            if (e.count >= static_cast<int64_t>(out.size())) {
                why = QStringLiteral("truncate %1 leaves nothing of the %2-byte frame")
                          .arg(e.count)
                          .arg(out.size());
                return false;
            }
            out.resize(out.size() - static_cast<size_t>(e.count));
            break;
        case Edit::Kind::ReplaceSum: {
            if (!sumPosition(out, cfg, i, why)) {
                return false;
            }
            const int hi = hexValue(out[i]);
            const int lo = hexValue(out[i + 1]);
            if (hi < 0 || lo < 0) {
                why = QStringLiteral("the SUM position does not hold two hex characters");
                return false;
            }
            const uint32_t current = static_cast<uint32_t>(hi * 16 + lo);
            const uint32_t value = e.relative ? (current + e.value) & 0xFF : e.value;
            static const char digits[] = "0123456789ABCDEF";
            out[i] = static_cast<uint8_t>(digits[(value >> 4) & 0xF]);
            out[i + 1] = static_cast<uint8_t>(digits[value & 0xF]);
            break;
        }
        }
    }
    return true;
}

ResolveResult resolvePlan(const Plan& plan, const Profile& profile, const QStringList& onlyGroups) {
    ResolveResult result;
    result.planId = plan.id;
    QSet<QString> seen;
    for (const Step& step : plan.steps) {
        if (!onlyGroups.isEmpty()) {
            bool selected = false;
            for (const QString& g : onlyGroups) {
                selected = selected || step.group.compare(g, Qt::CaseInsensitive) == 0;
            }
            if (!selected) {
                continue;
            }
        }
        QString why;
        QVector<Step> expanded;
        if (!expandStep(step, profile, expanded, why)) {
            result.errors.push_back(StepError{step.id, why});
            continue;
        }
        for (const Step& s : expanded) {
            if (seen.contains(s.id)) {
                result.errors.push_back(StepError{s.id, QStringLiteral("duplicate step id")});
                continue;
            }
            seen.insert(s.id);
            ResolvedStep rs;
            Ctx ctx{profile, plan, s.id, profile.device.frame};
            if (!resolveStep(s, ctx, profile, rs, why)) {
                result.errors.push_back(StepError{s.id, why});
                continue;
            }
            result.steps.push_back(std::move(rs));
        }
    }
    return result;
}

} // namespace mc::hil
