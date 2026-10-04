// ALC-01 (spec: ideas §9.3; SPEC-core-model.md "Complexity and allocation"): zero allocations
// across parseDevice, validate, chunk, convert::float64At, hexDump and formatDevice.
//
// This is the one .cpp in mc_core_model_tests that includes tests/common/alloc_counter.h (see
// that file's banner: including it from a second .cpp in this binary would fail to link).
#include "doctest/doctest.h"

#include "common/alloc_counter.h"

#include <cstdint>

#include "mc/core/convert.h"
#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/limits.h"
#include "mc/core/log.h"
#include "mc/core/request.h"

using mc::ByteView;
using mc::Chunk;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::Request;

TEST_CASE("ALC-01 positive control: the counter is live") {
    // A deliberate allocation the counter MUST see; if this fails, the whole file's zero-count
    // assertions below would be meaningless (a broken counter reads 0 for everything).
    const size_t count = mc::test::probeAllocCount();

    CHECK(count >= 1);
}

TEST_CASE("ALC-01 positive control (aligned): the counter sees an over-aligned allocation too") {
    // Alignment beyond __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16 on every platform this project
    // builds for) routes `new`/`delete` through the align_val_t overloads in alloc_counter.h,
    // not the plain ones above; this proves those are counted too, not just linked in unused.
    struct alignas(64) Aligned64 {
        uint8_t bytes[64];
    };

    mc::test::resetAllocCount();
    auto* p = new Aligned64();
    size_t count = mc::test::allocCount();
    auto address = reinterpret_cast<uintptr_t>(p);
    delete p;

    CHECK(count >= 1);
    CHECK(address % 64 == 0); // also confirms _aligned_malloc actually honoured the alignment.
}

TEST_CASE("ALC-01 parseDevice: zero allocations") {
    // Warm-up: run once first, discarding the result, in case anything is lazily initialized
    // only on its very first call ever (spec's own caution: "after a warm-up").
    (void)mc::parseDevice("D100");

    mc::test::resetAllocCount();
    auto result = mc::parseDevice("TN1234");
    size_t count = mc::test::allocCount();

    REQUIRE(result.hasValue());
    CHECK(result.value().type == DeviceType::TN);
    CHECK(count == 0);
}

TEST_CASE("ALC-01 validate: zero allocations") {
    Request warmReq = Request::readWords(Device{DeviceType::D, 100}, 1);
    FrameConfig warmCfg = FrameConfig::frame3E();
    (void)mc::validate(warmReq, warmCfg);

    Request r = Request::readWords(Device{DeviceType::D, 200}, 10);
    FrameConfig cfg = FrameConfig::frame3E();

    mc::test::resetAllocCount();
    auto result = mc::validate(r, cfg);
    size_t count = mc::test::allocCount();

    CHECK(result.hasValue());
    CHECK(count == 0);
}

TEST_CASE("ALC-01 chunk: zero allocations") {
    // 1C WR/QR on a bit device is 32 words per command (spec section 4.4); 50 needs 2 chunks,
    // exercising the loop rather than a single trivial pass.
    Request warmReq = Request::readWords(Device{DeviceType::M, 0}, 50);
    FrameConfig warmCfg = FrameConfig::frame1C();
    Chunk warmChunks[4];
    (void)mc::chunk(warmReq, warmCfg, warmChunks, 4);

    Request r = Request::readWords(Device{DeviceType::M, 0}, 50);
    FrameConfig cfg = FrameConfig::frame1C();
    Chunk chunks[4];

    mc::test::resetAllocCount();
    auto result = mc::chunk(r, cfg, chunks, 4);
    size_t count = mc::test::allocCount();

    REQUIRE(result.hasValue());
    CHECK(result.value() == 2);
    CHECK(count == 0);
}

TEST_CASE("ALC-01 convert::float64At: zero allocations") {
    // D0..D3 = 0.0 (low word first); the exact value does not matter, only that reading it
    // allocates nothing. A plain stack array avoids using fromFloat64() here, which itself
    // allocates one ByteBuf (that allocation belongs to fromFloat64's own contract, not
    // float64At's, and must not leak into this measurement).
    uint8_t bytes[8] = {0, 0, 0, 0, 0, 0, 0xF0, 0x3F}; // 1.0 as IEEE-754 double, little-endian.
    ByteView view{bytes, sizeof(bytes)};

    (void)mc::convert::float64At(view, 0);

    mc::test::resetAllocCount();
    double v = mc::convert::float64At(view, 0);
    size_t count = mc::test::allocCount();

    CHECK(v == 1.0);
    CHECK(count == 0);
}

TEST_CASE("ALC-01 hexDump: zero allocations") {
    uint8_t bytes[8] = {0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x10};
    ByteView view{bytes, sizeof(bytes)};
    char out[64];

    (void)mc::hexDump(view, out, sizeof(out), true);

    mc::test::resetAllocCount();
    size_t len = mc::hexDump(view, out, sizeof(out), true);
    size_t count = mc::test::allocCount();

    CHECK(len > 0);
    CHECK(count == 0);
}

TEST_CASE("ALC-01 formatDevice: zero allocations") {
    Device d{DeviceType::TN, 1234};
    char out[16];

    (void)mc::formatDevice(d, out, sizeof(out));

    mc::test::resetAllocCount();
    size_t len = mc::formatDevice(d, out, sizeof(out));
    size_t count = mc::test::allocCount();

    CHECK(len == 6); // "TN1234".
    CHECK(count == 0);
}

TEST_CASE("ALC-01 XYN parseDevice and formatDevice with Octal: zero allocations") {
    (void)mc::parseDevice("X10", mc::XyNumbering::Octal);
    char out[16];
    (void)mc::formatDevice(Device{DeviceType::X, 8}, out, sizeof(out), mc::XyNumbering::Octal);

    mc::test::resetAllocCount();
    auto parsed = mc::parseDevice("Y377", mc::XyNumbering::Octal);
    size_t len =
        mc::formatDevice(Device{DeviceType::X, 255}, out, sizeof(out), mc::XyNumbering::Octal);
    size_t count = mc::test::allocCount();

    REQUIRE(parsed.hasValue());
    CHECK(parsed.value().number == 255u);
    CHECK(len == 4); // "X377".
    CHECK(count == 0);
}
