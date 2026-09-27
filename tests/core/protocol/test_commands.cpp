// CMD-01..14, CMDD-01..08/17/18 (docs/mc_reference/mc-protocol-frame-spec.md sections 9.4/9.5),
// driven by tests/vectors/cmd.vec and cmdd.vec, plus two standalone TEST_CASEs for behaviour the
// reference's own CMD/CMDD examples never exercise (PackedLsbFirst, the iQ-R series) -- see their
// own comments for why they still belong here.
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "core/protocol/command_qna.h"
#include "core/protocol/field_codec.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
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
using mc::MutableByteView;
using mc::PlcSeries;
using mc::Request;
using mc::detail::AsciiCodec;
using mc::detail::BinaryCodec;
using mc::detail::qnaRequestData;
using mc::detail::qnaRequestDataSize;
using mc::detail::qnaResponseData;
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

uint16_t parseCount(const Vector& v) {
    return static_cast<uint16_t>(std::stoul(v.field("count")));
}

// "1995,1202,1130" -> [0x1995, 0x1202, 0x1130] (write words) or bit values "1,1,0,0" -> [1,1,0,0]
// (write/expect bits); which one a caller wants is a property of the op, not of the text itself.
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

// buildRequest() and its callers keep `writeStorage` alive across the qnaRequestData()/
// qnaResponseData() call that uses the returned Request (Request::data is a non-owning view).
Request buildRequest(const Vector& v, std::vector<uint8_t>& writeStorage) {
    std::string op = v.field("op");
    Device head = parseDeviceField(v.field("device"));
    uint16_t count = parseCount(v);

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
    FAIL("cmd.vec/cmdd.vec: vector ", v.id, " has unrecognized op '", op, "'");
    return Request::readWords(head, count);
}

template <class Codec> void checkCmdRequest(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);

    size_t needed = qnaRequestDataSize<Codec>(r, PlcSeries::QL);
    std::vector<uint8_t> out(needed, 0xCC);
    auto result = qnaRequestData<Codec>(r, PlcSeries::QL, MutableByteView{out.data(), out.size()});
    REQUIRE(result.hasValue());
    CHECK(result.value() == needed);
    CHECK(out == v.bytes);
}

template <class Codec> void checkCmddResponse(const Vector& v) {
    std::vector<uint8_t> writeStorage; // unused (reads only), buildRequest() still wants it
    Request r = buildRequest(v, writeStorage);

    // Normalized payload size (protocol.h's future payloadSize()): codec-independent, unlike
    // qnaResponseDataSize<Codec>() which is the WIRE size qnaResponseData() checks `in.size`
    // against. cmdd.vec's response vectors are all BitLayout::BytePerPoint (the request default;
    // none of the reference's CMDD examples set PackedLsbFirst -- see QNA-BITS-PACKED below).
    size_t payloadSize = r.isBitOp() ? r.count : static_cast<size_t>(r.count) * 2;
    std::vector<uint8_t> payloadOut(payloadSize, 0xFF);
    auto result = qnaResponseData<Codec>(r, ByteView{v.bytes.data(), v.bytes.size()},
                                          MutableByteView{payloadOut.data(), payloadOut.size()});
    REQUIRE(result.hasValue());
    CHECK(result.value() == payloadSize);

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

template <class Codec> void checkCmddError(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);

    std::vector<uint8_t> payloadOut(64, 0); // generous scratch; every error path returns first.
    auto result = qnaResponseData<Codec>(r, ByteView{v.bytes.data(), v.bytes.size()},
                                          MutableByteView{payloadOut.data(), payloadOut.size()});
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().category == ErrorCategory::Protocol);

    std::string errorName = v.field("error");
    if (errorName == "LengthMismatch") {
        CHECK(result.error().code == ErrorCode::LengthMismatch);
    } else if (errorName == "InvalidCharacter") {
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    } else {
        FAIL("cmdd.vec: vector ", v.id, " has unrecognized error '", errorName, "'");
    }
}

} // namespace

TEST_CASE("CMD-01..10 (0401/1401 request data): driven by cmd.vec; CMD-11..14 (v1.1) skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmd.vec");
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (v.hasTag("v1.1")) {
            continue; // 0403/1402: transcribed for the vector file only (VEC-02 still covers
                       // them); not reachable through Op in v1, so nothing to encode here.
        }
        if (v.field("code") == "Ascii") {
            checkCmdRequest<AsciiCodec>(v);
        } else {
            checkCmdRequest<BinaryCodec>(v);
        }
    }
}

