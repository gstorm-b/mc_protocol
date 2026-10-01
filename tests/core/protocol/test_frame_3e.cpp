// 3E-01..13 (docs/mc_reference/mc-protocol-frame-spec.md section 9.6, both Binary and ASCII) and
// SZ-01, driven by tests/vectors/3e_binary.vec and 3e_ascii.vec, plus standalone TEST_CASEs for
// checkRoute (spec section 5.1's optional route check; no 3E-xx ID names it), UnsupportedCommand
// (frame families this module does not implement yet), and the 4E vectors (A.3/A.4, tagged v2
// and skipped). This file uses only the public include/mc/core/protocol.h surface -- no PRIVATE
// src/ include path needed, unlike test_primitives.cpp/test_device_encode.cpp/test_commands.cpp.
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
using mc::ErrorCategory;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::McProtocol;
using mc::MutableByteView;
using mc::Parser;
using mc::ParseStatus;
using mc::PlcSeries;
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
    FrameConfig cfg = FrameConfig::frame3E(code);
    cfg.series = (v.field("series") == "IqR") ? PlcSeries::IqR : PlcSeries::QL;
    cfg.network = static_cast<uint8_t>(parseHexOr(v.field("network"), cfg.network));
    cfg.pc = static_cast<uint8_t>(parseHexOr(v.field("pc"), cfg.pc));
    cfg.io = static_cast<uint16_t>(parseHexOr(v.field("io"), cfg.io));
    cfg.station = static_cast<uint8_t>(parseHexOr(v.field("station"), cfg.station));
    return cfg;
}

// buildRequest()'s caller keeps `writeStorage` alive across the McProtocol call that uses the
// returned Request (Request::data is a non-owning view) -- same pattern as T-016/17's own
// command-level tests.
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
    FrameConfig cfg = buildConfig(v);
    McProtocol proto(cfg);

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
    FrameConfig cfg = buildConfig(v);
    McProtocol proto(cfg);

    CHECK(v.bytes.size() <= proto.maxResponseSize(r)); // SZ-01.

    Parser parser = proto.parser(r);
    ByteView wire{v.bytes.data(), v.bytes.size()};
    ParseStatus status = parser.feed(wire);
    REQUIRE(status == ParseStatus::Done);
    CHECK(parser.frameLength() == v.bytes.size());
    CHECK(parser.skipped() == 0);

    size_t payloadSize = proto.payloadSize(r);
    std::vector<uint8_t> payloadOut(payloadSize, 0xFF);
    auto payloadResult =
        parser.payload(wire, MutableByteView{payloadOut.data(), payloadOut.size()});
    REQUIRE(payloadResult.hasValue());
    CHECK(payloadResult.value() == payloadSize); // SZ-01: payload length == payloadSize().

    auto expected = parseCsvHex(v.field("expect"));
    if (r.isBitOp()) {
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
    FrameConfig cfg = buildConfig(v);
    McProtocol proto(cfg);

    CHECK(v.bytes.size() <= proto.maxResponseSize(r)); // SZ-01.

    Parser parser = proto.parser(r);
    ByteView wire{v.bytes.data(), v.bytes.size()};
    ParseStatus status = parser.feed(wire);
    REQUIRE(status == ParseStatus::Failed);

    const mc::Error& err = parser.error();
    std::string errorName = v.field("error");
    if (errorName == "FrameMismatch") {
        CHECK(err.category == ErrorCategory::Protocol);
        CHECK(err.code == ErrorCode::FrameMismatch);
    } else if (errorName == "LengthMismatch") {
        CHECK(err.category == ErrorCategory::Protocol);
        CHECK(err.code == ErrorCode::LengthMismatch);
    } else if (errorName == "Plc") {
        CHECK(err.category == ErrorCategory::Plc);
        CHECK(err.code == ErrorCode::PlcError);
        CHECK(err.plcCode == parseHexOr(v.field("plccode"), 0));

        auto info = parseCsvHex(v.field("errorinfo"));
        if (info.empty()) {
            CHECK(err.info.network == 0);
            CHECK(err.info.pc == 0);
            CHECK(err.info.io == 0);
            CHECK(err.info.station == 0);
            CHECK(err.info.command == 0);
            CHECK(err.info.subcommand == 0);
        } else {
            REQUIRE(info.size() == 6);
            CHECK(err.info.network == info[0]);
            CHECK(err.info.pc == info[1]);
            CHECK(err.info.io == info[2]);
            CHECK(err.info.station == info[3]);
            CHECK(err.info.command == info[4]);
            CHECK(err.info.subcommand == info[5]);
        }
    } else {
        FAIL("vector ", v.id, " (", v.file, ") has unrecognized error '", errorName, "'");
    }
}

// Shared by both the Binary and ASCII TEST_CASEs below: every non-`v1.1` record in `fileName`
// is checked by request/response(-error) kind, exactly the same way regardless of `code:`
// (buildRequest()/buildConfig() already read that field).
void runFrame3eVectorFile(const char* fileName) {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / fileName);
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (v.hasTag("v1.1")) {
            continue; // The 0403 command wrapped in 3E: transcribed for VEC-02 only.
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

} // namespace

