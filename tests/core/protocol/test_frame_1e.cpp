// 1E-01..14 (docs/mc_reference/mc-protocol-frame-spec.md section 9.7, both Binary and ASCII) and
// SZ-01, driven by tests/vectors/1e_binary.vec and 1e_ascii.vec (Appendix A.5/A.6 plus the derived
// rows 1E-07, 1E-11, 1E-12 and 1E-13), and standalone TEST_CASEs for what the vectors cannot say:
// how much of a partial or over-long buffer the parser consumes (1E-05, 1E-06), the request-side
// errors (1E-09, 1E-14), invalid characters and the timer byte order. This file uses only the
// public include/mc/core/protocol.h surface, like test_frame_3e.cpp.
#include "doctest/doctest.h"

#include "common/vectors.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using mc::BitLayout;
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
using mc::test::loadVectors;
using mc::test::Vector;

namespace {

std::filesystem::path vectorsRoot() {
    return std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
}

Device parseDeviceField(const std::string& text) {
    auto r = mc::parseDevice(text);
    REQUIRE(r.hasValue());
    return r.value();
}

uint32_t parseHexOr(const std::string& s, uint32_t fallback) {
    return s.empty() ? fallback : static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

std::vector<uint32_t> parseCsvHex(const std::string& s) {
    std::vector<uint32_t> values;
    std::string current;
    auto flush = [&]() {
        if (!current.empty()) {
            values.push_back(static_cast<uint32_t>(std::stoul(current, nullptr, 16)));
            current.clear();
        }
    };
    for (char c : s) {
        if (c == ',') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return values;
}

FrameConfig buildConfig(const Vector& v) {
    mc::DataCode code = (v.field("code") == "Ascii") ? mc::DataCode::Ascii : mc::DataCode::Binary;
    FrameConfig cfg = FrameConfig::frame1E(code);
    cfg.pc = static_cast<uint8_t>(parseHexOr(v.field("pc"), cfg.pc));
    return cfg;
}

// buildRequest()'s caller keeps `writeStorage` alive across the McProtocol call that uses the
// returned Request (Request::data is a non-owning view).
Request buildRequest(const Vector& v, std::vector<uint8_t>& writeStorage) {
    std::string op = v.field("op");
    Device head = parseDeviceField(v.field("device"));
    uint16_t count = static_cast<uint16_t>(std::stoul(v.field("count")));

    if (op == "ReadWords") {
        return Request::readWords(head, count);
    }
    if (op == "ReadBits") {
        return Request::readBits(head, count);
    }
    if (op == "WriteWords") {
        auto values = parseCsvHex(v.field("write"));
        writeStorage.resize(values.size() * 2);
        for (size_t i = 0; i < values.size(); ++i) {
            writeStorage[2 * i] = static_cast<uint8_t>(values[i] & 0xFFu);
            writeStorage[2 * i + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFFu);
        }
        return Request::writeWords(head, ByteView{writeStorage.data(), writeStorage.size()});
    }
    if (op == "WriteBits") {
        auto values = parseCsvHex(v.field("write"));
        writeStorage.resize(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            writeStorage[i] = static_cast<uint8_t>(values[i]);
        }
        return Request::writeBits(head, ByteView{writeStorage.data(), writeStorage.size()});
    }
    FAIL("vector ", v.id, " (", v.file, ") has unrecognized op '", op, "'");
    return Request::readWords(head, count);
}

void checkRequestVector(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);
    McProtocol proto(buildConfig(v));

    auto sizeResult = proto.encodedSize(r);
    REQUIRE(sizeResult.hasValue());
    CHECK(sizeResult.value() == v.bytes.size()); // SZ-01: encodedSize() == vector length.

    std::vector<uint8_t> out(sizeResult.value(), 0xCC);
    auto encodeResult = proto.encode(r, MutableByteView{out.data(), out.size()});
    REQUIRE(encodeResult.hasValue());
    CHECK(encodeResult.value() == v.bytes.size());
    CHECK(out == v.bytes);
}

void checkResponseVector(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);
    McProtocol proto(buildConfig(v));

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

    auto expected = parseCsvHex(v.field("expect"));
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
}

void checkResponseErrorVector(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);
    McProtocol proto(buildConfig(v));

