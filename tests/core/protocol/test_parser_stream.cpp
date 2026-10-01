// STR-01, STR-02, STR-04 (docs/spec/SPEC-core-protocol.md Testing Strategy), reusing
// tests/vectors/3e_binary.vec, 3e_ascii.vec, 1e_binary.vec, 1e_ascii.vec, 3c_f1.vec ..
// 3c_f4.vec and 1c_f1.vec .. 1c_f4.vec's own response vectors rather than inventing new byte
// sequences: every one of them is already independently proven correct by test_frame_3e.cpp's /
// test_frame_1e.cpp's / test_frame_serial.cpp's own vector-driven test, so streaming the very same
// bytes here isolates exactly the incremental/coalesced-frame behaviour these three IDs are about.
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

FrameConfig buildConfig(const Vector& v) {
    if (v.field("frame") == "3C" || v.field("frame") == "1C") {
        return mc::test::serialConfigFromVector(v);
    }
    mc::DataCode code = (v.field("code") == "Ascii") ? mc::DataCode::Ascii : mc::DataCode::Binary;
    FrameConfig cfg = (v.field("frame") == "1E") ? FrameConfig::frame1E(code)
                                                 : FrameConfig::frame3E(code);
    cfg.series = (v.field("series") == "IqR") ? PlcSeries::IqR : PlcSeries::QL;
    return cfg;
}

// Response parsing (frame3eTryParse(), qnaResponseData()) only ever reads r.op/count/bitLayout,
// never r.data -- confirmed by command_qna.h's own contract -- so a write's Request needs no
// real write payload to reconstruct correctly here, unlike test_frame_3e.cpp's own
// buildRequest() (which also encodes requests, and so does need one).
Request buildRequestForResponse(const Vector& v) {
    Device head = parseDeviceField(v.field("device"));
    uint16_t count = static_cast<uint16_t>(std::stoul(v.field("count")));
    std::string op = v.field("op");
    if (op == "ReadBits") {
        return Request::readBits(head, count);
    }
    if (op == "WriteWords") {
        return Request::writeWords(head, ByteView{});
    }
    if (op == "WriteBits") {
        return Request::writeBits(head, ByteView{});
    }
    return Request::readWords(head, count);
}

void checkPayloadMatchesExpect(const Request& r, Parser& parser, ByteView wire) {
    size_t payloadSize = McProtocol(FrameConfig::frame3E()).payloadSize(r); // frame-independent.
    std::vector<uint8_t> payloadOut(payloadSize, 0xFF);
    auto payloadResult = parser.payload(wire, MutableByteView{payloadOut.data(), payloadSize});
    REQUIRE(payloadResult.hasValue());
    CHECK(payloadResult.value() == payloadSize);
}

// Every non-`v1.1`, `kind: response` vector of the 3E and 1E vector files (STR-01/02 draw from this
// same pool; the "expect" values are already proven correct by test_frame_3e.cpp, so nothing
// here re-checks them beyond confirming payload() still succeeds and returns the right size).
std::vector<Vector> allSuccessResponseVectors() {
    std::vector<Vector> result;
    for (const char* fileName : {"3e_binary.vec", "3e_ascii.vec", "1e_binary.vec", "1e_ascii.vec",
                                 "3c_f1.vec", "3c_f2.vec", "3c_f3.vec", "3c_f4.vec", "1c_f1.vec",
                                 "1c_f2.vec", "1c_f3.vec", "1c_f4.vec"}) {
        for (auto& v : loadVectors(vectorsRoot() / fileName)) {
            if (!v.hasTag("v1.1") && v.field("kind") == "response") {
                result.push_back(std::move(v));
            }
        }
    }
    return result;
}

} // namespace

TEST_CASE("STR-01: every response vector fed one byte at a time reaches Done with the same "
          "payload") {
    for (const auto& v : allSuccessResponseVectors()) {
        INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
        Request r = buildRequestForResponse(v);
        FrameConfig cfg = buildConfig(v);
        McProtocol proto(cfg);
        Parser parser = proto.parser(r);

        for (size_t n = 1; n < v.bytes.size(); ++n) {
            INFO("fed ", n, " of ", v.bytes.size(), " bytes");
            ParseStatus status = parser.feed(ByteView{v.bytes.data(), n});
            CHECK(status == ParseStatus::NeedMore);
        }
        ParseStatus finalStatus = parser.feed(ByteView{v.bytes.data(), v.bytes.size()});
        REQUIRE(finalStatus == ParseStatus::Done);
        CHECK(parser.frameLength() == v.bytes.size());

        checkPayloadMatchesExpect(r, parser, ByteView{v.bytes.data(), v.bytes.size()});
    }
}

