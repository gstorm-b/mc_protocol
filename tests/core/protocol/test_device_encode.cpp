// DEV-07 (wire half; the data half -- kDeviceTable itself -- is DEV-07-data in core-model,
// tests/core/model/test_device.cpp): every cell of spec `mc-protocol-frame-spec.md` §3.3's
// example table (D100, X1F, TN10, M1234, M9000 x 8 families), expected bytes typed from that
// table. Unsupported-device cases (acceptance criterion 3) follow in their own TEST_CASE.
#include "doctest/doctest.h"

#include "core/protocol/device_encode.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

using mc::ByteView;
using mc::C1CommandSet;
using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCategory;
using mc::ErrorCode;
using mc::MutableByteView;
using mc::PlcSeries;
using mc::detail::c1Device;
using mc::detail::c1DeviceSize;
using mc::detail::e1Device;
using mc::detail::e1DeviceSize;
using mc::detail::qnaDevice;
using mc::detail::qnaDeviceSize;

namespace {

// Compares `got` against `expectedText`'s own characters, byte for byte (ASCII cells: the
// device's own text code plus zero-padded digits, e.g. "D*000100" -- typed exactly as spec §3.3
// prints it, so a wrongly-space-padded or lower-case encoder fails this comparison).
void checkAscii(ByteView got, const char* expectedText) {
    size_t len = std::strlen(expectedText);
    REQUIRE(got.size == len);
    for (size_t i = 0; i < len; ++i) {
        CHECK(got.data[i] == static_cast<uint8_t>(expectedText[i]));
    }
}

// Compares `got` against `expectedBytes` (Binary cells).
void checkBinary(ByteView got, std::initializer_list<uint8_t> expectedBytes) {
    REQUIRE(got.size == expectedBytes.size());
    size_t i = 0;
    for (uint8_t b : expectedBytes) {
        CHECK(got.data[i] == b);
        ++i;
    }
}

} // namespace

