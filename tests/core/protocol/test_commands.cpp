// CMD-01..33, CMDD-01..18 (docs/mc_reference/mc-protocol-frame-spec.md sections 9.4/9.5; the
// QnA rows since T-016, the 1E rows CMD-15..21 / CMDD-09..13 since T-041, the 1C rows CMD-25..30,
// 33 / CMDD-14..16 since T-048), driven by tests/vectors/cmd.vec and cmdd.vec (`frame: qna` / `1e`
// / `1c` picks the command layer), plus standalone TEST_CASEs for behaviour the reference's own
// CMD/CMDD examples never exercise (PackedLsbFirst, the iQ-R series, the 1E test commands 04H/05H
// and the 1C test commands BT/WT, error paths) -- see their own comments for why they still belong
// here.
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "core/protocol/command_a1c.h"
#include "core/protocol/command_a1e.h"
#include "core/protocol/command_qna.h"
#include "core/protocol/field_codec.h"
#include "core/protocol/sumcheck.h"

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
using mc::detail::a1cCommand;
using mc::detail::a1cRequestData;
using mc::detail::a1cRequestDataSize;
using mc::detail::a1cResponseData;
using mc::detail::a1cResponseDataSize;
using mc::detail::A1cTestBit;
using mc::detail::a1cTestBitsRequestData;
using mc::detail::a1cTestBitsRequestDataSize;
using mc::detail::A1cTestWord;
using mc::detail::a1cTestWordsRequestData;
using mc::detail::a1cTestWordsRequestDataSize;
using mc::detail::a1eCommandCode;
using mc::detail::a1eRequestData;
using mc::detail::a1eRequestDataSize;
using mc::detail::a1eResponseData;
using mc::detail::a1eResponseDataSize;
using mc::detail::A1eTestBit;
using mc::detail::a1eTestBitsRequestData;
using mc::detail::a1eTestBitsRequestDataSize;
using mc::detail::A1eTestWord;
using mc::detail::a1eTestWordsRequestData;
using mc::detail::a1eTestWordsRequestDataSize;
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

// The command layer under test: the QnA one (Q/L series, as every CMD-01..10 vector) or the 1E
// one. Each wraps the three functions the checks below need behind one shape.
struct QnaCommands {
    template <class Codec> static size_t requestDataSize(const Request& r) {
        return qnaRequestDataSize<Codec>(r, PlcSeries::QL);
    }
    template <class Codec> static mc::Expected<size_t> requestData(const Request& r,
                                                                    MutableByteView out) {
        return qnaRequestData<Codec>(r, PlcSeries::QL, out);
    }
    template <class Codec> static mc::Expected<size_t> responseData(const Request& r, ByteView in,
                                                                     MutableByteView out) {
        return qnaResponseData<Codec>(r, in, out);
    }
};

struct A1eCommands {
    template <class Codec> static size_t requestDataSize(const Request& r) {
        return a1eRequestDataSize<Codec>(r);
    }
    template <class Codec> static mc::Expected<size_t> requestData(const Request& r,
                                                                    MutableByteView out) {
        return a1eRequestData<Codec>(r, out);
    }
    template <class Codec> static mc::Expected<size_t> responseData(const Request& r, ByteView in,
                                                                     MutableByteView out) {
        return a1eResponseData<Codec>(r, in, out);
    }
};

// 1C response data is always ASCII and needs no series; the request side takes the command set and
// message wait of the vector, so it is checked by checkA1cRequest() below instead.
struct A1cCommands {
    template <class Codec>
    static mc::Expected<size_t> responseData(const Request& r, ByteView in, MutableByteView out) {
        return a1cResponseData(r, in, out);
    }
};

template <class Cmds, class Codec> void checkCmdRequest(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);

    size_t needed = Cmds::template requestDataSize<Codec>(r);
    std::vector<uint8_t> out(needed, 0xCC);
    auto result = Cmds::template requestData<Codec>(r, MutableByteView{out.data(), out.size()});
    REQUIRE(result.hasValue());
    CHECK(result.value() == needed);
    CHECK(out == v.bytes);
}

