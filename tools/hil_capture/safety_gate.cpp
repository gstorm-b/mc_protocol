#include "hil_capture/safety_gate.h"

#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"

#include <QSet>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace mc::hil {

namespace {

QString rangeText(DeviceType t, uint32_t head, uint64_t points) {
    const uint64_t last = static_cast<uint64_t>(head) + points - 1;
    return QStringLiteral("%1%2-%1%3 (%4 point%5)")
        .arg(deviceSymbol(t), formatDeviceNumber(t, head),
             formatDeviceNumber(t, static_cast<uint32_t>(last)))
        .arg(points)
        .arg(points == 1 ? QString() : QStringLiteral("s"));
}

QString scratchListText(const Profile& p, DeviceType t) {
    QStringList parts;
    for (const ScratchRange& r : p.scratch) {
        if (r.type == t) {
            parts << QStringLiteral("%1%2-%1%3")
                         .arg(deviceSymbol(t), formatDeviceNumber(t, r.first),
                              formatDeviceNumber(t, r.last));
        }
    }
    return parts.join(QStringLiteral(", "));
}

QString explain(const Profile& p, DeviceType t, uint32_t head, uint64_t points) {
    const uint64_t last = static_cast<uint64_t>(head) + points - 1;
    const QString list = scratchListText(p, t);
    if (list.isEmpty()) {
        return QStringLiteral("the profile has no scratch range of %1").arg(deviceSymbol(t));
    }
    for (const ScratchRange& r : p.scratch) {
        if (r.type == t && head >= r.first && head <= r.last && last > r.last) {
            return QStringLiteral("runs past the end of scratch range %1%2-%1%3; a write must lie "
                                  "inside ONE scratch range")
                .arg(deviceSymbol(t), formatDeviceNumber(t, r.first),
                     formatDeviceNumber(t, r.last));
        }
    }
    return QStringLiteral("outside the scratch area of %1 (%2)").arg(deviceSymbol(t), list);
}

uint64_t numbersOf(Op op, Device head, uint16_t count) {
    ResolvedRequest r;
    r.op = op;
    r.head = head;
    r.count = count;
    return r.numbersCovered();
}

void checkWriteRange(GateReport& report, const Profile& p, const QString& id, const QString& kind,
                     Device head, uint64_t points) {
    if (points == 0 || p.inScratch(head.type, head.number, points)) {
        return;
    }
    GateViolation v;
    v.stepId = id;
    v.kind = kind;
    v.range = rangeText(head.type, head.number, points == 0 ? 1 : points);
    v.why = explain(p, head.type, head.number, points);
    report.violations.push_back(v);
}

void checkPoll(GateReport& report, const Profile& p, const QString& id, const ResolvedPoll& poll) {
    if (poll.heartbeat) {
        checkWriteRange(report, p, id, QStringLiteral("poll heartbeat"), *poll.heartbeat, 1);
    }
    const auto checkSub = [&](const ResolvedSub& s) {
        if (s.input) {
            return; // a subscription only reads
        }
        if (!p.inScratch(s.head.type, s.head.number, s.count)) {
            GateViolation v;
            v.stepId = id;
            v.kind = QStringLiteral("poll subscription '%1'").arg(s.name);
            v.range = rangeText(s.head.type, s.head.number, s.count);
            v.why = explain(p, s.head.type, s.head.number, s.count) +
                    QStringLiteral(" (mark it \"input\": true if it is a read-only input)");
            report.violations.push_back(v);
        }
    };
    for (const ResolvedSub& s : poll.subs) {
        checkSub(s);
    }
    for (const ResolvedAction& a : poll.actions) {
        if (a.kind == PollAction::Kind::Write && a.write.request.isWrite()) {
            checkWriteRange(report, p, id, QStringLiteral("poll write"), a.write.request.head,
                            a.write.request.numbersCovered());
        } else if (a.kind == PollAction::Kind::Subscribe) {
            checkSub(a.sub);
        }
    }
}

// A mock configured like the profile and the frame: its own limits per device type.
std::unique_ptr<MockPlc> makeMock(const Profile& p, const FrameConfig& frame) {
    auto plc = std::make_unique<MockPlc>(frame);
    for (size_t i = 0; i < static_cast<size_t>(DeviceType::Count); ++i) {
        const auto t = static_cast<DeviceType>(i);
        if (const std::optional<uint32_t> end = p.end(t)) {
            plc->setDeviceLimit(t, *end + 1);
        }
    }
    return plc;
}

bool isWriteOp(Op op) { return op == Op::WriteBits || op == Op::WriteWords; }

// A plain read of the first scratch device, encoded for @p frame: sent after the frames of an
// operation, it is decoded as exactly this request only when those frames ended on a frame
// boundary (nothing is left half received).
std::optional<ByteBuf> sentinelFrame(const Profile& p, const FrameConfig& frame, Request& request) {
    if (p.scratch.isEmpty()) {
        return std::nullopt;
    }
    const Device head{p.scratch[0].type, p.scratch[0].first};
    request = deviceInfo(head.type).kind == DeviceKind::Bit ? Request::readBits(head, 1)
                                                            : Request::readWords(head, 1);
    const Expected<ByteBuf> bytes = McProtocol(frame).encode(request);
    if (!bytes) {
        return std::nullopt;
    }
    return bytes.value();
}

// Whether a mock that has taken every byte of @p plc's input is left with a half-received frame.
bool endsMidFrame(MockPlc& plc, const Profile& p, const FrameConfig& frame) {
    Request probe;
    const std::optional<ByteBuf> bytes = sentinelFrame(p, frame, probe);
    if (!bytes) {
        return false;
    }
    const size_t before = plc.requests().size();
    plc.bytesIn(ByteView{bytes->data(), bytes->size()});
    const std::vector<MockRequestRecord>& log = plc.requests();
    if (log.size() != before + 1) {
        return true;
    }
    const MockRequestRecord& last = log.back();
    return last.op != probe.op || !(last.head == probe.head) || last.count != probe.count;
}

// ---- Locating the commands in the bytes of a raw or mutate frame ----
//
// One locator per frame family. Each one reports every command it finds, at every place a PLC
// could take a frame to start, as a CommandHit; one accounting loop (FrameGate) then decides what
// every hit is. safety_gate.h documents the lists and the places.

enum class Family : uint8_t { Qna, E1, C1 };

// One command found in the bytes.
struct CommandHit {
    size_t at{0}; // Offset of the frame start (the subheader, the ENQ/STX, the 1E command).
    QString code; // "0401", "01", "BR".
    Family family{Family::Qna};
    bool allowed{false}; // A read-only command of its family.
    QString note;        // Words added to a refusal.
};

// The read-only commands per family (reference spec 4.1, 4.2, 4.3 and 4.5).
const QStringList& readCommands(Family family) {
    static const QStringList qna{QStringLiteral("0401"), QStringLiteral("0403"),
                                 QStringLiteral("0406")};
    static const QStringList e1{QStringLiteral("00"), QStringLiteral("01")};
    static const QStringList c1{QStringLiteral("BR"), QStringLiteral("WR"), QStringLiteral("JR"),
                                QStringLiteral("QR")};
    return family == Family::Qna ? qna : family == Family::E1 ? e1 : c1;
}

QString readCommandsText(Family family) { return readCommands(family).join(QStringLiteral(", ")); }

CommandHit makeHit(size_t at, const QString& code, Family family, const QString& note = QString()) {
    return CommandHit{at, code, family, readCommands(family).contains(code), note};
}

// The 1E command codes the reference spec mentions run from 00H to 3CH (its scope line lists the
// extended range 17H-3CH). A first byte above that is no 1E command at all: such a frame is
// malformed, not a command, and meets the recovery rule like any other fragment (the plan step "1E
// command code 7FH") as long as it is no longer than one read request.
constexpr unsigned kLastE1Command = 0x3C;

bool isHexDigit(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

bool isLetter(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// n ASCII hex digits at pos as a number; false when they are not all there.
bool hexAt(const ByteBuf& b, size_t pos, size_t n, unsigned& value) {
    if (pos + n > b.size()) {
        return false;
    }
    value = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = b[pos + i];
        if (!isHexDigit(c)) {
            return false;
        }
        value = value * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return true;
}

QString asciiAt(const ByteBuf& b, size_t pos, size_t n) {
    QString text;
    for (size_t i = 0; i < n; ++i) {
        text += QChar::fromLatin1(static_cast<char>(b[pos + i])).toUpper();
    }
    return text;
}

// The characters as they are: a 1C command in lower case is not one of the listed letters, so it
// is refused as a command that is not accounted for.
QString exactAt(const ByteBuf& b, size_t pos, size_t n) {
    QString text;
    for (size_t i = 0; i < n; ++i) {
        text += QChar::fromLatin1(static_cast<char>(b[pos + i]));
    }
    return text;
}

bool matches(const ByteBuf& b, size_t pos, const char* text) {
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (pos + i >= b.size() || b[pos + i] != static_cast<uint8_t>(text[i])) {
            return false;
        }
    }
    return true;
}

// 3E and 4E, Binary or ASCII (reference spec 5.1 and 5.2). A header is recognised at the start of
// the bytes whenever its command is there, truncated or not, and at any later offset only when the
// whole frame lies inside the bytes (a frame hidden behind junk or behind a swallowing length
// field).
void locateQna(const ByteBuf& b, bool ascii, std::vector<CommandHit>& hits) {
    for (size_t at = 0; at < b.size(); ++at) {
        const bool first = at == 0;
        size_t lengthAt = 0;
        size_t headerSize = 0;
        size_t commandAt = 0;
        size_t commandSize = 0;
        if (!ascii && b.size() > at + 1 && b[at] == 0x50 && b[at + 1] == 0x00) {
            lengthAt = at + 7;
            headerSize = 9;
            commandAt = at + 11;
            commandSize = 2;
        } else if (!ascii && b.size() > at + 5 && b[at] == 0x54 && b[at + 1] == 0x00 &&
                   b[at + 4] == 0x00 && b[at + 5] == 0x00) {
            lengthAt = at + 11;
            headerSize = 13;
            commandAt = at + 15;
            commandSize = 2;
        } else if (ascii && matches(b, at, "5000")) {
            lengthAt = at + 14;
            headerSize = 18;
            commandAt = at + 22;
            commandSize = 4;
        } else if (ascii && matches(b, at, "5400") && matches(b, at + 8, "0000")) {
            lengthAt = at + 22;
            headerSize = 26;
            commandAt = at + 30;
            commandSize = 4;
        } else {
            continue;
        }
        if (commandAt + commandSize > b.size()) {
            continue; // the command is not there
        }
        QString code;
        if (ascii) {
            unsigned ignored = 0;
            if (!hexAt(b, commandAt, 4, ignored)) {
                continue;
            }
            code = asciiAt(b, commandAt, 4);
        } else {
            code = QStringLiteral("%1%2")
                       .arg(uint(b[commandAt + 1]), 2, 16, QLatin1Char('0'))
                       .arg(uint(b[commandAt]), 2, 16, QLatin1Char('0'))
                       .toUpper();
        }
        if (!first) {
            unsigned length = b[lengthAt] | (b[lengthAt + 1] << 8);
            if (ascii && !hexAt(b, lengthAt, 4, length)) {
                continue;
            }
            if (length < 4 || at + headerSize + length > b.size()) {
                continue;
            }
        }
        hits.push_back(makeHit(at, code, Family::Qna));
    }
}

// 1E (reference spec 5.3): no length field, no start mark. The command is the first byte (two
// characters in ASCII). After a read, whose request has a fixed size, the next frame can be found.
// A first byte that is no command (above 3CH, or not hexadecimal in ASCII) shows nothing to check,
// so what follows could be anything: that is accepted only when it is no longer than one read
// request, too short to be a write, and refused as a command of its own otherwise.
void locateE1(const ByteBuf& b, bool ascii, std::vector<CommandHit>& hits) {
    const size_t readSize = ascii ? 24 : 12;
    size_t at = 0;
    while (at < b.size()) {
        unsigned command = b[at];
        const bool isCommand = ascii ? hexAt(b, at, 2, command) : true;
        if (!isCommand || command > kLastE1Command) {
            if (b.size() - at > readSize) {
                const QString code =
                    ascii ? asciiAt(b, at, 2)
                          : QStringLiteral("%1").arg(command, 2, 16, QLatin1Char('0')).toUpper();
                hits.push_back(makeHit(at, code, Family::E1,
                                       QStringLiteral(" (no 1E command starts with it, and the "
                                                      "frame is longer than one read request of "
                                                      "%1 bytes)")
                                           .arg(readSize)));
            }
            return;
        }
        const QString code = QStringLiteral("%1").arg(command, 2, 16, QLatin1Char('0')).toUpper();
        hits.push_back(makeHit(at, code, Family::E1));
        if (!hits.back().allowed) {
            return;
        }
        at += readSize;
    }
}

// 3C, 4C and 1C, ASCII formats 1-4 (reference spec 5.4-5.6), with any frame id. Every ENQ (formats
// 1, 2, 4) or STX (format 3) starts a frame, so an EOT before it or junk in front of it hides
// nothing. A frame id "F9" (3C, 8 character route) or "F8" (4C, 14 character route) is followed by
// the 4 character command; without an id the frame is 1C: station and PC No. (4 characters), then
// the 2 letter command. Format 2 adds a 2 character block number after the start byte; a frame id
// is looked for with and without it, so a frame of another format hides nothing either. Sum check
// and CR LF come after the command and move nothing. The binary format 5 is not a format of
// this port (the profile loader rejects it) and is not looked for.
void locateSerial(const ByteBuf& b, const FrameConfig& f, std::vector<CommandHit>& hits) {
    const size_t block = f.format == SerialFormat::Format2 ? 2 : 0;
    // A frame id at q: reports the QnA command behind it. True when the id is there.
    const auto withFrameId = [&](size_t start, size_t q) {
        size_t route = 0;
        if (matches(b, q, "F9")) {
            route = 8;
        } else if (matches(b, q, "F8")) {
            route = 14;
        } else {
            return false;
        }
        unsigned ignored = 0;
        const size_t command = q + 2 + route;
        if (hexAt(b, command, 4, ignored)) {
            hits.push_back(makeHit(start, asciiAt(b, command, 4), Family::Qna));
        }
        return true;
    };
    for (size_t at = 0; at < b.size(); ++at) {
        if (b[at] != 0x05 && b[at] != 0x02) {
            continue;
        }
        const bool idHere = withFrameId(at, at + 1 + block);
        withFrameId(at, at + 1 + (block == 0 ? 2 : 0));
        if (idHere) {
            continue;
        }
        const size_t command = at + 1 + block + 4;
        if (command + 2 <= b.size() && isLetter(b[command]) && isLetter(b[command + 1])) {
            hits.push_back(makeHit(at, exactAt(b, command, 2), Family::C1));
        }
    }
}

// Every command the bytes of one operation ask the PLC for, in the order of their offsets. The
// families a port could take are all looked for: an Ethernet port takes 3E and 4E frames in both
// data codes (and 1E ones when the profile is 1E), a serial port 3C, 4C and 1C frames.
std::vector<CommandHit> commandsIn(const ByteBuf& b, const FrameConfig& f) {
    std::vector<CommandHit> hits;
    switch (f.frame) {
    case FrameType::F3E:
    case FrameType::F4E:
        locateQna(b, false, hits);
        locateQna(b, true, hits);
        break;
    case FrameType::F1E:
        locateQna(b, false, hits);
        locateQna(b, true, hits);
        locateE1(b, f.code == DataCode::Ascii, hits);
        break;
    case FrameType::F3C:
    case FrameType::F4C:
    case FrameType::F1C:
        locateSerial(b, f, hits);
        break;
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const CommandHit& x, const CommandHit& y) { return x.at < y.at; });
    return hits;
}

// ---- The accounting of one mutate or raw operation ----------------------------------------------
//
// The principle is default deny: nothing goes out unless the gate can say what it does. Every
// command the locators find must be a read-only command, or a write that the mock decodes at the
// same offset and that lies inside scratch. Whatever cannot be accounted for refuses the run.
class FrameGate {
  public:
    FrameGate(GateReport& report, const Profile& p, const ResolvedOp& op)
        : m_report(report), m_profile(p), m_op(op),
          m_kind(op.via == Via::Mutate ? QStringLiteral("mutate") : QStringLiteral("raw")) {
        for (const ByteBuf& frame : op.frames) {
            m_hex += (m_hex.isEmpty() ? QString() : QStringLiteral(" | ")) + hexText(frame);
            m_bytes.insert(m_bytes.end(), frame.begin(), frame.end());
        }
        m_hits = commandsIn(m_bytes, op.frame);
        // What the mock understands of the bytes as one stream. A request it frames but does not
        // execute (another command, an unknown device code) and bytes that cannot start a frame
        // are logged with a count of 0 and the default operation, which looks like a harmless
        // read: they must never be taken for one.
        std::unique_ptr<MockPlc> plc = makeMock(p, op.frame);
        plc->bytesIn(ByteView{m_bytes.data(), m_bytes.size()});
        m_requests = plc->requests();
        m_understood = plc->eotCount() > 0;
        for (const MockRequestRecord& r : m_requests) {
            if (r.count == 0) {
                m_partial = true;
            } else {
                m_understood = true;
            }
        }
        m_partial = m_partial || endsMidFrame(*plc, p, op.frame);
    }