TEST_CASE("DEV-07 (wire half): every cell of spec section 3.3's example table") {
    // The five devices of the table, in its own column order.
    const Device d100{DeviceType::D, 100};
    const Device x1f{DeviceType::X, 0x1F};
    const Device tn10{DeviceType::TN, 10};
    const Device m1234{DeviceType::M, 1234};
    const Device m9000{DeviceType::M, 9000};

    uint8_t buf[16];
    MutableByteView out{buf, sizeof(buf)};

    SUBCASE("D100 / QnA ASCII Q-L") {
        auto r = qnaDevice(d100, DataCode::Ascii, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        CHECK(r.value() == qnaDeviceSize(DataCode::Ascii, PlcSeries::QL));
        checkAscii(ByteView{buf, r.value()}, "D*000100");
    }
    SUBCASE("D100 / QnA Binary Q-L") {
        auto r = qnaDevice(d100, DataCode::Binary, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x64, 0x00, 0x00, 0xA8});
    }
    SUBCASE("D100 / QnA ASCII iQ-R") {
        auto r = qnaDevice(d100, DataCode::Ascii, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "D***00000100");
    }
    SUBCASE("D100 / QnA Binary iQ-R") {
        auto r = qnaDevice(d100, DataCode::Binary, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x64, 0x00, 0x00, 0x00, 0xA8, 0x00});
    }
    SUBCASE("D100 / 1E ASCII") {
        auto r = e1Device(d100, DataCode::Ascii, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "442000000064");
    }
    SUBCASE("D100 / 1E Binary") {
        auto r = e1Device(d100, DataCode::Binary, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x64, 0x00, 0x00, 0x00, 0x20, 0x44});
    }
    SUBCASE("D100 / 1C ACPU") {
        auto r = c1Device(d100, C1CommandSet::ACPU, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "D0100");
    }
    SUBCASE("D100 / 1C AnA-AnU") {
        auto r = c1Device(d100, C1CommandSet::AnA, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "D000100");
    }

    SUBCASE("X1F / QnA ASCII Q-L") {
        auto r = qnaDevice(x1f, DataCode::Ascii, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X*00001F");
    }
    SUBCASE("X1F / QnA Binary Q-L") {
        auto r = qnaDevice(x1f, DataCode::Binary, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x1F, 0x00, 0x00, 0x9C});
    }
    SUBCASE("X1F / QnA ASCII iQ-R") {
        auto r = qnaDevice(x1f, DataCode::Ascii, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X***0000001F");
    }
    SUBCASE("X1F / QnA Binary iQ-R") {
        auto r = qnaDevice(x1f, DataCode::Binary, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x1F, 0x00, 0x00, 0x00, 0x9C, 0x00});
    }
    SUBCASE("X1F / 1E ASCII") {
        auto r = e1Device(x1f, DataCode::Ascii, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "58200000001F");
    }
    SUBCASE("X1F / 1E Binary") {
        auto r = e1Device(x1f, DataCode::Binary, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x1F, 0x00, 0x00, 0x00, 0x20, 0x58});
    }
    SUBCASE("X1F / 1C ACPU") {
        auto r = c1Device(x1f, C1CommandSet::ACPU, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X001F");
    }
    SUBCASE("X1F / 1C AnA-AnU") {
        auto r = c1Device(x1f, C1CommandSet::AnA, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X00001F");
    }

    SUBCASE("TN10 / QnA ASCII Q-L") {
        auto r = qnaDevice(tn10, DataCode::Ascii, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "TN000010");
    }
    SUBCASE("TN10 / QnA Binary Q-L") {
        auto r = qnaDevice(tn10, DataCode::Binary, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x0A, 0x00, 0x00, 0xC2});
    }
    SUBCASE("TN10 / QnA ASCII iQ-R") {
        auto r = qnaDevice(tn10, DataCode::Ascii, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "TN**00000010");
    }
    SUBCASE("TN10 / QnA Binary iQ-R") {
        auto r = qnaDevice(tn10, DataCode::Binary, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x0A, 0x00, 0x00, 0x00, 0xC2, 0x00});
    }
    SUBCASE("TN10 / 1E ASCII") {
        auto r = e1Device(tn10, DataCode::Ascii, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "544E0000000A");
    }
    SUBCASE("TN10 / 1E Binary") {
        auto r = e1Device(tn10, DataCode::Binary, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x0A, 0x00, 0x00, 0x00, 0x4E, 0x54});
    }
    SUBCASE("TN10 / 1C ACPU") {
        auto r = c1Device(tn10, C1CommandSet::ACPU, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "TN010");
    }
    SUBCASE("TN10 / 1C AnA-AnU") {
        auto r = c1Device(tn10, C1CommandSet::AnA, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "TN00010");
    }

    SUBCASE("M1234 / QnA ASCII Q-L") {
        auto r = qnaDevice(m1234, DataCode::Ascii, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M*001234");
    }
    SUBCASE("M1234 / QnA Binary Q-L") {
        auto r = qnaDevice(m1234, DataCode::Binary, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0xD2, 0x04, 0x00, 0x90});
    }
    SUBCASE("M1234 / QnA ASCII iQ-R") {
        auto r = qnaDevice(m1234, DataCode::Ascii, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M***00001234");
    }
    SUBCASE("M1234 / QnA Binary iQ-R") {
        auto r = qnaDevice(m1234, DataCode::Binary, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0xD2, 0x04, 0x00, 0x00, 0x90, 0x00});
    }
    SUBCASE("M1234 / 1E ASCII") {
        auto r = e1Device(m1234, DataCode::Ascii, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "4D20000004D2");
    }
    SUBCASE("M1234 / 1E Binary") {
        auto r = e1Device(m1234, DataCode::Binary, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0xD2, 0x04, 0x00, 0x00, 0x20, 0x4D});
    }
    SUBCASE("M1234 / 1C ACPU") {
        auto r = c1Device(m1234, C1CommandSet::ACPU, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M1234");
    }
    SUBCASE("M1234 / 1C AnA-AnU") {
        auto r = c1Device(m1234, C1CommandSet::AnA, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M001234");
    }

    SUBCASE("M9000 / QnA ASCII Q-L") {
        auto r = qnaDevice(m9000, DataCode::Ascii, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M*009000");
    }
    SUBCASE("M9000 / QnA Binary Q-L") {
        auto r = qnaDevice(m9000, DataCode::Binary, PlcSeries::QL, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x28, 0x23, 0x00, 0x90});
    }
    SUBCASE("M9000 / QnA ASCII iQ-R") {
        auto r = qnaDevice(m9000, DataCode::Ascii, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M***00009000");
    }
    SUBCASE("M9000 / QnA Binary iQ-R") {
        auto r = qnaDevice(m9000, DataCode::Binary, PlcSeries::IqR, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x28, 0x23, 0x00, 0x00, 0x90, 0x00});
    }
    SUBCASE("M9000 / 1E ASCII") {
        auto r = e1Device(m9000, DataCode::Ascii, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "4D2000002328");
    }
    SUBCASE("M9000 / 1E Binary") {
        auto r = e1Device(m9000, DataCode::Binary, out);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0x28, 0x23, 0x00, 0x00, 0x20, 0x4D});
    }
    SUBCASE("M9000 / 1C ACPU") {
        auto r = c1Device(m9000, C1CommandSet::ACPU, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M9000");
    }
    SUBCASE("M9000 / 1C AnA-AnU") {
        auto r = c1Device(m9000, C1CommandSet::AnA, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "M009000");
    }
}

