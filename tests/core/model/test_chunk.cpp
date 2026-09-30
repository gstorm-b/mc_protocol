#include "doctest/doctest.h"

#include "mc/core/limits.h"

#include <vector>

using mc::BitLayout;
using mc::ByteView;
using mc::Chunk;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::Request;

TEST_CASE("CHK-01 chunk(): exact fit across multiple commands") {
    // 1C WR/QR on a bit device is 32 words per command (spec section 4.4); 64 = 2 x 32.
    Request r = Request::readWords(Device{DeviceType::M, 0}, 64);
    FrameConfig cfg = FrameConfig::frame1C();

    auto count = mc::chunkCount(r, cfg);
    REQUIRE(count.hasValue());
    CHECK(count.value() == 2);

    Chunk chunks[2];
    auto written = mc::chunk(r, cfg, chunks, 2);
    REQUIRE(written.hasValue());
    CHECK(written.value() == 2);

    CHECK(chunks[0].headNumber == 0);
    CHECK(chunks[0].count == 32);
    CHECK(chunks[0].dataOffset == 0);
    // Word-unit access to a bit device steps the head by 16 per word (spec section 2.4).
    CHECK(chunks[1].headNumber == 32 * 16);
    CHECK(chunks[1].count == 32);
    CHECK(chunks[1].dataOffset == 0); // read: no data offset.
}

TEST_CASE("CHK-02 chunk(): a remainder produces a smaller final chunk") {
    Request r = Request::readWords(Device{DeviceType::M, 0}, 50); // 32 + 18.
    FrameConfig cfg = FrameConfig::frame1C();

    auto count = mc::chunkCount(r, cfg);
    REQUIRE(count.hasValue());
    CHECK(count.value() == 2);

    Chunk chunks[2];
    REQUIRE(mc::chunk(r, cfg, chunks, 2).hasValue());
    CHECK(chunks[0].count == 32);
    CHECK(chunks[1].headNumber == 32 * 16);
    CHECK(chunks[1].count == 18);
}

TEST_CASE("CHK-03 chunk(): word-op-on-bit-device head steps by 16 per word, across many chunks") {
    // 100 = 32 + 32 + 32 + 4: four chunks, to show the step accumulates rather than resetting.
    Request r = Request::readWords(Device{DeviceType::M, 0}, 100);
    FrameConfig cfg = FrameConfig::frame1C();

    auto count = mc::chunkCount(r, cfg);
    REQUIRE(count.hasValue());
    CHECK(count.value() == 4);

    Chunk chunks[4];
    REQUIRE(mc::chunk(r, cfg, chunks, 4).hasValue());
    CHECK(chunks[0].headNumber == 0);
    CHECK(chunks[0].count == 32);
    CHECK(chunks[1].headNumber == 32 * 16);
    CHECK(chunks[1].count == 32);
    CHECK(chunks[2].headNumber == 64 * 16);
    CHECK(chunks[2].count == 32);
    CHECK(chunks[3].headNumber == 96 * 16);
    CHECK(chunks[3].count == 4);
}

TEST_CASE("CHK-04 chunk(): a write exceeding the limit is PointCount without splitWrites") {
    // 1C WW/QW on a word device is 64 points per command (spec section 4.4); 100 needs 2.
    std::vector<uint8_t> payload(100 * 2, 0);
    ByteView data{payload.data(), payload.size()};
    Request r = Request::writeWords(Device{DeviceType::D, 100}, data);
    FrameConfig cfg = FrameConfig::frame1C(); // splitWrites defaults to false.

    auto count = mc::chunkCount(r, cfg);
    CHECK_FALSE(count.hasValue());
    CHECK(count.error().code == ErrorCode::PointCount);

    Chunk chunks[2];
    auto written = mc::chunk(r, cfg, chunks, 2);
    CHECK_FALSE(written.hasValue());
    CHECK(written.error().code == ErrorCode::PointCount);
}