    void run() {
        checkClaim();
        accountForCommands();
        checkDecodedWrites();
        checkCompleteness();
    }

  private:
    void refuse(const QString& what, const QString& range, const QString& why) {
        GateViolation v;
        v.stepId = m_op.recordId;
        v.kind = what;
        v.range = range;
        v.why = why;
        m_report.violations.push_back(v);
    }

    QString declared() const { return m_kind + QStringLiteral(" (declared readOnly)"); }

    // A mutate whose base request is a write is never read-only, whatever the plan says.
    void checkClaim() {
        if (m_op.via == Via::Mutate && m_op.readOnly && m_op.request.isWrite()) {
            refuse(declared(),
                   rangeText(m_op.request.head.type, m_op.request.head.number,
                             m_op.request.numbersCovered()),
                   QStringLiteral("the base request is a write: a mutate of a write can never be "
                                  "declared readOnly"));
        }
    }

    // The write a mock fed only the bytes of [from, to) decodes (its first request), if any.
    std::optional<MockRequestRecord> decodedWrite(size_t from, size_t to) const {
        std::unique_ptr<MockPlc> inner = makeMock(m_profile, m_op.frame);
        inner->bytesIn(ByteView{m_bytes.data() + from, to - from});
        const std::vector<MockRequestRecord>& log = inner->requests();
        if (log.empty() || log.front().count == 0 || !isWriteOp(log.front().op)) {
            return std::nullopt;
        }
        return log.front();
    }