TEST_CASE("STR-02: two response frames (3E and 1E) in one buffer; frameLength() points at the second") {
    std::vector<Vector> responses = allSuccessResponseVectors();
    // Two distinct-content response vectors of the same frame and code, so the boundary between
    // them is unambiguous: the first two Binary and first two ASCII responses of each frame in file
    // order (V-3E-B-02/V-3E-B-04, V-3E-A-02/V-3E-A-04, V-1E-B-02/V-1E-B-04, V-1E-A-02/V-1E-A-04 --
    // the "response" vectors of G1 and G2).
    struct FrameCode {
        const char* frame;
        const char* code;
    };
    for (const FrameCode& fc : {FrameCode{"3E", "Binary"}, FrameCode{"3E", "Ascii"},
                                FrameCode{"1E", "Binary"}, FrameCode{"1E", "Ascii"}}) {
        const char* code = fc.code;
        std::vector<const Vector*> matching;
        for (const auto& v : responses) {
            if (v.field("code") == code && v.field("frame") == fc.frame) {
                matching.push_back(&v);
            }
        }
        INFO("frame ", fc.frame, " code ", code);
        REQUIRE(matching.size() >= 2);
        const Vector& first = *matching[0];
        const Vector& second = *matching[1];

        std::vector<uint8_t> combined = first.bytes;
        combined.insert(combined.end(), second.bytes.begin(), second.bytes.end());

        Request firstRequest = buildRequestForResponse(first);
        FrameConfig cfg = buildConfig(first);
        McProtocol proto(cfg);
        Parser parser = proto.parser(firstRequest);

        ParseStatus status = parser.feed(ByteView{combined.data(), combined.size()});
        REQUIRE(status == ParseStatus::Done);
        CHECK(parser.frameLength() == first.bytes.size()); // points at the second frame's start.
        checkPayloadMatchesExpect(firstRequest, parser,
                                   ByteView{combined.data(), combined.size()});

        // The remainder, fed to a fresh parser for the second request, is a complete frame too.
        Request secondRequest = buildRequestForResponse(second);
        Parser secondParser = McProtocol(buildConfig(second)).parser(secondRequest);
        ByteView remainder{combined.data() + parser.frameLength(),
                            combined.size() - parser.frameLength()};
        ParseStatus secondStatus = secondParser.feed(remainder);
        REQUIRE(secondStatus == ParseStatus::Done);
        CHECK(secondParser.frameLength() == second.bytes.size());
    }
}

TEST_CASE("STR-04: a frame cut short, then reset(): the parser recovers") {
    std::vector<Vector> responses = allSuccessResponseVectors();
    REQUIRE_FALSE(responses.empty());
    const Vector& v = responses.front();

    Request r = buildRequestForResponse(v);
    FrameConfig cfg = buildConfig(v);
    McProtocol proto(cfg);
    Parser parser = proto.parser(r);

    // Cut short: feed only half the frame.
    size_t shortLength = v.bytes.size() / 2;
    REQUIRE(shortLength > 0);
    CHECK(parser.feed(ByteView{v.bytes.data(), shortLength}) == ParseStatus::NeedMore);

    parser.reset();

    // Recovers: the full, correct buffer from the start now reaches Done, exactly as if this
    // Parser had never seen the short feed.
    ParseStatus status = parser.feed(ByteView{v.bytes.data(), v.bytes.size()});
    REQUIRE(status == ParseStatus::Done);
    CHECK(parser.frameLength() == v.bytes.size());
    checkPayloadMatchesExpect(r, parser, ByteView{v.bytes.data(), v.bytes.size()});
}

namespace {

const Vector& vectorById(const std::vector<Vector>& all, const std::string& id) {
    for (const auto& v : all) {
        if (v.id == id) {
            return v;
        }
    }
    FAIL("no vector with id ", id);
    static Vector dummy;
    return dummy;
}

} // namespace

