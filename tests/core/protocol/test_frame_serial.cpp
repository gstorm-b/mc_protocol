// 3C-01..03, 1C-01..08 and the serial checks 4C-09, 4C-10, 4C-13, 4C-14, 4C-15 of
// docs/mc_reference/ mc-protocol-frame-spec.md section 9.8, applied to 3C and 1C, driven by
// tests/vectors/3c_f1.vec .. 3c_f4.vec and 1c_f1.vec .. 1c_f4.vec (Appendix A.12-A.19 plus derived
// rows whose sums were computed outside the C++ code), and standalone TEST_CASEs for what the
// vectors cannot say: a non-default access route worked out by hand, junk before the frame (4C-15,
// STR-03 through McProtocol), invalid characters, and the size bounds. Uses the public
// include/mc/core/protocol.h surface only.
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "serial_vector_config.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using mc::ByteView;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCategory;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::McProtocol;
using mc::MutableByteView;
using mc::Parser;
using mc::ParseStatus;
using mc::Request;
using mc::SerialFormat;
using mc::test::csvHex;
using mc::test::loadVectors;
using mc::test::serialConfigFromVector;
using mc::test::serialRequestFromVector;
using mc::test::Vector;

namespace {

std::filesystem::path vectorsRoot() {
    return std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
}

uint32_t parseHexOr(const std::string& s, uint32_t fallback) {
    return s.empty() ? fallback : static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

void checkRequestVector(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = serialRequestFromVector(v, writeStorage);
    McProtocol proto(serialConfigFromVector(v));

    auto sizeResult = proto.encodedSize(r);
    REQUIRE(sizeResult.hasValue());
    CHECK(sizeResult.value() == v.bytes.size()); // SZ-01: encodedSize() == vector length.

    std::vector<uint8_t> out(sizeResult.value(), 0xCC);
    auto encodeResult = proto.encode(r, MutableByteView{out.data(), out.size()});
    REQUIRE(encodeResult.hasValue());
    CHECK(encodeResult.value() == v.bytes.size());
    CHECK(out == v.bytes);
}

// Every prefix of the frame is NeedMore (a frame is judged only when complete), the last byte
// decides; `skipped` is what the parse reports as junk.
void checkStreamed(const Vector& v, Parser parser, ParseStatus expected) {
    for (size_t n = 1; n < v.bytes.size(); ++n) {
        REQUIRE(parser.feed(ByteView{v.bytes.data(), n}) == ParseStatus::NeedMore);
    }
    CHECK(parser.feed(ByteView{v.bytes.data(), v.bytes.size()}) == expected);
    CHECK(parser.frameLength() == v.bytes.size());
}

void checkResponseVector(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = serialRequestFromVector(v, writeStorage);
    McProtocol proto(serialConfigFromVector(v));

    CHECK(v.bytes.size() <= proto.maxResponseSize(r)); // SZ-01.

    Parser parser = proto.parser(r);
    ByteView wire{v.bytes.data(), v.bytes.size()};
    REQUIRE(parser.feed(wire) == ParseStatus::Done);
    CHECK(parser.frameLength() == v.bytes.size());
    CHECK(parser.skipped() == 0);

    size_t payloadSize = proto.payloadSize(r);
    std::vector<uint8_t> payloadOut(payloadSize, 0xFF);
    auto payloadResult =
        parser.payload(wire, MutableByteView{payloadOut.data(), payloadOut.size()});
    REQUIRE(payloadResult.hasValue());
    CHECK(payloadResult.value() == payloadSize); // SZ-01: payload length == payloadSize().

    std::vector<uint32_t> expected = csvHex(v.field("expect"));
    if (r.isWrite()) {
        CHECK(payloadSize == 0);
    } else if (r.isBitOp()) {
        REQUIRE(payloadOut.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK(static_cast<uint32_t>(payloadOut[i]) == expected[i]);
        }
    } else {
        REQUIRE(payloadOut.size() == expected.size() * 2);
        for (size_t i = 0; i < expected.size(); ++i) {
            uint32_t got = static_cast<uint32_t>(payloadOut[2 * i]) |
                           (static_cast<uint32_t>(payloadOut[2 * i + 1]) << 8);
            CHECK(got == expected[i]);
        }
    }

    checkStreamed(v, proto.parser(r), ParseStatus::Done);
}

void checkResponseErrorVector(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = serialRequestFromVector(v, writeStorage);
    McProtocol proto(serialConfigFromVector(v));

    // SZ-01 bounds what a PLC can legitimately answer: a NAK. A frame that is wrong by size (a
    // data frame to a write) is not a response the bound is about.
    if (v.field("error") == "Plc") {
        CHECK(v.bytes.size() <= proto.maxResponseSize(r));
    }

    Parser parser = proto.parser(r);
    REQUIRE(parser.feed(ByteView{v.bytes.data(), v.bytes.size()}) == ParseStatus::Failed);
    CHECK(parser.frameLength() == v.bytes.size()); // the whole frame is the vector
    CHECK(parser.skipped() == 0);

    const mc::Error& err = parser.error();
    const std::string errorName = v.field("error");
    if (errorName == "Plc") {
        CHECK(err.category == ErrorCategory::Plc);
        CHECK(err.code == ErrorCode::PlcError);
        CHECK(err.plcCode == parseHexOr(v.field("plccode"), 0));
    } else {
        CHECK(err.category == ErrorCategory::Protocol);
        if (errorName == "FrameMismatch") {
            CHECK(err.code == ErrorCode::FrameMismatch);
        } else if (errorName == "LengthMismatch") {
            CHECK(err.code == ErrorCode::LengthMismatch);
        } else if (errorName == "SumCheck") {
            CHECK(err.code == ErrorCode::SumCheck);
        } else if (errorName == "InvalidCharacter") {
            CHECK(err.code == ErrorCode::InvalidCharacter);
        } else {
            FAIL("vector ", v.id, " (", v.file, ") has unrecognized error '", errorName, "'");
        }
    }

    checkStreamed(v, proto.parser(r), ParseStatus::Failed);
}

struct Counts {
    size_t requests{0};
    size_t responses{0};
    size_t errors{0};
    size_t skipped{0};
};

// Every record of `fileName` except the tagged ones is checked by its `kind:`.
Counts runSerialVectorFile(const char* fileName, size_t expectedRecords) {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / fileName);
    REQUIRE(vectors.size() == expectedRecords);

    Counts counts;
    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (v.hasTag("v1.1") || v.hasTag("v2")) {
            ++counts.skipped;
            continue;
        }
        const std::string kind = v.field("kind");
        if (kind == "request") {
            checkRequestVector(v);
            ++counts.requests;
        } else if (kind == "response") {
            checkResponseVector(v);
            ++counts.responses;
        } else if (kind == "response-error") {
            checkResponseErrorVector(v);
            ++counts.errors;
        } else {
            FAIL("vector ", v.id, " (", v.file, ") has unknown kind '", kind, "'");
        }
    }
    MESSAGE(std::string(fileName), ": ", vectors.size() - counts.skipped, " enabled (",
            counts.requests, " requests, ", counts.responses, " responses, ", counts.errors,
            " errors), ", counts.skipped, " tagged and skipped");
    return counts;
}

const std::string kStx(1, '\x02');
const std::string kEtx(1, '\x03');
const std::string kEnq(1, '\x05');
const std::string kAck(1, '\x06');
const std::string kNak(1, '\x15');
const std::string kCrLf = "\r\n";

std::vector<uint8_t> toWire(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

Request readG1() { return Request::readWords(Device{DeviceType::D, 100}, 3); }

Request writeG4(std::vector<uint8_t>& storage) {
    storage = {1, 1, 0, 0, 1, 1, 0, 0};
    return Request::writeBits(Device{DeviceType::M, 100}, ByteView{storage.data(), storage.size()});
}

const SerialFormat kFormats[] = {SerialFormat::Format1, SerialFormat::Format2,
                                 SerialFormat::Format3, SerialFormat::Format4};

const std::string kRoute = "F90000FF00"; // "F9" station 00, network 00, PC FF, self-station 00
const std::string kG1Data = "199512021130";

// The G1 response of the spec (Appendix A.12-A.15) in a format, with the station field and the SUM
// text as given (the SUM is not computed here: every caller passes a literal).
std::string g1Response(SerialFormat format, const std::string& station, const std::string& sum) {
    const std::string route = "F9" + station + "00FF00";
    switch (format) {
    case SerialFormat::Format1:
        return kStx + route + kG1Data + kEtx + sum;
    case SerialFormat::Format2:
        return kStx + "00" + route + kG1Data + kEtx + sum;
    case SerialFormat::Format3:
        return kStx + route + "QACK" + kG1Data + kEtx + sum;
    default:
        return kStx + route + kG1Data + kEtx + sum + kCrLf;
    }
}

// SUM of V-3C1-02, V-3C2-02, V-3C3-02 and V-3C4-02, straight from the spec.
std::string g1Sum(SerialFormat format) {
    switch (format) {
    case SerialFormat::Format1:
        return "90";
    case SerialFormat::Format2:
        return "F0";
    case SerialFormat::Format3:
        return "B0";
    default:
        return "90";
    }
}

// The same frame from station 01: one byte changes by +1, so each SUM rises by 1.
std::string g1SumStation01(SerialFormat format) {
    switch (format) {
    case SerialFormat::Format1:
        return "91";
    case SerialFormat::Format2:
        return "F1";
    case SerialFormat::Format3:
        return "B1";
    default:
        return "91";
    }
}

// A NAK in a format (Format 3: the QNAK form), for the default route.
std::string nakFrame(SerialFormat format, const std::string& err) {
    switch (format) {
    case SerialFormat::Format1:
        return kNak + kRoute + err;
    case SerialFormat::Format2:
        return kNak + "00" + kRoute + err;
    case SerialFormat::Format3:
        return kStx + kRoute + "QNAK" + err + kEtx;
    default:
        return kNak + kRoute + err + kCrLf;
    }
}

} // namespace

