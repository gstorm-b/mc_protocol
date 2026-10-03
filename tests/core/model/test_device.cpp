#include "doctest/doctest.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"

#include <string>
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

// ---- XYN-01..04: X/Y numbering for FX CPUs ----------------------------------------------
// Expected values are worked out by hand from the vendor tables: FX3 X000-X377 octal = indices
// 0-255 = 0000-00FFH (fx3-enet-adp.pdf 7.5), X0000-X0377 (fx3-data-communication.pdf), X0-X1777 on
// the FX5U (fx5-ethernet-communication.pdf), X10 octal = index 8.

namespace {

mc::Expected<Device> parseOctal(std::string_view text) {
    return mc::parseDevice(text, mc::XyNumbering::Octal);
}

std::string formatWith(Device d, mc::XyNumbering xy) {
    char buf[32];
    const size_t n = mc::formatDevice(d, buf, sizeof buf, xy);
    return std::string(buf, n);
}

} // namespace

TEST_CASE("XYN-01 parseDevice with Octal: X10 is index 8, X377 is 255, 8 and 9 are rejected") {
    auto x10 = parseOctal("X10");
    REQUIRE(x10.hasValue());
    CHECK(x10.value().type == DeviceType::X);
    CHECK(x10.value().number == 8u);

    auto x377 = parseOctal("X377");
    REQUIRE(x377.hasValue());
    CHECK(x377.value().number == 255u);

    auto x1777 = parseOctal("x1777"); // the last input of an FX5U, lower case symbol
    REQUIRE(x1777.hasValue());
    CHECK(x1777.value().number == 1023u);

    auto y20 = parseOctal("Y20");
    REQUIRE(y20.hasValue());
    CHECK(y20.value().type == DeviceType::Y);
    CHECK(y20.value().number == 16u);

    CHECK(parseOctal("X0").value().number == 0u);
    CHECK(parseOctal("X7").value().number == 7u);

    for (const char* bad : {"X8", "X9", "X19", "X18", "Y80", "X1F", "X1A", "Y7F", "X", "Y"}) {
        INFO(bad);
        auto r = parseOctal(bad);
        CHECK_FALSE(r.hasValue());
        if (!r.hasValue()) {
            CHECK(r.error().code == mc::ErrorCode::InvalidDevice);
        }
    }
}