TEST_CASE("3E-01..13, SZ-01 (3E Binary): driven by 3e_binary.vec; V-3E-B-13 (v1.1) skipped") {
    runFrame3eVectorFile("3e_binary.vec");
}

TEST_CASE("3E-01..13, SZ-01 (3E ASCII): driven by 3e_ascii.vec; V-3E-A-13 (v1.1) skipped") {
    runFrame3eVectorFile("3e_ascii.vec");
}

TEST_CASE("3E checkRoute: a response route that disagrees with the request is FrameMismatch") {
    // No 3E-xx ID names this (spec section 5.1's route check is optional, off by default,
    // per-frame like every other FrameConfig field); proved directly since checkRoute is a real,
    // reachable branch of frame3eTryParse(), not otherwise exercised by any 3e_binary.vec record.
    FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
    cfg.checkRoute = true;
    McProtocol proto(cfg);
    Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3);

    SUBCASE("matching route: Done") {
        Parser parser = proto.parser(r);
        std::vector<uint8_t> wire = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08,
                                      0x00, 0x00, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);
    }
    SUBCASE("mismatched route (network 01 instead of 00): FrameMismatch") {
        Parser parser = proto.parser(r);
        std::vector<uint8_t> wire = {0xD0, 0x00, 0x01, 0xFF, 0xFF, 0x03, 0x00, 0x08,
                                      0x00, 0x00, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::FrameMismatch);
        CHECK(parser.frameLength() == 9); // header size: route mismatch found there.
    }
}

TEST_CASE("3E UnsupportedCommand: frame types/codes not implemented yet never UB") {
    SUBCASE("F4E is not implemented") {
        FrameConfig cfg;
        cfg.frame = mc::FrameType::F4E; // reserved for v2: no named constructor
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 1);

        auto sizeResult = proto.encodedSize(r);
        REQUIRE_FALSE(sizeResult.hasValue());
        CHECK(sizeResult.error().code == ErrorCode::UnsupportedCommand);

        CHECK(proto.maxResponseSize(r) == 0);

        Parser parser = proto.parser(r);
        uint8_t dummy[1] = {0};
        CHECK(parser.feed(ByteView{dummy, 0}) == ParseStatus::Failed);
        CHECK(parser.error().code == ErrorCode::UnsupportedCommand);
    }
    SUBCASE("F4E encode() is not implemented either") {
        // 3E ASCII used to be the example here (T-017), then 3C, then 1C; every v1 frame family is
        // implemented now, so this SUBCASE moved to 4E (reserved for v2) to keep proving the same
        // point through encode() rather than encodedSize().
        FrameConfig cfg;
        cfg.frame = mc::FrameType::F4E;
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 1);

        auto encodeResult = proto.encode(r, MutableByteView{nullptr, 0});
        REQUIRE_FALSE(encodeResult.hasValue());
        CHECK(encodeResult.error().code == ErrorCode::UnsupportedCommand);
    }
    SUBCASE("payloadSize() is frame-independent: still correct for an unimplemented frame") {
        FrameConfig cfg;
        cfg.frame = mc::FrameType::F4E;
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3);
        CHECK(proto.payloadSize(r) == 6);
    }
}

TEST_CASE("4E vectors: A.3/A.4 exist, are tagged v2, and are reported as skipped") {
    // 4E has no frame_4e.h/.cpp of its own (FrameConfig::validate() already rejects F4E as
    // "reserved for v2") -- nothing in this module can encode or parse these vectors yet, so the
    // only property to check is that every one of them is actually tagged `v2` (VEC-02, T-013,
    // already covers their `bytes:` field automatically). MESSAGE (unlike INFO) prints
    // unconditionally, so a test run's own log is the "report" a human or CI reads.
    for (const char* fileName : {"4e_binary.vec", "4e_ascii.vec"}) {
        std::vector<Vector> vectors = loadVectors(vectorsRoot() / fileName);
        REQUIRE_FALSE(vectors.empty());
        for (const auto& v : vectors) {
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            CHECK(v.hasTag("v2"));
            MESSAGE("skipping ", v.id, " (", fileName, "): tagged v2, 4E is reserved for v2");
        }
    }
}