    // The accounting loop: every located command is a read, or a write the mock decodes there.
    void accountForCommands() {
        for (size_t i = 0; i < m_hits.size(); ++i) {
            const CommandHit& hit = m_hits[i];
            if (hit.allowed) {
                continue;
            }
            const QString where = QStringLiteral("command %1 at byte %2").arg(hit.code).arg(hit.at);
            if (m_op.readOnly) {
                // The plan's word is not enough: a readOnly frame carries reads only, also a
                // write that the mock decodes and that lies inside scratch.
                refuse(declared(), where,
                       QStringLiteral("command %1 is not a read-only command of this frame family "
                                      "(read-only: %2); a readOnly frame may carry nothing else%3")
                           .arg(hit.code, readCommandsText(hit.family), hit.note));
                continue;
            }
            // Not declared: the command must at least be one the mock decodes as a write at this
            // offset (its frame runs up to the next frame start), so that the scratch check
            // below sees what it writes.
            size_t end = m_bytes.size();
            for (size_t j = i + 1; j < m_hits.size(); ++j) {
                if (m_hits[j].at > hit.at) {
                    end = m_hits[j].at;
                    break;
                }
            }
            const std::optional<MockRequestRecord> write = decodedWrite(hit.at, end);
            if (!write) {
                refuse(m_kind + QStringLiteral(" (command not accounted for)"), where,
                       QStringLiteral("command %1 is no read-only command (read-only: %2) and the "
                                      "mock does not decode a write at this offset, so what it "
                                      "does is unknown%3")
                           .arg(hit.code, readCommandsText(hit.family), hit.note));
                continue;
            }
            checkWrite(*write,
                       hit.at == 0
                           ? m_kind + QStringLiteral(" (decoded as a write)")
                           : m_kind + QStringLiteral(" (write embedded at byte %1)").arg(hit.at));
        }
    }