    CHECK(v.bytes.size() <= proto.maxResponseSize(r)); // SZ-01.

    Parser parser = proto.parser(r);
    ByteView wire{v.bytes.data(), v.bytes.size()};
    REQUIRE(parser.feed(wire) == ParseStatus::Failed);

    const mc::Error& err = parser.error();
    std::string errorName = v.field("error");
    if (errorName == "FrameMismatch") {
        CHECK(err.category == ErrorCategory::Protocol);
        CHECK(err.code == ErrorCode::FrameMismatch);
    } else if (errorName == "Plc") {
        CHECK(err.category == ErrorCategory::Plc);
        CHECK(err.code == ErrorCode::PlcError);
        CHECK(err.plcCode == parseHexOr(v.field("plccode"), 0));
        CHECK(err.abnormalCode == parseHexOr(v.field("abnormal"), 0));
        // The whole vector is the frame: 5BH ends after the abnormal code (1E-05), any other end
        // code after itself (1E-06).
        CHECK(parser.frameLength() == v.bytes.size());
    } else {
        FAIL("vector ", v.id, " (", v.file, ") has unrecognized error '", errorName, "'");
    }
}

// Every record of `fileName` except the `v1.1` ones (04H/05H frames, 1E-12) is checked by its
// `kind:`, the same way for Binary and ASCII (buildConfig()/buildRequest() read `code:`).
void runFrame1eVectorFile(const char* fileName, size_t expectedRecords) {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / fileName);
    REQUIRE(vectors.size() == expectedRecords);

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (v.hasTag("v1.1")) {
            continue;
        }
        std::string kind = v.field("kind");
        if (kind == "request") {
            checkRequestVector(v);
        } else if (kind == "response") {
            checkResponseVector(v);
        } else if (kind == "response-error") {
            checkResponseErrorVector(v);
        } else {
            FAIL("vector ", v.id, " (", v.file, ") has unknown kind '", kind, "'");
        }
    }
}

std::vector<uint8_t> toWire(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

Parser parserFor(const FrameConfig& cfg, const Request& r) { return McProtocol(cfg).parser(r); }

} // namespace

TEST_CASE("1E-01, 1E-03..1E-08, 1E-10, 1E-11, 1E-13, SZ-01 (1E Binary): driven by 1e_binary.vec; "
          "1E-12 (v1.1) skipped") {
    // 13 vectors of Appendix A.5, 1E-07, 1E-11, 1E-13 and the two Binary 1E-12 frames.
    runFrame1eVectorFile("1e_binary.vec", 18);
}

TEST_CASE("1E-02, 1E-03..1E-08, 1E-10, 1E-11, 1E-13, SZ-01 (1E ASCII): driven by 1e_ascii.vec; "
          "1E-12 (v1.1) skipped") {
    runFrame1eVectorFile("1e_ascii.vec", 18);
}

