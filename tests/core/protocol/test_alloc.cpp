// ALC-01 for the codec (spec "Complexity and allocation"; SPEC-core-protocol.md
// test_alloc.cpp): zero allocations across encode-into-buffer, a full byte-at-a-time feed(), and
// payload(), on 3E and 1E, Binary and ASCII.
//
// This is the one .cpp in mc_core_protocol_tests that includes tests/common/alloc_counter.h (see
// that file's banner: a second .cpp in this binary including it too would fail to link with a
// duplicate-symbol error). mc_core_model_tests (a separate binary/process) already includes the
// same header in its own test_alloc.cpp; the two never conflict, since neither shares a linker
// symbol table with the other.
#include "doctest/doctest.h"

#include "common/alloc_counter.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstdint>
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
    mc::test::resetAllocCount();
    auto* p = new int(42);
    size_t count = mc::test::allocCount();
    delete p;

    CHECK(count >= 1);
}

TEST_CASE("ALC-01 encode-into-buffer: zero allocations (3E and 1E, Binary and ASCII)") {
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
}

TEST_CASE("ALC-01 byte-at-a-time feed() and payload(): zero allocations (3E and 1E, Binary and "
          "ASCII)") {
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
