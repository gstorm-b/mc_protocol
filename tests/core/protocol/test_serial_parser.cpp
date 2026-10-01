// The serial receive state machine (spec section 6.3) on its own, below McProtocol: STR-03 and
// STR-04 for serial, plus one synthetic frame for every format (1-4), every response kind (data,
// ack, nak; Format 3 short forms with and without a SUM), for 3C and 1C, built by
// serial_synthetic.h from the format table of spec 5.4-5.6 and fed whole and one byte at a time.
// The hand-written literals at the end are the spec's own frames with offsets worked out by hand,
// so the builder and the parser cannot share a misreading of the table.
#include "doctest/doctest.h"

#include "serial_synthetic.h"

#include "core/protocol/serial_parser.h"

#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

using mc::ByteView;
using mc::ErrorCategory;
using mc::ErrorCode;
using mc::FrameType;
using mc::ParseStatus;
using mc::SerialFormat;
using mc::detail::SerialCursor;
using mc::detail::serialFeed;
using mc::detail::SerialFrame;
using mc::detail::SerialParams;
using mc::detail::SerialResponseKind;
using mc::test::buildSyntheticFrame;
using mc::test::SyntheticFrame;
using mc::test::syntheticSerialFrames;
using mc::test::SyntheticSpec;

namespace {

SerialParams paramsOf(const SyntheticSpec& s) {
    return SerialParams{s.frame, s.format, s.sumCheck, s.f3ShortSum};
}

struct Outcome {
    ParseStatus status{ParseStatus::NeedMore};
    SerialFrame frame{};
    mc::Error error{};
    SerialCursor cursor{};
};

Outcome feed(const SerialParams& p, const std::vector<uint8_t>& bytes, size_t n) {
    Outcome o;
    o.status = serialFeed(p, ByteView{bytes.data(), n}, o.cursor, o.frame, o.error);
    return o;
}

bool sameFrame(const SerialFrame& a, const SerialFrame& b) {
    return a.kind == b.kind && a.blockOffset == b.blockOffset && a.routeOffset == b.routeOffset &&
           a.dataOffset == b.dataOffset && a.dataSize == b.dataSize && a.end == b.end;
}

void checkLayout(const SyntheticFrame& f, const SerialFrame& got, size_t junk) {
    CHECK(got.kind == f.spec.kind);
    CHECK(got.routeOffset == f.routeOffset + junk);
    CHECK(got.dataOffset == f.dataOffset + junk);
    CHECK(got.dataSize == f.dataSize);
    CHECK(got.end == f.bytes.size() + junk);
    if (f.spec.format == SerialFormat::Format2) {
        CHECK(got.blockOffset == f.blockOffset + junk);
    }
}

std::vector<uint8_t> bytesOf(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // namespace

TEST_CASE("SER-01: every synthetic frame parses whole, with the layout the builder laid out") {
    std::vector<SyntheticFrame> frames = syntheticSerialFrames();
    REQUIRE(frames.size() > 60);
    for (const SyntheticFrame& f : frames) {
        INFO("frame ", f.name);
        Outcome o = feed(paramsOf(f.spec), f.bytes, f.bytes.size());
        REQUIRE(o.status == ParseStatus::Done);
        CHECK(o.cursor.skipped == 0);
        checkLayout(f, o.frame, 0);
    }
}

TEST_CASE("SER-02: fed one byte at a time, every frame waits until its last byte, then equals the "
          "whole-buffer result") {
    for (const SyntheticFrame& f : syntheticSerialFrames()) {
        INFO("frame ", f.name);
        const SerialParams p = paramsOf(f.spec);
        Outcome whole = feed(p, f.bytes, f.bytes.size());
        REQUIRE(whole.status == ParseStatus::Done);

        Outcome o;
        size_t previousScanned = 0;
        for (size_t n = 1; n < f.bytes.size(); ++n) {
            INFO("fed ", n, " of ", f.bytes.size(), " bytes");
            o.status = serialFeed(p, ByteView{f.bytes.data(), n}, o.cursor, o.frame, o.error);
            REQUIRE(o.status == ParseStatus::NeedMore);
            // Never back: the next call starts where this one stopped, so no byte is scanned twice.
            CHECK(o.cursor.scanned >= previousScanned);
            CHECK(o.cursor.scanned <= n);
            previousScanned = o.cursor.scanned;
        }
        o.status =
            serialFeed(p, ByteView{f.bytes.data(), f.bytes.size()}, o.cursor, o.frame, o.error);
        REQUIRE(o.status == ParseStatus::Done);
        CHECK(sameFrame(o.frame, whole.frame));
        CHECK(o.cursor.skipped == 0);
    }
}

TEST_CASE("SER-03: the body scan examines only new bytes (a byte changed behind the cursor is "
          "not seen again)") {
    // Format 1, 3C, sum on: STX P data ETX SUM. Feed everything up to, not including, the ETX;
    // then break the rule that fed bytes never change by turning one body byte into ETX, and feed
    // the rest. A parser that rescans from the start would end the frame at the planted ETX.
    SyntheticSpec spec;
    spec.kind = SerialResponseKind::Data;
    spec.data = "199512021130";
    spec.err = "7151";
    SyntheticFrame f = buildSyntheticFrame(spec);
    const SerialParams p = paramsOf(spec);
    const size_t etxAt = f.bytes.size() - 3; // ... ETX S S
    REQUIRE(f.bytes[etxAt] == 0x03);

    std::vector<uint8_t> buffer = f.bytes;
    SerialCursor cursor;
    SerialFrame frame;
    mc::Error error;
    REQUIRE(serialFeed(p, ByteView{buffer.data(), etxAt}, cursor, frame, error) ==
            ParseStatus::NeedMore);

    const size_t planted = 15;
    buffer[planted] = 0x03;
    ParseStatus status =
        serialFeed(p, ByteView{buffer.data(), buffer.size()}, cursor, frame, error);
    // Found at the real ETX, so the frame is the whole buffer; the planted byte only breaks the
    // SUM.
    CHECK(status == ParseStatus::Failed);
    CHECK(error.code == ErrorCode::SumCheck);
    CHECK(frame.end == f.bytes.size());

    // Control: the same buffer from a fresh cursor does stop at the planted ETX, so the probe
    // above can tell a rescan from a resume.
    SerialCursor fresh;
    SerialFrame freshFrame;
    mc::Error freshError;
    (void)serialFeed(p, ByteView{buffer.data(), buffer.size()}, fresh, freshFrame, freshError);
    CHECK(freshFrame.end == planted + 1 + 2);
}

TEST_CASE("SER-04: while the SUM or CR LF is still missing the parser rests on ETX and does not "
          "re-read the body") {
    SyntheticSpec spec;
    spec.format = SerialFormat::Format4;
    spec.kind = SerialResponseKind::Data;
    spec.data = "199512021130";
    SyntheticFrame f = buildSyntheticFrame(spec);
    const SerialParams p = paramsOf(spec);
    const size_t etxAt = 1 + 10 + 12; // STX + P + data
    REQUIRE(f.bytes[etxAt] == 0x03);

    Outcome o;
    for (size_t n = etxAt + 1; n < f.bytes.size(); ++n) { // ETX seen, SUM and CR LF incomplete
        o.status = serialFeed(p, ByteView{f.bytes.data(), n}, o.cursor, o.frame, o.error);
        CHECK(o.status == ParseStatus::NeedMore);
        CHECK(o.cursor.scanned == etxAt);
    }
}

TEST_CASE("STR-03 (serial): junk before the start byte is skipped and reported, whole and byte "
          "at a time") {
    const std::vector<std::vector<uint8_t>> junks = {
        {0x00},
        {0x00, 0xFF},
        {0x41, 0x0D, 0x0A, 0x05, 0xFF, 0x00, 0x7F}, // includes ENQ, CR, LF: never a start byte
    };
    for (const SyntheticFrame& f : syntheticSerialFrames()) {
        for (const std::vector<uint8_t>& junk : junks) {
            INFO("frame ", f.name, ", junk bytes ", junk.size());
            std::vector<uint8_t> wire = junk;
            // Format 3 starts on STX only, so ACK and NAK bytes are junk there; other formats
            // would take them as a start.
            if (f.spec.format == SerialFormat::Format3) {
                wire.push_back(0x06);
                wire.push_back(0x15);
            }
            const size_t junkSize = wire.size();
            wire.insert(wire.end(), f.bytes.begin(), f.bytes.end());
            const SerialParams p = paramsOf(f.spec);

            Outcome whole = feed(p, wire, wire.size());
            REQUIRE(whole.status == ParseStatus::Done);
            CHECK(whole.cursor.skipped == junkSize);
            checkLayout(f, whole.frame, junkSize);

            Outcome o;
            for (size_t n = 1; n < wire.size(); ++n) {
                o.status = serialFeed(p, ByteView{wire.data(), n}, o.cursor, o.frame, o.error);
                REQUIRE(o.status == ParseStatus::NeedMore);
            }
            o.status =
                serialFeed(p, ByteView{wire.data(), wire.size()}, o.cursor, o.frame, o.error);
            REQUIRE(o.status == ParseStatus::Done);
            CHECK(o.cursor.skipped == junkSize);
            CHECK(sameFrame(o.frame, whole.frame));
        }
    }
}

TEST_CASE("STR-03 (serial): only junk so far is reported as skipped, and a format that starts on "
          "ACK or NAK takes them as a start") {
    SyntheticSpec f1;
    f1.kind = SerialResponseKind::Ack;
    SerialParams p1 = paramsOf(f1);
    const std::vector<uint8_t> junk = {0x00, 0xFF, 0x41};
    Outcome o = feed(p1, junk, junk.size());
    CHECK(o.status == ParseStatus::NeedMore);
    CHECK(o.cursor.skipped == 3);
    CHECK(o.cursor.scanned == 3);

    // Format 1: ACK is a start byte, so `00 06` skips one byte and then waits for P.
    const std::vector<uint8_t> ackStart = {0x00, 0x06};
    Outcome a = feed(p1, ackStart, ackStart.size());
    CHECK(a.status == ParseStatus::NeedMore);
    CHECK(a.cursor.skipped == 1);

    // Format 3: the same two bytes are all junk.
    SyntheticSpec f3;
    f3.format = SerialFormat::Format3;
    Outcome b = feed(paramsOf(f3), ackStart, ackStart.size());
    CHECK(b.status == ParseStatus::NeedMore);
    CHECK(b.cursor.skipped == 2);
}

TEST_CASE("STR-04 (serial): a frame cut short, then a fresh cursor: the parser recovers") {
    for (const SyntheticFrame& f : syntheticSerialFrames()) {
        INFO("frame ", f.name);
        const SerialParams p = paramsOf(f.spec);
        Outcome o;
        size_t half = f.bytes.size() / 2;
        REQUIRE(half > 0);
        REQUIRE(serialFeed(p, ByteView{f.bytes.data(), half}, o.cursor, o.frame, o.error) ==
                ParseStatus::NeedMore);

        // Reset: a value-initialized cursor, as McProtocol's Parser::reset() leaves it.
        o.cursor = SerialCursor{};
        o.status =
            serialFeed(p, ByteView{f.bytes.data(), f.bytes.size()}, o.cursor, o.frame, o.error);
        REQUIRE(o.status == ParseStatus::Done);
        checkLayout(f, o.frame, 0);
    }
}

TEST_CASE("SER-05: two frames in one buffer: the first ends where the second begins") {
    const std::vector<SyntheticFrame> frames = syntheticSerialFrames();
    for (size_t i = 0; i + 1 < frames.size(); ++i) {
        const SyntheticFrame& a = frames[i];
        // Pair each frame with a following frame of the same parameters.
        const SyntheticFrame* b = nullptr;
        for (size_t k = i + 1; k < frames.size() && b == nullptr; ++k) {
            const SyntheticSpec& sa = a.spec;
            const SyntheticSpec& sb = frames[k].spec;
            if (sa.frame == sb.frame && sa.format == sb.format && sa.sumCheck == sb.sumCheck &&
                sa.f3ShortSum == sb.f3ShortSum && sa.block == sb.block &&
                (sa.kind != sb.kind || sa.data != sb.data)) {
                b = &frames[k];
            }
        }
        if (b == nullptr) {
            continue;
        }
        INFO("frames ", a.name, " + ", b->name);
        std::vector<uint8_t> wire = a.bytes;
        wire.insert(wire.end(), b->bytes.begin(), b->bytes.end());
        const SerialParams p = paramsOf(a.spec);

        Outcome first = feed(p, wire, wire.size());
        REQUIRE(first.status == ParseStatus::Done);
        CHECK(first.frame.end == a.bytes.size());

        std::vector<uint8_t> rest(wire.begin() + static_cast<std::ptrdiff_t>(first.frame.end),
                                  wire.end());
        Outcome second = feed(p, rest, rest.size());
        REQUIRE(second.status == ParseStatus::Done);
        checkLayout(*b, second.frame, 0);
    }
}

TEST_CASE("SER-06: a wrong SUM fails with SumCheck; the failure consumes the whole frame") {
    for (const SyntheticFrame& f : syntheticSerialFrames()) {
        const bool hasSum = f.spec.sumCheck && f.bytes.size() > 2 &&
                            (f.spec.kind == SerialResponseKind::Data ||
                             (f.spec.format == SerialFormat::Format3 && f.spec.f3ShortSum));
        if (!hasSum) {
            continue;
        }
        INFO("frame ", f.name);
        const SerialParams p = paramsOf(f.spec);
        const size_t tail = f.spec.format == SerialFormat::Format4 ? 2 : 0;
        const size_t lastSum = f.bytes.size() - tail - 1;

        std::vector<uint8_t> wire = f.bytes;
        wire[lastSum] = wire[lastSum] == '0' ? '1' : '0';
        Outcome o = feed(p, wire, wire.size());
        REQUIRE(o.status == ParseStatus::Failed);
        CHECK(o.error.category == ErrorCategory::Protocol);
        CHECK(o.error.code == ErrorCode::SumCheck);
        CHECK(o.frame.end == wire.size());

        // The same damage seen one byte at a time reports the same thing, and only at the end.
        Outcome s;
        for (size_t n = 1; n < wire.size(); ++n) {
            REQUIRE(serialFeed(p, ByteView{wire.data(), n}, s.cursor, s.frame, s.error) ==
                    ParseStatus::NeedMore);
        }
        REQUIRE(serialFeed(p, ByteView{wire.data(), wire.size()}, s.cursor, s.frame, s.error) ==
                ParseStatus::Failed);
        CHECK(s.error.code == ErrorCode::SumCheck);

        // A SUM character that is not hex is InvalidCharacter, not SumCheck.
        std::vector<uint8_t> bad = f.bytes;
        bad[lastSum] = 'G';
        Outcome g = feed(p, bad, bad.size());
        REQUIRE(g.status == ParseStatus::Failed);
        CHECK(g.error.code == ErrorCode::InvalidCharacter);
        CHECK(g.frame.end == bad.size());
    }
}

TEST_CASE("SER-07: SUM letters are accepted in lower case (PRIM-05)") {
    size_t lowered = 0;
    for (const SyntheticFrame& f : syntheticSerialFrames()) {
        if (!f.spec.sumCheck || f.spec.kind != SerialResponseKind::Data) {
            continue;
        }
        const size_t tail = f.spec.format == SerialFormat::Format4 ? 2 : 0;
        std::vector<uint8_t> wire = f.bytes;
        bool changed = false;
        for (size_t k = wire.size() - tail - 2; k < wire.size() - tail; ++k) {
            if (wire[k] >= 'A' && wire[k] <= 'F') {
                wire[k] = static_cast<uint8_t>(wire[k] - 'A' + 'a');
                changed = true;
            }
        }
        if (!changed) {
            continue;
        }
        ++lowered;
        INFO("frame ", f.name);
        Outcome o = feed(paramsOf(f.spec), wire, wire.size());
        CHECK(o.status == ParseStatus::Done);
    }
    CHECK(lowered > 0); // at least some sums in the set contain a letter
}

TEST_CASE("SER-08: Format 4 frames must end in CR LF; anything else is FrameMismatch") {
    for (const SyntheticFrame& f : syntheticSerialFrames()) {
        if (f.spec.format != SerialFormat::Format4) {
            continue;
        }
        INFO("frame ", f.name);
        const SerialParams p = paramsOf(f.spec);
        std::vector<uint8_t> noLf = f.bytes;
        noLf.back() = 'X';
        Outcome a = feed(p, noLf, noLf.size());
        REQUIRE(a.status == ParseStatus::Failed);
        CHECK(a.error.code == ErrorCode::FrameMismatch);
        CHECK(a.frame.end == noLf.size());

        std::vector<uint8_t> noCr = f.bytes;
        noCr[noCr.size() - 2] = 'X';
        Outcome b = feed(p, noCr, noCr.size());
        REQUIRE(b.status == ParseStatus::Failed);
        CHECK(b.error.code == ErrorCode::FrameMismatch);
    }
}

TEST_CASE("SER-09: Format 3 end code, size and SUM rules") {
    const std::string stx(1, '\x02');
    const std::string etx(1, '\x03');
    SUBCASE("3C: an end code that is neither QACK nor QNAK is FrameMismatch at ETX") {
        SyntheticSpec s;
        s.format = SerialFormat::Format3;
        std::vector<uint8_t> w = bytesOf(stx + "F90000FF00QXCK" + etx + "00");
        Outcome o = feed(paramsOf(s), w, w.size());
        REQUIRE(o.status == ParseStatus::Failed);
        CHECK(o.error.code == ErrorCode::FrameMismatch);
        CHECK(o.frame.end == w.size() - 2); // through ETX; the 2 bytes behind it are not waited for
    }
    SUBCASE("1C: GG and NN are the codes; QACK is not") {
        SyntheticSpec s;
        s.frame = FrameType::F1C;
        s.format = SerialFormat::Format3;
        std::vector<uint8_t> w = bytesOf(stx + "00FFQACK" + etx);
        Outcome o = feed(paramsOf(s), w, w.size());
        REQUIRE(o.status == ParseStatus::Failed);
        CHECK(o.error.code == ErrorCode::FrameMismatch);
    }
    SUBCASE("a body shorter than P and the end code is LengthMismatch") {
        SyntheticSpec s;
        s.format = SerialFormat::Format3;
        std::vector<uint8_t> w = bytesOf(stx + "F90000FF00QAC" + etx);
        Outcome o = feed(paramsOf(s), w, w.size());
        REQUIRE(o.status == ParseStatus::Failed);
        CHECK(o.error.code == ErrorCode::LengthMismatch);
        CHECK(o.frame.end == w.size());
    }
    SUBCASE("a QNAK whose error code has the wrong size is LengthMismatch (3C: 4, 1C: 2)") {
        SyntheticSpec s3;
        s3.format = SerialFormat::Format3;
        std::vector<uint8_t> w3 = bytesOf(stx + "F90000FF00QNAK71" + etx);
        Outcome a = feed(paramsOf(s3), w3, w3.size());
        REQUIRE(a.status == ParseStatus::Failed);
        CHECK(a.error.code == ErrorCode::LengthMismatch);

        SyntheticSpec s1;
        s1.frame = FrameType::F1C;
        s1.format = SerialFormat::Format3;
        std::vector<uint8_t> w1 = bytesOf(stx + "00FFNN061" + etx);
        Outcome b = feed(paramsOf(s1), w1, w1.size());
        REQUIRE(b.status == ParseStatus::Failed);
        CHECK(b.error.code == ErrorCode::LengthMismatch);
    }
    SUBCASE("with f3ShortResponseHasSum the short forms wait for and verify a SUM") {
        SyntheticSpec s;
        s.format = SerialFormat::Format3;
        s.f3ShortSum = true;
        s.kind = SerialResponseKind::Ack;
        SyntheticFrame f = buildSyntheticFrame(s);
        REQUIRE(f.bytes.size() == 1 + 10 + 4 + 1 + 2);
        Outcome early = feed(paramsOf(s), f.bytes, f.bytes.size() - 1);
        CHECK(early.status == ParseStatus::NeedMore);
        Outcome full = feed(paramsOf(s), f.bytes, f.bytes.size());
        CHECK(full.status == ParseStatus::Done);
        // Without the option the same frame ends at ETX and the SUM characters are left over.
        s.f3ShortSum = false;
        Outcome plain = feed(paramsOf(s), f.bytes, f.bytes.size());
        CHECK(plain.status == ParseStatus::Done);
        CHECK(plain.frame.end == f.bytes.size() - 2);
    }
    SUBCASE("sumCheck off: no SUM even on a response with data") {
        SyntheticSpec s;
        s.format = SerialFormat::Format3;
        s.sumCheck = false;
        s.kind = SerialResponseKind::Data;
        s.data = "199512021130";
        SyntheticFrame f = buildSyntheticFrame(s);
        Outcome o = feed(paramsOf(s), f.bytes, f.bytes.size());
        REQUIRE(o.status == ParseStatus::Done);
        CHECK(o.frame.end == f.bytes.size());
        CHECK(f.bytes.back() == 0x03);
    }
}

TEST_CASE("SER-10: a response body shorter than the access route is LengthMismatch (Formats 1, 2, "
          "4), after its sum check") {
    const std::string stx(1, '\x02');
    const std::string etx(1, '\x03');
    for (SerialFormat format :
         {SerialFormat::Format1, SerialFormat::Format2, SerialFormat::Format4}) {
        SyntheticSpec s;
        s.format = format;
        // STX "F9" ETX SUM: 2 characters of body where P needs 10.
        std::string body = "F9";
        std::string wire = stx + body + etx + mc::test::syntheticSum(body + etx);
        if (format == SerialFormat::Format4) {
            wire += "\r\n";
        }
        INFO("format ", static_cast<int>(format));
        std::vector<uint8_t> w = bytesOf(wire);
        Outcome o = feed(paramsOf(s), w, w.size());
        REQUIRE(o.status == ParseStatus::Failed);
        CHECK(o.error.code == ErrorCode::LengthMismatch);
        CHECK(o.frame.end == w.size());

        // With a wrong SUM the same frame is a SumCheck failure: the sum is judged first.
        w[3 + 1] = w[3 + 1] == '0' ? '1' : '0';
        Outcome bad = feed(paramsOf(s), w, w.size());
        REQUIRE(bad.status == ParseStatus::Failed);
        CHECK(bad.error.code == ErrorCode::SumCheck);
    }
}

TEST_CASE("SER-11: ACK and NAK frames have a fixed length and carry no SUM; F4 adds CR LF") {
    const std::string ack(1, '\x06');
    const std::string nak(1, '\x15');
    SUBCASE("3C Format 1 NAK with bytes behind it: exactly 1 + 10 + 4") {
        SyntheticSpec s;
        std::vector<uint8_t> w = bytesOf(nak + "F90000FF007151" + "XYZ");
        Outcome early = feed(paramsOf(s), w, 14);
        CHECK(early.status == ParseStatus::NeedMore);
        Outcome o = feed(paramsOf(s), w, w.size());
        REQUIRE(o.status == ParseStatus::Done);
        CHECK(o.frame.end == 15);
        CHECK(o.frame.kind == SerialResponseKind::Nak);
    }
    SUBCASE("1C Format 2 ACK: 1 + 2 + 4") {
        SyntheticSpec s;
        s.frame = FrameType::F1C;
        s.format = SerialFormat::Format2;
        std::vector<uint8_t> w = bytesOf(ack + "0000FF" + "ZZ");
        Outcome o = feed(paramsOf(s), w, w.size());
        REQUIRE(o.status == ParseStatus::Done);
        CHECK(o.frame.end == 7);
        CHECK(o.frame.kind == SerialResponseKind::Ack);
        CHECK(o.frame.blockOffset == 1);
        CHECK(o.frame.routeOffset == 3);
    }
    SUBCASE("sumCheck on or off changes nothing for ACK and NAK") {
        for (bool sum : {true, false}) {
            SyntheticSpec s;
            s.sumCheck = sum;
            std::vector<uint8_t> w = bytesOf(ack + "F90000FF00");
            Outcome o = feed(paramsOf(s), w, w.size());
            CHECK(o.status == ParseStatus::Done);
            CHECK(o.frame.end == 11);
        }
    }
}

TEST_CASE("SER-12: serial field sizes and the data offset of every format, worked out by hand") {
    // Spec 6.3: P is 4 (1C) or 10 (3C) characters; the error code is 2 (1C) or 4 (3C).
    CHECK(mc::detail::serialRouteSize(FrameType::F3C) == 10);
    CHECK(mc::detail::serialRouteSize(FrameType::F1C) == 4);
    CHECK(mc::detail::serialCodeSize(FrameType::F3C) == 4);
    CHECK(mc::detail::serialCodeSize(FrameType::F1C) == 2);

    auto offset = [](FrameType frame, SerialFormat format) {
        return mc::detail::serialDataOffset(SerialParams{frame, format, true, false});
    };
    // 3C: STX + P(10); F2 adds BLK(2); F3 adds QACK(4).
    CHECK(offset(FrameType::F3C, SerialFormat::Format1) == 11);
    CHECK(offset(FrameType::F3C, SerialFormat::Format2) == 13);
    CHECK(offset(FrameType::F3C, SerialFormat::Format3) == 15);
    CHECK(offset(FrameType::F3C, SerialFormat::Format4) == 11);
    // 1C: STX + P(4); F2 adds BLK(2); F3 adds GG(2).
    CHECK(offset(FrameType::F1C, SerialFormat::Format1) == 5);
    CHECK(offset(FrameType::F1C, SerialFormat::Format2) == 7);
    CHECK(offset(FrameType::F1C, SerialFormat::Format3) == 7);
    CHECK(offset(FrameType::F1C, SerialFormat::Format4) == 5);
}

TEST_CASE(
    "SER-13: the reference spec's own frames (Appendix A.12-A.19), layout worked out by hand") {
    struct Case {
        const char* what;
        FrameType frame;
        SerialFormat format;
        std::string wire;
        SerialResponseKind kind;
        size_t blockOffset;
        size_t routeOffset;
        size_t dataOffset;
        size_t dataSize;
        size_t end;
    };
    const std::string stx(1, '\x02');
    const std::string etx(1, '\x03');
    const std::string ack(1, '\x06');
    const std::string nak(1, '\x15');
    const std::string crlf = "\r\n";
    const std::vector<Case> cases = {
        {"V-3C1-02", FrameType::F3C, SerialFormat::Format1,
         stx + "F90000FF00199512021130" + etx + "90", SerialResponseKind::Data, 1, 1, 11, 12, 26},
        {"V-3C3-02", FrameType::F3C, SerialFormat::Format3,
         stx + "F90000FF00QACK199512021130" + etx + "B0", SerialResponseKind::Data, 1, 1, 15, 12,
         30},
        {"V-3C3-04", FrameType::F3C, SerialFormat::Format3, stx + "F90000FF00QACK" + etx,
         SerialResponseKind::Ack, 1, 1, 15, 0, 16},
        {"V-3C3-05", FrameType::F3C, SerialFormat::Format3, stx + "F90000FF00QNAK7151" + etx,
         SerialResponseKind::Nak, 1, 1, 15, 4, 20},
        {"V-3C4-05", FrameType::F3C, SerialFormat::Format4, nak + "F90000FF007151" + crlf,
         SerialResponseKind::Nak, 1, 1, 11, 4, 17},
        {"V-3C2-04", FrameType::F3C, SerialFormat::Format2, ack + "00F90000FF00",
         SerialResponseKind::Ack, 1, 3, 13, 0, 13},
        {"V-1C1-08", FrameType::F1C, SerialFormat::Format1, nak + "00FF06", SerialResponseKind::Nak,
         1, 1, 5, 2, 7},
        {"V-1C2-04", FrameType::F1C, SerialFormat::Format2, stx + "0000FF00010011" + etx + "D2",
         SerialResponseKind::Data, 1, 3, 7, 8, 18},
        {"V-1C3-08", FrameType::F1C, SerialFormat::Format3, stx + "00FFNN06" + etx,
         SerialResponseKind::Nak, 1, 1, 7, 2, 10},
        {"V-1C3-07", FrameType::F1C, SerialFormat::Format3, stx + "00FFGG" + etx,
         SerialResponseKind::Ack, 1, 1, 7, 0, 8},
        {"V-1C4-07", FrameType::F1C, SerialFormat::Format4, ack + "00FF" + crlf,
         SerialResponseKind::Ack, 1, 1, 5, 0, 7},
        {"V-1C4-02", FrameType::F1C, SerialFormat::Format4,
         stx + "00FF199512021130" + etx + "51" + crlf, SerialResponseKind::Data, 1, 1, 5, 12, 22},
    };
    for (const Case& c : cases) {
        INFO("vector ", c.what);
        SerialParams p{c.frame, c.format, true, false};
        std::vector<uint8_t> w = bytesOf(c.wire);
        Outcome o = feed(p, w, w.size());
        REQUIRE(o.status == ParseStatus::Done);
        CHECK(o.frame.kind == c.kind);
        CHECK(o.frame.blockOffset == c.blockOffset);
        CHECK(o.frame.routeOffset == c.routeOffset);
        CHECK(o.frame.dataOffset == c.dataOffset);
        CHECK(o.frame.dataSize == c.dataSize);
        CHECK(o.frame.end == c.end);
        CHECK(o.frame.end == w.size());
    }
}