    // Whatever the mock decodes as a write (words or bits) lies inside one scratch range, also a
    // write that starts in the middle of the bytes (a length field that swallows a second frame).
    void checkDecodedWrites() {
        for (const MockRequestRecord& r : m_requests) {
            checkWrite(r, m_kind + QStringLiteral(" (decoded as a write)"));
        }
        // Every byte after the first is also tried as a frame start. A mock skips junk until a
        // frame starts, so the same write is found from every earlier offset too; going from the
        // end, the first hit names the offset it really starts at.
        for (size_t at = m_bytes.size(); at-- > 1;) {
            std::unique_ptr<MockPlc> inner = makeMock(m_profile, m_op.frame);
            inner->bytesIn(ByteView{m_bytes.data() + at, m_bytes.size() - at});
            for (const MockRequestRecord& r : inner->requests()) {
                checkWrite(r, m_kind + QStringLiteral(" (write embedded at byte %1)").arg(at));
            }
        }
    }

    void checkWrite(const MockRequestRecord& r, const QString& what) {
        if (r.count == 0 || !isWriteOp(r.op)) {
            return;
        }
        const uint64_t points = numbersOf(r.op, r.head, r.count);
        if (m_profile.inScratch(r.head.type, r.head.number, points)) {
            return;
        }
        const QString range = rangeText(r.head.type, r.head.number, points);
        if (m_reported.contains(range)) {
            return;
        }
        m_reported.insert(range);
        refuse(what, range, explain(m_profile, r.head.type, r.head.number, points));
    }

