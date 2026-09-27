// PRIM-01..09, 12..17 (docs/mc_reference/mc-protocol-frame-spec.md §9.2), driven by
// tests/vectors/prim.vec except PRIM-04 and PRIM-17 (see their own TEST_CASEs below for why).
// PRIM-10, 11, 18 (DLE stuffing) are transcribed and tagged `v2` (module spec, Testing
// Strategy table: "PRIM-10, 11, 18 (DLE) -> v2"); no DLE implementation is expected in v1
// (see the dedicated TEST_CASE below, after PRIM-17).
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "core/protocol/field_codec.h"
#include "core/protocol/hexascii.h"
#include "core/protocol/sumcheck.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using mc::ByteView;
using mc::MutableByteView;
using mc::detail::AsciiCodec;
using mc::detail::BinaryCodec;
using mc::detail::sumcheckEncode;
using mc::test::loadVectors;
using mc::test::Vector;

namespace {

std::filesystem::path vectorsRoot() {
    return std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
}

uint32_t parseHexField(const std::string& s) {
    return s.empty() ? 0 : static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

std::vector<uint8_t> parsePoints(const std::string& s) {
    std::vector<uint8_t> points;
    std::string current;
    auto flush = [&]() {
        if (!current.empty()) {
            points.push_back(current == "1" ? 1 : 0);
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
    return points;
}

ByteView view(const std::vector<uint8_t>& b) { return ByteView{b.data(), b.size()}; }

MutableByteView mview(std::vector<uint8_t>& b) { return MutableByteView{b.data(), b.size()}; }

// ---- kind: encode -- Codec::put<field>(value) == the hex line; Codec::get<field>(hex line)
// == value (round trip). PRIM-01 (u16), PRIM-02 (uppercase), PRIM-03/PRIM-16 (u32/dword).
template <class Codec> void checkEncodeU16(const Vector& v, uint16_t value) {
    std::vector<uint8_t> out(Codec::u16Size(), 0);
    auto putResult = Codec::putU16(value, mview(out));
    REQUIRE(putResult.hasValue());
    CHECK(putResult.value() == Codec::u16Size());
    CHECK(out == v.bytes);

    auto getResult = Codec::getU16(view(v.bytes));
    REQUIRE(getResult.hasValue());
    CHECK(getResult.value() == value);
}

template <class Codec> void checkEncodeU32(const Vector& v, uint32_t value) {
    std::vector<uint8_t> out(Codec::u32Size(), 0);
    auto putResult = Codec::putU32(value, mview(out));
    REQUIRE(putResult.hasValue());
    CHECK(putResult.value() == Codec::u32Size());
    CHECK(out == v.bytes);

    auto getResult = Codec::getU32(view(v.bytes));
    REQUIRE(getResult.hasValue());
    CHECK(getResult.value() == value);
}

void checkEncode(const Vector& v) {
    std::string code = v.field("code");
    std::string field = v.field("field");
    uint32_t value = parseHexField(v.field("value"));
    if (code == "Ascii") {
        if (field == "u16") {
            checkEncodeU16<AsciiCodec>(v, static_cast<uint16_t>(value));
        } else {
            checkEncodeU32<AsciiCodec>(v, value);
        }
    } else {
        if (field == "u16") {
            checkEncodeU16<BinaryCodec>(v, static_cast<uint16_t>(value));
        } else {
            checkEncodeU32<BinaryCodec>(v, value);
        }
    }
}

// ---- kind: decode -- PRIM-05 (lower case accepted).
void checkDecode(const Vector& v) {
    uint32_t expect = parseHexField(v.field("expect"));
    std::string field = v.field("field");
    ByteView in = view(v.bytes);
    if (v.field("code") == "Ascii") {
        if (field == "u16") {
            auto r = AsciiCodec::getU16(in);
            REQUIRE(r.hasValue());
            CHECK(r.value() == static_cast<uint16_t>(expect));
        } else {
            auto r = AsciiCodec::getU32(in);
            REQUIRE(r.hasValue());
            CHECK(r.value() == expect);
        }
    } else {
        if (field == "u16") {
            auto r = BinaryCodec::getU16(in);
            REQUIRE(r.hasValue());
            CHECK(r.value() == static_cast<uint16_t>(expect));
        } else {
            auto r = BinaryCodec::getU32(in);
            REQUIRE(r.hasValue());
            CHECK(r.value() == expect);
        }
    }
}

// ---- kind: decode-error -- PRIM-06 (non-hex character).
void checkDecodeError(const Vector& v) {
    ByteView in = view(v.bytes);
    std::string field = v.field("field");
    auto checkFails = [](auto result) {
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == mc::ErrorCategory::Protocol);
        CHECK(result.error().code == mc::ErrorCode::InvalidCharacter);
    };
    if (v.field("code") == "Ascii") {
        if (field == "u16") {
            checkFails(AsciiCodec::getU16(in));
        } else {
            checkFails(AsciiCodec::getU32(in));
        }
    } else {
        if (field == "u16") {
            checkFails(BinaryCodec::getU16(in));
        } else {
            checkFails(BinaryCodec::getU32(in));
        }
    }
}

// ---- kind: sumcheck -- PRIM-07, 08, 09.
void checkSumcheck(const Vector& v) {
    std::vector<uint8_t> out(2, 0);
    auto r = sumcheckEncode(view(v.bytes), mview(out));
    REQUIRE(r.hasValue());
    std::string got(out.begin(), out.end());
    CHECK(got == v.field("expect"));
}

// ---- kind: bits-encode / bits-decode -- PRIM-12, PRIM-14 (valid case).
template <class Codec> void checkBits(const Vector& v, bool alsoEncode) {
    std::vector<uint8_t> points = parsePoints(v.field("points"));
    ByteView in = view(v.bytes);

    if (alsoEncode) {
        std::vector<uint8_t> out(Codec::bitsSize(points.size()), 0);
        auto putResult = Codec::putBits(view(points), mview(out));
        REQUIRE(putResult.hasValue());
        CHECK(putResult.value() == Codec::bitsSize(points.size()));
        CHECK(out == v.bytes);
    }

    std::vector<uint8_t> decoded(points.size(), 0xFF);
    auto getResult = Codec::getBits(in, points.size(), mview(decoded));
    REQUIRE(getResult.hasValue());
    CHECK(getResult.value() == points.size());
    CHECK(decoded == points);
}

void checkBitsEncode(const Vector& v) {
    if (v.field("code") == "Ascii") {
        checkBits<AsciiCodec>(v, true);
    } else {
        checkBits<BinaryCodec>(v, true);
    }
}

void checkBitsDecode(const Vector& v) {
    if (v.field("code") == "Ascii") {
        checkBits<AsciiCodec>(v, false);
    } else {
        checkBits<BinaryCodec>(v, false);
    }
}

// ---- kind: bits-decode-error -- PRIM-13, PRIM-14 (invalid case).
void checkBitsDecodeError(const Vector& v) {
    size_t count = static_cast<size_t>(std::stoul(v.field("count")));
    std::vector<uint8_t> out(count, 0xFF);
    ByteView in = view(v.bytes);
    auto checkFails = [](auto result) {
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == mc::ErrorCategory::Protocol);
        CHECK(result.error().code == mc::ErrorCode::InvalidCharacter);
    };
    if (v.field("code") == "Ascii") {
        checkFails(AsciiCodec::getBits(in, count, mview(out)));
    } else {
        checkFails(BinaryCodec::getBits(in, count, mview(out)));
    }
}

} // namespace

TEST_CASE("PRIM-01, 02, 03, 05, 06, 07, 08, 09, 12, 13, 14, 16: driven by prim.vec; "
          "PRIM-10, 11 (v2) skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "prim.vec");
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (v.hasTag("v2")) {
            continue; // PRIM-10, 11 (DLE): reported as skipped by their own TEST_CASE below.
        }
        std::string kind = v.field("kind");

        if (kind == "encode") {
            checkEncode(v);
        } else if (kind == "decode") {
            checkDecode(v);
        } else if (kind == "decode-error") {
            checkDecodeError(v);
        } else if (kind == "sumcheck") {
            checkSumcheck(v);
        } else if (kind == "bits-encode") {
            checkBitsEncode(v);
        } else if (kind == "bits-decode") {
            checkBitsDecode(v);
        } else if (kind == "bits-decode-error") {
            checkBitsDecodeError(v);
        } else {
            FAIL("prim.vec: vector ", v.id, " has unknown kind '", kind, "'");
        }
    }
}