TEST_CASE("DEV-07 acceptance criterion 3: a device the family cannot encode is InvalidDevice") {
    uint8_t buf[16];
    MutableByteView out{buf, sizeof(buf)};

    auto checkInvalidDevice = [](auto result) {
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::InvalidDevice);
    };

    SUBCASE("RD has no QnA Q-L code (ASCII)") {
        checkInvalidDevice(
            qnaDevice(Device{DeviceType::RD, 0}, DataCode::Ascii, PlcSeries::QL, out));
    }
    SUBCASE("RD has no QnA Q-L code (Binary)") {
        checkInvalidDevice(
            qnaDevice(Device{DeviceType::RD, 0}, DataCode::Binary, PlcSeries::QL, out));
    }
    SUBCASE("SM has no 1E code") {
        checkInvalidDevice(e1Device(Device{DeviceType::SM, 0}, DataCode::Ascii, out));
    }
    SUBCASE("ZR has no 1E code") {
        checkInvalidDevice(e1Device(Device{DeviceType::ZR, 0}, DataCode::Binary, out));
    }
    SUBCASE("V has no 1C code") {
        checkInvalidDevice(c1Device(Device{DeviceType::V, 0}, C1CommandSet::ACPU, out));
    }
}

TEST_CASE("DEV-07: BufferTooSmall when the destination is short of the field's own size") {
    auto checkBufferTooSmall = [](auto result) {
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().category == ErrorCategory::Encode);
        CHECK(result.error().code == ErrorCode::BufferTooSmall);
    };

    SUBCASE("qnaDevice (Binary Q-L needs 4)") {
        uint8_t buf[3];
        checkBufferTooSmall(
            qnaDevice(Device{DeviceType::D, 100}, DataCode::Binary, PlcSeries::QL,
                      MutableByteView{buf, sizeof(buf)}));
    }
    SUBCASE("e1Device (Binary needs 6; Checkpoint B coverage gap, T-019)") {
        uint8_t buf[5];
        MutableByteView tooSmall{buf, sizeof(buf)};
        checkBufferTooSmall(e1Device(Device{DeviceType::D, 100}, DataCode::Binary, tooSmall));
    }
    SUBCASE("c1Device (ACPU needs 5; Checkpoint B coverage gap, T-019)") {
        uint8_t buf[4];
        MutableByteView tooSmall{buf, sizeof(buf)};
        checkBufferTooSmall(c1Device(Device{DeviceType::D, 100}, C1CommandSet::ACPU, tooSmall));
    }
}