    // A frame the mock cannot fully decode (not understood, framed but not executed, or ending
    // half received) needs `readOnly: true`, and then a recovery that throws the half frame away
    // before anything else is sent: `reconnect` on Ethernet, `eot` on a serial line.
    void checkCompleteness() {
        if (m_understood && !m_partial) {
            return;
        }
        if (!m_op.readOnly) {
            refuse(m_kind, QStringLiteral("frame of %1 byte(s)").arg(m_bytes.size()),
                   QStringLiteral("the mock %1 this frame, so what it writes is unknown; declare "
                                  "\"readOnly\": true in the plan if it cannot write")
                       .arg(m_understood ? QStringLiteral("cannot decode part of")
                                         : QStringLiteral("cannot decode")));
            return;
        }
        const Recover needed = m_op.frame.isSerial() ? Recover::Eot : Recover::Reconnect;
        if (m_op.recover != needed) {
            refuse(declared(), QStringLiteral("frame of %1 byte(s)").arg(m_bytes.size()),
                   QStringLiteral("a readOnly frame the mock cannot fully decode must carry "
                                  "\"recover\": \"%1\", so that no later byte can complete it or "
                                  "be swallowed by it")
                       .arg(needed == Recover::Eot ? QStringLiteral("eot")
                                                   : QStringLiteral("reconnect")));
            return;
        }
        m_report.readOnlyFrames.push_back(UndecodedFrame{m_op.recordId, m_op.description, m_hex});
    }