template <class Cmds, class Codec> void checkCmddResponse(const Vector& v) {
    std::vector<uint8_t> writeStorage; // unused (reads only), buildRequest() still wants it
    Request r = buildRequest(v, writeStorage);

    // Normalized payload size (protocol.h's future payloadSize()): codec-independent, unlike
    // qnaResponseDataSize<Codec>() which is the WIRE size qnaResponseData() checks `in.size`
    // against. cmdd.vec's response vectors are all BitLayout::BytePerPoint (the request default;
    // none of the reference's CMDD examples set PackedLsbFirst -- see QNA-BITS-PACKED below).
    size_t payloadSize = r.isBitOp() ? r.count : static_cast<size_t>(r.count) * 2;
    std::vector<uint8_t> payloadOut(payloadSize, 0xFF);
    auto result = Cmds::template responseData<Codec>(
        r, ByteView{v.bytes.data(), v.bytes.size()},
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

template <class Cmds, class Codec> void checkCmddError(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);

    std::vector<uint8_t> payloadOut(64, 0); // generous scratch; every error path returns first.
    auto result = Cmds::template responseData<Codec>(
        r, ByteView{v.bytes.data(), v.bytes.size()},
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

// Which command layer a vector belongs to; an unknown `frame:` fails instead of being skipped, so
// a mistyped vector cannot silently drop out of every runner.
bool isFrame(const Vector& v, const char* frame) {
    std::string f = v.field("frame");
    if (f != "qna" && f != "1e" && f != "1c") {
        FAIL("cmd.vec/cmdd.vec: vector ", v.id, " has unrecognized frame '", f, "'");
    }
    return f == frame;
}

template <class Cmds> void runCmdRequestVector(const Vector& v) {
    if (v.field("code") == "Ascii") {
        checkCmdRequest<Cmds, AsciiCodec>(v);
    } else {
        checkCmdRequest<Cmds, BinaryCodec>(v);
    }
}

template <class Cmds> void runCmddVector(const Vector& v) {
    std::string kind = v.field("kind");
    bool ascii = v.field("code") == "Ascii";
    if (kind == "response") {
        if (ascii) {
            checkCmddResponse<Cmds, AsciiCodec>(v);
        } else {
            checkCmddResponse<Cmds, BinaryCodec>(v);
        }
    } else if (kind == "response-error") {
        if (ascii) {
            checkCmddError<Cmds, AsciiCodec>(v);
        } else {
            checkCmddError<Cmds, BinaryCodec>(v);
        }
    } else {
        FAIL("cmdd.vec: vector ", v.id, " has unknown kind '", kind, "'");
    }
}

} // namespace

TEST_CASE("CMD-01..10 (0401/1401 request data): driven by cmd.vec; CMD-11..14 (v1.1) skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmd.vec");
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (!isFrame(v, "qna")) {
            continue; // 1E and 1C rows: the CMD-15..21 and CMD-25..30 TEST_CASEs below.
        }
        if (v.hasTag("v1.1")) {
            continue; // 0403/1402: transcribed for the vector file only (VEC-02 still covers
                       // them); not reachable through Op in v1, so nothing to encode here.
        }
        runCmdRequestVector<QnaCommands>(v);
    }
}

TEST_CASE("CMDD-01..06, 17, 18 (0401/1401 response data): driven by cmdd.vec; "
          "CMDD-07/08 (v1.1) skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmdd.vec");
    REQUIRE_FALSE(vectors.empty());

    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (!isFrame(v, "qna")) {
            continue; // 1E and 1C rows: the CMDD-09..13 and CMDD-14..16 TEST_CASEs below.
        }
        if (v.hasTag("v1.1")) {
            continue;
        }
        runCmddVector<QnaCommands>(v);
    }
}

TEST_CASE("CMD-15..21 (1E 00H-03H request data): driven by cmd.vec; CMD-22..24, 37 (v1.1) "
          "skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmd.vec");
    REQUIRE_FALSE(vectors.empty());

    size_t checked = 0;
    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (!isFrame(v, "1e")) {
            continue;
        }
        if (v.hasTag("v1.1")) {
            continue; // 04H/05H: not reachable through Op in v1; their encoders are proved by
                       // "CMD-22..24, 37" below.
        }
        runCmdRequestVector<A1eCommands>(v);
        ++checked;
    }
    CHECK(checked == 7); // CMD-15..21: a vector that silently left the file would fail here.
}

