// VEC-01 / VEC-02 (SPEC-core-protocol.md "Golden vector files", T-013): the `.vec` loader
// itself, and the transcription guard that runs over every checked-in vector file; VEC-RT (T-050):
// every enabled vector record of every v1 family (the Appendix A rows and the derived rows)
// round-trips through McProtocol and Parser, the tagged ones are present and skipped, counted per
// file.
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "serial_vector_config.h"

#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef MC_TESTS_SOURCE_DIR
#error "MC_TESTS_SOURCE_DIR not defined; see tests/CMakeLists.txt (mc_core_protocol_tests)"
#endif

using mc::test::loadAllVectors;
using mc::test::loadVectors;
using mc::test::parseVectors;
using mc::test::Vector;
using mc::test::VecFormatError;

namespace {

// tests/vectors, resolved from the source tree rather than the test binary's own working
// directory: ctest runs mc_core_protocol_tests from its own build tree, and the MSVC
// (build/cmake-debug) and MinGW (build/cmake-mingw) trees sit at different depths under the
// repository root, so a relative "../../tests/vectors" would not be portable between them.
std::filesystem::path vectorsRoot() {
    return std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
}

const Vector& findById(const std::vector<Vector>& vectors, std::string_view id) {
    for (const auto& v : vectors) {
        if (v.id == id) {
            return v;
        }
    }
    FAIL("no vector with id ", id);
    static Vector dummy;
    return dummy;
}

} // namespace

TEST_CASE("VEC-01: sample.vec uses every syntax element and loads to the expected records") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "sample.vec");
    REQUIRE(vectors.size() == 5);

    const Vector& s1 = findById(vectors, "SAMPLE-01");
    CHECK(s1.field("frame") == "3E");
    CHECK(s1.field("code") == "Binary");
    CHECK(s1.field("op") == "ReadWords");
    CHECK(s1.field("device") == "D100");
    CHECK(s1.field("count") == "3");
    CHECK(s1.field("kind") == "request");
    CHECK(s1.field("bytes") == "21");
    REQUIRE(s1.bytes.size() == 21);
    CHECK(s1.bytes.front() == 0x50);
    CHECK(s1.bytes.back() == 0x00);
    CHECK_FALSE(s1.hasText);

    const Vector& s2 = findById(vectors, "SAMPLE-02");
    CHECK(s2.field("kind") == "response");
    CHECK(s2.field("of") == "SAMPLE-01");
    CHECK(s2.field("expect") == "words 1995 1202 1130");
    REQUIRE(s2.bytes.size() == 17);
    CHECK(s2.bytes.front() == 0xD0);

    const Vector& s3 = findById(vectors, "SAMPLE-03");
    CHECK(s3.field("op") == "WriteWords");
    CHECK(s3.field("write") == "1995 1202 1130");
    REQUIRE(s3.bytes.size() == 27);

    const Vector& s4 = findById(vectors, "SAMPLE-04");
    CHECK(s4.field("frame") == "3C");
    CHECK(s4.field("format") == "F1");
    CHECK(s4.hasTag("v2"));
    REQUIRE(s4.bytes.size() == 39);
    CHECK(s4.hasText);
    CHECK(s4.text == "<ENQ>F80000FF03FF000004010000D*000100000350");

    const Vector& s5 = findById(vectors, "SAMPLE-05");
    CHECK(s5.field("of") == "SAMPLE-04");
    CHECK(s5.hasTag("v2"));
    REQUIRE(s5.bytes.size() == 32);
    CHECK(s5.hasText);
    CHECK(s5.text == "<STX>F80000FF03FF0000199512021130<ETX>DE");
}

