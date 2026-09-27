#include "doctest/doctest.h"

#include "mc/core/frame_config.h"
#include "mc/core/request.h"

#include <array>

using mc::BitLayout;
using mc::C1CommandSet;
using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::FrameType;
using mc::Op;
using mc::PlcSeries;
using mc::Request;
using mc::SerialFormat;

TEST_CASE("CFG-01 Named-constructor defaults equal spec section 8.3") {
    SUBCASE("frame3E") {
        FrameConfig cfg = FrameConfig::frame3E();
        CHECK(cfg.frame == FrameType::F3E);
        CHECK(cfg.code == DataCode::Binary);
        CHECK(cfg.network == 0x00);
        CHECK(cfg.pc == 0xFF);
        CHECK(cfg.io == 0x03FF);
        CHECK(cfg.station == 0x00);
        CHECK(cfg.monitoringTimer == 0x0010);
        CHECK(cfg.series == PlcSeries::QL);
        CHECK_FALSE(cfg.checkRoute);

        FrameConfig asciiCfg = FrameConfig::frame3E(DataCode::Ascii);
        CHECK(asciiCfg.code == DataCode::Ascii);
    }

    SUBCASE("frame1E") {
        FrameConfig cfg = FrameConfig::frame1E();
        CHECK(cfg.frame == FrameType::F1E);
        CHECK(cfg.code == DataCode::Binary);
        CHECK(cfg.pc == 0xFF);
        CHECK(cfg.monitoringTimer == 0x000A);
        CHECK_FALSE(cfg.checkRoute); // Ethernet default: false, per the Plan ("false for 3E/1E").
    }

    SUBCASE("frame3C") {
        FrameConfig cfg = FrameConfig::frame3C();
        CHECK(cfg.frame == FrameType::F3C);
        CHECK(cfg.code == DataCode::Ascii);
        CHECK(cfg.format == SerialFormat::Format1);
        CHECK(cfg.stationNo == 0x00);
        CHECK(cfg.network == 0x00);
        CHECK(cfg.pc == 0xFF);
        CHECK(cfg.selfStation == 0x00);
        CHECK(cfg.sumCheck);
        CHECK(cfg.blockNo == 0x00);
        CHECK(cfg.series == PlcSeries::QL);
        CHECK(cfg.sendEotOnError);
        CHECK(cfg.checkRoute); // Serial default: true, per the Plan ("true for 3C/1C").
        CHECK(cfg.checkBlockNo);
        CHECK_FALSE(cfg.f3ShortResponseHasSum);
    }

    SUBCASE("frame1C") {
        FrameConfig cfg = FrameConfig::frame1C();
        CHECK(cfg.frame == FrameType::F1C);
        CHECK(cfg.code == DataCode::Ascii);
        CHECK(cfg.format == SerialFormat::Format1);
        CHECK(cfg.stationNo == 0x00);
        CHECK(cfg.pc == 0xFF);
        CHECK(cfg.messageWait == 0);
        CHECK(cfg.sumCheck);
        CHECK(cfg.blockNo == 0x00);
        CHECK(cfg.commandSet == C1CommandSet::ACPU);
        CHECK(cfg.sendEotOnError);
        CHECK(cfg.checkRoute); // Serial default: true, per the Plan ("true for 3C/1C").
    }
}

TEST_CASE("CFG-02 validate() rejects Format5 for every frame") {
    std::array<FrameConfig, 4> configs{FrameConfig::frame3E(), FrameConfig::frame1E(),
                                        FrameConfig::frame3C(), FrameConfig::frame1C()};
    for (FrameConfig& cfg : configs) {
        cfg.format = SerialFormat::Format5;
        auto result = cfg.validate();
        CHECK_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::InvalidConfig);
    }
}

TEST_CASE("validate() rejects F4E/F4C (Checkpoint A coverage gap)") {
    // No frame4E()/frame1C()-style named constructor exists for these (reserved for v2), so the
    // only way to construct one is to override `frame` after building an otherwise-valid config.
    FrameConfig f4e = FrameConfig::frame3E();
    f4e.frame = FrameType::F4E;
    auto r4e = f4e.validate();
    CHECK_FALSE(r4e.hasValue());
    CHECK(r4e.error().code == ErrorCode::InvalidConfig);

    FrameConfig f4c = FrameConfig::frame3C();
    f4c.frame = FrameType::F4C;
    auto r4c = f4c.validate();
    CHECK_FALSE(r4c.hasValue());
    CHECK(r4c.error().code == ErrorCode::InvalidConfig);
}