TEST_CASE("3C-01 (Format 1), SZ-01: driven by 3c_f1.vec (A.12 + derived rows)") {
    Counts c = runSerialVectorFile("3c_f1.vec", 23);
    CHECK(c.requests + c.responses + c.errors == 23);
    CHECK(c.skipped == 0);
}

TEST_CASE("3C-01 (Format 4), SZ-01: driven by 3c_f4.vec (A.15 + derived rows)") {
    Counts c = runSerialVectorFile("3c_f4.vec", 23);
    CHECK(c.requests + c.responses + c.errors == 23);
    CHECK(c.skipped == 0);
}

TEST_CASE("3C-01 (Format 2), SZ-01: driven by 3c_f2.vec (A.13 + derived rows)") {
    Counts c = runSerialVectorFile("3c_f2.vec", 29);
    CHECK(c.requests + c.responses + c.errors == 29);
    CHECK(c.skipped == 0);
}

TEST_CASE("3C-01 (Format 3), SZ-01: driven by 3c_f3.vec (A.14 + derived rows)") {
    Counts c = runSerialVectorFile("3c_f3.vec", 27);
    CHECK(c.requests + c.responses + c.errors == 27);
    CHECK(c.skipped == 0);
}

TEST_CASE("4C vectors A.7-A.11: transcribed, tagged v2 and skipped; VEC-02 checks their sizes") {
    struct File {
        const char* name;
        size_t records;
    };
    size_t total = 0;
    for (const File& f : {File{"4c_f1.vec", 6}, File{"4c_f2.vec", 5}, File{"4c_f3.vec", 5},
                          File{"4c_f4.vec", 5}, File{"4c_f5.vec", 8}}) {
        Counts c = runSerialVectorFile(f.name, f.records);
        CHECK(c.skipped == f.records); // every record is tagged, none is run
        total += c.skipped;
        for (const Vector& v : loadVectors(vectorsRoot() / f.name)) {
            INFO("vector ", v.id);
            CHECK(v.hasTag("v2"));
            CHECK(v.field("frame") == "4C");
        }
    }
    CHECK(total == 29);
}

TEST_CASE("3C-03: Format 5 is a configuration error, and no frame is built for it") {
    FrameConfig cfg = FrameConfig::frame3C(SerialFormat::Format5);
    auto validated = cfg.validate();
    REQUIRE_FALSE(validated.hasValue());
    CHECK(validated.error().category == ErrorCategory::Config);
    CHECK(validated.error().code == ErrorCode::InvalidConfig);

    McProtocol proto(cfg);
    auto encoded = proto.encodedSize(readG1());
    REQUIRE_FALSE(encoded.hasValue());
    CHECK(encoded.error().code == ErrorCode::UnsupportedCommand);
    CHECK(proto.maxResponseSize(readG1()) == 0);
}

TEST_CASE("3C-02: a NAK (Format 3: QNAK) is McPlcError(7151H) in every format") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        FrameConfig cfg = FrameConfig::frame3C(format);
        McProtocol proto(cfg);
        // V-3C1-05 .. V-3C4-05, written out (Format 2 also carries its block number).
        std::vector<uint8_t> wire = toWire(nakFrame(format, "7151"));

        Parser parser = proto.parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Plc);
        CHECK(parser.error().code == ErrorCode::PlcError);
        CHECK(parser.error().plcCode == 0x7151);
        CHECK(parser.frameLength() == wire.size());

        std::vector<uint8_t> storage;
        Parser writeParser = proto.parser(writeG4(storage));
        REQUIRE(writeParser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(writeParser.error().plcCode == 0x7151);
    }
}

TEST_CASE("3C-02: the NAK error code is 4 hex characters, lower case accepted, non-hex rejected") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        McProtocol proto(FrameConfig::frame3C(format));

        std::vector<uint8_t> lower = toWire(nakFrame(format, "c051"));
        Parser a = proto.parser(readG1());
        REQUIRE(a.feed(ByteView{lower.data(), lower.size()}) == ParseStatus::Failed);
        CHECK(a.error().plcCode == 0xC051);

        std::vector<uint8_t> bad = toWire(nakFrame(format, "G151"));
        Parser b = proto.parser(readG1());
        REQUIRE(b.feed(ByteView{bad.data(), bad.size()}) == ParseStatus::Failed);
        CHECK(b.error().category == ErrorCategory::Protocol);
        CHECK(b.error().code == ErrorCode::InvalidCharacter);
    }
}

TEST_CASE("3C-01: the access route is station, network, PC, self-station in that order (worked "
          "out by hand)") {
    // P = "F9" + "05" "07" "03" "0A". The spec's V-3C1-01 has SUM 02 for P = F90000FF00; the route
    // bytes differ by +5 +7 -41 +17 = -12 in total, so the SUM is 02 - 0C = F6. The response to
    // that request has SUM 90 - 0C = 84.
    FrameConfig cfg = FrameConfig::frame3C();
    cfg.stationNo = 0x05;
    cfg.network = 0x07;
    cfg.pc = 0x03;
    cfg.selfStation = 0x0A;
    McProtocol proto(cfg);
    Request r = readG1();

    auto encoded = proto.encode(r);
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) ==
          kEnq + "F90507030A04010000D*0001000003F6");

    std::vector<uint8_t> good = toWire(kStx + "F90507030A199512021130" + kEtx + "84");
    Parser parser = proto.parser(r);
    CHECK(parser.feed(ByteView{good.data(), good.size()}) == ParseStatus::Done);

    // The same response under the default route is another station's: FrameMismatch.
    Parser other = McProtocol(FrameConfig::frame3C()).parser(r);
    std::vector<uint8_t> wrong = toWire(kStx + "F90507030A199512021130" + kEtx + "84");
    REQUIRE(other.feed(ByteView{wrong.data(), wrong.size()}) == ParseStatus::Failed);
    CHECK(other.error().code == ErrorCode::FrameMismatch);
}

TEST_CASE("4C-09 applied to 3C: a wrong sum check is SumCheck (every format)") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        // The spec's G1 response with the last SUM character changed: 90 -> 91, F0 -> F1, B0 -> B1.
        std::string good = g1Sum(format);
        std::string wrong = good.substr(0, 1) + (good[1] == '0' ? "1" : "0");
        std::vector<uint8_t> wire = toWire(g1Response(format, "00", wrong));
        Parser parser = McProtocol(FrameConfig::frame3C(format)).parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::SumCheck);
        CHECK(parser.frameLength() == wire.size());

        // The spec's own SUM is right.
        std::vector<uint8_t> ok = toWire(g1Response(format, "00", good));
        Parser fine = McProtocol(FrameConfig::frame3C(format)).parser(readG1());
        CHECK(fine.feed(ByteView{ok.data(), ok.size()}) == ParseStatus::Done);
    }
}

TEST_CASE(
    "4C-10 applied to 3C: sumCheck off means no SUM in the request and none in the response") {
    // The request of each format without its SUM (spec 4C-10: V-4C1-06 is V-4C1-01 minus SUM).
    const std::string rd = "04010000D*0001000003";
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        FrameConfig cfg = FrameConfig::frame3C(format);
        cfg.sumCheck = false;
        McProtocol proto(cfg);
        Request r = readG1();

        std::string expected;
        switch (format) {
        case SerialFormat::Format1:
            expected = kEnq + kRoute + rd;
            break;
        case SerialFormat::Format2:
            expected = kEnq + "00" + kRoute + rd;
            break;
        case SerialFormat::Format3:
            expected = kStx + kRoute + rd + kEtx;
            break;
        default:
            expected = kEnq + kRoute + rd + kCrLf;
            break;
        }
        auto encoded = proto.encode(r);
        REQUIRE(encoded.hasValue());
        CHECK(std::string(encoded.value().begin(), encoded.value().end()) == expected);
        CHECK(proto.encodedSize(r).value() == expected.size());

        // A response without SUM parses; with the sum check on the same bytes still wait for it.
        std::vector<uint8_t> wire = toWire(g1Response(format, "00", ""));
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        CHECK(parser.frameLength() == wire.size());

        Parser waiting = McProtocol(FrameConfig::frame3C(format)).parser(r);
        CHECK(waiting.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::NeedMore);
    }
}