TEST_CASE("XYN-02 formatDevice with Octal writes X and Y in octal and round-trips") {
    CHECK(formatWith(Device{DeviceType::X, 8}, mc::XyNumbering::Octal) == "X10");
    CHECK(formatWith(Device{DeviceType::X, 255}, mc::XyNumbering::Octal) == "X377");
    CHECK(formatWith(Device{DeviceType::Y, 16}, mc::XyNumbering::Octal) == "Y20");
    CHECK(formatWith(Device{DeviceType::X, 0}, mc::XyNumbering::Octal) == "X0");
    CHECK(formatWith(Device{DeviceType::X, 7}, mc::XyNumbering::Octal) == "X7");
    CHECK(formatWith(Device{DeviceType::X, 1023}, mc::XyNumbering::Octal) == "X1777");
    // The largest index needs 11 octal digits.
    CHECK(formatWith(Device{DeviceType::X, 0xFFFFFFFFu}, mc::XyNumbering::Octal) == "X37777777777");

    for (DeviceType t : {DeviceType::X, DeviceType::Y}) {
        for (uint32_t n : {0u, 1u, 7u, 8u, 63u, 64u, 255u, 256u, 1023u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
            const std::string text = formatWith(Device{t, n}, mc::XyNumbering::Octal);
            auto back = mc::parseDevice(text, mc::XyNumbering::Octal);
            INFO(text);
            REQUIRE(back.hasValue());
            CHECK(back.value() == (Device{t, n}));
        }
    }

    // The snprintf-like contract holds with octal digits too.
    char small[4];
    CHECK(mc::formatDevice(Device{DeviceType::X, 255}, small, sizeof small,
                           mc::XyNumbering::Octal) == 4u);
    CHECK(std::string(small) == "X37");
}

TEST_CASE("XYN-03 Hex overloads and every other symbol are unaffected by the notation") {
    // The one-argument forms are the Hex forms.
    CHECK(mc::parseDevice("X10").value().number == 16u);
    CHECK(mc::parseDevice("X10", mc::XyNumbering::Hex).value().number == 16u);
    CHECK(mc::parseDevice("X1F", mc::XyNumbering::Hex).value().number == 31u);
    char buf[16];
    CHECK(mc::formatDevice(Device{DeviceType::X, 16}, buf, sizeof buf) == 3u);
    CHECK(std::string(buf) == "X10");
    CHECK(formatWith(Device{DeviceType::X, 31}, mc::XyNumbering::Hex) == "X1F");

    // Symbols other than X and Y keep their own radix under Octal: M and D decimal, B and W hex.
    CHECK(parseOctal("M100").value().number == 100u);
    CHECK(parseOctal("D99").value().number == 99u);
    CHECK(parseOctal("B1F").value().number == 31u);
    CHECK(parseOctal("W10").value().number == 16u);
    CHECK(parseOctal("TN19").value().number == 19u);
    CHECK_FALSE(parseOctal("M1F").hasValue());
    CHECK(formatWith(Device{DeviceType::M, 100}, mc::XyNumbering::Octal) == "M100");
    CHECK(formatWith(Device{DeviceType::B, 31}, mc::XyNumbering::Octal) == "B1F");
    CHECK(formatWith(Device{DeviceType::W, 16}, mc::XyNumbering::Octal) == "W10");
    // DX and DY are direct access symbols of their own, not X and Y.
    CHECK(parseOctal("DX1F").value().number == 31u);
    CHECK(parseOctal("DY10").value().number == 16u);
}

TEST_CASE("XYN-06 parseDevice rejects a number that does not fit 32 bits, in every radix") {
    const auto invalid = [](std::string_view text, mc::XyNumbering xy) {
        const auto r = mc::parseDevice(text, xy);
        return !r.hasValue() && r.error().code == mc::ErrorCode::InvalidDevice;
    };
    // 2^32 = 4294967296 = 0x100000000 = 0o40000000000; the largest value fits, one more does not.
    CHECK(invalid("D4294967296", mc::XyNumbering::Hex));
    CHECK(invalid("D99999999999", mc::XyNumbering::Hex));
    CHECK(invalid("X100000000", mc::XyNumbering::Hex));
    CHECK(invalid("W100000000", mc::XyNumbering::Hex));
    CHECK(invalid("X40000000000", mc::XyNumbering::Octal));
    CHECK(invalid("Y77777777777777", mc::XyNumbering::Octal));
    CHECK(invalid("D4294967296", mc::XyNumbering::Octal)); // D stays decimal under Octal
    CHECK(invalid("X100000000", mc::XyNumbering::Hex));

    CHECK(mc::parseDevice("D4294967295").value().number == 0xFFFFFFFFu);
    CHECK(mc::parseDevice("X0FFFFFFFF").value().number == 0xFFFFFFFFu);
    CHECK(mc::parseDevice("X37777777777", mc::XyNumbering::Octal).value().number == 0xFFFFFFFFu);
    // Leading zeros do not count against the limit.
    CHECK(mc::parseDevice("D00000000000000000000100").value().number == 100u);
    CHECK(mc::parseDevice("X0000000000000010", mc::XyNumbering::Octal).value().number == 8u);
}

TEST_CASE("XYN-04 validate() counts the digits actually written") {
    const auto readX = [](uint32_t number) {
        return Request::readBits(Device{DeviceType::X, number}, 1);
    };
    const auto ok = [](const mc::Expected<void>& r) { return r.hasValue(); };

    // 1C ACPU: 4 characters. Octal: 07777 = 4095 is the last index; hex: 0xFFFF.
    FrameConfig oct1c = FrameConfig::frame1C();
    oct1c.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK(ok(mc::validate(readX(4095), oct1c)));
    CHECK_FALSE(ok(mc::validate(readX(4096), oct1c)));
    FrameConfig hex1c = FrameConfig::frame1C();
    CHECK(ok(mc::validate(readX(4096), hex1c)));
    CHECK(ok(mc::validate(readX(0xFFFF), hex1c)));
    CHECK_FALSE(ok(mc::validate(readX(0x10000), hex1c)));
    // The notation of the text has no effect on a Device.
    oct1c.xyNotation = mc::XyNumbering::Octal;
    CHECK_FALSE(ok(mc::validate(readX(4096), oct1c)));
    // Another device is not narrowed: M9999 stays the 1C ACPU limit.
    CHECK(ok(mc::validate(Request::readBits(Device{DeviceType::M, 9999}, 1), oct1c)));
    // AnA: 6 characters, octal 777777 = 262143.
    oct1c.commandSet = mc::C1CommandSet::AnA;
    CHECK(ok(mc::validate(readX(262143), oct1c)));
    CHECK_FALSE(ok(mc::validate(readX(262144), oct1c)));

    // 3E ASCII Q/L: 6 characters, octal 777777 = 262143; iQ-R 8 characters, octal 77777777.
    FrameConfig oct3e = FrameConfig::frame3E(DataCode::Ascii);
    oct3e.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK(ok(mc::validate(readX(262143), oct3e)));
    CHECK_FALSE(ok(mc::validate(readX(262144), oct3e)));
    FrameConfig hex3e = FrameConfig::frame3E(DataCode::Ascii);
    CHECK(ok(mc::validate(readX(262144), hex3e)));
    CHECK(ok(mc::validate(readX(0xFFFFFF), hex3e)));
    CHECK_FALSE(ok(mc::validate(readX(0x1000000), hex3e)));
    oct3e.series = mc::PlcSeries::IqR;
    CHECK(ok(mc::validate(readX(16777215), oct3e)));
    CHECK_FALSE(ok(mc::validate(readX(16777216), oct3e)));

    // 3C is always ASCII: the same rule.
    FrameConfig oct3c = FrameConfig::frame3C();
    oct3c.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK_FALSE(ok(mc::validate(readX(262144), oct3c)));

    // 1E ASCII: 8 characters; octal reaches 0xFFFFFF, hex the whole uint32_t.
    FrameConfig oct1eAscii = FrameConfig::frame1E(DataCode::Ascii);
    oct1eAscii.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK(ok(mc::validate(readX(16777215), oct1eAscii)));
    CHECK_FALSE(ok(mc::validate(readX(16777216), oct1eAscii)));
    FrameConfig hex1eAscii = FrameConfig::frame1E(DataCode::Ascii);
    CHECK(ok(mc::validate(readX(16777216), hex1eAscii)));
    CHECK(ok(mc::validate(readX(0xFFFFFFFFu), hex1eAscii)));
    // Only X and Y are narrowed.
    CHECK(ok(mc::validate(Request::readBits(Device{DeviceType::M, 16777216u}, 1), oct1eAscii)));

    // Binary frames carry the index: the digits setting never narrows them.
    FrameConfig octBin = FrameConfig::frame3E(DataCode::Binary);
    octBin.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK(ok(mc::validate(readX(0xFFFFFF), octBin)));
    CHECK_FALSE(ok(mc::validate(readX(0x1000000), octBin)));
    CHECK(ok(mc::validate(readX(262144), octBin)));
    FrameConfig octBin1e = FrameConfig::frame1E(DataCode::Binary);
    octBin1e.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK(ok(mc::validate(readX(0xFFFFFFFFu), octBin1e)));

    // The word-unit alignment is on indices: X20 octal = 16 is a multiple of 16, X10 octal is not.
    FrameConfig oct1cAlign = FrameConfig::frame1C();
    oct1cAlign.xyAsciiDigits = mc::XyNumbering::Octal;
    CHECK(ok(mc::validate(Request::readWords(Device{DeviceType::X, 16}, 1), oct1cAlign)));
    CHECK_FALSE(ok(mc::validate(Request::readWords(Device{DeviceType::X, 8}, 1), oct1cAlign)));
}