TEST_CASE("STR-02 (serial): two 3C or 1C response frames in one buffer; frameLength() points at "
          "the second") {
    // Every format: a response with data to a read, then an ACK (Format 3: QACK) to a write.
    struct Pair {
        const char* file;
        const char* first;
        const char* second;
    };
    for (const Pair& p :
         {Pair{"3c_f1.vec", "V-3C1-02", "V-3C1-04"}, Pair{"3c_f2.vec", "V-3C2-02", "V-3C2-04"},
          Pair{"3c_f3.vec", "V-3C3-02", "V-3C3-04"}, Pair{"3c_f4.vec", "V-3C4-02", "V-3C4-04"},
          Pair{"1c_f1.vec", "V-1C1-02", "V-1C1-07"}, Pair{"1c_f2.vec", "V-1C2-02", "V-1C2-07"},
          Pair{"1c_f3.vec", "V-1C3-02", "V-1C3-07"}, Pair{"1c_f4.vec", "V-1C4-02", "V-1C4-07"}}) {
        std::vector<Vector> all = loadVectors(vectorsRoot() / p.file);
        const std::string firstId = p.first;
        const std::string secondId = p.second;
        const Vector& first = vectorById(all, firstId);
        const Vector& second = vectorById(all, secondId);
        INFO("file ", p.file);

        std::vector<uint8_t> combined = first.bytes;
        combined.insert(combined.end(), second.bytes.begin(), second.bytes.end());

        Request firstRequest = buildRequestForResponse(first);
        Parser parser = McProtocol(buildConfig(first)).parser(firstRequest);
        REQUIRE(parser.feed(ByteView{combined.data(), combined.size()}) == ParseStatus::Done);
        CHECK(parser.frameLength() == first.bytes.size()); // points at the second frame's start.
        CHECK(parser.skipped() == 0);
        checkPayloadMatchesExpect(firstRequest, parser, ByteView{combined.data(), combined.size()});

        Request secondRequest = buildRequestForResponse(second);
        Parser secondParser = McProtocol(buildConfig(second)).parser(secondRequest);
        ByteView remainder{combined.data() + parser.frameLength(),
                           combined.size() - parser.frameLength()};
        REQUIRE(secondParser.feed(remainder) == ParseStatus::Done);
        CHECK(secondParser.frameLength() == second.bytes.size());
    }
}

TEST_CASE("STR-04 (serial): junk and half a frame, then reset(): the parser recovers (3C and 1C)") {
    struct Source {
        const char* file;
        const char* id;
    };
    for (const Source& source : {Source{"3c_f1.vec", "V-3C1-02"}, Source{"1c_f1.vec", "V-1C1-02"},
                                 Source{"1c_f2.vec", "V-1C2-02"}, Source{"1c_f3.vec", "V-1C3-02"},
                                 Source{"1c_f4.vec", "V-1C4-02"}}) {
        std::vector<Vector> all = loadVectors(vectorsRoot() / source.file);
        const std::string id = source.id;
        const Vector& v = vectorById(all, id);
        INFO("vector ", id);
        Request r = buildRequestForResponse(v);
        Parser parser = McProtocol(buildConfig(v)).parser(r);

        std::vector<uint8_t> junked = {0x00, 0xFF};
        junked.insert(junked.end(), v.bytes.begin(), v.bytes.end());
        CHECK(parser.feed(ByteView{junked.data(), junked.size() / 2}) == ParseStatus::NeedMore);

        parser.reset();

        // Recovers: the clean frame from the start reaches Done with nothing skipped, as if this
        // Parser had never seen the junk or the cut-short feed.
        REQUIRE(parser.feed(ByteView{v.bytes.data(), v.bytes.size()}) == ParseStatus::Done);
        CHECK(parser.skipped() == 0);
        CHECK(parser.frameLength() == v.bytes.size());
        checkPayloadMatchesExpect(r, parser, ByteView{v.bytes.data(), v.bytes.size()});

        // And after Done, reset() takes it through the junked form of the same frame.
        parser.reset();
        REQUIRE(parser.feed(ByteView{junked.data(), junked.size()}) == ParseStatus::Done);
        CHECK(parser.skipped() == 2);
        CHECK(parser.frameLength() == junked.size());
    }
}