TEST_CASE("1E-12 (v1.1): the 04H/05H frames are the 1E header plus CMD-22/23/24/37, and skipped") {
    // Nothing in v1 can encode or parse these (04H/05H are not reachable through Op), so the only
    // things to prove are that the transcribed rows are tagged v1.1 and that each one is exactly
    // "header + the request data of the CMD row it names" (spec 9.7: `04 FF 0A 00` / `05 FF 0A 00`
    // Binary, "04FF000A" / "05FF000A" ASCII).
    std::vector<Vector> cmd = loadVectors(vectorsRoot() / "cmd.vec");
    auto cmdBytes = [&](const std::string& id) {
        for (const auto& v : cmd) {
            if (v.id == id) {
                return v.bytes;
            }
        }
        FAIL("no vector with id ", id);
        return std::vector<uint8_t>{};
    };
    struct Row {
        const char* file;
        const char* id;
        const char* cmdId;
        std::vector<uint8_t> header;
    };
    const std::vector<Row> rows = {
        {"1e_binary.vec", "1E-12a", "CMD-22", {0x04, 0xFF, 0x0A, 0x00}},
        {"1e_binary.vec", "1E-12b", "CMD-24", {0x05, 0xFF, 0x0A, 0x00}},
        {"1e_ascii.vec", "1E-12c", "CMD-23", toWire("04FF000A")},
        {"1e_ascii.vec", "1E-12d", "CMD-37", toWire("05FF000A")},
    };
    for (const Row& row : rows) {
        std::vector<Vector> vectors = loadVectors(vectorsRoot() / row.file);
        bool found = false;
        for (const auto& v : vectors) {
            if (v.id != row.id) {
                continue;
            }
            found = true;
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            CHECK(v.hasTag("v1.1"));
            std::vector<uint8_t> expected = row.header;
            std::vector<uint8_t> data = cmdBytes(row.cmdId);
            expected.insert(expected.end(), data.begin(), data.end());
            CHECK(v.bytes == expected);
            MESSAGE("skipping ", v.id, " (", row.file, "): tagged v1.1, 04H/05H are not in v1");
        }
        CHECK(found);
    }
}

TEST_CASE("1E-05: end code 5BH + abnormal code: the parser consumes exactly 3 bytes / 6 chars") {
    Request r = Request::readWords(Device{DeviceType::D, 100}, 3);

    SUBCASE("Binary: partial input waits for the abnormal code, longer input is not consumed") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        std::vector<uint8_t> wire = {0x81, 0x5B, 0x10, 0xAA, 0xBB};
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), 1}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), 2}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.frameLength() == 3);
        CHECK(parser.error().category == ErrorCategory::Plc);
        CHECK(parser.error().code == ErrorCode::PlcError);
        CHECK(parser.error().plcCode == 0x5B);
        CHECK(parser.error().abnormalCode == 0x10);
    }
    SUBCASE("ASCII: 815B waits, 815B1 waits, 815B10 fails after exactly 6 characters") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Ascii);
        std::vector<uint8_t> wire = toWire("815B10XY");
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), 4}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), 5}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.frameLength() == 6);
        CHECK(parser.error().plcCode == 0x5B);
        CHECK(parser.error().abnormalCode == 0x10);
    }
}

TEST_CASE("1E-06: any other end code fails after exactly 2 bytes / 4 chars and never waits") {
    Request r = Request::readWords(Device{DeviceType::D, 100}, 3);

    SUBCASE("Binary: 81 50 is complete at 2 bytes; the first byte alone waits") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        std::vector<uint8_t> wire = {0x81, 0x50, 0xAA, 0xBB, 0xCC};
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), 1}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), 2}) == ParseStatus::Failed);
        CHECK(parser.frameLength() == 2);
        CHECK(parser.error().category == ErrorCategory::Plc);
        CHECK(parser.error().code == ErrorCode::PlcError);
        CHECK(parser.error().plcCode == 0x50);
        CHECK(parser.error().abnormalCode == 0);
    }
    SUBCASE("Binary: the highest end code is reported as a u8") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        std::vector<uint8_t> wire = {0x81, 0xFF};
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().plcCode == 0xFF);
    }
    SUBCASE("ASCII: 8150 is complete at 4 characters, with more input behind it") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Ascii);
        std::vector<uint8_t> wire = toWire("8150XYZ");
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), 3}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.frameLength() == 4);
        CHECK(parser.error().plcCode == 0x50);
    }
    SUBCASE("ASCII: a lower-case end code is accepted (PRIM-05)") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Ascii);
        std::vector<uint8_t> wire = toWire("815b10");
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().plcCode == 0x5B);
        CHECK(parser.error().abnormalCode == 0x10);
    }
}