TEST_CASE("4C-13 applied to 3C: another station's response is FrameMismatch (SUM recomputed); "
          "with checkRoute off it parses") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        // The spec's G1 response with station "00" -> "01": the byte sum rises by 1.
        std::vector<uint8_t> wire = toWire(g1Response(format, "01", g1SumStation01(format)));
        Request r = readG1();

        Parser parser = McProtocol(FrameConfig::frame3C(format)).parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().code == ErrorCode::FrameMismatch);
        CHECK(parser.frameLength() == wire.size());

        FrameConfig lax = FrameConfig::frame3C(format);
        lax.checkRoute = false;
        Parser acceptant = McProtocol(lax).parser(r);
        CHECK(acceptant.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
    }
}

TEST_CASE("4C-14 applied to 3C: Format 2 block number from the configuration, checked on the "
          "response") {
    // Block 3A. The spec's V-3C2-01 (block 00) has SUM 62; "00" -> "3A" raises the byte sum by
    // 0x03 + 0x11 = 0x14, so the request's SUM is 76, and the response V-3C2-02 (SUM F0) gets 04.
    FrameConfig cfg = FrameConfig::frame3C(SerialFormat::Format2);
    cfg.blockNo = 0x3A;
    McProtocol proto(cfg);
    Request r = readG1();

    auto encoded = proto.encode(r);
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) ==
          kEnq + "3A" + kRoute + "04010000D*0001000003" + "76");

    std::vector<uint8_t> same = toWire(kStx + "3A" + kRoute + kG1Data + kEtx + "04");
    Parser ok = proto.parser(r);
    CHECK(ok.feed(ByteView{same.data(), same.size()}) == ParseStatus::Done);

    // Lower case in the echoed block is the same number: 'a' is 0x20 above 'A', SUM 04 -> 24.
    std::vector<uint8_t> lower = toWire(kStx + "3a" + kRoute + kG1Data + kEtx + "24");
    Parser lowerParser = proto.parser(r);
    CHECK(lowerParser.feed(ByteView{lower.data(), lower.size()}) == ParseStatus::Done);

    // Another block (00, the spec's own V-3C2-02) is FrameMismatch; with checkBlockNo off it
    // parses.
    std::vector<uint8_t> other = toWire(g1Response(SerialFormat::Format2, "00", "F0"));
    Parser mismatch = proto.parser(r);
    REQUIRE(mismatch.feed(ByteView{other.data(), other.size()}) == ParseStatus::Failed);
    CHECK(mismatch.error().category == ErrorCategory::Protocol);
    CHECK(mismatch.error().code == ErrorCode::FrameMismatch);
    CHECK(mismatch.frameLength() == other.size());

    FrameConfig lax = cfg;
    lax.checkBlockNo = false;
    Parser acceptant = McProtocol(lax).parser(r);
    CHECK(acceptant.feed(ByteView{other.data(), other.size()}) == ParseStatus::Done);

    // A NAK is checked the same way: its own block first, then the PLC error.
    std::vector<uint8_t> nakOther = toWire(kNak + "00" + kRoute + "7151");
    Parser nak = proto.parser(r);
    REQUIRE(nak.feed(ByteView{nakOther.data(), nakOther.size()}) == ParseStatus::Failed);
    CHECK(nak.error().code == ErrorCode::FrameMismatch);
    std::vector<uint8_t> nakSame = toWire(kNak + "3A" + kRoute + "7151");
    Parser nakOk = proto.parser(r);
    REQUIRE(nakOk.feed(ByteView{nakSame.data(), nakSame.size()}) == ParseStatus::Failed);
    CHECK(nakOk.error().code == ErrorCode::PlcError);
    CHECK(nakOk.error().plcCode == 0x7151);

    // A non-hex block is InvalidCharacter ('G' is 0x06 above 'A', so the SUM 04 becomes 0A; the
    // sum is judged before the block).
    std::vector<uint8_t> bad = toWire(kStx + "3G" + kRoute + kG1Data + kEtx + "0A");
    Parser badParser = proto.parser(r);
    REQUIRE(badParser.feed(ByteView{bad.data(), bad.size()}) == ParseStatus::Failed);
    CHECK(badParser.error().code == ErrorCode::InvalidCharacter);
}

TEST_CASE("3C-02, spec 10 Q1: Format 3 short responses (QACK / QNAK) and f3ShortResponseHasSum") {
    // Byte sums worked out by hand: "F90000FF00" = 0x22B, "QACK" = 0x120, "QNAK" = 0x12B,
    // "7151" = 0xCE, ETX = 0x03. QACK ETX: 0x22B + 0x120 + 0x03 = 0x34E -> SUM 4E.
    // QNAK 7151 ETX: 0x22B + 0x12B + 0xCE + 0x03 = 0x427 -> SUM 27.
    FrameConfig plain = FrameConfig::frame3C(SerialFormat::Format3);
    FrameConfig withSum = plain;
    withSum.f3ShortResponseHasSum = true;
    std::vector<uint8_t> storage;
    Request write = writeG4(storage);
    Request read = readG1();

    const std::string qack = kStx + kRoute + "QACK" + kEtx;
    const std::string qnak = kStx + kRoute + "QNAK7151" + kEtx;

    SUBCASE("default: the short forms end at ETX and carry no SUM") {
        std::vector<uint8_t> ack = toWire(qack);
        Parser a = McProtocol(plain).parser(write);
        REQUIRE(a.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Done);
        CHECK(a.frameLength() == 16);

        std::vector<uint8_t> nak = toWire(qnak);
        Parser n = McProtocol(plain).parser(read);
        REQUIRE(n.feed(ByteView{nak.data(), nak.size()}) == ParseStatus::Failed);
        CHECK(n.error().plcCode == 0x7151);
        CHECK(n.frameLength() == 20);
    }
    SUBCASE("default: bytes behind the short form belong to the next frame") {
        std::vector<uint8_t> two = toWire(qack + qnak);
        Parser a = McProtocol(plain).parser(write);
        REQUIRE(a.feed(ByteView{two.data(), two.size()}) == ParseStatus::Done);
        CHECK(a.frameLength() == 16);
    }
    SUBCASE("option on: the short forms wait for their SUM and verify it") {
        std::vector<uint8_t> ack = toWire(qack + "4E");
        Parser early = McProtocol(withSum).parser(write);
        CHECK(early.feed(ByteView{ack.data(), ack.size() - 1}) == ParseStatus::NeedMore);
        Parser a = McProtocol(withSum).parser(write);
        REQUIRE(a.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Done);
        CHECK(a.frameLength() == 18);

        std::vector<uint8_t> nak = toWire(qnak + "27");
        Parser n = McProtocol(withSum).parser(read);
        REQUIRE(n.feed(ByteView{nak.data(), nak.size()}) == ParseStatus::Failed);
        CHECK(n.error().category == ErrorCategory::Plc);
        CHECK(n.error().plcCode == 0x7151);
        CHECK(n.frameLength() == 22);

        std::vector<uint8_t> wrong = toWire(qack + "4F");
        Parser w = McProtocol(withSum).parser(write);
        REQUIRE(w.feed(ByteView{wrong.data(), wrong.size()}) == ParseStatus::Failed);
        CHECK(w.error().code == ErrorCode::SumCheck);
    }
    SUBCASE("option on but sumCheck off: still no SUM") {
        FrameConfig none = withSum;
        none.sumCheck = false;
        std::vector<uint8_t> ack = toWire(qack);
        Parser a = McProtocol(none).parser(write);
        CHECK(a.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Done);
    }
    SUBCASE("a response with data always carries its SUM, option or not") {
        std::vector<uint8_t> data = toWire(g1Response(SerialFormat::Format3, "00", "B0"));
        for (const FrameConfig& cfg : {plain, withSum}) {
            Parser p = McProtocol(cfg).parser(read);
            CHECK(p.feed(ByteView{data.data(), data.size()}) == ParseStatus::Done);
        }
    }
    SUBCASE("QACK with no data answers a write, not a read; QACK with data not a write") {
        std::vector<uint8_t> ack = toWire(qack);
        Parser r = McProtocol(plain).parser(read);
        REQUIRE(r.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Failed);
        CHECK(r.error().code == ErrorCode::LengthMismatch);

        std::vector<uint8_t> data = toWire(g1Response(SerialFormat::Format3, "00", "B0"));
        Parser w = McProtocol(plain).parser(write);
        REQUIRE(w.feed(ByteView{data.data(), data.size()}) == ParseStatus::Failed);
        CHECK(w.error().code == ErrorCode::LengthMismatch);
    }
    SUBCASE("an end code that is neither QACK nor QNAK is FrameMismatch, at ETX") {
        std::vector<uint8_t> bad = toWire(kStx + kRoute + "QXCK" + kEtx + "00");
        Parser p = McProtocol(plain).parser(write);
        REQUIRE(p.feed(ByteView{bad.data(), bad.size()}) == ParseStatus::Failed);
        CHECK(p.error().code == ErrorCode::FrameMismatch);
        CHECK(p.frameLength() == bad.size() - 2);
    }
}