    GateReport& m_report;
    const Profile& m_profile;
    const ResolvedOp& m_op;
    QString m_kind;                            // "mutate" or "raw"
    QString m_hex;                             // the frames as text, for the prompt
    ByteBuf m_bytes;                           // every byte of the operation, in order
    std::vector<CommandHit> m_hits;            // the located commands
    std::vector<MockRequestRecord> m_requests; // what the mock decoded from the stream
    bool m_understood{false};
    bool m_partial{false};
    QSet<QString> m_reported; // write ranges already refused
};

// ---- Overrides ----
//
// A step's frameOverride changes the frame the step is encoded with. The gate reads the bytes of
// a raw or mutate step under that frame, while the PLC reads them as they are: so such a step may
// not change anything the reading depends on. A write must reach the PLC whose scratch the profile
// declares: it may not change the routing either.

// The fields of @p b that differ from @p a: the routing always, frame, code, format and sum check
// when @p withStructure is set.
QStringList changedFields(const FrameConfig& a, const FrameConfig& b, bool withStructure) {
    QStringList names;
    const auto note = [&](bool differs, const char* name) {
        if (differs) {
            names << QLatin1String(name);
        }
    };
    if (withStructure) {
        note(a.frame != b.frame, "frame");
        note(a.code != b.code, "code");
        note(a.format != b.format, "format");
        note(a.sumCheck != b.sumCheck, "sumCheck");
    }
    note(a.network != b.network, "network");
    note(a.pc != b.pc, "pc");
    note(a.io != b.io, "io");
    note(a.station != b.station, "station");
    note(a.stationNo != b.stationNo, "stationNo");
    note(a.selfStation != b.selfStation, "selfStation");
    return names;
}

// A poll also runs the profile's own session heartbeat, a write, when it is enabled.
bool pollWrites(const ResolvedPoll& poll, const Profile& p) {
    if (poll.heartbeat || p.device.session.heartbeat.enabled) {
        return true;
    }
    for (const ResolvedAction& a : poll.actions) {
        if (a.kind == PollAction::Kind::Write && a.write.request.isWrite()) {
            return true;
        }
    }
    return false;
}

// Whether the step sends a write through the library (frames of a raw or mutate step are the
// frame gate's).
bool stepWrites(const ResolvedStep& step, const Profile& p) {
    switch (step.kind) {
    case StepKind::Write:
    case StepKind::Read:
    case StepKind::Mutate:
    case StepKind::Raw:
        for (const ResolvedOp& op : step.ops) {
            if (op.via == Via::Api && op.request.isWrite()) {
                return true;
            }
        }
        return false;
    case StepKind::Poll:
        return pollWrites(step.poll, p);
    case StepKind::Bench:
        return step.bench.isPollSet ? pollWrites(step.bench.poll, p)
                                    : step.bench.request.request.isWrite();
    }
    return false;
}

void checkOverride(GateReport& report, const Profile& p, const ResolvedStep& step) {
    const bool frames = step.kind == StepKind::Mutate || step.kind == StepKind::Raw;
    if (!frames && !stepWrites(step, p)) {
        return;
    }
    const QStringList changed = changedFields(p.device.frame, step.frame, frames);
    if (changed.isEmpty()) {
        return;
    }
    GateViolation v;
    v.stepId = step.id;
    v.kind = QStringLiteral("frameOverride");
    v.range = changed.join(QStringLiteral(", "));
    v.why = frames ? QStringLiteral("a raw or mutate step may not change the frame, code, format, "
                                    "sum check or routing of the profile: the gate reads its bytes "
                                    "under the profile's frame")
                   : QStringLiteral("a write may not change the routing of the profile: it must "
                                    "reach the station whose scratch the profile declares");
    report.violations.push_back(v);
}

} // namespace