// Index of the first byte of the response data of a serial response (junk before the frame
// included): start byte, the block number (Format 2), the access route and, in Format 3, the end
// code `QACK` / `GG`.
namespace {

size_t serialDataStart(const FrameConfig& cfg, size_t junk) {
    const size_t route = cfg.frame == mc::FrameType::F1C ? 4 : 10;
    const size_t block = cfg.format == mc::SerialFormat::Format2 ? 2 : 0;
    const size_t code =
        cfg.format == mc::SerialFormat::Format3 ? (cfg.frame == mc::FrameType::F1C ? 2 : 4) : 0;
    return junk + 1 + block + route + code;
}

} // namespace

TEST_CASE("STR-01 (serial): the ETX scan never re-reads bytes already fed, through the public "
          "Parser (poisoned buffer)") {
    // The caller keeps one buffer and feeds it again after every receive; a serial parser examines
    // only the bytes it has not seen. Proof: after every NeedMore the data bytes already fed are
    // overwritten with ETX in the caller's buffer (this breaks the "fed bytes do not change" rule
    // on purpose, like SER-03 does for serialFeed()). A parser that rescans from the start of the
    // body, for instance because the Parser lost its scan position between calls, meets the
    // planted ETX and ends the frame early; one that resumes at its cursor does not. The bytes are
    // restored before the completing feed, so the frame itself is the vector's.
    const std::string kJunk = std::string("\x41\x0D\x0A\x05\x00", 5);
    size_t frames = 0;
    size_t feeds = 0;
    for (const char* fileName : {"3c_f1.vec", "3c_f2.vec", "3c_f3.vec", "3c_f4.vec", "1c_f1.vec",
                                 "1c_f2.vec", "1c_f3.vec", "1c_f4.vec"}) {
        for (const Vector& v : loadVectors(vectorsRoot() / fileName)) {
            if (v.field("kind") != "response") {
                continue;
            }
            for (size_t junk : {size_t{0}, kJunk.size()}) {
                INFO("vector ", v.id, " (", v.file, ":", v.line, "), ", junk, " junk bytes");
                std::vector<uint8_t> all(kJunk.begin(), kJunk.begin() + junk);
                all.insert(all.end(), v.bytes.begin(), v.bytes.end());
                const FrameConfig cfg = buildConfig(v);
                const Request r = buildRequestForResponse(v);
                Parser parser = McProtocol(cfg).parser(r);

                std::vector<uint8_t> buffer = all;
                const size_t poisonFrom = serialDataStart(cfg, junk);
                for (size_t n = 1; n < all.size(); ++n) {
                    REQUIRE(parser.feed(ByteView{buffer.data(), n}) == ParseStatus::NeedMore);
                    ++feeds;
                    for (size_t i = poisonFrom; i + 1 < n; ++i) {
                        buffer[i] = 0x03; // ETX; the last byte fed is left alone
                    }
                }
                buffer = all;
                REQUIRE(parser.feed(ByteView{buffer.data(), buffer.size()}) == ParseStatus::Done);
                CHECK(parser.skipped() == junk);
                CHECK(parser.frameLength() == all.size());

                // The payload is the one the vector states.
                McProtocol proto(cfg);
                std::vector<uint8_t> payload(proto.payloadSize(r), 0xFF);
                auto decoded = parser.payload(ByteView{buffer.data(), buffer.size()},
                                              MutableByteView{payload.data(), payload.size()});
                REQUIRE(decoded.hasValue());
                std::vector<uint8_t> want;
                for (uint32_t value : mc::test::csvHex(v.field("expect"))) {
                    if (r.isBitOp()) {
                        want.push_back(static_cast<uint8_t>(value));
                    } else if (!r.isWrite()) {
                        want.push_back(static_cast<uint8_t>(value & 0xFFu));
                        want.push_back(static_cast<uint8_t>(value >> 8));
                    }
                }
                CHECK(payload == want);
                ++frames;
            }
        }
    }
    // 4 + 4 files of serial response rows, each fed with and without junk: a row that left the
    // vector files would shrink this count.
    CHECK(frames == 2 * 78);
    CHECK(feeds > 2000);
}