TEST_CASE("CMDD-09..13 (1E 00H/01H response data): driven by cmdd.vec, plus the 1E length and "
          "nibble rows") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmdd.vec");
    REQUIRE_FALSE(vectors.empty());

    size_t checked = 0;
    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (!isFrame(v, "1e")) {
            continue;
        }
        runCmddVector<A1eCommands>(v);
        ++checked;
    }
    // CMDD-09..13 (5), CMDD-13a (wrong length), CMDD-10a (Binary odd count, zero last nibble),
    // CMDD-17e / CMDD-18e (the 1E twins of CMDD-17 / CMDD-18).
    CHECK(checked == 9);
}

namespace {

mc::C1CommandSet commandSetOf(const Vector& v) {
    return v.field("commandset") == "ana" ? mc::C1CommandSet::AnA : mc::C1CommandSet::ACPU;
}

// Message wait of a 1C vector: one hex digit in `wait:`, absent means 0.
uint8_t messageWaitOf(const Vector& v) {
    std::string w = v.field("wait");
    return w.empty() ? uint8_t{0} : static_cast<uint8_t>(std::stoul(w, nullptr, 16));
}

void checkA1cRequest(const Vector& v) {
    std::vector<uint8_t> writeStorage;
    Request r = buildRequest(v, writeStorage);
    mc::C1CommandSet set = commandSetOf(v);

    size_t needed = a1cRequestDataSize(r, set);
    CHECK(needed == v.bytes.size());
    std::vector<uint8_t> out(needed, 0xCC);
    auto result = a1cRequestData(r, set, messageWaitOf(v), MutableByteView{out.data(), out.size()});
    REQUIRE(result.hasValue());
    CHECK(result.value() == needed);
    CHECK(out == v.bytes);
}

} // namespace

TEST_CASE("CMD-25..30, 33 (1C BR/JR, WR/QR, BW, WW request data; manual sum check): driven by "
          "cmd.vec; CMD-31, 32 (v1.1) skipped") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmd.vec");
    REQUIRE_FALSE(vectors.empty());

    size_t requests = 0;
    size_t sums = 0;
    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (!isFrame(v, "1c")) {
            continue;
        }
        if (v.hasTag("v1.1")) {
            continue; // BT/WT: not reachable through Op in v1; "CMD-31, 32" below proves them.
        }
        if (v.field("kind") == "sumcheck") {
            // CMD-33 (and 1C-08): the sum check of station 00, PC FF and "BR3M0000" is C0.
            std::vector<uint8_t> sum(2, 0);
            auto result = mc::detail::sumcheckEncode(ByteView{v.bytes.data(), v.bytes.size()},
                                                     MutableByteView{sum.data(), sum.size()});
            REQUIRE(result.hasValue());
            CHECK(std::string(sum.begin(), sum.end()) == v.field("expect"));
            ++sums;
            continue;
        }
        checkA1cRequest(v);
        ++requests;
    }
    CHECK(requests == 6); // CMD-25..30: a vector that silently left the file would fail here.
    CHECK(sums == 1);     // CMD-33
}

TEST_CASE("CMDD-14..16 (1C BR / WR response data): driven by cmdd.vec, plus the 1C length and "
          "character rows") {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmdd.vec");
    REQUIRE_FALSE(vectors.empty());

    size_t checked = 0;
    for (const auto& v : vectors) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        if (!isFrame(v, "1c")) {
            continue;
        }
        runCmddVector<A1cCommands>(v);
        ++checked;
    }
    // CMDD-14..16 (3), then CMDD-14a / 16a (wrong length) and CMDD-14b / 16b (invalid character),
    // the 1C twins of CMDD-17 / CMDD-18.
    CHECK(checked == 7);
}

TEST_CASE("A1C-CODE: the command letters per operation and command set (spec 4.3 table)") {
    Device d{DeviceType::M, 0};
    uint8_t one[1] = {1};
    uint8_t word[2] = {0, 0};
    const Request ops[] = {Request::readBits(d, 1), Request::readWords(d, 1),
                           Request::writeBits(d, ByteView{one, 1}),
                           Request::writeWords(d, ByteView{word, 2})};
    const char* acpu[] = {"BR", "WR", "BW", "WW"};
    const char* ana[] = {"JR", "QR", "JW", "QW"};
    for (size_t i = 0; i < 4; ++i) {
        INFO("operation ", i);
        auto a = a1cCommand(ops[i], mc::C1CommandSet::ACPU);
        CHECK(std::string(a.letters, 2) == acpu[i]);
        auto b = a1cCommand(ops[i], mc::C1CommandSet::AnA);
        CHECK(std::string(b.letters, 2) == ana[i]);
    }
}