TEST_CASE("4C-15, STR-03 applied to 3C: junk before the frame is skipped and reported") {
    for (SerialFormat format : kFormats) {
        FrameConfig cfg = FrameConfig::frame3C(format);
        McProtocol proto(cfg);
        Request r = readG1();
        std::string frame = g1Response(format, "00", g1Sum(format));
        // "00 FF" of spec 9.8, then a longer run with control bytes that are not start bytes
        // (ENQ is not one for a response in any format).
        for (const std::string& junk :
             {std::string("\x00\xFF", 2), std::string("\x41\x0D\x0A\x05\x00", 5)}) {
            INFO("junk bytes ", junk.size(), ", format ", static_cast<int>(format));
            std::vector<uint8_t> wire = toWire(junk + frame);

            Parser whole = proto.parser(r);
            REQUIRE(whole.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
            CHECK(whole.skipped() == junk.size());
            CHECK(whole.frameLength() == wire.size());
            uint8_t payload[6] = {};
            auto result = whole.payload(ByteView{wire.data(), wire.size()},
                                        MutableByteView{payload, sizeof(payload)});
            REQUIRE(result.hasValue());
            CHECK(std::vector<uint8_t>(payload, payload + 6) ==
                  std::vector<uint8_t>{0x95, 0x19, 0x02, 0x12, 0x30, 0x11});

            Parser streamed = proto.parser(r);
            for (size_t n = 1; n < wire.size(); ++n) {
                REQUIRE(streamed.feed(ByteView{wire.data(), n}) == ParseStatus::NeedMore);
            }
            REQUIRE(streamed.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
            CHECK(streamed.skipped() == junk.size());
        }
    }
}

TEST_CASE("3C parse: response data characters are checked when the payload is decoded") {
    FrameConfig cfg = FrameConfig::frame3C(SerialFormat::Format1);
    cfg.sumCheck = false; // so a hand-edited body needs no new SUM
    McProtocol proto(cfg);
    Request r = readG1();

    SUBCASE("a non-hex data character: feed() is Done, payload() is InvalidCharacter") {
        std::vector<uint8_t> wire = toWire(kStx + "F90000FF0019G512021130" + kEtx);
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[6];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Protocol);
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("lower-case hex in the data is accepted (PRIM-05)") {
        std::vector<uint8_t> wire = toWire(kStx + "F90000FF001a2b3c4d5e6f" + kEtx);
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[6];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE(result.hasValue());
        CHECK(std::vector<uint8_t>(out, out + 6) ==
              std::vector<uint8_t>{0x2B, 0x1A, 0x4D, 0x3C, 0x6F, 0x5E});
    }
    SUBCASE("a non-hex character in the route is InvalidCharacter, not FrameMismatch") {
        std::vector<uint8_t> wire = toWire(kStx + "F90G00FF00199512021130" + kEtx);
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("payload(): BufferTooSmall when out is smaller than payloadSize()") {
        std::vector<uint8_t> wire = toWire(kStx + "F90000FF00199512021130" + kEtx);
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[5];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("PackedLsbFirst bit read: ceil(count / 8) bytes from the same frame") {
        Request bits = Request::readBits(Device{DeviceType::M, 100}, 8);
        bits.bitLayout = mc::BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = toWire(kStx + "F90000FF0000010011" + kEtx);
        Parser parser = proto.parser(bits);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[1] = {0xFF};
        auto result = parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, 1});
        REQUIRE(result.hasValue());
        CHECK(out[0] == 0xC8); // points 3, 6, 7 on: 0,0,0,1,0,0,1,1
    }
}

TEST_CASE("3C size bounds, worked out by hand (SZ-01)") {
    std::vector<uint8_t> storage;
    Request read = readG1();                                         // 12 response data characters
    Request bits = Request::readBits(Device{DeviceType::M, 100}, 5); // 5 characters
    Request write = writeG4(storage);

    // Format 1: STX + P(10) + data + ETX + SUM(2); a write: ACK + P = 11, NAK + P + err(4) = 15.
    McProtocol f1(FrameConfig::frame3C(SerialFormat::Format1));
    CHECK(f1.maxResponseSize(read) == 1 + 10 + 12 + 1 + 2);
    CHECK(f1.maxResponseSize(bits) == 1 + 10 + 5 + 1 + 2);
    CHECK(f1.maxResponseSize(write) == 1 + 10 + 4);
    // Format 4 adds CR LF to each form.
    McProtocol f4(FrameConfig::frame3C(SerialFormat::Format4));
    CHECK(f4.maxResponseSize(read) == 28);
    CHECK(f4.maxResponseSize(write) == 17);
    // Sum check off drops the SUM of a data frame (the NAK is the longer form for a write).
    FrameConfig nosum = FrameConfig::frame3C(SerialFormat::Format1);
    nosum.sumCheck = false;
    CHECK(McProtocol(nosum).maxResponseSize(read) == 24);

    // Format 2: STX BLK(2) P(10) data(12) ETX SUM(2) = 28; a write: NAK BLK P err(4) = 17.
    McProtocol f2(FrameConfig::frame3C(SerialFormat::Format2));
    CHECK(f2.maxResponseSize(read) == 28);
    CHECK(f2.maxResponseSize(write) == 17);
    // Format 3: STX P QACK(4) data(12) ETX SUM(2) = 30; a write: QNAK form STX P QNAK err(4) ETX =
    // 20 against QACK ETX = 16, and 2 more each when the short forms carry a SUM.
    FrameConfig f3cfg = FrameConfig::frame3C(SerialFormat::Format3);
    McProtocol f3(f3cfg);
    CHECK(f3.maxResponseSize(read) == 30);
    CHECK(f3.maxResponseSize(write) == 20);
    f3cfg.f3ShortResponseHasSum = true;
    CHECK(McProtocol(f3cfg).maxResponseSize(write) == 22);
    f3cfg.sumCheck = false;
    CHECK(McProtocol(f3cfg).maxResponseSize(write) == 20);

    // Request sizes: ENQ + P(10) + request data (0401 0000 D*000100 0003 = 20) + SUM; + CR LF;
    // Format 2 adds BLK(2); Format 3 is STX P data ETX SUM.
    CHECK(f1.encodedSize(read).value() == 1 + 10 + 20 + 2);
    CHECK(f2.encodedSize(read).value() == 35);
    CHECK(f3.encodedSize(read).value() == 34);
    CHECK(f4.encodedSize(read).value() == 35);
}

TEST_CASE("3C encode: BufferTooSmall, and the validate() errors come first") {
    McProtocol proto(FrameConfig::frame3C());
    SUBCASE("BufferTooSmall") {
        uint8_t out[10];
        auto result = proto.encode(readG1(), MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("a zero point count is PointCount") {
        uint8_t out[64];
        auto result = proto.encode(Request::readWords(Device{DeviceType::D, 100}, 0),
                                   MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::PointCount);
    }
}

TEST_CASE("3C config: Binary code is rejected by validate() and reported as not implemented") {
    FrameConfig cfg = FrameConfig::frame3C();
    cfg.code = mc::DataCode::Binary;
    CHECK_FALSE(cfg.validate().hasValue());
    McProtocol proto(cfg);
    auto size = proto.encodedSize(readG1());
    REQUIRE_FALSE(size.hasValue());
    CHECK(size.error().code == ErrorCode::UnsupportedCommand);
}

// ---- 1C (T-048, T-049): formats 1-4, both command sets ----------------------------------------

namespace {

const std::string k1cRoute = "00FF"; // station 00, PC FF (spec 5.6)

// Spec Appendix A.16-A.19 G1 response (WR D100 x 3) of a format with the route and SUM text as
// given (the SUM is not computed here: every caller passes a literal). Format 2 carries block 00.
std::string g1Response1c(SerialFormat format, const std::string& route, const std::string& sum) {
    switch (format) {
    case SerialFormat::Format1:
        return kStx + route + kG1Data + kEtx + sum;
    case SerialFormat::Format2:
        return kStx + "00" + route + kG1Data + kEtx + sum;
    case SerialFormat::Format3:
        return kStx + route + "GG" + kG1Data + kEtx + sum;
    default:
        return kStx + route + kG1Data + kEtx + sum + kCrLf;
    }
}

// SUM of V-1C1-02, V-1C2-02, V-1C3-02 and V-1C4-02, straight from the spec.
std::string g1Sum1c(SerialFormat format) {
    switch (format) {
    case SerialFormat::Format1:
        return "51";
    case SerialFormat::Format2:
        return "B1";
    case SerialFormat::Format3:
        return "DF";
    default:
        return "51";
    }
}

// The same frame from station 01 (one byte +1) or from PC FE (one byte -1): the SUM follows.
std::string g1SumStation01For1c(SerialFormat format) {
    switch (format) {
    case SerialFormat::Format1:
        return "52";
    case SerialFormat::Format2:
        return "B2";
    case SerialFormat::Format3:
        return "E0";
    default:
        return "52";
    }
}

std::string g1SumPcFe1c(SerialFormat format) {
    switch (format) {
    case SerialFormat::Format1:
        return "50";
    case SerialFormat::Format2:
        return "B0";
    case SerialFormat::Format3:
        return "DE";
    default:
        return "50";
    }
}

// A NAK in a format (Format 3: the NN form) with its 2-character error code, default route.
std::string nak1c(SerialFormat format, const std::string& err) {
    switch (format) {
    case SerialFormat::Format1:
        return kNak + k1cRoute + err;
    case SerialFormat::Format2:
        return kNak + "00" + k1cRoute + err;
    case SerialFormat::Format3:
        return kStx + k1cRoute + "NN" + err + kEtx;
    default:
        return kNak + k1cRoute + err + kCrLf;
    }
}

} // namespace

TEST_CASE("1C-01 (Format 1), SZ-01: driven by 1c_f1.vec (A.16 + derived rows)") {
    Counts c = runSerialVectorFile("1c_f1.vec", 33);
    CHECK(c.requests + c.responses + c.errors == 33);
    CHECK(c.skipped == 0);
}

TEST_CASE("1C-01 (Format 2), SZ-01: driven by 1c_f2.vec (A.17 + derived rows)") {
    Counts c = runSerialVectorFile("1c_f2.vec", 38);
    CHECK(c.requests + c.responses + c.errors == 38);
    CHECK(c.skipped == 0);
}

TEST_CASE("1C-01 (Format 3), SZ-01: driven by 1c_f3.vec (A.18 + derived rows)") {
    Counts c = runSerialVectorFile("1c_f3.vec", 36);
    CHECK(c.requests + c.responses + c.errors == 36);
    CHECK(c.skipped == 0);
}

TEST_CASE("1C-01 (Format 4), SZ-01: driven by 1c_f4.vec (A.19 + derived rows)") {
    Counts c = runSerialVectorFile("1c_f4.vec", 32);
    CHECK(c.requests + c.responses + c.errors == 32);
    CHECK(c.skipped == 0);
}

TEST_CASE("1C-02: a NAK is McPlcError(06H) in formats 1, 2 and 4; the error code is 2 hex "
          "characters, lower case accepted, non-hex rejected") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        McProtocol proto(FrameConfig::frame1C(format));

        // V-1C1-08 .. V-1C4-08, written out (format 3 is the NN form: 1C-03).
        std::vector<uint8_t> wire = toWire(nak1c(format, "06"));
        Parser parser = proto.parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Plc);
        CHECK(parser.error().code == ErrorCode::PlcError);
        CHECK(parser.error().plcCode == 0x06);
        CHECK(parser.frameLength() == wire.size());

        std::vector<uint8_t> storage;
        Parser writeParser = proto.parser(writeG4(storage));
        REQUIRE(writeParser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(writeParser.error().plcCode == 0x06);

        std::vector<uint8_t> lower = toWire(nak1c(format, "0a"));
        Parser a = proto.parser(readG1());
        REQUIRE(a.feed(ByteView{lower.data(), lower.size()}) == ParseStatus::Failed);
        CHECK(a.error().plcCode == 0x0A);

        std::vector<uint8_t> bad = toWire(nak1c(format, "G6"));
        Parser b = proto.parser(readG1());
        REQUIRE(b.feed(ByteView{bad.data(), bad.size()}) == ParseStatus::Failed);
        CHECK(b.error().category == ErrorCategory::Protocol);
        CHECK(b.error().code == ErrorCode::InvalidCharacter);
    }
}

TEST_CASE("1C-03: NN (Format 3) is McPlcError(06H); the 2-character code follows NN and ETX ends "
          "the frame") {
    McProtocol proto(FrameConfig::frame1C(SerialFormat::Format3));
    // V-1C3-08: STX 00FF NN 06 ETX, 10 bytes, no SUM.
    std::vector<uint8_t> wire = toWire(kStx + "00FFNN06" + kEtx);
    REQUIRE(wire.size() == 10);

    Parser parser = proto.parser(readG1());
    REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
    CHECK(parser.error().category == ErrorCategory::Plc);
    CHECK(parser.error().code == ErrorCode::PlcError);
    CHECK(parser.error().plcCode == 0x06);
    CHECK(parser.frameLength() == 10);

    std::vector<uint8_t> storage;
    Parser writeParser = proto.parser(writeG4(storage));
    REQUIRE(writeParser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
    CHECK(writeParser.error().plcCode == 0x06);

    // An NN with a 4-character code (the 3C size) is the wrong size for 1C: LengthMismatch.
    std::vector<uint8_t> longCode = toWire(kStx + "00FFNN7151" + kEtx);
    Parser tooLong = proto.parser(readG1());
    REQUIRE(tooLong.feed(ByteView{longCode.data(), longCode.size()}) == ParseStatus::Failed);
    CHECK(tooLong.error().category == ErrorCategory::Protocol);
    CHECK(tooLong.error().code == ErrorCode::LengthMismatch);
}

TEST_CASE("1C-04: the AnA/AnU command set sends QR with a 7-character device (V-1C1-09)") {
    FrameConfig cfg = FrameConfig::frame1C();
    cfg.commandSet = mc::C1CommandSet::AnA;
    McProtocol proto(cfg);
    auto encoded = proto.encode(readG1());
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) ==
          kEnq + "00FFQR0D0001000387");

    // The same request with the ACPU letters is WR with the 5-character device (V-1C1-01).
    auto acpu = McProtocol(FrameConfig::frame1C()).encode(readG1());
    REQUIRE(acpu.hasValue());
    CHECK(std::string(acpu.value().begin(), acpu.value().end()) == kEnq + "00FFWR0D0100032D");

    // AnA writes: JW (bits) and QW (words), device D000100 / M000100 (spec 3.3).
    std::vector<uint8_t> storage;
    auto bits = proto.encode(writeG4(storage));
    REQUIRE(bits.hasValue());
    CHECK(std::string(bits.value().begin(), bits.value().end()).substr(5, 20) ==
          "JW0M0001000811001100");
    const uint8_t words[6] = {0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
    auto wordWrite =
        proto.encode(Request::writeWords(Device{DeviceType::D, 100}, ByteView{words, 6}));
    REQUIRE(wordWrite.hasValue());
    CHECK(std::string(wordWrite.value().begin(), wordWrite.value().end()).substr(5, 24) ==
          "QW0D000100"
          "03"
          "199512021130");
}

TEST_CASE("1C-01: the access route is station then PC, no frame ID (worked out by hand)") {
    // P = "050A". The spec's V-1C1-01 has SUM 2D for P = "00FF". "050A" sums to 0xD6 (214) against
    // 0xEC (236) for "00FF", 0x16 less, so the SUM is 2D - 16 = 17. The response V-1C1-02 (SUM 51)
    // becomes 51 - 16 = 3B.
    FrameConfig cfg = FrameConfig::frame1C();
    cfg.stationNo = 0x05;
    cfg.pc = 0x0A;
    McProtocol proto(cfg);
    Request r = readG1();

    auto encoded = proto.encode(r);
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) ==
          kEnq + "050AWR0D010003" + "17");

    std::vector<uint8_t> good = toWire(g1Response1c(SerialFormat::Format1, "050A", "3B"));
    Parser parser = proto.parser(r);
    CHECK(parser.feed(ByteView{good.data(), good.size()}) == ParseStatus::Done);

    // The same response under the default route is another station's: FrameMismatch.
    Parser other = McProtocol(FrameConfig::frame1C()).parser(r);
    REQUIRE(other.feed(ByteView{good.data(), good.size()}) == ParseStatus::Failed);
    CHECK(other.error().code == ErrorCode::FrameMismatch);
}