TEST_CASE("VEC-01 negative: odd hex digit count fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# id: BAD-01\n"
                     "50 00 0\n",
                     "bad_odd_hex.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_odd_hex.vec:2") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-01 negative: unknown control name fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# id: BAD-02\n"
                     "02 46\n"
                     "<XYZ>F\n",
                     "bad_unknown_control.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_unknown_control.vec:3") != std::string::npos);
        CHECK(what.find("XYZ") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-01 negative: a hex line with no preceding id fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# kind: request\n"
                     "50 00\n",
                     "bad_no_id.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_no_id.vec:2") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-01 negative: a malformed bytes: value fails with file and line") {
    bool threw = false;
    try {
        parseVectors("# id: BAD-04\n"
                     "# bytes: abc\n"
                     "50 00\n",
                     "bad_bytes_value.vec");
    } catch (const VecFormatError& e) {
        threw = true;
        std::string what = e.what();
        CHECK(what.find("bad_bytes_value.vec:2") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("VEC-02 transcription guard: every vector's hex byte count matches its bytes: field") {
    std::vector<Vector> vectors = loadAllVectors(vectorsRoot());
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        std::string bytesField = v.field("bytes");
        REQUIRE_FALSE(bytesField.empty());
        size_t expected = static_cast<size_t>(std::stoul(bytesField));
        CHECK(v.bytes.size() == expected);
    }
}

namespace {

// One vector record through McProtocol: a request must be reproduced byte for byte, a response
// must reach Done with the stated payload, an error response must fail with the stated error.
// Returns false (with the reason in `why`) instead of asserting, so the caller can count honestly.
bool roundTrips(const Vector& v, std::string& why) {
    std::vector<uint8_t> writeStorage;
    mc::Request r = mc::test::serialRequestFromVector(v, writeStorage);
    mc::McProtocol proto(mc::test::frameConfigFromVector(v));
    const mc::ByteView wire{v.bytes.data(), v.bytes.size()};
    const std::string kind = v.field("kind");

    if (kind == "request") {
        auto size = proto.encodedSize(r);
        if (!size.hasValue() || size.value() != v.bytes.size()) {
            why = "encodedSize() differs from the vector length";
            return false;
        }
        std::vector<uint8_t> out(size.value(), 0xCC);
        auto result = proto.encode(r, mc::MutableByteView{out.data(), out.size()});
        if (!result.hasValue() || result.value() != v.bytes.size() || out != v.bytes) {
            why = "encode() does not reproduce the vector";
            return false;
        }
        return true;
    }

    mc::Parser parser = proto.parser(r);
    const mc::ParseStatus status = parser.feed(wire);
    if (kind == "response") {
        if (status != mc::ParseStatus::Done || parser.frameLength() != v.bytes.size()) {
            why = "the response does not reach Done over exactly its bytes";
            return false;
        }
        std::vector<uint8_t> payload(proto.payloadSize(r), 0xFF);
        auto decoded = parser.payload(wire, mc::MutableByteView{payload.data(), payload.size()});
        if (!decoded.hasValue() || decoded.value() != payload.size()) {
            why = "payload() fails or has the wrong size";
            return false;
        }
        const std::vector<uint32_t> expect = mc::test::csvHex(v.field("expect"));
        std::vector<uint8_t> want;
        if (r.isWrite()) {
            // no payload
        } else if (r.isBitOp()) {
            for (uint32_t bit : expect) {
                want.push_back(static_cast<uint8_t>(bit));
            }
        } else {
            for (uint32_t word : expect) {
                want.push_back(static_cast<uint8_t>(word & 0xFFu));
                want.push_back(static_cast<uint8_t>(word >> 8));
            }
        }
        if (payload != want) {
            why = "the decoded payload differs from `expect:`";
            return false;
        }
        return true;
    }

    if (kind != "response-error" || status != mc::ParseStatus::Failed) {
        why = "an error response does not fail";
        return false;
    }
    const std::string name = v.field("error");
    const mc::Error& e = parser.error();
    if (name == "Plc") {
        const bool ok =
            e.category == mc::ErrorCategory::Plc && e.code == mc::ErrorCode::PlcError &&
            static_cast<unsigned long>(e.plcCode) == std::stoul(v.field("plccode"), nullptr, 16);
        if (!ok) {
            why = "the PLC error code differs from `plccode:`";
        }
        return ok;
    }
    const mc::ErrorCode code = name == "FrameMismatch"      ? mc::ErrorCode::FrameMismatch
                               : name == "LengthMismatch"   ? mc::ErrorCode::LengthMismatch
                               : name == "SumCheck"         ? mc::ErrorCode::SumCheck
                               : name == "InvalidCharacter" ? mc::ErrorCode::InvalidCharacter
                                                            : mc::ErrorCode::PlcError;
    if (e.category != mc::ErrorCategory::Protocol || e.code != code) {
        why = "the protocol error differs from `error:`";
        return false;
    }
    return true;
}

} // namespace

TEST_CASE("VEC-RT: every enabled vector record of every v1 family (Appendix A rows and derived "
          "rows) round-trips; the tagged v1.1 / v2 ones are present and skipped") {
    struct File {
        const char* name;
        bool v1; ///< a v1 family: its untagged records must round-trip
    };
    // A.1, A.2 (3E), A.5, A.6 (1E), A.12-A.15 (3C), A.16-A.19 (1C); A.3, A.4 (4E) and A.7-A.11 (4C)
    // are transcribed for v2.
    const File files[] = {
        {"3e_binary.vec", true},  {"3e_ascii.vec", true},  {"1e_binary.vec", true},
        {"1e_ascii.vec", true},   {"3c_f1.vec", true},     {"3c_f2.vec", true},
        {"3c_f3.vec", true},      {"3c_f4.vec", true},     {"1c_f1.vec", true},
        {"1c_f2.vec", true},      {"1c_f3.vec", true},     {"1c_f4.vec", true},
        {"4e_binary.vec", false}, {"4e_ascii.vec", false}, {"4c_f1.vec", false},
        {"4c_f2.vec", false},     {"4c_f3.vec", false},    {"4c_f4.vec", false},
        {"4c_f5.vec", false}};

    size_t totalEnabled = 0;
    size_t totalTagged = 0;
    size_t totalRoundTripped = 0;
    for (const File& f : files) {
        std::vector<Vector> vectors = loadVectors(vectorsRoot() / f.name);
        REQUIRE_FALSE(vectors.empty());
        size_t enabled = 0;
        size_t tagged = 0;
        size_t roundTripped = 0;
        for (const Vector& v : vectors) {
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            if (v.hasTag("v1.1") || v.hasTag("v2")) {
                ++tagged;
                continue;
            }
            ++enabled;
            std::string why;
            const bool ok = roundTrips(v, why);
            CHECK_MESSAGE(ok, why);
            roundTripped += ok ? 1 : 0;
        }
        if (f.v1) {
            CHECK(roundTripped == enabled);
        } else {
            // 4E and 4C: every record is tagged v2; none is run.
            CHECK(enabled == 0);
            CHECK(tagged == vectors.size());
        }
        MESSAGE("VEC-RT ", std::string(f.name), ": ", vectors.size(), " records, ", enabled,
                " enabled, ", tagged, " tagged and skipped, ", roundTripped, " round-tripped");
        totalEnabled += enabled;
        totalTagged += tagged;
        totalRoundTripped += roundTripped;
    }
    MESSAGE("VEC-RT total: ", totalEnabled, " enabled, ", totalTagged, " tagged and skipped, ",
            totalRoundTripped, " round-tripped");
    CHECK(totalRoundTripped == totalEnabled);
}

// XYN-13: tests/vectors/fx_xy.vec, hand-typed from the FX manuals, round-trips like the
// Appendix A files: every request is reproduced byte for byte under the record's `xy:` / `xyascii:`
// keys, every response reaches Done with its payload.
TEST_CASE("XYN-13: every record of fx_xy.vec round-trips under its xy and xyascii keys") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "fx_xy.vec");
    REQUIRE(vectors.size() == 19);
    size_t requests = 0;
    for (const Vector& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        std::string why;
        const bool ok = roundTrips(v, why);
        CHECK_MESSAGE(ok, why);
        requests += v.field("kind") == "request" ? 1 : 0;
    }
    CHECK(requests == 12);
}