TEST_CASE("CFG-03 validate() rejects messageWait 16 and a zero monitoringTimer without a timeout") {
    SUBCASE("messageWait") {
        FrameConfig cfg = FrameConfig::frame1C();
        cfg.messageWait = 15;
        CHECK(cfg.validate().hasValue()); // boundary: 15 is still valid.

        cfg.messageWait = 16;
        auto result = cfg.validate();
        CHECK_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::InvalidConfig);
    }

    SUBCASE("monitoringTimer 0 without an explicit timeout") {
        FrameConfig cfg = FrameConfig::frame3E();
        cfg.monitoringTimer = 0;
        cfg.timeoutMs = 0;
        auto result = cfg.validate();
        CHECK_FALSE(result.hasValue());
        CHECK(result.error().code == ErrorCode::InvalidConfig);

        cfg.timeoutMs = 5000; // an explicit timeout makes "wait forever" acceptable.
        CHECK(cfg.validate().hasValue());
    }
}

TEST_CASE("CFG-04 validate() forces Ascii for 3C and 1C") {
    FrameConfig c3 = FrameConfig::frame3C();
    CHECK(c3.validate().hasValue()); // default code is already Ascii.
    c3.code = DataCode::Binary;
    auto r3 = c3.validate();
    CHECK_FALSE(r3.hasValue());
    CHECK(r3.error().code == ErrorCode::InvalidConfig);

    FrameConfig c1 = FrameConfig::frame1C();
    c1.code = DataCode::Binary;
    auto r1 = c1.validate();
    CHECK_FALSE(r1.hasValue());
    CHECK(r1.error().code == ErrorCode::InvalidConfig);
}

TEST_CASE("CFG-05 effectiveTimeoutMs derivation") {
    CHECK(FrameConfig::frame3E().effectiveTimeoutMs() == 5000);  // 0010H * 250 + 1000.
    CHECK(FrameConfig::frame1E().effectiveTimeoutMs() == 3500);  // 000AH * 250 + 1000.
    CHECK(FrameConfig::frame3C().effectiveTimeoutMs() == 3000);  // serial: flat 3 s.
    CHECK(FrameConfig::frame1C().effectiveTimeoutMs() == 3000);  // serial: flat 3 s.

    FrameConfig explicitTimeout = FrameConfig::frame3E();
    explicitTimeout.timeoutMs = 12345;
    CHECK(explicitTimeout.effectiveTimeoutMs() == 12345); // explicit value always wins.
}

TEST_CASE("Request builders derive count and classify op") {
    auto rb = Request::readBits(Device{DeviceType::X, 0}, 10);
    CHECK(rb.op == Op::ReadBits);
    CHECK(rb.count == 10);
    CHECK(rb.isBitOp());
    CHECK_FALSE(rb.isWrite());

    auto rw = Request::readWords(Device{DeviceType::D, 0}, 20);
    CHECK(rw.op == Op::ReadWords);
    CHECK(rw.count == 20);
    CHECK_FALSE(rw.isBitOp());
    CHECK_FALSE(rw.isWrite());

    uint8_t bitBytes[4] = {1, 0, 1, 1};
    auto wb = Request::writeBits(Device{DeviceType::Y, 0}, mc::ByteView{bitBytes, 4});
    CHECK(wb.op == Op::WriteBits);
    CHECK(wb.count == 4); // BytePerPoint: one point per byte.
    CHECK(wb.isWrite());
    CHECK(wb.isBitOp());

    auto wbPacked = Request::writeBits(Device{DeviceType::Y, 0}, mc::ByteView{bitBytes, 2},
                                        BitLayout::PackedLsbFirst);
    CHECK(wbPacked.count == 16); // PackedLsbFirst: eight points per byte.

    uint8_t wordBytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    auto ww = Request::writeWords(Device{DeviceType::D, 0}, mc::ByteView{wordBytes, 8});
    CHECK(ww.op == Op::WriteWords);
    CHECK(ww.count == 4); // 8 bytes / 2.
    CHECK(ww.isWrite());
    CHECK_FALSE(ww.isBitOp());
}

TEST_CASE("validate() rule 1: count == 0 is rejected regardless of an otherwise-valid request") {
    Request r = Request::readWords(Device{DeviceType::D, 100}, 0);
    auto result = mc::validate(r, FrameConfig::frame3E());
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().code == ErrorCode::PointCount);
}

TEST_CASE("validate() rule 2 (DEV-13): a bit operation on a non-bit device is rejected") {
    Request r = Request::readBits(Device{DeviceType::D, 0}, 1);
    auto result = mc::validate(r, FrameConfig::frame3E());
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().code == ErrorCode::InvalidDevice);
}

TEST_CASE("validate() rule 3: an unsupported device for the frame family is rejected") {
    Request r = Request::readBits(Device{DeviceType::SM, 0}, 1);
    auto result = mc::validate(r, FrameConfig::frame1E());
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().code == ErrorCode::InvalidDevice);
}