TEST_CASE("1C-05: the message wait is the character after the command (CMD-25 as a frame, every "
          "format)") {
    // BR X40 x 5 with message wait 100 ms: request data "BRAX004005". The byte sum of the route
    // "00FF" is 0xEC, of "BR" 0x94, 'A' 0x41, "X0040" 0x11C, "05" 0x65: 0x342, SUM 42 in format 1,
    // 3 and 4. Format 2 adds BLK "00" (0x60): SUM A2; format 3 adds ETX (0x03): SUM 45.
    struct Expect {
        SerialFormat format;
        std::string frame;
    };
    const Expect cases[] = {
        {SerialFormat::Format1, kEnq + "00FFBRAX004005" + "42"},
        {SerialFormat::Format2, kEnq + "00" + "00FFBRAX004005" + "A2"},
        {SerialFormat::Format3, kStx + "00FFBRAX004005" + kEtx + "45"},
        {SerialFormat::Format4, kEnq + "00FFBRAX004005" + "42" + kCrLf},
    };
    for (const Expect& c : cases) {
        INFO("format ", static_cast<int>(c.format));
        FrameConfig cfg = FrameConfig::frame1C(c.format);
        cfg.messageWait = 10;
        McProtocol proto(cfg);
        auto encoded = proto.encode(Request::readBits(Device{DeviceType::X, 0x40}, 5));
        REQUIRE(encoded.hasValue());
        CHECK(std::string(encoded.value().begin(), encoded.value().end()) == c.frame);
    }
}