TEST_CASE("3E Error mapping (T-019 criterion 3): every row 3E can produce") {
    // The module spec's own "Error mapping" table rows 3E can ever reach: Plc/PlcError (V-3E-B/
    // A-10, 3E-11/3E-11a above), Protocol/FrameMismatch (3E-08/3E-08a above, and "3E checkRoute"'s
    // mismatched-route SUBCASE), Protocol/LengthMismatch (3E-09/10, 3E-09a/10a above). The one row
    // with no existing coverage anywhere in this task's own vector-driven tests is
    // Protocol/InvalidCharacter: no 3e_ascii.vec record has a malformed hex character (every one
    // of them is a faithful, valid transcription), so it needs its own direct proof -- one
    // corrupted character at a time, starting from V-3E-A-02's own already-proven-correct bytes.
    FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Ascii);
    McProtocol proto(cfg);
    Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3);
    std::string base = "D00000FF03FF0000100000199512021130"; // V-3E-A-02, verbatim.

    auto toWire = [](std::string text) { return std::vector<uint8_t>(text.begin(), text.end()); };

    SUBCASE("bad hex character in the route (network field): feed() fails immediately") {
        std::string corrupted = base;
        corrupted[4] = 'G'; // network field is characters [4, 6).
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = proto.parser(r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("bad hex character in the length field: feed() fails immediately") {
        std::string corrupted = base;
        corrupted[14] = 'G'; // length field is characters [14, 18).
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = proto.parser(r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("bad hex character in the end code field: feed() fails immediately") {
        std::string corrupted = base;
        corrupted[18] = 'G'; // end code field is characters [18, 22).
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = proto.parser(r);
        CHECK(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Failed);
        CHECK(parser.error().category == ErrorCategory::Protocol);
        CHECK(parser.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("bad hex character in the response data: feed() still reaches Done (only the "
            "length is checked there); payload() is what fails") {
        std::string corrupted = base;
        corrupted[22] = 'G'; // response data starts at character 22 (after the end code).
        std::vector<uint8_t> wire = toWire(corrupted);
        Parser parser = proto.parser(r);
        REQUIRE(parser.feed(ByteView{wire.data(), wire.size()}) == ParseStatus::Done);

        uint8_t payloadOut[6];
        auto payloadResult = parser.payload(ByteView{wire.data(), wire.size()},
                                             MutableByteView{payloadOut, sizeof(payloadOut)});
        REQUIRE_FALSE(payloadResult.hasValue());
        CHECK(payloadResult.error().category == ErrorCategory::Protocol);
        CHECK(payloadResult.error().code == ErrorCode::InvalidCharacter);
    }
}

TEST_CASE("McProtocol/Parser: Checkpoint B coverage gaps (T-019) beyond the Error mapping table") {
    SUBCASE("config() returns what the constructor was given") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Ascii);
        cfg.network = 7;
        McProtocol proto(cfg);
        CHECK(proto.config().network == 7);
        CHECK(proto.config().code == mc::DataCode::Ascii);
    }
    SUBCASE("encode(r, out): BufferTooSmall when out is smaller than encodedSize(r)") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3);
        uint8_t out[1];
        auto result = proto.encode(r, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("encode()/encodedSize(): validate()'s own error propagates (count == 0)") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 0); // validate() rule 1.

        auto sizeResult = proto.encodedSize(r);
        REQUIRE_FALSE(sizeResult.hasValue());
        CHECK(sizeResult.error().code == ErrorCode::PointCount);

        uint8_t out[64];
        auto encodeResult = proto.encode(r, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(encodeResult.hasValue());
        CHECK(encodeResult.error().code == ErrorCode::PointCount);
    }
    SUBCASE("encode(r): the allocating convenience overload") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3);

        auto result = proto.encode(r);
        REQUIRE(result.hasValue());
        std::vector<uint8_t> expected = {0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00,
                                          0x10, 0x00, 0x01, 0x04, 0x00, 0x00, 0x64, 0x00, 0x00,
                                          0xA8, 0x03, 0x00};
        CHECK(result.value() == expected);
    }
    SUBCASE("feed() is idempotent after Done: a second call returns the same status without "
            "re-examining buffer") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3);
        std::vector<uint8_t> wire = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08,
                                      0x00, 0x00, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        Parser parser = proto.parser(r);
        ByteView view{wire.data(), wire.size()};
        REQUIRE(parser.feed(view) == ParseStatus::Done);
        CHECK(parser.feed(view) == ParseStatus::Done);
        CHECK(parser.feed(ByteView{wire.data(), 1}) == ParseStatus::Done); // buffer size ignored.
    }
    SUBCASE("payload(): UnsupportedCommand for a frame this module does not implement") {
        FrameConfig cfg;
        cfg.frame = mc::FrameType::F4E;
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 1);
        Parser parser = proto.parser(r);
        uint8_t out[16];
        auto result = parser.payload(ByteView{nullptr, 0}, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::UnsupportedCommand);
    }
    SUBCASE("payload(): BufferTooSmall when out is smaller than payloadSize(r)") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
        McProtocol proto(cfg);
        Request r = Request::readWords(Device{mc::DeviceType::D, 100}, 3); // payloadSize == 6.
        std::vector<uint8_t> wire = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08,
                                      0x00, 0x00, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        Parser parser = proto.parser(r);
        ByteView view{wire.data(), wire.size()};
        REQUIRE(parser.feed(view) == ParseStatus::Done);
        uint8_t out[5];
        auto result = parser.payload(view, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("payloadSize(): BitLayout::PackedLsbFirst branch") {
        FrameConfig cfg = FrameConfig::frame3E(mc::DataCode::Binary);
        McProtocol proto(cfg);
        Request r = Request::readBits(Device{mc::DeviceType::M, 100}, 5);
        r.bitLayout = BitLayout::PackedLsbFirst;
        CHECK(proto.payloadSize(r) == 1); // ceil(5 / 8).
    }
}