TEST_CASE("PRIM-04 field overflow: validate() rejects it before any codec ever sees the value") {
    // The module spec maps "value wider than the field" to Encode/PointCount (point counts) and
    // Encode/InvalidDevice (device numbers) -- decided by validate(), which McProtocol::encode()
    // always runs first (module spec, Open Question 3), never by AsciiCodec/BinaryCodec. That is
    // why there is no overflow case in field_codec.h itself: by the time a Request reaches
    // put/getU8/U16/U32, validate() has already guaranteed its count and device number fit.
    // Exhaustive per-family cases already exist and are tested in core-model (Phase 1):
    // tests/core/model/test_frame_config.cpp ("validate() rule 4", "validate() rule 6") and
    // tests/core/model/test_device.cpp. This reproduces one case of each from core-protocol's
    // own test binary too, so "PRIM-04" is a name a grep finds.
    auto overCount = mc::validate(mc::Request::readBits(mc::Device{mc::DeviceType::X, 0}, 257),
                                   mc::FrameConfig::frame1E());
    REQUIRE_FALSE(overCount.hasValue());
    CHECK(overCount.error().code == mc::ErrorCode::PointCount);

    auto overDevice =
        mc::validate(mc::Request::readWords(mc::Device{mc::DeviceType::D, 1000000}, 1),
                     mc::FrameConfig::frame3E(mc::DataCode::Ascii));
    REQUIRE_FALSE(overDevice.hasValue());
    CHECK(overDevice.error().code == mc::ErrorCode::InvalidDevice);
}