TEST_CASE("1C-05: a message wait above 15 is InvalidConfig from encode(), not a bad character") {
    FrameConfig cfg = FrameConfig::frame1C();
    cfg.messageWait = 16;
    McProtocol proto(cfg);
    auto encoded = proto.encode(readG1());
    REQUIRE_FALSE(encoded.hasValue());
    CHECK(encoded.error().category == ErrorCategory::Config);
    CHECK(encoded.error().code == ErrorCode::InvalidConfig);
}

TEST_CASE("1C-06: 256 points are written as 00 and the 256-character response is sized") {
    McProtocol proto(FrameConfig::frame1C());
    Request r = Request::readBits(Device{DeviceType::M, 0}, 256);
    // V-1C1-11: ENQ 00FF BR 0 M0000 00 and SUM 1D.
    auto encoded = proto.encode(r);
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) == kEnq + "00FFBR0M0000001D");
    // STX P + 256 data characters + ETX + SUM: 1 + 4 + 256 + 1 + 2.
    CHECK(proto.maxResponseSize(r) == 264);
    CHECK(proto.payloadSize(r) == 256);
}

TEST_CASE("1C-07: WR on a bit device is a word read of X40 (V-1C1-10)") {
    McProtocol proto(FrameConfig::frame1C());
    Request r = Request::readWords(Device{DeviceType::X, 0x40}, 2);
    auto encoded = proto.encode(r);
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) == kEnq + "00FFWR0X00400243");
    // A word read at X41 is not a multiple of 16: DEV-12 (validate) refuses it before any encoding.
    auto bad = proto.encode(Request::readWords(Device{DeviceType::X, 0x41}, 2));
    REQUIRE_FALSE(bad.hasValue());
}

TEST_CASE("1C-08: the manual sum check: station 00, PC FF and BR3M0000 give C0") {
    // The manual abbreviates the request data (no points); with 1 point appended the byte sum grows
    // by '0' + '1' = 0x61, so the frame ENQ 00FF BR 3 M0000 01 has SUM C0 + 61 = 0x121 -> 21.
    FrameConfig cfg = FrameConfig::frame1C();
    cfg.messageWait = 3;
    McProtocol proto(cfg);
    auto encoded = proto.encode(Request::readBits(Device{DeviceType::M, 0}, 1));
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) ==
          kEnq + "00FFBR3M000001" + "21");
}

TEST_CASE("4C-09 applied to 1C: a wrong sum check is SumCheck (every format)") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        // The spec's G1 response with the last SUM character changed: 51 -> 50, B1 -> B0, DF -> DE.
        std::string good = g1Sum1c(format);
        std::string wrong = good.substr(0, 1) + (good[1] == '0' ? "1" : "0");
        std::vector<uint8_t> wire = toWire(g1Response1c(format, k1cRoute, wrong));
        Parser parser = McProtocol(FrameConfig::frame1C(format)).parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::SumCheck);
        CHECK(parser.frameLength() == wire.size());

        // The spec's own SUM is right.
        std::vector<uint8_t> ok = toWire(g1Response1c(format, k1cRoute, good));
        Parser fine = McProtocol(FrameConfig::frame1C(format)).parser(readG1());
        CHECK(fine.feed(ByteView{ok.data(), ok.size()}) == ParseStatus::Done);
    }
}

TEST_CASE(
    "4C-10 applied to 1C: sumCheck off means no SUM in the request and none in the response") {
    // The request of each format without its SUM (V-1C1-01 minus SUM 2D).
    const std::string rd = "WR0D010003";
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        FrameConfig cfg = FrameConfig::frame1C(format);
        cfg.sumCheck = false;
        McProtocol proto(cfg);
        Request r = readG1();

        std::string expected;
        switch (format) {
        case SerialFormat::Format1:
            expected = kEnq + k1cRoute + rd;
            break;
        case SerialFormat::Format2:
            expected = kEnq + "00" + k1cRoute + rd;
            break;
        case SerialFormat::Format3:
            expected = kStx + k1cRoute + rd + kEtx;
            break;
        default:
            expected = kEnq + k1cRoute + rd + kCrLf;
            break;
        }
        auto encoded = proto.encode(r);
        REQUIRE(encoded.hasValue());
        CHECK(std::string(encoded.value().begin(), encoded.value().end()) == expected);
        CHECK(proto.encodedSize(r).value() == expected.size());

        // A response without SUM parses; with the sum check on the same bytes still wait for it.
        std::vector<uint8_t> wire = toWire(g1Response1c(format, k1cRoute, ""));
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        CHECK(parser.frameLength() == wire.size());

        Parser waiting = McProtocol(FrameConfig::frame1C(format)).parser(r);
        CHECK(waiting.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::NeedMore);
    }
}

TEST_CASE("4C-13 applied to 1C: another station or PC is FrameMismatch (SUM recomputed); with "
          "checkRoute off it parses") {
    for (SerialFormat format : kFormats) {
        INFO("format ", static_cast<int>(format));
        // Station 00 -> 01 raises the byte sum by 1; PC FF -> FE lowers it by 1.
        std::vector<uint8_t> otherStation =
            toWire(g1Response1c(format, "01FF", g1SumStation01For1c(format)));
        std::vector<uint8_t> otherPc = toWire(g1Response1c(format, "00FE", g1SumPcFe1c(format)));
        Request r = readG1();

        for (const std::vector<uint8_t>* wire : {&otherStation, &otherPc}) {
            Parser parser = McProtocol(FrameConfig::frame1C(format)).parser(r);
            REQUIRE(parser.feed(ByteView{wire->data(), wire->size()}) == ParseStatus::Failed);
            CHECK(parser.error().code == ErrorCode::FrameMismatch);
            CHECK(parser.frameLength() == wire->size());

            FrameConfig lax = FrameConfig::frame1C(format);
            lax.checkRoute = false;
            Parser acceptant = McProtocol(lax).parser(r);
            CHECK(acceptant.feed(ByteView{wire->data(), wire->size()}) == ParseStatus::Done);
        }
    }
}

TEST_CASE("4C-14 applied to 1C: Format 2 block number from the configuration, checked on the "
          "response") {
    // Block 3A. The spec's V-1C2-01 (block 00) has SUM 8D; "00" -> "3A" raises the byte sum by
    // 0x03 + 0x11 = 0x14, so the request's SUM is A1, and the response V-1C2-02 (SUM B1) gets C5.
    FrameConfig cfg = FrameConfig::frame1C(SerialFormat::Format2);
    cfg.blockNo = 0x3A;
    McProtocol proto(cfg);
    Request r = readG1();

    auto encoded = proto.encode(r);
    REQUIRE(encoded.hasValue());
    CHECK(std::string(encoded.value().begin(), encoded.value().end()) ==
          kEnq + "3A" + k1cRoute + "WR0D010003" + "A1");

    std::vector<uint8_t> same = toWire(kStx + "3A" + k1cRoute + kG1Data + kEtx + "C5");
    Parser ok = proto.parser(r);
    CHECK(ok.feed(ByteView{same.data(), same.size()}) == ParseStatus::Done);

    // Lower case in the echoed block is the same number: 'a' is 0x20 above 'A', SUM C5 -> E5.
    std::vector<uint8_t> lower = toWire(kStx + "3a" + k1cRoute + kG1Data + kEtx + "E5");
    Parser lowerParser = proto.parser(r);
    CHECK(lowerParser.feed(ByteView{lower.data(), lower.size()}) == ParseStatus::Done);

    // Another block (00, the spec's own V-1C2-02) is FrameMismatch; with checkBlockNo off it
    // parses.
    std::vector<uint8_t> other = toWire(g1Response1c(SerialFormat::Format2, k1cRoute, "B1"));
    Parser mismatch = proto.parser(r);
    REQUIRE(mismatch.feed(ByteView{other.data(), other.size()}) == ParseStatus::Failed);
    CHECK(mismatch.error().category == ErrorCategory::Protocol);
    CHECK(mismatch.error().code == ErrorCode::FrameMismatch);
    CHECK(mismatch.frameLength() == other.size());

    FrameConfig lax = cfg;
    lax.checkBlockNo = false;
    Parser acceptant = McProtocol(lax).parser(r);
    CHECK(acceptant.feed(ByteView{other.data(), other.size()}) == ParseStatus::Done);

    // A NAK is checked the same way: its own block first, then the PLC error.
    std::vector<uint8_t> nakOther = toWire(kNak + "00" + k1cRoute + "06");
    Parser nak = proto.parser(r);
    REQUIRE(nak.feed(ByteView{nakOther.data(), nakOther.size()}) == ParseStatus::Failed);
    CHECK(nak.error().code == ErrorCode::FrameMismatch);
    std::vector<uint8_t> nakSame = toWire(kNak + "3A" + k1cRoute + "06");
    Parser nakOk = proto.parser(r);
    REQUIRE(nakOk.feed(ByteView{nakSame.data(), nakSame.size()}) == ParseStatus::Failed);
    CHECK(nakOk.error().code == ErrorCode::PlcError);
    CHECK(nakOk.error().plcCode == 0x06);

    // A non-hex block is InvalidCharacter ('G' is 0x06 above 'A', so the SUM C5 becomes CB; the sum
    // is judged before the block).
    std::vector<uint8_t> bad = toWire(kStx + "3G" + k1cRoute + kG1Data + kEtx + "CB");
    Parser badParser = proto.parser(r);
    REQUIRE(badParser.feed(ByteView{bad.data(), bad.size()}) == ParseStatus::Failed);
    CHECK(badParser.error().code == ErrorCode::InvalidCharacter);
}

