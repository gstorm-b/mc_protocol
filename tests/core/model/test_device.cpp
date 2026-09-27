#include "doctest/doctest.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"

#include <string_view>

using mc::DataCode;
using mc::Device;
using mc::DeviceInfo;
using mc::DeviceKind;
using mc::DeviceType;
using mc::FrameConfig;
using mc::Radix;
using mc::Request;

namespace {

// One row per spec §3.2, typed directly from the reference table (not copied from
// src/core/model/device_table.cpp) so a transcription error in the implementation shows up as a
// test failure. "" / mc::kNoCode mean "this frame family has no code for this device" (spec
// footnotes 1 and 2 for SM/SD and L/S respectively; RD is iQ-R only).
struct ExpectedRow {
    DeviceType type;
    const char* symbol;
    DeviceKind kind;
    Radix radix;
    const char* qnaAsciiQL;
    uint16_t qnaBinQL;
    const char* qnaAsciiIqr;
    uint16_t qnaBinIqr;
    uint16_t e1Code;
    const char* c1Code;
};

constexpr uint16_t kNC = mc::kNoCode;

// clang-format off
constexpr ExpectedRow kExpected[] = {
    {DeviceType::SM,  "SM",  DeviceKind::Bit,  Radix::Dec, "SM", 0x91, "SM**", 0x0091, kNC,    ""   },
    {DeviceType::SD,  "SD",  DeviceKind::Word, Radix::Dec, "SD", 0xA9, "SD**", 0x00A9, kNC,    ""   },
    {DeviceType::X,   "X",   DeviceKind::Bit,  Radix::Hex, "X*", 0x9C, "X***", 0x009C, 0x5820, "X"  },
    {DeviceType::Y,   "Y",   DeviceKind::Bit,  Radix::Hex, "Y*", 0x9D, "Y***", 0x009D, 0x5920, "Y"  },
    {DeviceType::M,   "M",   DeviceKind::Bit,  Radix::Dec, "M*", 0x90, "M***", 0x0090, 0x4D20, "M"  },
    {DeviceType::L,   "L",   DeviceKind::Bit,  Radix::Dec, "L*", 0x92, "L***", 0x0092, 0x4D20, "L"  },
    {DeviceType::F,   "F",   DeviceKind::Bit,  Radix::Dec, "F*", 0x93, "F***", 0x0093, 0x4620, "F"  },
    {DeviceType::V,   "V",   DeviceKind::Bit,  Radix::Dec, "V*", 0x94, "V***", 0x0094, kNC,    ""   },
    {DeviceType::B,   "B",   DeviceKind::Bit,  Radix::Hex, "B*", 0xA0, "B***", 0x00A0, 0x4220, "B"  },
    {DeviceType::D,   "D",   DeviceKind::Word, Radix::Dec, "D*", 0xA8, "D***", 0x00A8, 0x4420, "D"  },
    {DeviceType::W,   "W",   DeviceKind::Word, Radix::Hex, "W*", 0xB4, "W***", 0x00B4, 0x5720, "W"  },
    {DeviceType::TS,  "TS",  DeviceKind::Bit,  Radix::Dec, "TS", 0xC1, "TS**", 0x00C1, 0x5453, "TS" },
    {DeviceType::TC,  "TC",  DeviceKind::Bit,  Radix::Dec, "TC", 0xC0, "TC**", 0x00C0, 0x5443, "TC" },
    {DeviceType::TN,  "TN",  DeviceKind::Word, Radix::Dec, "TN", 0xC2, "TN**", 0x00C2, 0x544E, "TN" },
    {DeviceType::STS, "STS", DeviceKind::Bit,  Radix::Dec, "SS", 0xC7, "STS*", 0x00C7, kNC,    ""   },
    {DeviceType::STC, "STC", DeviceKind::Bit,  Radix::Dec, "SC", 0xC6, "STC*", 0x00C6, kNC,    ""   },
    {DeviceType::STN, "STN", DeviceKind::Word, Radix::Dec, "SN", 0xC8, "STN*", 0x00C8, kNC,    ""   },
    {DeviceType::CS,  "CS",  DeviceKind::Bit,  Radix::Dec, "CS", 0xC4, "CS**", 0x00C4, 0x4353, "CS" },
    {DeviceType::CC,  "CC",  DeviceKind::Bit,  Radix::Dec, "CC", 0xC3, "CC**", 0x00C3, 0x4343, "CC" },
    {DeviceType::CN,  "CN",  DeviceKind::Word, Radix::Dec, "CN", 0xC5, "CN**", 0x00C5, 0x434E, "CN" },
    {DeviceType::SB,  "SB",  DeviceKind::Bit,  Radix::Hex, "SB", 0xA1, "SB**", 0x00A1, kNC,    ""   },
    {DeviceType::SW,  "SW",  DeviceKind::Word, Radix::Hex, "SW", 0xB5, "SW**", 0x00B5, kNC,    ""   },
    {DeviceType::S,   "S",   DeviceKind::Bit,  Radix::Dec, "S*", 0x98, "S***", 0x0098, 0x4D20, "S"  },
    {DeviceType::DX,  "DX",  DeviceKind::Bit,  Radix::Hex, "DX", 0xA2, "DX**", 0x00A2, kNC,    ""   },
    {DeviceType::DY,  "DY",  DeviceKind::Bit,  Radix::Hex, "DY", 0xA3, "DY**", 0x00A3, kNC,    ""   },
    {DeviceType::Z,   "Z",   DeviceKind::Word, Radix::Dec, "Z*", 0xCC, "Z***", 0x00CC, kNC,    ""   },
    {DeviceType::R,   "R",   DeviceKind::Word, Radix::Dec, "R*", 0xAF, "R***", 0x00AF, 0x5220, "R"  },
    {DeviceType::ZR,  "ZR",  DeviceKind::Word, Radix::Hex, "ZR", 0xB0, "ZR**", 0x00B0, kNC,    ""   },
    {DeviceType::RD,  "RD",  DeviceKind::Word, Radix::Dec, "",   kNC,  "RD**", 0x002C, kNC,    ""   },
};
// clang-format on

} // namespace