TEST_CASE("CMDD-01..06, 17, 18 (0401/1401 response data): driven by cmdd.vec; "
          "CMDD-07/08 (v1.1) skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmdd.vec");
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (v.hasTag("v1.1")) {
            continue;
        }
        std::string kind = v.field("kind");
        bool ascii = v.field("code") == "Ascii";
        if (kind == "response") {
            if (ascii) {
                checkCmddResponse<AsciiCodec>(v);
            } else {
                checkCmddResponse<BinaryCodec>(v);
            }
        } else if (kind == "response-error") {
            if (ascii) {
                checkCmddError<AsciiCodec>(v);
            } else {
                checkCmddError<BinaryCodec>(v);
            }
        } else {
            FAIL("cmdd.vec: vector ", v.id, " has unknown kind '", kind, "'");
        }
    }
}

TEST_CASE("QNA-BITS-PACKED: ReadBits/WriteBits in BitLayout::PackedLsbFirst") {
    // Not tied to a named CMD/CMDD vector: every reference CMD/CMDD bit example describes
    // individual point values (BytePerPoint, Request's own default) -- none uses PackedLsbFirst.
    // The module spec's Payload contract and this task's own Plan text both name PackedLsbFirst
    // as in scope for response decoding (and, symmetrically, request encoding), so it needs a
    // direct proof of its own, hand-computed against the same byte-for-byte discipline as the
    // vector-driven cases above.
    SUBCASE("write, 16 points, packed input 0x55 0x55 (points 0,2,4,...,14 ON)") {
        std::vector<uint8_t> packedIn = {0x55, 0x55};
        Request r = Request::writeBits(Device{DeviceType::M, 100},
                                        ByteView{packedIn.data(), packedIn.size()},
                                        BitLayout::PackedLsbFirst);
        REQUIRE(r.count == 16);

        std::vector<uint8_t> out(qnaRequestDataSize<BinaryCodec>(r, PlcSeries::QL), 0);
        auto binResult =
            qnaRequestData<BinaryCodec>(r, PlcSeries::QL, MutableByteView{out.data(), out.size()});
        REQUIRE(binResult.hasValue());
        // command(0401? no: write=1401 LE)=01 14; subcommand(bit,QL=0001 LE)=01 00; device M100
        // Binary QL (spec sec 3.3)=64 00 00 90; points(16 LE)=10 00; write data: two nibble-packed
        // bytes per chunk of 8 points, each chunk = [1,0,1,0,1,0,1,0] -> 10 10 10 10 per chunk.
        std::vector<uint8_t> expected = {0x01, 0x14, 0x01, 0x00, 0x64, 0x00, 0x00, 0x90,
                                          0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                                          0x10, 0x10};
        CHECK(out == expected);

        std::vector<uint8_t> ascOut(qnaRequestDataSize<AsciiCodec>(r, PlcSeries::QL), 0);
        MutableByteView ascOutView{ascOut.data(), ascOut.size()};
        auto ascResult = qnaRequestData<AsciiCodec>(r, PlcSeries::QL, ascOutView);
        REQUIRE(ascResult.hasValue());
        std::string ascText(ascOut.begin(), ascOut.end());
        CHECK(ascText == "14010001M*00010000101010101010101010");
    }

    SUBCASE("read, 5 points, reusing CMDD-06's wire bytes (10 10 10 -> 1,0,1,0,1)") {
        Request r = Request::readBits(Device{DeviceType::M, 100}, 5);
        r.bitLayout = BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = {0x10, 0x10, 0x10};
        std::vector<uint8_t> payloadOut(1, 0xFF);
        auto result = qnaResponseData<BinaryCodec>(r, ByteView{wire.data(), wire.size()},
                                                     MutableByteView{payloadOut.data(), 1});
        REQUIRE(result.hasValue());
        CHECK(result.value() == 1);
        // Points 0, 2, 4 ON (bits 0, 2, 4); unused high bits (5, 6, 7) 0 (Payload contract).
        CHECK(payloadOut[0] == 0x15);
    }
    SUBCASE("read, 5 points, Ascii (Checkpoint B coverage gap, T-019: decodeBits<AsciiCodec> "
            "was untested for PackedLsbFirst)") {
        Request r = Request::readBits(Device{DeviceType::M, 100}, 5);
        r.bitLayout = BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = {'1', '0', '1', '0', '1'};
        std::vector<uint8_t> payloadOut(1, 0xFF);
        auto result = qnaResponseData<AsciiCodec>(r, ByteView{wire.data(), wire.size()},
                                                    MutableByteView{payloadOut.data(), 1});
        REQUIRE(result.hasValue());
        CHECK(result.value() == 1);
        CHECK(payloadOut[0] == 0x15); // Same packing as the Binary case above.
    }
    SUBCASE("read, invalid nibble propagates through the PackedLsbFirst chunk loop (Checkpoint B "
            "coverage gap, T-019: decodeBits()'s own error-propagation return was untested)") {
        Request r = Request::readBits(Device{DeviceType::M, 100}, 2);
        r.bitLayout = BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = {0x21}; // same invalid nibble as CMDD-18.
        std::vector<uint8_t> payloadOut(1, 0xFF);
        auto result = qnaResponseData<BinaryCodec>(r, ByteView{wire.data(), wire.size()},
                                                     MutableByteView{payloadOut.data(), 1});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Protocol);
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    }
    SUBCASE("read, invalid character, Ascii (Checkpoint B coverage gap, T-019: "
            "decodeBits<AsciiCodec>'s own error-propagation return was untested)") {
        Request r = Request::readBits(Device{DeviceType::M, 100}, 5);
        r.bitLayout = BitLayout::PackedLsbFirst;
        std::vector<uint8_t> wire = {'1', 'X', '1', '0', '1'}; // 'X' is not '0'/'1'.
        std::vector<uint8_t> payloadOut(1, 0xFF);
        auto result = qnaResponseData<AsciiCodec>(r, ByteView{wire.data(), wire.size()},
                                                    MutableByteView{payloadOut.data(), 1});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Protocol);
        CHECK(result.error().code == ErrorCode::InvalidCharacter);
    }
}