TEST_CASE("1C-05: the message wait is one upper-case hex digit after the command") {
    // CMD-25 has wait A (100 ms); every value 0-15 is one digit, hand-listed.
    const char digits[] = "0123456789ABCDEF";
    Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
    for (uint8_t wait = 0; wait < 16; ++wait) {
        std::vector<uint8_t> out(a1cRequestDataSize(r, mc::C1CommandSet::ACPU), 0);
        auto result = a1cRequestData(r, mc::C1CommandSet::ACPU, wait,
                                     MutableByteView{out.data(), out.size()});
        REQUIRE(result.hasValue());
        CHECK(std::string(out.begin(), out.end()) == std::string("WR") + digits[wait] + "D010003");
    }
}

TEST_CASE("CMD-31, 32 (1C WT / BT test commands, v1.1): encoders match the tagged vectors") {
    // Not reachable through Op in v1 (module spec "Commands"); the encoders exist so the vectors
    // can be enabled later, and are proved here directly against the transcribed bytes (by id).
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmd.vec");
    auto bytesOf = [&](const char* id) -> const std::vector<uint8_t>& {
        for (const auto& v : vectors) {
            if (v.id == id) {
                REQUIRE(v.hasTag("v1.1"));
                return v.bytes;
            }
        }
        FAIL("no vector with id ", id);
        static std::vector<uint8_t> none;
        return none;
    };

    // CMD-32: M50 ON, B31A OFF, Y2F ON. CMD-31: D500 = 1234H, Y100 = BCA9H, CN100 = 0064H.
    const A1cTestBit bits[] = {{Device{DeviceType::M, 50}, true},
                               {Device{DeviceType::B, 0x31A}, false},
                               {Device{DeviceType::Y, 0x2F}, true}};
    const A1cTestWord words[] = {{Device{DeviceType::D, 500}, 0x1234},
                                 {Device{DeviceType::Y, 0x100}, 0xBCA9},
                                 {Device{DeviceType::CN, 100}, 0x0064}};
    const mc::C1CommandSet acpu = mc::C1CommandSet::ACPU;

    const std::vector<uint8_t>& bitBytes = bytesOf("CMD-32");
    CHECK(a1cTestBitsRequestDataSize(bits, 3, acpu) == bitBytes.size());
    std::vector<uint8_t> bitOut(bitBytes.size(), 0xCC);
    auto bitResult =
        a1cTestBitsRequestData(bits, 3, acpu, 0, MutableByteView{bitOut.data(), bitOut.size()});
    REQUIRE(bitResult.hasValue());
    CHECK(bitResult.value() == bitBytes.size());
    CHECK(bitOut == bitBytes);

    const std::vector<uint8_t>& wordBytes = bytesOf("CMD-31");
    CHECK(a1cTestWordsRequestDataSize(words, 3, acpu) == wordBytes.size());
    std::vector<uint8_t> wordOut(wordBytes.size(), 0xCC);
    auto wordResult =
        a1cTestWordsRequestData(words, 3, acpu, 0, MutableByteView{wordOut.data(), wordOut.size()});
    REQUIRE(wordResult.hasValue());
    CHECK(wordResult.value() == wordBytes.size());
    CHECK(wordOut == wordBytes);

    // AnA/AnU: JT / QT with 7-character devices (spec 3.3), a message wait of F; worked out by
    // hand.
    const A1cTestBit one[] = {{Device{DeviceType::M, 50}, true}};
    std::vector<uint8_t> ana(a1cTestBitsRequestDataSize(one, 1, mc::C1CommandSet::AnA), 0);
    REQUIRE(a1cTestBitsRequestData(one, 1, mc::C1CommandSet::AnA, 15,
                                   MutableByteView{ana.data(), ana.size()})
                .hasValue());
    CHECK(std::string(ana.begin(), ana.end()) == "JTF01"
                                                 "M000050"
                                                 "1");
    const A1cTestWord oneWord[] = {{Device{DeviceType::D, 500}, 0x1234}};
    std::vector<uint8_t> anaWord(a1cTestWordsRequestDataSize(oneWord, 1, mc::C1CommandSet::AnA), 0);
    REQUIRE(a1cTestWordsRequestData(oneWord, 1, mc::C1CommandSet::AnA, 15,
                                    MutableByteView{anaWord.data(), anaWord.size()})
                .hasValue());
    CHECK(std::string(anaWord.begin(), anaWord.end()) == "QTF01"
                                                         "D000500"
                                                         "1234");
}