TEST_CASE("DEV-07-data every row of kDeviceTable equals spec section 3.2") {
    for (const ExpectedRow& row : kExpected) {
        SUBCASE(row.symbol) {
            const DeviceInfo& info = mc::deviceInfo(row.type);
            CHECK(info.type == row.type);
            CHECK(std::string_view(info.symbol) == row.symbol);
            CHECK(info.kind == row.kind);
            CHECK(info.radix == row.radix);
            CHECK(std::string_view(info.qnaAsciiQL) == row.qnaAsciiQL);
            CHECK(info.qnaBinQL == row.qnaBinQL);
            CHECK(std::string_view(info.qnaAsciiIqr) == row.qnaAsciiIqr);
            CHECK(info.qnaBinIqr == row.qnaBinIqr);
            CHECK(info.e1Code == row.e1Code);
            CHECK(std::string_view(info.c1Code) == row.c1Code);
        }
    }
}

TEST_CASE("formatDevice: number 0 (Checkpoint A coverage gap)") {
    // formatDevice()'s "n == 0" branch is not exercised by any other test (D100, X1F, ... all
    // have a nonzero number); D0 is the canonical form with no digits to loop over.
    char buf[16];
    size_t n = mc::formatDevice(Device{DeviceType::D, 0}, buf, sizeof(buf));
    CHECK(n == 2);
    CHECK(std::string_view(buf, n) == "D0");
}

TEST_CASE("DEV-01 Basic parse: D100, d100") {
    auto a = mc::parseDevice("D100");
    REQUIRE(a.hasValue());
    CHECK(a.value().type == DeviceType::D);
    CHECK(a.value().number == 100);

    auto b = mc::parseDevice("d100");
    REQUIRE(b.hasValue());
    CHECK(b.value().type == DeviceType::D);
    CHECK(b.value().number == 100);

    // formatDevice round trip (canonical form, no padding).
    char buf[16];
    size_t n = mc::formatDevice(a.value(), buf, sizeof(buf));
    CHECK(n == 4);
    CHECK(std::string_view(buf, n) == "D100");
}