TEST_CASE("CHK-05 chunk(): a write exceeding the limit splits when splitWrites is enabled") {
    std::vector<uint8_t> payload(100 * 2, 0);
    ByteView data{payload.data(), payload.size()};
    Request r = Request::writeWords(Device{DeviceType::D, 100}, data);
    FrameConfig cfg = FrameConfig::frame1C();
    cfg.splitWrites = true;

    auto count = mc::chunkCount(r, cfg);
    REQUIRE(count.hasValue());
    CHECK(count.value() == 2);

    Chunk chunks[2];
    auto written = mc::chunk(r, cfg, chunks, 2);
    REQUIRE(written.hasValue());
    CHECK(written.value() == 2);

    // Word device: step is 1 per point, not 16 (that rule is only for word access to a bit device).
    CHECK(chunks[0].headNumber == 100);
    CHECK(chunks[0].count == 64);
    CHECK(chunks[0].dataOffset == 0);
    CHECK(chunks[1].headNumber == 164);
    CHECK(chunks[1].count == 36);
    CHECK(chunks[1].dataOffset == 64 * 2); // two bytes per word.
}

TEST_CASE("CHK-06 chunk(): BufferTooSmall when capacity is smaller than the chunk count") {
    Request r = Request::readWords(Device{DeviceType::M, 0}, 64); // needs 2 chunks (CHK-01).
    FrameConfig cfg = FrameConfig::frame1C();

    Chunk chunks[1];
    auto written = mc::chunk(r, cfg, chunks, 1);
    CHECK_FALSE(written.hasValue());
    CHECK(written.error().code == ErrorCode::BufferTooSmall);
}

TEST_CASE("chunkCount()/chunk(): an already-invalid request propagates validate()'s error "
          "(Checkpoint A coverage gap)") {
    Request r = Request::readWords(Device{DeviceType::D, 100}, 0); // count == 0: rule 1.
    FrameConfig cfg = FrameConfig::frame3E();

    auto count = mc::chunkCount(r, cfg);
    CHECK_FALSE(count.hasValue());
    CHECK(count.error().code == ErrorCode::PointCount);

    Chunk chunks[1];
    auto written = mc::chunk(r, cfg, chunks, 1);
    CHECK_FALSE(written.hasValue());
    CHECK(written.error().code == ErrorCode::PointCount);
}

TEST_CASE("chunk(): WriteBits/PackedLsbFirst dataOffset in bytes, across chunks (Checkpoint A "
          "coverage gap)") {
    // 1C BW/JW is 160 points per command (spec section 4.4); 170 needs 2 chunks with
    // splitWrites enabled. 170 packed points need ceil(170 / 8) = 22 bytes.
    std::vector<uint8_t> payload(22, 0);
    Request r{};
    r.op = mc::Op::WriteBits;
    r.head = Device{DeviceType::X, 0};
    r.count = 170;
    r.bitLayout = BitLayout::PackedLsbFirst;
    r.data = ByteView{payload.data(), payload.size()};

    FrameConfig cfg = FrameConfig::frame1C();
    cfg.splitWrites = true;

    auto count = mc::chunkCount(r, cfg);
    REQUIRE(count.hasValue());
    CHECK(count.value() == 2);

    Chunk chunks[2];
    auto written = mc::chunk(r, cfg, chunks, 2);
    REQUIRE(written.hasValue());
    CHECK(written.value() == 2);

    CHECK(chunks[0].count == 160);
    CHECK(chunks[0].dataOffset == 0);
    CHECK(chunks[1].count == 10);
    CHECK(chunks[1].dataOffset == 20); // 160 packed points / 8 = 20 bytes in.
}