TEST_CASE("1C-03, spec 10 Q1: Format 3 short responses (GG / NN) and f3ShortResponseHasSum") {
    // Byte sums worked out by hand: "00FF" = 0xEC, "GG" = 0x8E, "NN" = 0x9C, "06" = 0x66, ETX =
    // 0x03. GG ETX: 0xEC + 0x8E + 0x03 = 0x17D -> SUM 7D. NN 06 ETX: 0xEC + 0x9C + 0x66 + 0x03 =
    // 0x1F1 -> F1.
    FrameConfig plain = FrameConfig::frame1C(SerialFormat::Format3);
    FrameConfig withSum = plain;
    withSum.f3ShortResponseHasSum = true;
    std::vector<uint8_t> storage;
    Request write = writeG4(storage);
    Request read = readG1();

    const std::string gg = kStx + k1cRoute + "GG" + kEtx;
    const std::string nn = kStx + k1cRoute + "NN06" + kEtx;

    SUBCASE("default: the short forms end at ETX and carry no SUM") {
        std::vector<uint8_t> ack = toWire(gg);
        Parser a = McProtocol(plain).parser(write);
        REQUIRE(a.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Done);
        CHECK(a.frameLength() == 8);

        std::vector<uint8_t> nak = toWire(nn);
        Parser n = McProtocol(plain).parser(read);
        REQUIRE(n.feed(ByteView{nak.data(), nak.size()}) == ParseStatus::Failed);
        CHECK(n.error().plcCode == 0x06);
        CHECK(n.frameLength() == 10);
    }
    SUBCASE("default: bytes behind the short form belong to the next frame") {
        std::vector<uint8_t> two = toWire(gg + nn);
        Parser a = McProtocol(plain).parser(write);
        REQUIRE(a.feed(ByteView{two.data(), two.size()}) == ParseStatus::Done);
        CHECK(a.frameLength() == 8);
    }
    SUBCASE("option on: the short forms wait for their SUM and verify it") {
        std::vector<uint8_t> ack = toWire(gg + "7D");
        Parser early = McProtocol(withSum).parser(write);
        CHECK(early.feed(ByteView{ack.data(), ack.size() - 1}) == ParseStatus::NeedMore);
        Parser a = McProtocol(withSum).parser(write);
        REQUIRE(a.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Done);
        CHECK(a.frameLength() == 10);

        std::vector<uint8_t> nak = toWire(nn + "F1");
        Parser n = McProtocol(withSum).parser(read);
        REQUIRE(n.feed(ByteView{nak.data(), nak.size()}) == ParseStatus::Failed);
        CHECK(n.error().category == ErrorCategory::Plc);
        CHECK(n.error().plcCode == 0x06);
        CHECK(n.frameLength() == 12);

        std::vector<uint8_t> wrong = toWire(gg + "7E");
        Parser w = McProtocol(withSum).parser(write);
        REQUIRE(w.feed(ByteView{wrong.data(), wrong.size()}) == ParseStatus::Failed);
        CHECK(w.error().code == ErrorCode::SumCheck);
    }
    SUBCASE("option on but sumCheck off: still no SUM") {
        FrameConfig none = withSum;
        none.sumCheck = false;
        std::vector<uint8_t> ack = toWire(gg);
        Parser a = McProtocol(none).parser(write);
        CHECK(a.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Done);
    }
    SUBCASE("a response with data always carries its SUM, option or not") {
        std::vector<uint8_t> data = toWire(g1Response1c(SerialFormat::Format3, k1cRoute, "DF"));
        for (const FrameConfig& cfg : {plain, withSum}) {
            Parser p = McProtocol(cfg).parser(read);
            CHECK(p.feed(ByteView{data.data(), data.size()}) == ParseStatus::Done);
        }
    }
    SUBCASE("GG with no data answers a write, not a read; GG with data not a write") {
        std::vector<uint8_t> ack = toWire(gg);
        Parser r = McProtocol(plain).parser(read);
        REQUIRE(r.feed(ByteView{ack.data(), ack.size()}) == ParseStatus::Failed);
        CHECK(r.error().code == ErrorCode::LengthMismatch);

        std::vector<uint8_t> data = toWire(g1Response1c(SerialFormat::Format3, k1cRoute, "DF"));
        Parser w = McProtocol(plain).parser(write);
        REQUIRE(w.feed(ByteView{data.data(), data.size()}) == ParseStatus::Failed);
        CHECK(w.error().code == ErrorCode::LengthMismatch);
    }
    SUBCASE("an end code that is neither GG nor NN is FrameMismatch, at ETX") {
        std::vector<uint8_t> bad = toWire(kStx + k1cRoute + "QA" + kEtx + "00");
        Parser p = McProtocol(plain).parser(write);
        REQUIRE(p.feed(ByteView{bad.data(), bad.size()}) == ParseStatus::Failed);
        CHECK(p.error().code == ErrorCode::FrameMismatch);
        CHECK(p.frameLength() == bad.size() - 2);
    }
}