TEST_CASE("A1C-REQUEST-ERRORS: 256 points wrap to 00; BufferTooSmall, InvalidDevice, "
          "InvalidConfig") {
    SUBCASE("256 points are written as 00 (spec E8, 1C-06)") {
        Request r = Request::readBits(Device{DeviceType::M, 0}, 256);
        std::vector<uint8_t> out(a1cRequestDataSize(r, mc::C1CommandSet::ACPU), 0xCC);
        REQUIRE(
            a1cRequestData(r, mc::C1CommandSet::ACPU, 0, MutableByteView{out.data(), out.size()})
                .hasValue());
        CHECK(std::string(out.begin(), out.end()) == "BR0M000000");
    }
    SUBCASE("BufferTooSmall") {
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        uint8_t out[1];
        auto result = a1cRequestData(r, mc::C1CommandSet::ACPU, 0, MutableByteView{out, 1});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("InvalidDevice propagates from c1Device() (V has no 1C code)") {
        Request r = Request::readBits(Device{DeviceType::V, 0}, 1);
        uint8_t out[32];
        auto result =
            a1cRequestData(r, mc::C1CommandSet::ACPU, 0, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::InvalidDevice);
    }
    SUBCASE("a message wait above 15 is InvalidConfig, not a non-hex character") {
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        uint8_t out[32];
        auto result =
            a1cRequestData(r, mc::C1CommandSet::ACPU, 16, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Config);
        CHECK(result.error().code == ErrorCode::InvalidConfig);
    }
    SUBCASE("test commands: BufferTooSmall, InvalidDevice and InvalidConfig") {
        const A1cTestBit bit[] = {{Device{DeviceType::V, 0}, true}};
        const A1cTestWord word[] = {{Device{DeviceType::V, 0}, 1}};
        const mc::C1CommandSet acpu = mc::C1CommandSet::ACPU;
        uint8_t small[1];
        uint8_t big[64];

        auto tooSmallBits = a1cTestBitsRequestData(bit, 1, acpu, 0, MutableByteView{small, 1});
        REQUIRE_FALSE(tooSmallBits.hasValue());
        CHECK(tooSmallBits.error().code == ErrorCode::BufferTooSmall);
        auto tooSmallWords = a1cTestWordsRequestData(word, 1, acpu, 0, MutableByteView{small, 1});
        REQUIRE_FALSE(tooSmallWords.hasValue());
        CHECK(tooSmallWords.error().code == ErrorCode::BufferTooSmall);

        auto badBits = a1cTestBitsRequestData(bit, 1, acpu, 0, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(badBits.hasValue());
        CHECK(badBits.error().code == ErrorCode::InvalidDevice);
        auto badWords =
            a1cTestWordsRequestData(word, 1, acpu, 0, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(badWords.hasValue());
        CHECK(badWords.error().code == ErrorCode::InvalidDevice);

        auto waitBits = a1cTestBitsRequestData(bit, 1, acpu, 16, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(waitBits.hasValue());
        CHECK(waitBits.error().code == ErrorCode::InvalidConfig);
        auto waitWords =
            a1cTestWordsRequestData(word, 1, acpu, 16, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(waitWords.hasValue());
        CHECK(waitWords.error().code == ErrorCode::InvalidConfig);

        // n is the 2-character entry count: 256 would wrap to 00, so it is refused, before `items`
        // is read.
        auto manyBits =
            a1cTestBitsRequestData(bit, 256, acpu, 0, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(manyBits.hasValue());
        CHECK(manyBits.error().category == ErrorCategory::Encode);
        CHECK(manyBits.error().code == ErrorCode::PointCount);
        auto manyWords =
            a1cTestWordsRequestData(word, 256, acpu, 0, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(manyWords.hasValue());
        CHECK(manyWords.error().code == ErrorCode::PointCount);
    }
}

TEST_CASE("A1C-BITS: ASCII bit data is exactly N characters (no padding); PackedLsbFirst goes "
          "through the same packing; words are 4 hex characters each") {
    // 3 points: "111", no dummy character and no zero nibble (1C is ASCII only).
    uint8_t three[3] = {1, 1, 1};
    Request r = Request::writeBits(Device{DeviceType::M, 100}, ByteView{three, 3});
    std::vector<uint8_t> out(a1cRequestDataSize(r, mc::C1CommandSet::ACPU), 0xCC);
    REQUIRE(a1cRequestData(r, mc::C1CommandSet::ACPU, 0, MutableByteView{out.data(), out.size()})
                .hasValue());
    CHECK(std::string(out.begin(), out.end()) == "BW0M0100"
                                                 "03"
                                                 "111");

    // 9 points packed LSB first (0xFF, 0x01): all nine ON.
    uint8_t packed[2] = {0xFF, 0x01};
    Request nine = Request::writeBits(Device{DeviceType::M, 100}, ByteView{packed, 2},
                                      BitLayout::PackedLsbFirst);
    nine.count = 9;
    std::vector<uint8_t> nineOut(a1cRequestDataSize(nine, mc::C1CommandSet::ACPU), 0xCC);
    REQUIRE(a1cRequestData(nine, mc::C1CommandSet::ACPU, 0,
                           MutableByteView{nineOut.data(), nineOut.size()})
                .hasValue());
    CHECK(std::string(nineOut.begin(), nineOut.end()) == "BW0M0100"
                                                         "09"
                                                         "111111111");

    // Response side, 5 points PackedLsbFirst: "10101" -> 0x15 (points 0, 2, 4 ON).
    Request read = Request::readBits(Device{DeviceType::M, 100}, 5);
    read.bitLayout = BitLayout::PackedLsbFirst;
    std::vector<uint8_t> wire = {'1', '0', '1', '0', '1'};
    std::vector<uint8_t> payload(1, 0xFF);
    auto result = a1cResponseData(read, ByteView{wire.data(), wire.size()},
                                  MutableByteView{payload.data(), 1});
    REQUIRE(result.hasValue());
    CHECK(payload[0] == 0x15);
    CHECK(a1cResponseDataSize(read) == 5); // N characters, no dummy.

    // Words: 4 hex characters each.
    Request words = Request::readWords(Device{DeviceType::D, 100}, 3);
    CHECK(a1cResponseDataSize(words) == 12);
}

TEST_CASE("A1C-WRITE-RESPONSE: a write response carries no data; a non-empty one is "
          "LengthMismatch") {
    uint8_t word[2] = {0x34, 0x12};
    Request w = Request::writeWords(Device{DeviceType::D, 100}, ByteView{word, 2});
    CHECK(a1cResponseDataSize(w) == 0);
    uint8_t out[2] = {0xAA, 0xAA};
    auto ok = a1cResponseData(w, ByteView{}, MutableByteView{out, 2});
    REQUIRE(ok.hasValue());
    CHECK(ok.value() == 0);
    CHECK(out[0] == 0xAA); // untouched.

    uint8_t extra[1] = {'0'};
    auto bad = a1cResponseData(w, ByteView{extra, 1}, MutableByteView{out, 2});
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().code == ErrorCode::LengthMismatch);
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

TEST_CASE("A1E-CODE: 1E command code per operation is the frame subheader (spec 4.2)") {
    Device d{DeviceType::M, 100};
    uint8_t one[1] = {1};
    CHECK(a1eCommandCode(Request::readBits(d, 1)) == 0x00);
    CHECK(a1eCommandCode(Request::readWords(d, 1)) == 0x01);
    CHECK(a1eCommandCode(Request::writeBits(d, ByteView{one, 1})) == 0x02);
    uint8_t word[2] = {0, 0};
    CHECK(a1eCommandCode(Request::writeWords(d, ByteView{word, 2})) == 0x03);
}

TEST_CASE("CMD-22..24, 37 (1E 04H/05H test commands, v1.1): encoders match the tagged vectors") {
    // The vector-driven CMD-15..21 case skips these rows because no Op reaches 04H/05H in v1; the
    // encoders still exist (module spec "Commands"), so they are proved here directly against the
    // transcribed bytes, which are looked up by id, not retyped.
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / "cmd.vec");
    auto bytesOf = [&](const char* id) -> const std::vector<uint8_t>& {
        for (const auto& v : vectors) {
            if (v.id == id) {
                REQUIRE(v.hasTag("v1.1"));
                return v.bytes;
            }
        }
        FAIL("no vector with id ", id);
        static std::vector<uint8_t> none;
        return none;
    };

    // CMD-22 / CMD-23: Y94 ON, M60 OFF, B26 ON.
    const A1eTestBit bits[] = {{Device{DeviceType::Y, 0x94}, true},
                                {Device{DeviceType::M, 60}, false},
                                {Device{DeviceType::B, 0x26}, true}};
    // CMD-24 / CMD-37: Y80 = 7B29H, W26 = 1234H, CN18 = 0050H.
    const A1eTestWord words[] = {{Device{DeviceType::Y, 0x80}, 0x7B29},
                                  {Device{DeviceType::W, 0x26}, 0x1234},
                                  {Device{DeviceType::CN, 18}, 0x0050}};

    auto checkBits = [&](const char* id, auto encode, auto size) {
        const std::vector<uint8_t>& expected = bytesOf(id);
        CHECK(size(3) == expected.size());
        std::vector<uint8_t> out(expected.size(), 0xCC);
        auto result = encode(bits, 3, MutableByteView{out.data(), out.size()});
        REQUIRE(result.hasValue());
        CHECK(result.value() == expected.size());
        CHECK(out == expected);
    };
    auto checkWords = [&](const char* id, auto encode, auto size) {
        const std::vector<uint8_t>& expected = bytesOf(id);
        CHECK(size(3) == expected.size());
        std::vector<uint8_t> out(expected.size(), 0xCC);
        auto result = encode(words, 3, MutableByteView{out.data(), out.size()});
        REQUIRE(result.hasValue());
        CHECK(result.value() == expected.size());
        CHECK(out == expected);
    };

    checkBits("CMD-22", a1eTestBitsRequestData<BinaryCodec>,
              a1eTestBitsRequestDataSize<BinaryCodec>);
    checkBits("CMD-23", a1eTestBitsRequestData<AsciiCodec>,
              a1eTestBitsRequestDataSize<AsciiCodec>);
    checkWords("CMD-24", a1eTestWordsRequestData<BinaryCodec>,
               a1eTestWordsRequestDataSize<BinaryCodec>);
    checkWords("CMD-37", a1eTestWordsRequestData<AsciiCodec>,
               a1eTestWordsRequestDataSize<AsciiCodec>);
}

TEST_CASE("A1E-REQUEST-ERRORS: 256 points wrap to 00; BufferTooSmall and InvalidDevice") {
    SUBCASE("256 points are written as 00 (spec E8), Binary and ASCII") {
        Request r = Request::readWords(Device{DeviceType::D, 0}, 256);
        std::vector<uint8_t> bin(a1eRequestDataSize<BinaryCodec>(r), 0xCC);
        REQUIRE(a1eRequestData<BinaryCodec>(r, MutableByteView{bin.data(), bin.size()}).hasValue());
        CHECK(bin == std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00, 0x20, 0x44, 0x00, 0x00});

        std::vector<uint8_t> asc(a1eRequestDataSize<AsciiCodec>(r), 0xCC);
        REQUIRE(a1eRequestData<AsciiCodec>(r, MutableByteView{asc.data(), asc.size()}).hasValue());
        CHECK(std::string(asc.begin(), asc.end()) == "4420" "00000000" "00" "00");
    }
    SUBCASE("BufferTooSmall") {
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        uint8_t out[1];
        auto result = a1eRequestData<BinaryCodec>(r, MutableByteView{out, 1});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    }
    SUBCASE("InvalidDevice propagates from e1Device() (SM has no 1E code)") {
        Request r = Request::readBits(Device{DeviceType::SM, 0}, 1);
        uint8_t out[32];
        auto result = a1eRequestData<AsciiCodec>(r, MutableByteView{out, sizeof(out)});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::InvalidDevice);
    }
    SUBCASE("test commands: BufferTooSmall and InvalidDevice") {
        const A1eTestBit bit[] = {{Device{DeviceType::SM, 0}, true}};
        const A1eTestWord word[] = {{Device{DeviceType::SM, 0}, 1}};
        uint8_t small[1];
        uint8_t big[64];

        auto tooSmallBits = a1eTestBitsRequestData<BinaryCodec>(bit, 1, MutableByteView{small, 1});
        REQUIRE_FALSE(tooSmallBits.hasValue());
        CHECK(tooSmallBits.error().code == ErrorCode::BufferTooSmall);
        auto tooSmallWords =
            a1eTestWordsRequestData<AsciiCodec>(word, 1, MutableByteView{small, 1});
        REQUIRE_FALSE(tooSmallWords.hasValue());
        CHECK(tooSmallWords.error().code == ErrorCode::BufferTooSmall);

        auto badBits =
            a1eTestBitsRequestData<AsciiCodec>(bit, 1, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(badBits.hasValue());
        CHECK(badBits.error().code == ErrorCode::InvalidDevice);
        auto badWords =
            a1eTestWordsRequestData<BinaryCodec>(word, 1, MutableByteView{big, sizeof(big)});
        REQUIRE_FALSE(badWords.hasValue());
        CHECK(badWords.error().code == ErrorCode::InvalidDevice);
    }
}

TEST_CASE("A1E-BITS: odd counts pad the Binary write with a zero nibble; ASCII sends N "
          "characters (1E-11, spec Q3); PackedLsbFirst goes through the same packing") {
    uint8_t three[3] = {1, 1, 1};
    Request r = Request::writeBits(Device{DeviceType::M, 100}, ByteView{three, 3});

    std::vector<uint8_t> bin(a1eRequestDataSize<BinaryCodec>(r), 0xCC);
    REQUIRE(a1eRequestData<BinaryCodec>(r, MutableByteView{bin.data(), bin.size()}).hasValue());
    // device M100 (64 00 00 00 20 4D), points 3, fixed 00, write data 11 10.
    CHECK(bin == std::vector<uint8_t>{0x64, 0x00, 0x00, 0x00, 0x20, 0x4D, 0x03, 0x00, 0x11, 0x10});

    std::vector<uint8_t> asc(a1eRequestDataSize<AsciiCodec>(r), 0xCC);
    REQUIRE(a1eRequestData<AsciiCodec>(r, MutableByteView{asc.data(), asc.size()}).hasValue());
    CHECK(std::string(asc.begin(), asc.end()) == "4D20" "00000064" "03" "00" "111");

    // 9 points packed LSB first (0xFF, 0x01): all nine ON.
    uint8_t packed[2] = {0xFF, 0x01};
    Request nine = Request::writeBits(Device{DeviceType::M, 100}, ByteView{packed, 2},
                                       BitLayout::PackedLsbFirst);
    REQUIRE(nine.count == 16); // count derives from the packed size (2 bytes = 16 points).
    nine.count = 9;
    std::vector<uint8_t> out(a1eRequestDataSize<BinaryCodec>(nine), 0xCC);
    REQUIRE(a1eRequestData<BinaryCodec>(nine, MutableByteView{out.data(), out.size()}).hasValue());
    CHECK(out == std::vector<uint8_t>{0x64, 0x00, 0x00, 0x00, 0x20, 0x4D, 0x09, 0x00, 0x11, 0x11,
                                       0x11, 0x11, 0x10});

    // Response side, Binary 5 points PackedLsbFirst: 10 10 10 -> 0x15 (points 0, 2, 4 ON).
    Request read = Request::readBits(Device{DeviceType::M, 100}, 5);
    read.bitLayout = BitLayout::PackedLsbFirst;
    std::vector<uint8_t> wire = {0x10, 0x10, 0x10};
    std::vector<uint8_t> payload(1, 0xFF);
    auto result = a1eResponseData<BinaryCodec>(read, ByteView{wire.data(), wire.size()},
                                                MutableByteView{payload.data(), 1});
    REQUIRE(result.hasValue());
    CHECK(payload[0] == 0x15);
    CHECK(a1eResponseDataSize<BinaryCodec>(read) == 3);
    CHECK(a1eResponseDataSize<AsciiCodec>(read) == 6); // N + (N mod 2): 5 points + 1 dummy.
}

TEST_CASE("A1E-WRITE-RESPONSE: a write response carries no data; a non-empty one is "
          "LengthMismatch") {
    uint8_t word[2] = {0x34, 0x12};
    Request w = Request::writeWords(Device{DeviceType::D, 100}, ByteView{word, 2});
    CHECK(a1eResponseDataSize<BinaryCodec>(w) == 0);
    CHECK(a1eResponseDataSize<AsciiCodec>(w) == 0);
    uint8_t out[2] = {0xAA, 0xAA};
    auto ok = a1eResponseData<BinaryCodec>(w, ByteView{}, MutableByteView{out, 2});
    REQUIRE(ok.hasValue());
    CHECK(ok.value() == 0);
    CHECK(out[0] == 0xAA); // untouched.

    uint8_t extra[1] = {0};
    auto bad = a1eResponseData<BinaryCodec>(w, ByteView{extra, 1}, MutableByteView{out, 2});
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().code == ErrorCode::LengthMismatch);
}