// The same request under the wrong digits is a different frame: with the digits left at hex the
// octal-digit records of 1C, 3C and 3E ASCII do not reproduce (X10 octal is index 8, which hex
// digits write as 8, not 10), and 1E Binary and 3E Binary do not depend on xyascii at all.
TEST_CASE("XYN-14: the ASCII digits setting changes ASCII requests and never Binary ones") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "fx_xy.vec");
    const auto encodeWith = [&](const std::string& id, mc::XyNumbering digits) {
        const Vector& v = findById(vectors, id);
        std::vector<uint8_t> storage;
        mc::Request r = mc::test::serialRequestFromVector(v, storage);
        mc::FrameConfig cfg = mc::test::frameConfigFromVector(v);
        cfg.xyAsciiDigits = digits;
        mc::McProtocol proto(cfg);
        std::vector<uint8_t> out(proto.encodedSize(r).value(), 0);
        auto n = proto.encode(r, mc::MutableByteView{out.data(), out.size()});
        REQUIRE(n.hasValue());
        return std::pair<std::vector<uint8_t>, std::vector<uint8_t>>{out, v.bytes};
    };

    for (const char* id : {"V-FXY-08", "V-FXY-13", "V-FXY-15"}) {
        INFO("vector ", id);
        const auto octal = encodeWith(id, mc::XyNumbering::Octal);
        const auto hex = encodeWith(id, mc::XyNumbering::Hex);
        CHECK(octal.first == octal.second);
        CHECK(hex.first != hex.second);
        CHECK(hex.first.size() == octal.first.size());
    }
    // 1E ASCII: hex digits reproduce the vector (the FX3 Ethernet adapter), octal digits do not.
    {
        const auto hex = encodeWith("V-FXY-06", mc::XyNumbering::Hex);
        const auto octal = encodeWith("V-FXY-06", mc::XyNumbering::Octal);
        CHECK(hex.first == hex.second);
        CHECK(octal.first != octal.second);
    }
    for (const char* id : {"V-FXY-01", "V-FXY-03", "V-FXY-04", "V-FXY-18"}) {
        INFO("vector ", id);
        const auto octal = encodeWith(id, mc::XyNumbering::Octal);
        const auto hex = encodeWith(id, mc::XyNumbering::Hex);
        CHECK(octal.first == octal.second);
        CHECK(hex.first == hex.second);
    }
    // 3E ASCII with hex digits (FX5 "ASCII (X,Y HEX)"): V-FXY-17 has the default digits.
    {
        const auto hex = encodeWith("V-FXY-17", mc::XyNumbering::Hex);
        CHECK(hex.first == hex.second);
    }
}