// PRIM-15 (words -> bits) is not repeated here: it is core-model's own convert::wordsToBits(),
// already tested as "CNV-06 (PRIM-15)" in tests/core/model/test_convert.cpp.

TEST_CASE("PRIM-17 property: decode(encode(x)) == x for u8/u16/u32/bits/words/dwords, "
          "both codecs") {
    // Fixed seed: reproducible across runs and CI, per this project's no-flaky-tests norm; not a
    // fixed data vector (the module spec's own L1 test levels name "property-based" as a
    // distinct method from table-driven, spec §9.1), so there is no prim.vec record for this ID.
    std::mt19937 rng(0xA5A5A5A5u);
    std::uniform_int_distribution<uint32_t> byteDist(0, 0xFF);
    std::uniform_int_distribution<uint32_t> wordDist(0, 0xFFFF);
    std::uniform_int_distribution<uint32_t> dwordDist(0, 0xFFFFFFFFu);
    std::uniform_int_distribution<size_t> countDist(1, 33); // exercise both even and odd counts.

    constexpr int kIterations = 64;

    auto roundTripU8 = [&](auto codecTag) {
        using Codec = decltype(codecTag);
        for (int i = 0; i < kIterations; ++i) {
            uint8_t value = static_cast<uint8_t>(byteDist(rng));
            std::vector<uint8_t> wire(Codec::u8Size(), 0);
            REQUIRE(Codec::putU8(value, mview(wire)).hasValue());
            auto decoded = Codec::getU8(view(wire));
            REQUIRE(decoded.hasValue());
            CHECK(decoded.value() == value);
        }
    };
    auto roundTripU16 = [&](auto codecTag) {
        using Codec = decltype(codecTag);
        for (int i = 0; i < kIterations; ++i) {
            uint16_t value = static_cast<uint16_t>(wordDist(rng));
            std::vector<uint8_t> wire(Codec::u16Size(), 0);
            REQUIRE(Codec::putU16(value, mview(wire)).hasValue());
            auto decoded = Codec::getU16(view(wire));
            REQUIRE(decoded.hasValue());
            CHECK(decoded.value() == value);
        }
    };
    auto roundTripU32 = [&](auto codecTag) {
        using Codec = decltype(codecTag);
        for (int i = 0; i < kIterations; ++i) {
            uint32_t value = dwordDist(rng);
            std::vector<uint8_t> wire(Codec::u32Size(), 0);
            REQUIRE(Codec::putU32(value, mview(wire)).hasValue());
            auto decoded = Codec::getU32(view(wire));
            REQUIRE(decoded.hasValue());
            CHECK(decoded.value() == value);
        }
    };
    auto roundTripBits = [&](auto codecTag) {
        using Codec = decltype(codecTag);
        for (int i = 0; i < kIterations; ++i) {
            size_t count = countDist(rng);
            std::vector<uint8_t> points(count, 0);
            for (auto& p : points) {
                p = static_cast<uint8_t>(byteDist(rng) & 1u);
            }
            std::vector<uint8_t> wire(Codec::bitsSize(count), 0);
            REQUIRE(Codec::putBits(view(points), mview(wire)).hasValue());
            std::vector<uint8_t> decoded(count, 0xFF);
            auto r = Codec::getBits(view(wire), count, mview(decoded));
            REQUIRE(r.hasValue());
            CHECK(decoded == points);
        }
    };
    auto roundTripWords = [&](auto codecTag) {
        using Codec = decltype(codecTag);
        for (int i = 0; i < kIterations; ++i) {
            size_t wordCount = countDist(rng);
            std::vector<uint8_t> words(wordCount * 2, 0);
            for (size_t w = 0; w < wordCount; ++w) {
                uint16_t value = static_cast<uint16_t>(wordDist(rng));
                words[2 * w] = static_cast<uint8_t>(value & 0xFFu);
                words[2 * w + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
            }
            std::vector<uint8_t> wire(Codec::wordsSize(wordCount), 0);
            REQUIRE(Codec::putWords(view(words), mview(wire)).hasValue());
            std::vector<uint8_t> decoded(wordCount * 2, 0);
            auto r = Codec::getWords(view(wire), wordCount, mview(decoded));
            REQUIRE(r.hasValue());
            CHECK(decoded == words);
        }
    };
    auto roundTripDwords = [&](auto codecTag) {
        using Codec = decltype(codecTag);
        for (int i = 0; i < kIterations; ++i) {
            size_t dwordCount = countDist(rng);
            std::vector<uint8_t> dwords(dwordCount * 4, 0);
            for (size_t d = 0; d < dwordCount; ++d) {
                uint32_t value = dwordDist(rng);
                dwords[4 * d] = static_cast<uint8_t>(value & 0xFFu);
                dwords[4 * d + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
                dwords[4 * d + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
                dwords[4 * d + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
            }
            std::vector<uint8_t> wire(Codec::dwordsSize(dwordCount), 0);
            REQUIRE(Codec::putDwords(view(dwords), mview(wire)).hasValue());
            std::vector<uint8_t> decoded(dwordCount * 4, 0);
            auto r = Codec::getDwords(view(wire), dwordCount, mview(decoded));
            REQUIRE(r.hasValue());
            CHECK(decoded == dwords);
        }
    };

    AsciiCodec asciiTag{};
    BinaryCodec binaryTag{};
    roundTripU8(asciiTag);
    roundTripU8(binaryTag);
    roundTripU16(asciiTag);
    roundTripU16(binaryTag);
    roundTripU32(asciiTag);
    roundTripU32(binaryTag);
    roundTripBits(asciiTag);
    roundTripBits(binaryTag);
    roundTripWords(asciiTag);
    roundTripWords(binaryTag);
    roundTripDwords(asciiTag);
    roundTripDwords(binaryTag);
}

TEST_CASE("PRIM-10, 11 (DLE): transcribed from the reference and tagged v2, reported as "
          "skipped; PRIM-18 (property) has no fixed vector, same reason as PRIM-17") {
    // Module spec, Testing Strategy: "PRIM-10, 11, 18 (DLE) -> v2" -- no DLE implementation is
    // expected in v1 (sumcheck.h/hexascii.h were "written so the DLE layer can be added beside
    // them for 4C later", module spec "Boundaries"; that layer does not exist yet). Mirrors the
    // 4E vectors' own "reported as skipped" TEST_CASE (T-018): the CHECK(v.hasTag("v2")) below is
    // the real, falsifiable check -- it would fail if either record were left untagged.
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "prim.vec");
    REQUIRE_FALSE(vectors.empty());

    bool foundPrim10 = false;
    bool foundPrim11 = false;
    for (const auto& v : vectors) {
        if (v.id == "PRIM-10") {
            foundPrim10 = true;
        } else if (v.id == "PRIM-11") {
            foundPrim11 = true;
        } else {
            continue;
        }
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        CHECK(v.hasTag("v2"));
        MESSAGE("skipping ", v.id, " (", v.file, "): tagged v2, DLE is not implemented in v1");
    }
    CHECK(foundPrim10);
    CHECK(foundPrim11);

    // PRIM-18 ("Property: DLE", random byte strings) has no prim.vec record: like PRIM-17 above,
    // its input is randomly generated, not a fixed value to transcribe from the reference table.
    // Deferred to v2 for the same reason as PRIM-10/11 (no DLE implementation in v1); a future v2
    // task adding the DLE layer would prove `unstuff(stuff(x)) == x` and "stuff(x) has no lone
    // 10H" here as a property-based test, the same style as "PRIM-17 property" above.
    MESSAGE("skipping PRIM-18 (property: unstuff(stuff(x)) == x; stuff(x) has no lone 10H): "
            "no fixed vector (random input, same as PRIM-17), tagged v2, DLE is not "
            "implemented in v1");
}

TEST_CASE("hexascii BufferTooSmall / odd-length: closes a Checkpoint B coverage gap (T-019)") {
    // No PRIM-* vector exercises hexEncode()/hexDecode()'s own BufferTooSmall checks or
    // hexDecode()'s odd-length check directly: every PRIM-driven put/get call above already sizes
    // its buffer exactly right, and field_codec.h's own callers do too. Proved directly against
    // hexascii.h itself rather than through a codec, since that is the layer that actually owns
    // these three checks.
    SUBCASE("hexEncode: output buffer too small") {
        uint8_t value = 0xAB;
        uint8_t out[1] = {0}; // needs 2.
        auto r = mc::detail::hexEncode(ByteView{&value, 1}, MutableByteView{out, 1});
        REQUIRE_FALSE(r.hasValue());
        CHECK(r.error().category == mc::ErrorCategory::Encode);
        CHECK(r.error().code == mc::ErrorCode::BufferTooSmall);
    }
    SUBCASE("hexDecode: output buffer too small") {
        // "AB" decodes to exactly 1 byte; give it zero capacity instead.
        uint8_t text[2] = {'A', 'B'};
        auto r = mc::detail::hexDecode(ByteView{text, 2}, MutableByteView{nullptr, 0});
        REQUIRE_FALSE(r.hasValue());
        CHECK(r.error().category == mc::ErrorCategory::Encode);
        CHECK(r.error().code == mc::ErrorCode::BufferTooSmall);
    }
    SUBCASE("hexDecode: odd number of hex characters") {
        uint8_t text[3] = {'A', 'B', 'C'};
        uint8_t out[2];
        auto r = mc::detail::hexDecode(ByteView{text, 3}, MutableByteView{out, 2});
        REQUIRE_FALSE(r.hasValue());
        CHECK(r.error().category == mc::ErrorCategory::Protocol);
        CHECK(r.error().code == mc::ErrorCode::InvalidCharacter);
    }
}
