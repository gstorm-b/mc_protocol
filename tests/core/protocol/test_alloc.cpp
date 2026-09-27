// ALC-01 for the codec (spec "Complexity and allocation"; SPEC-core-protocol.md
// test_alloc.cpp): zero allocations across encode-into-buffer, a full byte-at-a-time feed(), and
// payload(), on 3E Binary and ASCII.
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

TEST_CASE("ALC-01 encode-into-buffer: zero allocations (3E Binary and ASCII)") {
    auto checkEncode = [](mc::DataCode code, const Request& r) {
        FrameConfig cfg = FrameConfig::frame3E(code);
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
        checkEncode(mc::DataCode::Binary, Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("Binary WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(mc::DataCode::Binary,
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
    SUBCASE("Ascii ReadWords") {
        checkEncode(mc::DataCode::Ascii, Request::readWords(Device{DeviceType::D, 100}, 3));
    }
    SUBCASE("Ascii WriteBits") {
        uint8_t data[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        checkEncode(mc::DataCode::Ascii,
                    Request::writeBits(Device{DeviceType::M, 100}, ByteView{data, 8}));
    }
}

TEST_CASE("ALC-01 byte-at-a-time feed() and payload(): zero allocations (3E Binary and ASCII)") {
    auto checkParse = [](mc::DataCode code, const Request& r, ByteView wire) {
        FrameConfig cfg = FrameConfig::frame3E(code);
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
        checkParse(mc::DataCode::Binary, Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
    SUBCASE("Ascii ReadWords response (V-3E-A-02)") {
        std::string text = "D00000FF03FF0000100000199512021130";
        std::vector<uint8_t> wire(text.begin(), text.end());
        checkParse(mc::DataCode::Ascii, Request::readWords(Device{DeviceType::D, 100}, 3),
                   ByteView{wire.data(), wire.size()});
    }
}