TEST_CASE("1E parse: a normal response waits for exactly the computed size, no more") {
    SUBCASE("write response 83 00 is complete at 2 bytes even with trailing bytes") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        uint8_t word[2] = {0x34, 0x12};
        Request w = Request::writeWords(Device{DeviceType::D, 100}, ByteView{word, 2});
        std::vector<uint8_t> wire = {0x83, 0x00, 0x83, 0x00};
        Parser parser = parserFor(cfg, w);
        CHECK(parser.feed(ByteView{wire.data(), 1}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        CHECK(parser.frameLength() == 2);
    }
    SUBCASE("word read Binary: 7 of 8 bytes wait") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        std::vector<uint8_t> wire = {0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), 7}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), 8}) == ParseStatus::Done);
    }
    SUBCASE("odd bit read ASCII: N + 1 characters (dummy included) complete the frame") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Ascii);
        Request r = Request::readBits(Device{DeviceType::M, 100}, 5);
        std::vector<uint8_t> wire = toWire("8000101010"); // V-1E-A-10: 4 + 5 + 1 dummy.
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), 9}) == ParseStatus::NeedMore);
        CHECK(parser.feed(ByteView{wire.data(), 10}) == ParseStatus::Done);
        CHECK(parser.frameLength() == 10);
        CHECK(McProtocol(cfg).maxResponseSize(r) == 10);
    }
}

TEST_CASE("1E parse: invalid characters and a non-hex subheader (ASCII)") {
    FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Ascii);
    Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
    std::string base = "8100199512021130"; // V-1E-A-02, verbatim.

    SUBCASE("bad hex character in the subheader: FrameMismatch, 4 characters examined") {
        std::string corrupted = base;
        corrupted[0] = 'G';
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::FrameMismatch);
        CHECK(parser.frameLength() == 4);
    }
    SUBCASE("bad hex character in the end code: InvalidCharacter") {
        std::string corrupted = base;
        corrupted[3] = 'G';
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("bad hex character in the abnormal code: InvalidCharacter, 6 characters consumed") {
        std::vector<uint8_t> wire = toWire("815B1Z");
        Parser parser = parserFor(cfg, r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
        CHECK(parser.frameLength() == 6);
    }
    SUBCASE("bad hex character in the response data: feed() reaches Done, payload() fails") {
        std::string corrupted = base;
        corrupted[6] = 'G';
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = parserFor(cfg, r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t payloadOut[6];
        auto result = parser.payload(ByteView{wire.data(), wire.size()},
                                      MutableByteView{payloadOut, sizeof(payloadOut)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Protocol);
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("lower-case hex in the response data is accepted (PRIM-05)") {
        std::vector<uint8_t> wire = toWire("81001a2b3c4d5e6f");
        Parser parser = parserFor(cfg, r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t payloadOut[6];
        auto result = parser.payload(ByteView{wire.data(), wire.size()},
                                      MutableByteView{payloadOut, sizeof(payloadOut)});
        REQUIRE(result.hasValue());
        CHECK(std::vector<uint8_t>(payloadOut, payloadOut + 6) ==
              std::vector<uint8_t>{0x2B, 0x1A, 0x4D, 0x3C, 0x6F, 0x5E});
    }
}

TEST_CASE("1E parse: an invalid bit character in a bit read fails in payload()") {
    // CMDD-18's 1E twin, one layer up: the frame itself is the right size, so feed() is Done.
    FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
    Request r = Request::readBits(Device{DeviceType::M, 100}, 2);
    std::vector<uint8_t> wire = {0x80, 0x00, 0x21};
    Parser parser = parserFor(cfg, r);
    REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
    uint8_t payloadOut[2];
    auto result = parser.payload(ByteView{wire.data(), wire.size()},
                                  MutableByteView{payloadOut, sizeof(payloadOut)});
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().category == ErrorCategory::Protocol);
    CHECK(result.error().code == ErrorCode::InvalidCharacter);
}

TEST_CASE("1E-09, 1E-14: request-side errors are the validate() errors, before any byte") {
    auto expectEncodeError = [](const Request& r, ErrorCode expected) {
        for (mc::DataCode code : {mc::DataCode::Binary, mc::DataCode::Ascii}) {
            INFO("code ", code == mc::DataCode::Binary ? "Binary" : "Ascii");
            McProtocol proto(FrameConfig::frame1E(code));
            uint8_t out[64];
            auto sizeResult = proto.encodedSize(r);
            REQUIRE_FALSE(sizeResult.hasValue());
            CHECK(sizeResult.error().code == expected);
            auto encodeResult = proto.encode(r, MutableByteView{out, sizeof(out)});
            REQUIRE_FALSE(encodeResult.hasValue());
            CHECK(encodeResult.error().code == expected);
        }
    };

    SUBCASE("1E-09: 257 points is PointCount") {
        expectEncodeError(Request::readWords(Device{DeviceType::D, 0}, 257),
                          ErrorCode::PointCount);
    }
    SUBCASE("1E-14: a word read of X41 (not a multiple of 16) is InvalidDevice") {
        expectEncodeError(Request::readWords(Device{DeviceType::X, 0x41}, 1),
                          ErrorCode::InvalidDevice);
    }
}

TEST_CASE("1E request head: monitoring timer byte order and PC No. come from the FrameConfig") {
    Request r = Request::readWords(Device{DeviceType::D, 100}, 3);

    SUBCASE("Binary: timer 0x1234 is LE 2 (34 12), PC 03") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        cfg.monitoringTimer = 0x1234;
        cfg.pc = 0x03;
        auto encoded = McProtocol(cfg).encode(r);
        REQUIRE(encoded.hasValue());
        std::vector<uint8_t> head(encoded.value().begin(), encoded.value().begin() + 4);
        CHECK(head == std::vector<uint8_t>{0x01, 0x03, 0x34, 0x12});
    }
    SUBCASE("ASCII: timer 0x1234 is 4 characters \"1234\", PC 03") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Ascii);
        cfg.monitoringTimer = 0x1234;
        cfg.pc = 0x03;
        auto encoded = McProtocol(cfg).encode(r);
        REQUIRE(encoded.hasValue());
        std::string head(encoded.value().begin(), encoded.value().begin() + 8);
        CHECK(head == "01031234");
    }
    SUBCASE("the default timer is 000AH (FrameConfig::frame1E)") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        auto encoded = McProtocol(cfg).encode(r);
        REQUIRE(encoded.hasValue());
        CHECK(encoded.value()[2] == 0x0A);
        CHECK(encoded.value()[3] == 0x00);
    }
}

