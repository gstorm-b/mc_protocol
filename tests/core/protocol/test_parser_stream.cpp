// STR-01, STR-02, STR-04 (docs/spec/SPEC-core-protocol.md Testing Strategy), reusing
// tests/vectors/3e_binary.vec, 3e_ascii.vec, 1e_binary.vec and 1e_ascii.vec's own response vectors
// rather than inventing new byte sequences: every one of them is already independently proven
// correct by test_frame_3e.cpp's / test_frame_1e.cpp's own vector-driven test, so streaming the
// very same bytes here isolates exactly the incremental/coalesced-frame behaviour these three IDs
// are about.
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
    for (const char* fileName : {"3e_binary.vec", "3e_ascii.vec", "1e_binary.vec", "1e_ascii.vec"}) {
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