// XYN-10..13: X and Y on an FX CPU. The device holds the point index; the ASCII digits are
// octal when xyAsciiDigits says so. Expected texts are worked out by hand from the vendor tables
// (X10 octal = index 8, X377 = index 255 = 00FFH; fx3-enet-adp.pdf 7.5, fx3-data-communication.pdf,
// fx5-ethernet-communication.pdf 5.3 *3), not produced by the encoder.
TEST_CASE(
    "XYN-10 qnaDevice: octal ASCII digits for X and Y only, Binary and other devices unchanged") {
    using mc::XyNumbering;
    const Device x8{DeviceType::X, 8};
    const Device x255{DeviceType::X, 255};
    const Device y16{DeviceType::Y, 16};
    uint8_t buf[16];
    MutableByteView out{buf, sizeof(buf)};

    SUBCASE("X10 octal, Q/L") {
        auto r = qnaDevice(x8, DataCode::Ascii, PlcSeries::QL, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X*000010");
    }
    SUBCASE("X377 octal, Q/L") {
        auto r = qnaDevice(x255, DataCode::Ascii, PlcSeries::QL, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X*000377");
    }
    SUBCASE("X10 octal, iQ-R") {
        auto r = qnaDevice(x8, DataCode::Ascii, PlcSeries::IqR, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X***00000010");
    }
    SUBCASE("Y20 octal is index 16") {
        auto r = qnaDevice(y16, DataCode::Ascii, PlcSeries::QL, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "Y*000020");
    }
    SUBCASE("hex digits, the default, write index 8 as 8") {
        auto r = qnaDevice(x8, DataCode::Ascii, PlcSeries::QL, out, XyNumbering::Hex);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X*000008");
        auto d = qnaDevice(x8, DataCode::Ascii, PlcSeries::QL, out);
        REQUIRE(d.hasValue());
        checkAscii(ByteView{buf, d.value()}, "X*000008");
    }
    SUBCASE("Binary carries the index whatever the setting") {
        auto r = qnaDevice(x255, DataCode::Binary, PlcSeries::QL, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkBinary(ByteView{buf, r.value()}, {0xFF, 0x00, 0x00, 0x9C});
        auto i = qnaDevice(x8, DataCode::Binary, PlcSeries::IqR, out, XyNumbering::Octal);
        REQUIRE(i.hasValue());
        checkBinary(ByteView{buf, i.value()}, {0x08, 0x00, 0x00, 0x00, 0x9C, 0x00});
    }
    SUBCASE("devices other than X and Y keep their own radix") {
        auto m = qnaDevice(Device{DeviceType::M, 100}, DataCode::Ascii, PlcSeries::QL, out,
                           XyNumbering::Octal);
        REQUIRE(m.hasValue());
        checkAscii(ByteView{buf, m.value()}, "M*000100");
        auto w = qnaDevice(Device{DeviceType::W, 0x1F}, DataCode::Ascii, PlcSeries::QL, out,
                           XyNumbering::Octal);
        REQUIRE(w.hasValue());
        checkAscii(ByteView{buf, w.value()}, "W*00001F");
        auto b = qnaDevice(Device{DeviceType::B, 0x1F}, DataCode::Ascii, PlcSeries::QL, out,
                           XyNumbering::Octal);
        REQUIRE(b.hasValue());
        checkAscii(ByteView{buf, b.value()}, "B*00001F");
    }
}

TEST_CASE("XYN-11 e1Device: 1E ASCII digits follow the setting, 1E Binary never does") {
    using mc::XyNumbering;
    const Device x8{DeviceType::X, 8};
    uint8_t buf[16];
    MutableByteView out{buf, sizeof(buf)};

    SUBCASE("hex digits (FX3 Ethernet adapter): index 8 is 00000008") {
        auto r = e1Device(x8, DataCode::Ascii, out, XyNumbering::Hex);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "582000000008");
    }
    SUBCASE("octal digits: index 8 is 00000010") {
        auto r = e1Device(x8, DataCode::Ascii, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "582000000010");
    }
    SUBCASE("octal digits, index 255 is 00000377") {
        auto r = e1Device(Device{DeviceType::X, 255}, DataCode::Ascii, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "582000000377");
    }
    SUBCASE("Binary: 08 00 00 00 + 5820H whatever the setting") {
        for (XyNumbering xy : {XyNumbering::Hex, XyNumbering::Octal}) {
            auto r = e1Device(x8, DataCode::Binary, out, xy);
            REQUIRE(r.hasValue());
            checkBinary(ByteView{buf, r.value()}, {0x08, 0x00, 0x00, 0x00, 0x20, 0x58});
        }
    }
    SUBCASE("a device other than X and Y is hex whatever the setting") {
        auto r = e1Device(Device{DeviceType::D, 100}, DataCode::Ascii, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "442000000064");
    }
}

TEST_CASE("XYN-12 c1Device: octal digits for X and Y, 4 characters (ACPU) or 6 (AnA)") {
    using mc::XyNumbering;
    uint8_t buf[16];
    MutableByteView out{buf, sizeof(buf)};

    SUBCASE("index 8 ACPU is X0010, index 255 is X0377 (fx3-data-communication.pdf)") {
        auto r = c1Device(Device{DeviceType::X, 8}, C1CommandSet::ACPU, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X0010");
        auto s = c1Device(Device{DeviceType::X, 255}, C1CommandSet::ACPU, out, XyNumbering::Octal);
        REQUIRE(s.hasValue());
        checkAscii(ByteView{buf, s.value()}, "X0377");
    }
    SUBCASE("AnA is X000377 for index 255") {
        auto r = c1Device(Device{DeviceType::X, 255}, C1CommandSet::AnA, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X000377");
    }
    SUBCASE("Y octal: index 15 is Y0017") {
        auto r = c1Device(Device{DeviceType::Y, 15}, C1CommandSet::ACPU, out, XyNumbering::Octal);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "Y0017");
    }
    SUBCASE("hex, the default, writes index 255 as X00FF") {
        auto r = c1Device(Device{DeviceType::X, 255}, C1CommandSet::ACPU, out);
        REQUIRE(r.hasValue());
        checkAscii(ByteView{buf, r.value()}, "X00FF");
    }
    SUBCASE("M100 and D100 are decimal whatever the setting") {
        auto m = c1Device(Device{DeviceType::M, 100}, C1CommandSet::ACPU, out, XyNumbering::Octal);
        REQUIRE(m.hasValue());
        checkAscii(ByteView{buf, m.value()}, "M0100");
        auto t = c1Device(Device{DeviceType::TN, 10}, C1CommandSet::ACPU, out, XyNumbering::Octal);
        REQUIRE(t.hasValue());
        checkAscii(ByteView{buf, t.value()}, "TN010");
    }
}