TEST_CASE("1E McProtocol: encode() BufferTooSmall, e1AliasLS, PackedLsbFirst") {
    SUBCASE("BufferTooSmall when out is smaller than encodedSize()") {
        McProtocol proto(FrameConfig::frame1E(mc::DataCode::Binary));
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        uint8_t out[3];
        auto result = proto.encode(r, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("L is rejected by default and encodes as M once e1AliasLS is set") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        Request l = Request::readBits(Device{DeviceType::L, 100}, 8);
        Request m = Request::readBits(Device{DeviceType::M, 100}, 8);

        auto rejected = McProtocol(cfg).encode(l);
        REQUIRE_FALSE(rejected.hasValue());
        CHECK(rejected.error().code == ErrorCode::InvalidDevice);

        cfg.e1AliasLS = true;
        auto aliased = McProtocol(cfg).encode(l);
        auto plain = McProtocol(cfg).encode(m);
        REQUIRE(aliased.hasValue());
        REQUIRE(plain.hasValue());
        CHECK(aliased.value() == plain.value());
    }
    SUBCASE("PackedLsbFirst bit read: payload is ceil(count / 8) bytes, from the same wire bytes") {
        FrameConfig cfg = FrameConfig::frame1E(mc::DataCode::Binary);
        Request r = Request::readBits(Device{DeviceType::M, 100}, 5);
        r.bitLayout = BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = {0x80, 0x00, 0x10, 0x10, 0x10}; // V-1E-B-10.
        McProtocol proto(cfg);
        CHECK(proto.payloadSize(r) == 1);
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
        uint8_t out[1] = {0xFF};
        auto result = parser.payload(ByteView{wire.data(), wire.size()}, MutableByteView{out, 1});
        REQUIRE(result.hasValue());
        CHECK(out[0] == 0x15); // points 0, 2, 4 ON.
    }
}
