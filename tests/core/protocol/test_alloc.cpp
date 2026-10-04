// ALC-01 for the codec (spec "Complexity and allocation"; SPEC-core-protocol.md
// test_alloc.cpp): zero allocations across encode-into-buffer, a full byte-at-a-time feed(), and
// payload(), on 3E and 1E (Binary and ASCII), 3C and 1C (formats 1-4).
//
// This is the one .cpp in mc_core_protocol_tests that includes tests/common/alloc_counter.h (see
// that file's banner: a second .cpp in this binary including it too would fail to link with a
// duplicate-symbol error). mc_core_model_tests (a separate binary/process) already includes the
// same header in its own test_alloc.cpp; the two never conflict, since neither shares a linker
// symbol table with the other.
#include "doctest/doctest.h"

#include "common/alloc_counter.h"
#include "common/vectors.h"
#include "core/protocol/serial_parser.h"
#include "serial_synthetic.h"
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
using mc::FrameConfig;
using mc::McProtocol;
using mc::MutableByteView;
using mc::Parser;
using mc::ParseStatus;
using mc::Request;

TEST_CASE("ALC-01 positive control: the counter is live") {
    // Same proof as tests/core/model/test_alloc.cpp: if this fails, every zero-count assertion
    // below would be meaningless (a broken counter reads 0 for everything).
    const size_t count = mc::test::probeAllocCount();

    CHECK(count >= 1);
}