TEST_CASE("4C-15, STR-03 applied to 1C: junk before the frame is skipped and reported") {
    for (SerialFormat format : kFormats) {
        McProtocol proto(FrameConfig::frame1C(format));
        Request r = readG1();
        std::string frame = g1Response1c(format, k1cRoute, g1Sum1c(format));
        for (const std::string& junk :
             {std::string("\x00\xFF", 2), std::string("\x41\x0D\x0A\x05\x00", 5)}) {
            INFO("junk bytes ", junk.size(), ", format ", static_cast<int>(format));
            std::vector<uint8_t> wire = toWire(junk + frame);

            Parser whole = proto.parser(r);
            REQUIRE(whole.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
            CHECK(whole.skipped() == junk.size());
            CHECK(whole.frameLength() == wire.size());
            uint8_t payload[6] = {};
            auto result = whole.payload(ByteView{wire.data(), wire.size()},
                                        MutableByteView{payload, sizeof(payload)});
            REQUIRE(result.hasValue());
            CHECK(std::vector<uint8_t>(payload, payload + 6) ==
                  std::vector<uint8_t>{0x95, 0x19, 0x02, 0x12, 0x30, 0x11});

            Parser streamed = proto.parser(r);
            for (size_t n = 1; n < wire.size(); ++n) {
                REQUIRE(streamed.feed(ByteView{wire.data(), n}) == ParseStatus::NeedMore);
            }
            REQUIRE(streamed.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
            CHECK(streamed.skipped() == junk.size());
        }
    }
}

TEST_CASE("1C parse: response data characters are checked when the payload is decoded") {
    FrameConfig cfg = FrameConfig::frame1C(SerialFormat::Format1);
    cfg.sumCheck = false; // so a hand-edited body needs no new SUM
    McProtocol proto(cfg);

    SUBCASE("a non-hex data character: feed() is Done, payload() is InvalidCharacter") {
        std::vector<uint8_t> wire = toWire(kStx + "00FF" + "1995G2021130" + kEtx);
        Parser parser = proto.parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[6];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Protocol);
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("lower-case hex in the data is accepted (PRIM-05)") {
        std::vector<uint8_t> wire = toWire(kStx + "00FF" + "1a2b3c4d5e6f" + kEtx);
        Parser parser = proto.parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[6];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE(result.hasValue());
        CHECK(std::vector<uint8_t>(out, out + 6) ==
              std::vector<uint8_t>{0x2B, 0x1A, 0x4D, 0x3C, 0x6F, 0x5E});
    }
    SUBCASE("a bit character other than 0 and 1 is InvalidCharacter") {
        Request bits = Request::readBits(Device{DeviceType::M, 100}, 8);
        std::vector<uint8_t> wire = toWire(kStx + "00FF" + "00010211" + kEtx);
        Parser parser = proto.parser(bits);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[8];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("a non-hex character in the route is InvalidCharacter, not FrameMismatch") {
        std::vector<uint8_t> wire = toWire(kStx + "0GFF" + kG1Data + kEtx);
        Parser parser = proto.parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("payload(): BufferTooSmall when out is smaller than payloadSize()") {
        std::vector<uint8_t> wire = toWire(kStx + "00FF" + kG1Data + kEtx);
        Parser parser = proto.parser(readG1());
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[5];
        auto result =
            parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("PackedLsbFirst bit read: ceil(count / 8) bytes from the same frame") {
        Request bits = Request::readBits(Device{DeviceType::M, 100}, 8);
        bits.bitLayout = mc::BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = toWire(kStx + "00FF" + "00010011" + kEtx);
        Parser parser = proto.parser(bits);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[1] = {0xFF};
        auto result = parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, 1});
        REQUIRE(result.hasValue());
        CHECK(out[0] == 0xC8); // points 3, 6, 7 on: 0,0,0,1,0,0,1,1
    }
}

TEST_CASE("1C size bounds, worked out by hand (SZ-01)") {
    std::vector<uint8_t> storage;
    Request read = readG1();                                         // 12 data characters
    Request bits = Request::readBits(Device{DeviceType::M, 100}, 5); // 5 characters
    Request write = writeG4(storage);

    // Format 1: STX P(4) + data + ETX + SUM(2); a write: ACK + P = 5, NAK + P + err(2) = 7.
    McProtocol f1(FrameConfig::frame1C(SerialFormat::Format1));
    CHECK(f1.maxResponseSize(read) == 1 + 4 + 12 + 1 + 2);
    CHECK(f1.maxResponseSize(bits) == 1 + 4 + 5 + 1 + 2);
    CHECK(f1.maxResponseSize(write) == 1 + 4 + 2);
    // Format 4 adds CR LF to each form.
    McProtocol f4(FrameConfig::frame1C(SerialFormat::Format4));
    CHECK(f4.maxResponseSize(read) == 22);
    CHECK(f4.maxResponseSize(write) == 9);
    // Sum check off drops the SUM of a data frame.
    FrameConfig nosum = FrameConfig::frame1C(SerialFormat::Format1);
    nosum.sumCheck = false;
    CHECK(McProtocol(nosum).maxResponseSize(read) == 18);

    // Format 2: STX BLK(2) P(4) data(12) ETX SUM(2) = 22; a write: NAK BLK P err(2) = 9.
    McProtocol f2(FrameConfig::frame1C(SerialFormat::Format2));
    CHECK(f2.maxResponseSize(read) == 22);
    CHECK(f2.maxResponseSize(write) == 9);
    // Format 3: STX P GG(2) data(12) ETX SUM(2) = 22; a write: the NN form STX P NN err(2) ETX = 10
    // against GG ETX = 8, and 2 more each when the short forms carry a SUM.
    FrameConfig f3cfg = FrameConfig::frame1C(SerialFormat::Format3);
    McProtocol f3(f3cfg);
    CHECK(f3.maxResponseSize(read) == 22);
    CHECK(f3.maxResponseSize(write) == 10);
    f3cfg.f3ShortResponseHasSum = true;
    CHECK(McProtocol(f3cfg).maxResponseSize(write) == 12);
    f3cfg.sumCheck = false;
    CHECK(McProtocol(f3cfg).maxResponseSize(write) == 10);

    // Request sizes: ENQ + P(4) + request data (WR 0 D0100 03 = 10) + SUM(2); + CR LF; Format 2
    // adds BLK(2); Format 3 is STX P data ETX SUM. The AnA device field is 2 characters longer
    // (D000100).
    CHECK(f1.encodedSize(read).value() == 1 + 4 + 10 + 2);
    CHECK(f2.encodedSize(read).value() == 19);
    CHECK(f3.encodedSize(read).value() == 18);
    CHECK(f4.encodedSize(read).value() == 19);
    FrameConfig ana = FrameConfig::frame1C(SerialFormat::Format1);
    ana.commandSet = mc::C1CommandSet::AnA;
    CHECK(McProtocol(ana).encodedSize(read).value() == 19);
    // Bit write of 8 points: BW 0 M0100 08 + 8 characters = 18, + ENQ + P + SUM.
    CHECK(f1.encodedSize(write).value() == 1 + 4 + 18 + 2);
}

TEST_CASE("1C encode: BufferTooSmall, and the validate() errors come first") {
    McProtocol proto(FrameConfig::frame1C());
    SUBCASE("BufferTooSmall") {
        uint8_t out[10];
        auto result = proto.encode(readG1(), MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("a zero point count is PointCount") {
        uint8_t out[64];
        auto result = proto.encode(Request::readWords(Device{DeviceType::D, 100}, 0),
                                   MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::PointCount);
    }
    SUBCASE("a device number past the 4-digit ACPU field is refused by validate()") {
        uint8_t out[64];
        auto result = proto.encode(Request::readWords(Device{DeviceType::D, 10000}, 1),
                                   MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
    }
}

TEST_CASE("1C config: Binary code is rejected by validate() and reported as not implemented") {
    FrameConfig cfg = FrameConfig::frame1C();
    cfg.code = mc::DataCode::Binary;
    CHECK_FALSE(cfg.validate().hasValue());
    McProtocol proto(cfg);
    auto size = proto.encodedSize(readG1());
    REQUIRE_FALSE(size.hasValue());
    CHECK(size.error().code == ErrorCode::UnsupportedCommand);
}

namespace {

// A status frame (no data) of a family and format on the access route `route`: a NAK (Format 3:
// the QNAK / NN form) with its error code, or the ACK a write is answered with (Format 3: QACK /
// GG). No SUM: the short forms carry none by default and ACK / NAK frames never do.
std::string statusFrame(bool is1c, SerialFormat format, const std::string& route, bool nak) {
    const std::string err = nak ? (is1c ? "06" : "7151") : "";
    const std::string kind = nak ? (is1c ? "NN" : "QNAK") : (is1c ? "GG" : "QACK");
    switch (format) {
    case SerialFormat::Format1:
        return (nak ? kNak : kAck) + route + err;
    case SerialFormat::Format2:
        return (nak ? kNak : kAck) + "00" + route + err;
    case SerialFormat::Format3:
        return kStx + route + kind + err + kEtx;
    default:
        return (nak ? kNak : kAck) + route + err + kCrLf;
    }
}

} // namespace

TEST_CASE("4C-13 applied to ACK and NAK frames: another station, PC or frame ID is FrameMismatch, "
          "never a PLC error or a success (3C and 1C, every format)") {
    // The route is judged before the frame is read as a status: a NAK or ACK from somewhere else is
    // not this request's answer (leader decision T-047/2). Frames are built from the format table.
    for (bool is1c : {false, true}) {
        for (SerialFormat format : kFormats) {
            for (bool nak : {true, false}) {
                INFO((is1c ? "1C" : "3C"), " format ", static_cast<int>(format),
                     (nak ? " NAK" : " ACK"));
                FrameConfig cfg =
                    is1c ? FrameConfig::frame1C(format) : FrameConfig::frame3C(format);
                std::vector<uint8_t> storage;
                const Request request = nak ? readG1() : writeG4(storage);
                const std::string own = is1c ? "00FF" : "F90000FF00";
                const std::string otherStation = is1c ? "01FF" : "F90100FF00";
                const std::string otherPc = is1c ? "00FE" : "F90000FE00";

                // Control: on the configured route the frame is what it says.
                std::vector<uint8_t> good = toWire(statusFrame(is1c, format, own, nak));
                Parser control = McProtocol(cfg).parser(request);
                if (nak) {
                    REQUIRE(control.feed(ByteView{good.data(), good.size()}) ==
                            ParseStatus::Failed);
                    CHECK(control.error().category == ErrorCategory::Plc);
                    CHECK(control.error().code == ErrorCode::PlcError);
                } else {
                    CHECK(control.feed(ByteView{good.data(), good.size()}) == ParseStatus::Done);
                }

                for (const std::string& route : {otherStation, otherPc}) {
                    std::vector<uint8_t> wire = toWire(statusFrame(is1c, format, route, nak));
                    Parser parser = McProtocol(cfg).parser(request);
                    REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
                    CHECK(parser.error().category == ErrorCategory::Protocol);
                    CHECK(parser.error().code == ErrorCode::FrameMismatch);
                    CHECK(parser.frameLength() == wire.size());

                    // With the route check off the same frame is accepted for what it is.
                    FrameConfig lax = cfg;
                    lax.checkRoute = false;
                    Parser acceptant = McProtocol(lax).parser(request);
                    ParseStatus status = acceptant.feed(ByteView{wire.data(), wire.size()});
                    CHECK(status == (nak ? ParseStatus::Failed : ParseStatus::Done));
                    if (nak) {
                        CHECK(acceptant.error().code == ErrorCode::PlcError);
                    }
                }

                if (!is1c) {
                    // 3C only: the frame ID is checked even with checkRoute off.
                    std::vector<uint8_t> wire =
                        toWire(statusFrame(is1c, format, "F80000FF00", nak));
                    FrameConfig lax = cfg;
                    lax.checkRoute = false;
                    Parser parser = McProtocol(lax).parser(request);
                    REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
                    CHECK(parser.error().code == ErrorCode::FrameMismatch);
                }
            }
        }
    }
}