TEST_CASE("validate() rule 4 (DEV-08 QnA Q/L overflow): device number exceeds the field width") {
    // D1000000: 7 decimal digits, over QnA Q/L ASCII's 6-digit limit (999999 max).
    Request rAscii = Request::readWords(Device{DeviceType::D, 1000000}, 1);
    FrameConfig cfgAscii = FrameConfig::frame3E(DataCode::Ascii);
    auto resultAscii = mc::validate(rAscii, cfgAscii);
    CHECK_FALSE(resultAscii.hasValue());
    CHECK(resultAscii.error().code == ErrorCode::InvalidDevice);
}

TEST_CASE("validate() rule 5 (DEV-12): word access to a bit device must be 16-aligned") {
    FrameConfig cfg = FrameConfig::frame1E();

    CHECK(mc::validate(Request::readWords(Device{DeviceType::X, 0x40}, 1), cfg).hasValue());
    CHECK_FALSE(mc::validate(Request::readWords(Device{DeviceType::X, 0x41}, 1), cfg).hasValue());
    CHECK(mc::validate(Request::readWords(Device{DeviceType::M, 9000}, 1), cfg).hasValue());
    CHECK_FALSE(mc::validate(Request::readWords(Device{DeviceType::M, 9008}, 1), cfg).hasValue());
    CHECK(mc::validate(Request::readWords(Device{DeviceType::M, 9016}, 1), cfg).hasValue());

    auto misaligned = mc::validate(Request::readWords(Device{DeviceType::X, 0x41}, 1), cfg);
    CHECK(misaligned.error().code == ErrorCode::InvalidDevice);
}

TEST_CASE("validate() rule 6: count above the frame family's field maximum is rejected") {
    Request r = Request::readBits(Device{DeviceType::X, 0}, 257); // 1E/1C cap at 256.
    auto result = mc::validate(r, FrameConfig::frame1E());
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().code == ErrorCode::PointCount);

    // 256 itself is still within the field maximum.
    CHECK(mc::validate(Request::readBits(Device{DeviceType::X, 0}, 256), FrameConfig::frame1E())
              .hasValue());
}

TEST_CASE("validate() rule 7: a write payload size that disagrees with count is rejected") {
    uint8_t payload[3] = {0, 0, 0};
    Request r = Request::writeWords(Device{DeviceType::D, 100}, mc::ByteView{payload, 3});
    // writeWords() derives count = 3 / 2 = 1, so the request as built is internally consistent;
    // force a mismatch the way core-protocol's caller could (count changed after construction).
    r.count = 2;
    auto result = mc::validate(r, FrameConfig::frame3E());
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().code == ErrorCode::DataSizeMismatch);
}

TEST_CASE("validate() rule 7: WriteBits sizing, both BitLayout shapes (Checkpoint A coverage "
          "gap)") {
    // Rule 7's WriteBits branch has two shapes (BytePerPoint vs PackedLsbFirst); the other rule-7
    // test above only exercises WriteWords, so neither WriteBits shape is covered otherwise.
    SUBCASE("PackedLsbFirst") {
        // 10 packed points need ceil(10/8) = 2 bytes.
        uint8_t packed[2] = {0, 0};
        Request r{};
        r.op = Op::WriteBits;
        r.head = Device{DeviceType::X, 0};
        r.count = 10;
        r.bitLayout = BitLayout::PackedLsbFirst;
        r.data = mc::ByteView{packed, 2};

        CHECK(mc::validate(r, FrameConfig::frame3E()).hasValue());

        r.data = mc::ByteView{packed, 1}; // one byte short of the 2 required.
        auto mismatched = mc::validate(r, FrameConfig::frame3E());
        CHECK_FALSE(mismatched.hasValue());
        CHECK(mismatched.error().code == ErrorCode::DataSizeMismatch);
    }
    SUBCASE("BytePerPoint") {
        // 10 points need exactly 10 bytes (one byte per point, the default layout).
        uint8_t bytePerPoint[10] = {0};
        Request r{};
        r.op = Op::WriteBits;
        r.head = Device{DeviceType::X, 0};
        r.count = 10;
        r.bitLayout = BitLayout::BytePerPoint;
        r.data = mc::ByteView{bytePerPoint, 10};

        CHECK(mc::validate(r, FrameConfig::frame3E()).hasValue());

        r.data = mc::ByteView{bytePerPoint, 9}; // one byte short of the 10 required.
        auto mismatched = mc::validate(r, FrameConfig::frame3E());
        CHECK_FALSE(mismatched.hasValue());
        CHECK(mismatched.error().code == ErrorCode::DataSizeMismatch);
    }
}