TEST_CASE("QNA-REQUEST-ERRORS: qnaRequestData()'s own BufferTooSmall/InvalidDevice "
          "(Checkpoint B coverage gap, T-019)") {
    // Neither is reachable through McProtocol::encode()/frame3eEncode(): validate() already
    // blocks an unsupported device (InvalidDevice) before any codec runs, and frame3eEncode's own
    // upfront size check already guarantees this function's identical one never fails. Both are
    // still directly testable (and worth testing) against qnaRequestData() itself, the layer that
    // actually owns each check.
    SUBCASE("BufferTooSmall") {
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        uint8_t out[1];
        auto result = qnaRequestData<BinaryCodec>(r, PlcSeries::QL, MutableByteView{out, 1});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("InvalidDevice propagates from qnaDevice()") {
        Request r = Request::readWords(Device{DeviceType::RD, 0}, 1); // RD has no QnA Q-L code.
        uint8_t out[64];
        auto result = qnaRequestData<BinaryCodec>(r, PlcSeries::QL, MutableByteView{out, 64});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::InvalidDevice);
    }
}

TEST_CASE("QNA-IQR: subcommand and device code use the iQ-R column when series is IqR") {
    // Not tied to a named CMD/CMDD vector: CMD-01..14 are all Q/L (Appendix A's iQ-R subcommand
    // vectors, V-3E-B-11/12, belong to the 3E-facade task). Proves qnaSubcommand()'s series
    // dispatch directly; the device field itself is DEV-07 (T-015), reused here byte-for-byte.
    Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
    std::vector<uint8_t> out(qnaRequestDataSize<BinaryCodec>(r, PlcSeries::IqR), 0);
    auto result =
        qnaRequestData<BinaryCodec>(r, PlcSeries::IqR, MutableByteView{out.data(), out.size()});
    REQUIRE(result.hasValue());
    // command(0401 LE)=01 04; subcommand(word,iQ-R=0002 LE)=02 00; device D100 QnA Binary iQ-R
    // (T-015's DEV-07)=64 00 00 00 A8 00; points(3 LE)=03 00.
    std::vector<uint8_t> expected = {0x01, 0x04, 0x02, 0x00, 0x64, 0x00,
                                      0x00, 0x00, 0xA8, 0x00, 0x03, 0x00};
    CHECK(out == expected);
}