QString GateViolation::text() const {
    return QStringLiteral("  %1  %2  %3  %4").arg(stepId, kind, range, why);
}

GateReport checkGate(const ResolveResult& resolved, const Profile& profile) {
    GateReport report;
    if (profile.device.session.heartbeat.enabled) {
        const Device hb = profile.device.session.heartbeat.device;
        checkWriteRange(report, profile, QStringLiteral("(profile)"),
                        QStringLiteral("session heartbeat"), hb, 1);
    }
    for (const ResolvedStep& step : resolved.steps) {
        if (step.skipped()) {
            continue; // sends nothing
        }
        checkOverride(report, profile, step);
        switch (step.kind) {
        case StepKind::Write:
        case StepKind::Read:
        case StepKind::Mutate:
        case StepKind::Raw:
            for (const ResolvedOp& op : step.ops) {
                if (op.via == Via::Api) {
                    if (op.request.isWrite()) {
                        checkWriteRange(report, profile, op.recordId, QStringLiteral("write"),
                                        op.request.head, op.request.numbersCovered());
                    }
                } else {
                    FrameGate(report, profile, op).run();
                }
            }
            break;
        case StepKind::Poll:
            checkPoll(report, profile, step.id, step.poll);
            break;
        case StepKind::Bench:
            if (step.bench.isPollSet) {
                checkPoll(report, profile, step.id, step.bench.poll);
            } else if (step.bench.request.request.isWrite()) {
                checkWriteRange(report, profile, step.id, QStringLiteral("bench write"),
                                step.bench.request.request.head,
                                step.bench.request.request.numbersCovered());
            }
            break;
        }
    }
    return report;
}

QString refusalText(const GateReport& report) {
    QString text = QStringLiteral("SAFETY GATE: run REFUSED, %1 violation(s); nothing was sent.\n"
                                  "  step  kind  resolved range  why\n")
                       .arg(report.violations.size());
    for (const GateViolation& v : report.violations) {
        text += v.text() + QLatin1Char('\n');
    }
    return text;
}

} // namespace mc::hil