TEST_CASE("CHK-07 chunkCount()/chunk(): a 1E or 1C read above the 256-unit field maximum splits "
          "by maxPoints() (spec section 8.5), it is not refused as PointCount") {
    SUBCASE("1E word read of a word device: 256 per command, 2000 words = 7 x 256 + 208") {
        Request r = Request::readWords(Device{DeviceType::D, 0}, 2000);
        FrameConfig cfg = FrameConfig::frame1E();
        auto count = mc::chunkCount(r, cfg);
        REQUIRE(count.hasValue());
        CHECK(count.value() == 8);

        Chunk chunks[8];
        auto written = mc::chunk(r, cfg, chunks, 8);
        REQUIRE(written.hasValue());
        CHECK(written.value() == 8);
        for (uint32_t i = 0; i < 7; ++i) {
            CHECK(chunks[i].headNumber == i * 256);
            CHECK(chunks[i].count == 256);
        }
        CHECK(chunks[7].headNumber == 1792);
        CHECK(chunks[7].count == 208);
    }
    SUBCASE("1E word read of a bit device: 128 words per command, step 16 per word") {
        Request r = Request::readWords(Device{DeviceType::M, 0}, 300);
        FrameConfig cfg = FrameConfig::frame1E();
        auto count = mc::chunkCount(r, cfg);
        REQUIRE(count.hasValue());
        CHECK(count.value() == 3);
        Chunk chunks[3];
        REQUIRE(mc::chunk(r, cfg, chunks, 3).hasValue());
        CHECK(chunks[1].headNumber == 128 * 16);
        CHECK(chunks[2].headNumber == 256 * 16);
        CHECK(chunks[2].count == 44);
    }
    SUBCASE("1C word read of a word device: 64 per command, 300 words = 5 chunks") {
        Request r = Request::readWords(Device{DeviceType::D, 0}, 300);
        auto count = mc::chunkCount(r, FrameConfig::frame1C());
        REQUIRE(count.hasValue());
        CHECK(count.value() == 5);
    }
}

TEST_CASE("CHK-08 chunkCount()/chunk(): a 1E write above 256 units is PointCount without "
          "splitWrites and splits with it, rule 7 (payload size) still applies") {
    std::vector<uint8_t> payload(300 * 2, 0);
    ByteView data{payload.data(), payload.size()};
    Request r = Request::writeWords(Device{DeviceType::D, 0}, data);

    FrameConfig cfg = FrameConfig::frame1E(); // splitWrites defaults to false.
    auto refused = mc::chunkCount(r, cfg);
    CHECK_FALSE(refused.hasValue());
    CHECK(refused.error().code == ErrorCode::PointCount);

    cfg.splitWrites = true;
    auto count = mc::chunkCount(r, cfg);
    REQUIRE(count.hasValue());
    CHECK(count.value() == 2);
    Chunk chunks[2];
    REQUIRE(mc::chunk(r, cfg, chunks, 2).hasValue());
    CHECK(chunks[0].count == 256);
    CHECK(chunks[0].dataOffset == 0);
    CHECK(chunks[1].headNumber == 256);
    CHECK(chunks[1].count == 44);
    CHECK(chunks[1].dataOffset == 256 * 2);

    // A payload that does not match the count is DataSizeMismatch, whatever the count.
    Request bad = r;
    bad.data = ByteView{payload.data(), payload.size() - 2};
    auto mismatch = mc::chunkCount(bad, cfg);
    CHECK_FALSE(mismatch.hasValue());
    CHECK(mismatch.error().code == ErrorCode::DataSizeMismatch);
}

TEST_CASE("CHK-09 chunkCount(): the other validate() rules still apply to a request above the "
          "field maximum, and the encode-path rule 6 is unchanged") {
    FrameConfig cfg = FrameConfig::frame1E();

    // Device without a 1E code (SM): rule 3, whatever the count.
    auto noCode = mc::chunkCount(Request::readWords(Device{DeviceType::SM, 0}, 300), cfg);
    CHECK_FALSE(noCode.hasValue());
    CHECK(noCode.error().code == ErrorCode::InvalidDevice);

    // Word access to a bit device off a 16-point boundary: rule 5.
    auto unaligned = mc::chunkCount(Request::readWords(Device{DeviceType::M, 5}, 300), cfg);
    CHECK_FALSE(unaligned.hasValue());
    CHECK(unaligned.error().code == ErrorCode::InvalidDevice);

    // Rule 6 stays in validate(), which the encode path uses (1E-14): 257 points is PointCount,
    // 256 is fine, for 1E and 1C.
    for (const FrameConfig& frame : {FrameConfig::frame1E(), FrameConfig::frame1C()}) {
        auto over = mc::validate(Request::readWords(Device{DeviceType::D, 0}, 257), frame);
        CHECK_FALSE(over.hasValue());
        CHECK(over.error().code == ErrorCode::PointCount);
        CHECK(mc::validate(Request::readWords(Device{DeviceType::D, 0}, 256), frame).hasValue());
    }
}