TEST_CASE("ALC-01 encode-into-buffer: zero allocations (3E, 1E, 3C, 1C)") {
    auto checkEncode = [](const FrameConfig& cfg, const Request& r) {
        McProtocol proto(cfg);

        // Warm-up outside the measured window, in case anything is lazily initialized only on
        // its first call ever.
        auto warmSize = proto.encodedSize(r);
        REQUIRE(warmSize.hasValue());
        std::vector<uint8_t> warmOut(warmSize.value());
        (void)proto.encode(r, MutableByteView{warmOut.data(), warmOut.size()});

        auto sizeResult = proto.encodedSize(r);
        REQUIRE(sizeResult.hasValue());
        std::vector<uint8_t> out(sizeResult.value());

        mc::test::resetAllocCount();
        auto encodeResult = proto.encode(r, MutableByteView{out.data(), out.size()});
        size_t count = mc::test::allocCount();

        REQUIRE(encodeResult.hasValue());
        CHECK(count == 0);
    };

    SUBCASE("Binary ReadWords") {
        checkEncode(FrameConfig::frame3E(mc::DataCode::Binary),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("Binary WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(FrameConfig::frame3E(mc::DataCode::Binary),
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
    SUBCASE("Ascii ReadWords") {
        checkEncode(FrameConfig::frame3E(mc::DataCode::Ascii),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("Ascii WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(FrameConfig::frame3E(mc::DataCode::Ascii),
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
    SUBCASE("1E Binary ReadWords") {
        checkEncode(FrameConfig::frame1E(mc::DataCode::Binary),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("1E Binary WriteBits (odd count)") {
        uint8_t data[3] = {1, 1, 1};
        checkEncode(FrameConfig::frame1E(mc::DataCode::Binary),
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 3}));
    }
    SUBCASE("1E Ascii ReadWords") {
        checkEncode(FrameConfig::frame1E(mc::DataCode::Ascii),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("1E Ascii WriteWords") {
        uint8_t data[6] = {0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        checkEncode(FrameConfig::frame1E(mc::DataCode::Ascii),
                    Request::writeWords(Device{DeviceType::D, 100}, ByteView{data, 6}));
    }
    SUBCASE("3C Format 1 ReadWords") {
        checkEncode(FrameConfig::frame3C(mc::SerialFormat::Format1),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("3C Format 4 WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(FrameConfig::frame3C(mc::SerialFormat::Format4),
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
    SUBCASE("3C Format 2 ReadWords") {
        checkEncode(FrameConfig::frame3C(mc::SerialFormat::Format2),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("3C Format 3 WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(FrameConfig::frame3C(mc::SerialFormat::Format3),
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
    SUBCASE("1C Format 1 ReadWords") {
        checkEncode(FrameConfig::frame1C(mc::SerialFormat::Format1),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("1C Format 4 WriteBits, AnA command set, message wait") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        FrameConfig cfg = FrameConfig::frame1C(mc::SerialFormat::Format4);
        cfg.commandSet = mc::C1CommandSet::AnA;
        cfg.messageWait = 10;
        checkEncode(cfg, Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
    SUBCASE("1C Format 2 ReadWords") {
        checkEncode(FrameConfig::frame1C(mc::SerialFormat::Format2),
                    Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("1C Format 3 WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(FrameConfig::frame1C(mc::SerialFormat::Format3),
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
}

TEST_CASE("ALC-01 byte-at-a-time feed() and payload(): zero allocations (3E, 1E, 3C, 1C)") {
    auto checkParse = [](const FrameConfig& cfg, const Request& r, ByteView wire) {
        McProtocol proto(cfg);
        size_t payloadSize = proto.payloadSize(r);
        std::vector<uint8_t> payloadOut(payloadSize);

        // Warm-up: a full parse outside the measured window.
        Parser warmParser = proto.parser(r);
        (void)warmParser.feed(wire);
        (void)warmParser.payload(wire, MutableByteView{payloadOut.data(), payloadOut.size()});

        Parser parser = proto.parser(r);
        mc::test::resetAllocCount();
        for (size_t n = 1; n <= wire.size; ++n) {
            (void)parser.feed(ByteView{wire.data, n});
        }
        size_t feedCount = mc::test::allocCount();
        REQUIRE(feedCount == 0);

        mc::test::resetAllocCount();
        auto payloadResult =
            parser.payload(wire, MutableByteView{payloadOut.data(), payloadOut.size()});
        size_t payloadCount = mc::test::allocCount();

        REQUIRE(payloadResult.hasValue());
        CHECK(payloadCount == 0);
    };

    SUBCASE("Binary ReadWords response (V-3E-B-02)") {
        std::vector<uint8_t> wire = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08,
                                      0x00, 0x00, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        checkParse(FrameConfig::frame3E(mc::DataCode::Binary),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("Ascii ReadWords response (V-3E-A-02)") {
        std::string text = "D00000FF03FF0000100000199512021130";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame3E(mc::DataCode::Ascii),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1E Binary ReadWords response (V-1E-B-02)") {
        std::vector<uint8_t> wire = {0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11};
        checkParse(FrameConfig::frame1E(mc::DataCode::Binary),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1E Ascii ReadWords response (V-1E-A-02)") {
        std::string text = "8100199512021130";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame1E(mc::DataCode::Ascii),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1E Ascii odd bit read response with the dummy character (V-1E-A-10)") {
        std::string text = "8000101010";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame1E(mc::DataCode::Ascii),
                   Request::readBits(Device{DeviceType::M, 100}, 5),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("3C Format 1 ReadWords response (V-3C1-02)") {
        std::string text = "\x02"
                           "F90000FF00199512021130"
                           "\x03"
                           "90";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame3C(mc::SerialFormat::Format1),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("3C Format 4 ReadBits response behind two junk bytes") {
        // 00 FF, then STX P data ETX SUM CR LF.
        std::string text("\x00\xFF"
                         "\x02"
                         "F90000FF0000010011"
                         "\x03"
                         "B1"
                         "\r\n",
                         2 + 1 + 18 + 1 + 2 + 2);
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame3C(mc::SerialFormat::Format4),
                   Request::readBits(Device{DeviceType::M, 100}, 8),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("3C Format 2 ReadWords response (V-3C2-02)") {
        std::string text = "\x02"
                           "00F90000FF00199512021130"
                           "\x03"
                           "F0";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame3C(mc::SerialFormat::Format2),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("3C Format 3 ReadWords response (V-3C3-02)") {
        std::string text = "\x02"
                           "F90000FF00QACK199512021130"
                           "\x03"
                           "B0";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame3C(mc::SerialFormat::Format3),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1C Format 1 ReadWords response (V-1C1-02)") {
        std::string text = "\x02"
                           "00FF199512021130"
                           "\x03"
                           "51";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame1C(mc::SerialFormat::Format1),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1C Format 4 ReadBits response (V-1C4-04) behind two junk bytes") {
        std::string text("\x00\xFF"
                         "\x02"
                         "00FF00010011"
                         "\x03"
                         "72"
                         "\r\n",
                         2 + 1 + 12 + 1 + 2 + 2);
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame1C(mc::SerialFormat::Format4),
                   Request::readBits(Device{DeviceType::M, 100}, 8),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1C Format 2 ReadWords response (V-1C2-02)") {
        std::string text = "\x02"
                           "000"
                           "0FF199512021130"
                           "\x03"
                           "B1";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame1C(mc::SerialFormat::Format2),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1C Format 3 ReadWords response (V-1C3-02)") {
        std::string text = "\x02"
                           "00FFGG199512021130"
                           "\x03"
                           "DF";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(FrameConfig::frame1C(mc::SerialFormat::Format3),
                   Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("1E Binary end code 5BH + abnormal code (V-1E-B-11): the error path allocates "
            "nothing either") {
        // payload() is not called for a Failed frame, so this one only measures feed().
        std::vector<uint8_t> wire = {0x81, 0x5B, 0x10};
        McProtocol proto(FrameConfig::frame1E(mc::DataCode::Binary));
        Request r = Request::readWords(Device{DeviceType::D, 100}, 3);
        Parser warm = proto.parser(r);
        (void)warm.feed(ByteView{wire.data(), wire.size()});

        Parser parser = proto.parser(r);
        mc::test::resetAllocCount();
        for (size_t n = 1; n <= wire.size(); ++n) {
            (void)parser.feed(ByteView{wire.data(), n});
        }
        size_t count = mc::test::allocCount();
        CHECK(parser.error().abnormalCode == 0x10);
        CHECK(count == 0);
    }
}

TEST_CASE("ALC-01 serial receive state machine: zero allocations over the synthetic frame set") {
    // Every format (1-4), response kind and sum option for 3C and 1C, fed one byte at a time the
    // way a serial link delivers it. The frames are built before the counter is reset.
    const std::vector<mc::test::SyntheticFrame> frames = mc::test::syntheticSerialFrames();
    REQUIRE(frames.size() > 60);

    size_t done = 0;
    mc::test::resetAllocCount();
    for (const mc::test::SyntheticFrame& f : frames) {
        const mc::detail::SerialParams p{f.spec.frame, f.spec.format, f.spec.sumCheck,
                                         f.spec.f3ShortSum};
        mc::detail::SerialCursor cursor;
        mc::detail::SerialFrame frame;
        mc::Error error;
        for (size_t n = 1; n <= f.bytes.size(); ++n) {
            if (mc::detail::serialFeed(p, ByteView{f.bytes.data(), n}, cursor, frame, error) ==
                ParseStatus::Done) {
                ++done;
            }
        }
    }
    size_t count = mc::test::allocCount();

    CHECK(count == 0);
    CHECK(done == frames.size()); // the loop really parsed every frame
}

TEST_CASE("ALC-01 serial receive state machine: junk, a wrong SUM and a malformed frame allocate "
          "nothing either") {
    mc::test::SyntheticSpec spec;
    spec.kind = mc::detail::SerialResponseKind::Data;
    spec.data = "199512021130";
    mc::test::SyntheticFrame f = mc::test::buildSyntheticFrame(spec);
    std::vector<uint8_t> junked = {0x00, 0xFF, 0x41};
    junked.insert(junked.end(), f.bytes.begin(), f.bytes.end());
    std::vector<uint8_t> badSum = f.bytes;
    badSum[badSum.size() - 1] = badSum[badSum.size() - 1] == '0' ? '1' : '0';
    std::vector<uint8_t> shortBody = {0x02, 'F', '9', 0x03, '0', '0'};
    const mc::detail::SerialParams p{spec.frame, spec.format, true, false};

    mc::test::resetAllocCount();
    for (const std::vector<uint8_t>* wire : {&junked, &badSum, &shortBody}) {
        mc::detail::SerialCursor cursor;
        mc::detail::SerialFrame frame;
        mc::Error error;
        for (size_t n = 1; n <= wire->size(); ++n) {
            (void)mc::detail::serialFeed(p, ByteView{wire->data(), n}, cursor, frame, error);
        }
    }
    CHECK(mc::test::allocCount() == 0);
}

namespace {

// One vector of Appendix A prepared for the measured window: its config, request and buffers are
// built (and may allocate) before the counter is reset.
struct AlcItem {
    FrameConfig cfg;
    Request request;
    bool isRequest{false};
    const mc::test::Vector* vector{nullptr};
    size_t family{0};
    std::vector<uint8_t> out;
};

// The 12 v1 families: 3E and 1E in each data code, 3C and 1C in each serial format.
std::string alcFamilyOf(const mc::test::Vector& v) {
    const std::string frame = v.field("frame");
    if (frame == "3C" || frame == "1C") {
        return frame + "/" + v.field("format");
    }
    return frame + "/" + v.field("code");
}

const char* const kAlcFamilies[] = {"3E/Binary", "3E/Ascii", "1E/Binary", "1E/Ascii",
                                    "3C/F1",     "3C/F2",    "3C/F3",     "3C/F4",
                                    "1C/F1",     "1C/F2",    "1C/F3",     "1C/F4"};

} // namespace

TEST_CASE("ALC-01 every v1 family: every request vector encodes and every response vector "
          "parses, byte by byte, with zero allocations") {
    const std::filesystem::path root = std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
    std::vector<mc::test::Vector> vectors;
    for (const char* file : {"3e_binary.vec", "3e_ascii.vec", "1e_binary.vec", "1e_ascii.vec",
                             "3c_f1.vec", "3c_f2.vec", "3c_f3.vec", "3c_f4.vec", "1c_f1.vec",
                             "1c_f2.vec", "1c_f3.vec", "1c_f4.vec", "fx_xy.vec"}) {
        for (mc::test::Vector& v : mc::test::loadVectors(root / file)) {
            if (!v.hasTag("v1.1") && !v.hasTag("v2")) {
                vectors.push_back(std::move(v));
            }
        }
    }
    REQUIRE(vectors.size() > 300);

    // Everything the measured loop touches is built here.
    std::vector<std::vector<uint8_t>> writeStorage(vectors.size());
    std::vector<AlcItem> items(vectors.size());
    for (size_t i = 0; i < vectors.size(); ++i) {
        const mc::test::Vector& v = vectors[i];
        AlcItem& item = items[i];
        item.vector = &v;
        item.cfg = mc::test::frameConfigFromVector(v);
        item.request = mc::test::serialRequestFromVector(v, writeStorage[i]);
        item.isRequest = v.field("kind") == "request";
        const std::string family = alcFamilyOf(v);
        bool known = false;
        for (size_t f = 0; f < sizeof(kAlcFamilies) / sizeof(kAlcFamilies[0]); ++f) {
            if (family == kAlcFamilies[f]) {
                item.family = f;
                known = true;
            }
        }
        REQUIRE_MESSAGE(known, "vector ", v.id, " is of an unknown family ", family);
        item.out.assign(item.isRequest ? v.bytes.size() + 64 : item.request.count * 2u + 64u, 0);
    }
    constexpr size_t kFamilies = sizeof(kAlcFamilies) / sizeof(kAlcFamilies[0]);
    std::vector<size_t> encoded(kFamilies, 0);
    std::vector<size_t> done(kFamilies, 0);
    std::vector<size_t> decoded(kFamilies, 0);

    mc::test::resetAllocCount();
    for (AlcItem& item : items) {
        const McProtocol proto(item.cfg);
        const std::vector<uint8_t>& bytes = item.vector->bytes;
        if (item.isRequest) {
            auto size = proto.encodedSize(item.request);
            auto result =
                proto.encode(item.request, MutableByteView{item.out.data(), item.out.size()});
            if (size.hasValue() && result.hasValue()) {
                ++encoded[item.family];
            }
            continue;
        }
        Parser parser = proto.parser(item.request);
        ParseStatus status = ParseStatus::NeedMore;
        for (size_t n = 1; n <= bytes.size(); ++n) {
            status = parser.feed(ByteView{bytes.data(), n});
        }
        if (status == ParseStatus::Done) {
            ++done[item.family];
            auto payload = parser.payload(ByteView{bytes.data(), bytes.size()},
                                          MutableByteView{item.out.data(), item.out.size()});
            if (payload.hasValue()) {
                ++decoded[item.family];
            }
        }
    }
    const size_t allocations = mc::test::allocCount();

    CHECK(allocations == 0);
    // The loop really exercised every family: a request that encodes, a response that reaches
    // Done and a payload that decodes, in each of the twelve.
    for (size_t f = 0; f < kFamilies; ++f) {
        INFO("family ", kAlcFamilies[f]);
        CHECK(encoded[f] > 0);
        CHECK(done[f] > 0);
        CHECK(decoded[f] > 0);
    }
}
