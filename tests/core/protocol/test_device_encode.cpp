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