TEST_CASE("DEV-02 Hex parse: X1F, x1f") {
    auto a = mc::parseDevice("X1F");
    REQUIRE(a.hasValue());
    CHECK(a.value().type == DeviceType::X);
    CHECK(a.value().number == 0x1F);

    auto b = mc::parseDevice("x1f");
    REQUIRE(b.hasValue());
    CHECK(b.value().type == DeviceType::X);
    CHECK(b.value().number == 0x1F);

    char buf[16];
    size_t n = mc::formatDevice(a.value(), buf, sizeof(buf));
    CHECK(n == 3);
    CHECK(std::string_view(buf, n) == "X1F");
}

TEST_CASE("DEV-03 Wrong radix: M1F, X1G") {
    auto a = mc::parseDevice("M1F");
    CHECK_FALSE(a.hasValue());
    CHECK(a.error().code == mc::ErrorCode::InvalidDevice);

    auto b = mc::parseDevice("X1G");
    CHECK_FALSE(b.hasValue());
    CHECK(b.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-04 Longest match") {
    auto sm = mc::parseDevice("SM400");
    REQUIRE(sm.hasValue());
    CHECK(sm.value().type == DeviceType::SM);
    CHECK(sm.value().number == 400);

    auto sd = mc::parseDevice("SD10");
    REQUIRE(sd.hasValue());
    CHECK(sd.value().type == DeviceType::SD);
    CHECK(sd.value().number == 10);

    auto sb = mc::parseDevice("SB1F");
    REQUIRE(sb.hasValue());
    CHECK(sb.value().type == DeviceType::SB);
    CHECK(sb.value().number == 0x1F);

    auto sw = mc::parseDevice("SW10");
    REQUIRE(sw.hasValue());
    CHECK(sw.value().type == DeviceType::SW);
    CHECK(sw.value().number == 0x10);

    auto dx = mc::parseDevice("DX10");
    REQUIRE(dx.hasValue());
    CHECK(dx.value().type == DeviceType::DX);
    CHECK(dx.value().number == 0x10);

    auto zr = mc::parseDevice("ZR100");
    REQUIRE(zr.hasValue());
    CHECK(zr.value().type == DeviceType::ZR);
    CHECK(zr.value().number == 0x100);

    auto sts = mc::parseDevice("STS5");
    REQUIRE(sts.hasValue());
    CHECK(sts.value().type == DeviceType::STS);
    CHECK(sts.value().number == 5);

    auto s = mc::parseDevice("S5");
    REQUIRE(s.hasValue());
    CHECK(s.value().type == DeviceType::S);
    CHECK(s.value().number == 5);

    auto tn = mc::parseDevice("TN10");
    REQUIRE(tn.hasValue());
    CHECK(tn.value().type == DeviceType::TN);
    CHECK(tn.value().number == 10);
}

TEST_CASE("DEV-05 Ambiguous symbol: T10, C5") {
    auto t10 = mc::parseDevice("T10");
    CHECK_FALSE(t10.hasValue());
    CHECK(t10.error().code == mc::ErrorCode::InvalidDevice);

    auto c5 = mc::parseDevice("C5");
    CHECK_FALSE(c5.hasValue());
    CHECK(c5.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-06 Malformed: empty, D, 100, Q10") {
    auto empty = mc::parseDevice("");
    CHECK_FALSE(empty.hasValue());
    CHECK(empty.error().code == mc::ErrorCode::InvalidDevice);

    auto d = mc::parseDevice("D");
    CHECK_FALSE(d.hasValue());
    CHECK(d.error().code == mc::ErrorCode::InvalidDevice);

    auto num = mc::parseDevice("100");
    CHECK_FALSE(num.hasValue());
    CHECK(num.error().code == mc::ErrorCode::InvalidDevice);

    auto q10 = mc::parseDevice("Q10");
    CHECK_FALSE(q10.hasValue());
    CHECK(q10.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-08 QnA width overflow") {
    // D1000000: 7 decimal digits, over the Q/L ASCII limit of 6 digits (999999 max).
    auto ascii = mc::validate(Request::readWords(Device{DeviceType::D, 1000000}, 1),
                               FrameConfig::frame3E(DataCode::Ascii));
    CHECK_FALSE(ascii.hasValue());
    CHECK(ascii.error().code == mc::ErrorCode::InvalidDevice);

    // X1000000 (hex 0x1000000): over the Q/L Binary limit of 0xFFFFFF.
    auto binary = mc::validate(Request::readBits(Device{DeviceType::X, 0x1000000}, 1),
                                FrameConfig::frame3E(DataCode::Binary));
    CHECK_FALSE(binary.hasValue());
    CHECK(binary.error().code == mc::ErrorCode::InvalidDevice);

    // iQ-R: the same D1000000 fits comfortably within the 8-digit ASCII limit.
    FrameConfig iqr = FrameConfig::frame3E(DataCode::Ascii);
    iqr.series = mc::PlcSeries::IqR;
    auto validIqr = mc::validate(Request::readWords(Device{DeviceType::D, 1000000}, 1), iqr);
    CHECK(validIqr.hasValue());
}

TEST_CASE("DEV-08 iQ-R width boundary at 8 hex digits (Checkpoint A review, T-008)") {
    // iQ-R + Binary always uses an 8-hex-digit limit regardless of the device's own radix (rule 4
    // forces Radix::Hex for Binary), i.e. 16^8 - 1 == 0xFFFFFFFF -- exactly uint32_t's own
    // maximum. That makes this the one width limit in the whole module where "one past the
    // limit" cannot be constructed at all (there is no uint32_t value greater than 0xFFFFFFFF):
    // the meaningful check here is that the true maximum still validates correctly under the
    // digitLimit() fix (a naive fix could easily clamp to 0 instead of 0xFFFFFFFF).
    FrameConfig iqrBinary = FrameConfig::frame3E(DataCode::Binary);
    iqrBinary.series = mc::PlcSeries::IqR;
    auto atMax = mc::validate(Request::readBits(Device{DeviceType::X, 0xFFFFFFFFu}, 1), iqrBinary);
    CHECK(atMax.hasValue());

    // A genuinely two-sided boundary at the same 8-digit width, using the same digitLimit() code
    // path: iQ-R + ASCII + a decimal-radix device gives an 8-*decimal*-digit limit (99999999),
    // which — unlike the hex/Binary case above — has a representable "one past" value.
    FrameConfig iqrAscii = FrameConfig::frame3E(DataCode::Ascii);
    iqrAscii.series = mc::PlcSeries::IqR;
    auto decimalAtMax =
        mc::validate(Request::readWords(Device{DeviceType::D, 99999999}, 1), iqrAscii);
    CHECK(decimalAtMax.hasValue());
    auto decimalOnePast =
        mc::validate(Request::readWords(Device{DeviceType::D, 100000000}, 1), iqrAscii);
    CHECK_FALSE(decimalOnePast.hasValue());
    CHECK(decimalOnePast.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-09 1C width overflow") {
    FrameConfig acpu = FrameConfig::frame1C(); // commandSet defaults to ACPU.

    // M10000: 5 decimal digits, over ACPU's 4-digit limit (9999 max) for a non-T/C device.
    auto m = mc::validate(Request::readBits(Device{DeviceType::M, 10000}, 1), acpu);
    CHECK_FALSE(m.hasValue());
    CHECK(m.error().code == mc::ErrorCode::InvalidDevice);

    // TN1000: TN is a Timer/Counter device, ACPU's limit for those is 3 digits (999 max).
    auto tnAcpu = mc::validate(Request::readWords(Device{DeviceType::TN, 1000}, 1), acpu);
    CHECK_FALSE(tnAcpu.hasValue());
    CHECK(tnAcpu.error().code == mc::ErrorCode::InvalidDevice);

    // AnA/AnU's Timer/Counter limit is 5 digits (99999 max): 1000 fits.
    FrameConfig anA = FrameConfig::frame1C();
    anA.commandSet = mc::C1CommandSet::AnA;
    auto tnAnA = mc::validate(Request::readWords(Device{DeviceType::TN, 1000}, 1), anA);
    CHECK(tnAnA.hasValue());
}

TEST_CASE("DEV-10 Device not supported by the family") {
    FrameConfig e1 = FrameConfig::frame1E();
    CHECK_FALSE(mc::validate(Request::readBits(Device{DeviceType::SM, 0}, 1), e1).hasValue());
    CHECK_FALSE(mc::validate(Request::readWords(Device{DeviceType::SD, 0}, 1), e1).hasValue());
    CHECK_FALSE(mc::validate(Request::readWords(Device{DeviceType::ZR, 0}, 1), e1).hasValue());

    FrameConfig c1 = FrameConfig::frame1C();
    auto v0 = mc::validate(Request::readBits(Device{DeviceType::V, 0}, 1), c1);
    CHECK_FALSE(v0.hasValue());
    CHECK(v0.error().code == mc::ErrorCode::InvalidDevice);

    FrameConfig ql = FrameConfig::frame3E(); // series defaults to QL.
    auto rd0 = mc::validate(Request::readWords(Device{DeviceType::RD, 0}, 1), ql);
    CHECK_FALSE(rd0.hasValue());
    CHECK(rd0.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-11 L/S with 1E: accepted and aliased to M only when e1AliasLS is enabled") {
    FrameConfig withAlias = FrameConfig::frame1E();
    withAlias.e1AliasLS = true;
    auto accepted = mc::validate(Request::readBits(Device{DeviceType::L, 100}, 1), withAlias);
    CHECK(accepted.hasValue());

    FrameConfig withoutAlias = FrameConfig::frame1E(); // e1AliasLS defaults to false.
    auto rejected = mc::validate(Request::readBits(Device{DeviceType::L, 100}, 1), withoutAlias);
    CHECK_FALSE(rejected.hasValue());
    CHECK(rejected.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-12 Alignment to 16 for word-unit access to a bit device (1E, 1C)") {
    for (const FrameConfig& cfg : {FrameConfig::frame1E(), FrameConfig::frame1C()}) {
        CHECK(mc::validate(Request::readWords(Device{DeviceType::X, 0x40}, 1), cfg).hasValue());
        CHECK_FALSE(
            mc::validate(Request::readWords(Device{DeviceType::X, 0x41}, 1), cfg).hasValue());
        CHECK(mc::validate(Request::readWords(Device{DeviceType::M, 9000}, 1), cfg).hasValue());
        CHECK_FALSE(
            mc::validate(Request::readWords(Device{DeviceType::M, 9008}, 1), cfg).hasValue());
        CHECK(mc::validate(Request::readWords(Device{DeviceType::M, 9016}, 1), cfg).hasValue());
    }
}

TEST_CASE("DEV-13 Bit command on a word device") {
    auto result = mc::validate(Request::readBits(Device{DeviceType::D, 0}, 1),
                                FrameConfig::frame3E());
    CHECK_FALSE(result.hasValue());
    CHECK(result.error().code == mc::ErrorCode::InvalidDevice);
}

TEST_CASE("DEV-14 Device ==, !=, < order by table order then number") {
    Device d100{DeviceType::D, 100};
    Device d100Again{DeviceType::D, 100};
    Device d200{DeviceType::D, 200};
    Device x100{DeviceType::X, 100};

    CHECK(d100 == d100Again);
    CHECK_FALSE(d100 != d100Again);
    CHECK(d100 != d200);
    CHECK_FALSE(d100 == d200);

    // Same type: ordered by number.
    CHECK(d100 < d200);
    CHECK_FALSE(d200 < d100);

    // Different type: ordered by table order (X comes before D... check the actual declaration
    // order instead of assuming, since that is exactly what operator< promises).
    bool xBeforeD = x100 < d100;
    bool dBeforeX = d100 < x100;
    CHECK(xBeforeD != dBeforeX); // exactly one direction holds; DeviceType::X == index 2, D == 9
    CHECK(xBeforeD);             // X (index 2) sorts before D (index 9) in table order

    // Equal devices are neither less than each other.
    CHECK_FALSE(d100 < d100Again);
    CHECK_FALSE(d100Again < d100);
}
