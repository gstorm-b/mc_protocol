// MCK-04 (writes change memory; bit and word views of one memory agree) and the memory side of
// MCK-12 (setDeviceLimit); the out-of-range *response* is checked end-to-end in
// test_mock_vectors.cpp. Reaches the private src/mock/memory_image.h for the range helper. Also
// MCK-17 (setTypeSupported) and MCK-18 (setSpecialRange), through the public API and the wire.
#include "doctest/doctest.h"

#include "serial_frames.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/mock/mock_plc.h"
#include "mock/memory_image.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::MockOptions;
using mc::MockPlc;
using mc::MockTarget;
using mc::SerialFormat;
using mc::detail::mock::MemoryImage;

namespace {

Device dev(DeviceType t, uint32_t n) { return Device{t, n}; }

MockPlc makeMock() { return MockPlc(FrameConfig::frame3E()); }

} // namespace

TEST_CASE("MCK-04 word write to a word device") {
    MockPlc plc = makeMock();
    CHECK(plc.word(dev(DeviceType::D, 100)) == 0);
    plc.setWords(dev(DeviceType::D, 100), {0x1995, 0x1202, 0x1130});
    CHECK(plc.word(dev(DeviceType::D, 100)) == 0x1995);
    CHECK(plc.word(dev(DeviceType::D, 101)) == 0x1202);
    CHECK(plc.word(dev(DeviceType::D, 102)) == 0x1130);
    CHECK(plc.word(dev(DeviceType::D, 103)) == 0);
    plc.setWord(dev(DeviceType::D, 101), 0xBEEF);
    CHECK(plc.word(dev(DeviceType::D, 101)) == 0xBEEF);
}

TEST_CASE("MCK-04 bit write with an odd count") {
    MockPlc plc = makeMock();
    plc.setBits(dev(DeviceType::M, 100), {true, false, true, false, true});
    CHECK(plc.bit(dev(DeviceType::M, 100)));
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 101)));
    CHECK(plc.bit(dev(DeviceType::M, 102)));
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 103)));
    CHECK(plc.bit(dev(DeviceType::M, 104)));
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 105))); // the point after the odd tail is untouched
    plc.setBit(dev(DeviceType::M, 105), true);
    CHECK(plc.bit(dev(DeviceType::M, 105)));
    plc.setBit(dev(DeviceType::M, 100), false);
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 100)));
}

TEST_CASE("MCK-04 word write to a bit device packs 16 points per word") {
    MockPlc plc = makeMock();
    // Spec section 2.4: 1234H -> M102, M104, M105, M109, M112 ON; 0002H -> M117 ON.
    plc.setWords(dev(DeviceType::M, 100), {0x1234, 0x0002});
    const uint32_t on[] = {102, 104, 105, 109, 112, 117};
    for (uint32_t n = 100; n < 132; ++n) {
        bool expected = false;
        for (uint32_t o : on) {
            expected = expected || (o == n);
        }
        CHECK_MESSAGE(plc.bit(dev(DeviceType::M, n)) == expected, "M", n);
    }
    CHECK(plc.word(dev(DeviceType::M, 100)) == 0x1234);
    CHECK(plc.word(dev(DeviceType::M, 116)) == 0x0002);
}

TEST_CASE("MCK-04 bit writes are seen through the word view, in both directions") {
    MockPlc plc = makeMock();
    plc.setBits(dev(DeviceType::M, 0), {true, false, false, false, false, false, false, false,
                                        false, false, false, false, false, false, false, true});
    CHECK(plc.word(dev(DeviceType::M, 0)) == 0x8001);
    plc.setWord(dev(DeviceType::M, 0), 0x0100);
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 0)));
    CHECK(plc.bit(dev(DeviceType::M, 8)));
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 15)));
}

TEST_CASE("MCK-04 the word view of an unaligned head starts at the head itself") {
    MockPlc plc = makeMock();
    plc.setBit(dev(DeviceType::M, 5), true);
    CHECK(plc.word(dev(DeviceType::M, 5)) == 0x0001); // M5 is bit 0 of the word at head 5
    CHECK(plc.word(dev(DeviceType::M, 4)) == 0x0002);
}

TEST_CASE("MCK-04 hex-radix bit devices and numbers are not aliased between types") {
    MockPlc plc = makeMock();
    plc.setBit(dev(DeviceType::X, 0x1F), true);
    CHECK(plc.bit(dev(DeviceType::X, 0x1F)));
    CHECK_FALSE(plc.bit(dev(DeviceType::Y, 0x1F)));
    CHECK_FALSE(plc.bit(dev(DeviceType::M, 0x1F)));
    plc.setWord(dev(DeviceType::D, 9000), 7);
    CHECK(plc.word(dev(DeviceType::SD, 1000)) == 0);
    CHECK(plc.word(dev(DeviceType::D, 9000)) == 7);
}

TEST_CASE("MCK-04 a word device has no bit view") {
    MockPlc plc = makeMock();
    plc.setWord(dev(DeviceType::D, 10), 0xFFFF);
    CHECK_FALSE(plc.bit(dev(DeviceType::D, 10)));
    plc.setBit(dev(DeviceType::D, 10), false);
    CHECK(plc.word(dev(DeviceType::D, 10)) == 0xFFFF);
    plc.setBits(dev(DeviceType::D, 10), {false});
    CHECK(plc.word(dev(DeviceType::D, 10)) == 0xFFFF);
}

TEST_CASE("MCK-04 sparse pages: far-apart numbers do not disturb each other") {
    MockPlc plc = makeMock();
    plc.setWord(dev(DeviceType::D, 0), 1);
    plc.setWord(dev(DeviceType::D, 4095), 2);
    plc.setWord(dev(DeviceType::D, 4096), 3);
    plc.setWord(dev(DeviceType::D, 0xFFFFFF), 4);
    CHECK(plc.word(dev(DeviceType::D, 0)) == 1);
    CHECK(plc.word(dev(DeviceType::D, 4095)) == 2);
    CHECK(plc.word(dev(DeviceType::D, 4096)) == 3);
    CHECK(plc.word(dev(DeviceType::D, 0xFFFFFF)) == 4);
    CHECK(plc.word(dev(DeviceType::D, 8192)) == 0);
}

TEST_CASE("MCK-04 the address space ends at 2^32 points") {
    MockPlc plc = makeMock();
    plc.setWord(dev(DeviceType::D, 0xFFFFFFFFu), 9);
    CHECK(plc.word(dev(DeviceType::D, 0xFFFFFFFFu)) == 9);
    // A word of a bit device that would run past 2^32 - 1 loses its out-of-space points.
    plc.setWord(dev(DeviceType::M, 0xFFFFFFF8u), 0xFFFF);
    CHECK(plc.bit(dev(DeviceType::M, 0xFFFFFFFFu)));
    CHECK(plc.word(dev(DeviceType::M, 0xFFFFFFF8u)) == 0x00FF);
}

TEST_CASE("MCK-12 range helper: ranges reaching the limit are out of range") {
    MemoryImage image;
    image.setLimit(DeviceType::M, 8190);
    CHECK_FALSE(image.outOfRange(DeviceType::M, 8180, 10));  // last point 8189 < limit
    CHECK(image.outOfRange(DeviceType::M, 8180, 11));        // last point 8190 = limit
    CHECK(image.outOfRange(DeviceType::M, 8190, 1));
    CHECK(image.outOfRange(DeviceType::M, 9000, 1));
    CHECK_FALSE(image.outOfRange(DeviceType::M, 0, 8190));
    CHECK_FALSE(image.outOfRange(DeviceType::M, 8189, 1));
    CHECK_FALSE(image.outOfRange(DeviceType::M, 8190, 0));   // an empty range never reaches
}

TEST_CASE("MCK-12 the limit is per device type and off by default") {
    MemoryImage image;
    CHECK_FALSE(image.outOfRange(DeviceType::D, 0, 65535));
    CHECK_FALSE(image.outOfRange(DeviceType::D, 0xFFFFFFF0u, 16));
    CHECK(image.outOfRange(DeviceType::D, 0xFFFFFFF0u, 17));
    image.setLimit(DeviceType::M, 100);
    CHECK(image.outOfRange(DeviceType::M, 99, 2));
    CHECK_FALSE(image.outOfRange(DeviceType::D, 99, 2));
    image.setLimit(DeviceType::M, 0);
    CHECK(image.outOfRange(DeviceType::M, 0, 1));
}

TEST_CASE("MCK-12 setDeviceLimit does not touch memory below the limit") {
    MockPlc plc = makeMock();
    plc.setWord(dev(DeviceType::D, 50), 5);
    plc.setDeviceLimit(DeviceType::D, 100);
    CHECK(plc.word(dev(DeviceType::D, 50)) == 5);
}

TEST_CASE("MCK-04 the request log and EOT counter start empty and clearLog empties the log") {
    MockPlc plc = makeMock();
    CHECK(plc.requests().empty());
    CHECK(plc.eotCount() == 0);
    plc.clearLog();
    CHECK(plc.requests().empty());
    mc::ByteView out;
    CHECK_FALSE(plc.nextResponse(out));
}

// ---- MCK-17 (setTypeSupported) and MCK-18 (setSpecialRange), through the wire
// --------------------
//
// The public API only: requests are written by hand from the tables of reference spec sections
// 4.1, 4.2 and 5.3 (3E, 1E) and by the serial builders of this directory (3C, 1C); a response is
// judged by its bytes. Generic target: the codes are the MockOptions defaults (C051H, C059H, 50H,
// 06H), pinned by MCK-09.

namespace {

using Bytes = std::vector<uint8_t>;
namespace sf = mc::test::serial;

void feed(MockPlc& plc, const Bytes& bytes) {
    plc.bytesIn(mc::ByteView{bytes.data(), bytes.size()});
}

// The one response pending (none or several fail the test).
Bytes takeOne(MockPlc& plc) {
    Bytes got;
    mc::ByteView view;
    REQUIRE(plc.nextResponse(view));
    got.assign(view.data, view.data + view.size);
    CHECK_FALSE(plc.nextResponse(view));
    return got;
}

Bytes answer(MockPlc& plc, const Bytes& request) {
    feed(plc, request);
    return takeOne(plc);
}

Bytes hexOf(const std::string& text) {
    Bytes out;
    unsigned value = 0;
    int digits = 0;
    for (char ch : text) {
        if (ch == ' ') {
            continue;
        }
        value = (value << 4) | static_cast<unsigned>(ch >= 'A' ? ch - 'A' + 10 : ch - '0');
        if (++digits == 2) {
            out.push_back(static_cast<uint8_t>(value));
            value = 0;
            digits = 0;
        }
    }
    return out;
}

Bytes textOf(const std::string& text) { return Bytes(text.begin(), text.end()); }

// 3E Binary batch read of `count` words (bit == false) or bits (bit == true) from D or M.
Bytes read3eBin(DeviceType t, uint32_t number, unsigned count, bool bit = false) {
    Bytes b = hexOf("50 00 00 FF FF 03 00 0C 00 10 00 01 04");
    b.push_back(bit ? 0x01 : 0x00);
    b.push_back(0x00);
    b.push_back(static_cast<uint8_t>(number & 0xFF));
    b.push_back(static_cast<uint8_t>((number >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((number >> 16) & 0xFF));
    b.push_back(t == DeviceType::D ? 0xA8 : 0x90);
    b.push_back(static_cast<uint8_t>(count & 0xFF));
    b.push_back(static_cast<uint8_t>(count >> 8));
    return b;
}

// The end code of a 3E Binary response (bytes 9 and 10): 0 = executed.
unsigned endCode3eBin(const Bytes& r) {
    REQUIRE(r.size() >= 11);
    return static_cast<unsigned>(r[9] | (r[10] << 8));
}

constexpr unsigned kOutOfRange = 0xC051;
constexpr unsigned kUnsupportedType = 0xC059;

// One wire representation: its config, a read of D100 x1, the error response the Generic target
// gives for a type that is not supported, and the response of an executed read of D100 = 1234H.
struct Wire {
    const char* name;
    FrameConfig cfg;
    Bytes read;
    Bytes unsupported;
    Bytes executed;
};

std::vector<Wire> wiresOfD100() {
    std::vector<Wire> list;
    list.push_back({"3E Binary", FrameConfig::frame3E(DataCode::Binary),
                    read3eBin(DeviceType::D, 100, 1),
                    hexOf("D0 00 00 FF FF 03 00 0B 00 59 C0 00 FF FF 03 00 01 04 00 00"),
                    hexOf("D0 00 00 FF FF 03 00 04 00 00 00 34 12")});
    list.push_back({"3E ASCII", FrameConfig::frame3E(DataCode::Ascii),
                    textOf("500000FF03FF00"
                           "0018"
                           "0010"
                           "04010000"
                           "D*000100"
                           "0001"),
                    textOf("D00000FF03FF000016C05900FF03FF0004010000"),
                    textOf("D00000FF03FF0000080000"
                           "1234")});
    list.push_back({"1E Binary", FrameConfig::frame1E(DataCode::Binary),
                    hexOf("01 FF 0A 00 64 00 00 00 20 44 01 00"), hexOf("81 50"),
                    hexOf("81 00 34 12")});
    list.push_back({"1E ASCII", FrameConfig::frame1E(DataCode::Ascii),
                    textOf("01FF000A"
                           "442000000064"
                           "0100"),
                    textOf("8150"), textOf("81001234")});
    for (bool threeC : {true, false}) {
        for (SerialFormat format : {SerialFormat::Format1, SerialFormat::Format2,
                                    SerialFormat::Format3, SerialFormat::Format4}) {
            static std::deque<std::string> names; // element addresses stay valid
            names.push_back(std::string(threeC ? "3C F" : "1C F") +
                            std::to_string(static_cast<int>(format)));
            FrameConfig cfg = threeC ? FrameConfig::frame3C(format) : FrameConfig::frame1C(format);
            list.push_back({names.back().c_str(), cfg, sf::request(cfg, sf::readD(cfg, 100, 1)),
                            sf::response(cfg, sf::Kind::Nak, threeC ? "C059" : "06"),
                            sf::response(cfg, sf::Kind::Data, "1234")});
        }
    }
    return list;
}

} // namespace

TEST_CASE("MCK-17 every type is supported by default") {
    for (const Wire& w : wiresOfD100()) {
        INFO(w.name);
        MockPlc plc(w.cfg);
        plc.setWord(dev(DeviceType::D, 100), 0x1234);
        CHECK(answer(plc, w.read) == w.executed);
    }
}

TEST_CASE("MCK-17 setTypeSupported(t, false) answers the unsupported-type code on every frame "
          "family, and true restores the request") {
    for (const Wire& w : wiresOfD100()) {
        INFO(w.name);
        MockPlc plc(w.cfg);
        plc.setWord(dev(DeviceType::D, 100), 0x1234);
        plc.setTypeSupported(DeviceType::D, false);
        CHECK(answer(plc, w.read) == w.unsupported);
        CHECK(answer(plc, w.read) == w.unsupported); // not a one-shot
        plc.setTypeSupported(DeviceType::D, true);
        CHECK(answer(plc, w.read) == w.executed);
        // true on a type that was never switched off changes nothing; false twice, true once.
        plc.setTypeSupported(DeviceType::D, true);
        CHECK(answer(plc, w.read) == w.executed);
        plc.setTypeSupported(DeviceType::D, false);
        plc.setTypeSupported(DeviceType::D, false);
        plc.setTypeSupported(DeviceType::D, true);
        CHECK(answer(plc, w.read) == w.executed);
    }
}

TEST_CASE("MCK-17 the request log records the unsupported-type answer as the PLC error") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setTypeSupported(DeviceType::D, false);
    feed(plc, read3eBin(DeviceType::D, 100, 1));
    REQUIRE(plc.requests().size() == 1);
    CHECK(plc.requests()[0].answered);
    CHECK(static_cast<unsigned>(plc.requests()[0].answeredWith.plcCode) == kUnsupportedType);
    CHECK(plc.requests()[0].head.type == DeviceType::D);
}

TEST_CASE("MCK-17 only the named type is affected, and types switch independently") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setWord(dev(DeviceType::D, 100), 0x1234);
    plc.setBit(dev(DeviceType::M, 100), true);
    plc.setTypeSupported(DeviceType::M, false);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::M, 100, 1, true))) == kUnsupportedType);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::M, 100, 1, false))) == kUnsupportedType);

    plc.setTypeSupported(DeviceType::D, false);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == kUnsupportedType);
    plc.setTypeSupported(DeviceType::M, true);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::M, 100, 1, true))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == kUnsupportedType);
}

TEST_CASE("MCK-17 a write to an unsupported type is refused and changes no memory") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setWord(dev(DeviceType::D, 100), 0x1234);
    plc.setTypeSupported(DeviceType::D, false);
    // Write D100 = 5678H.
    const Bytes write =
        hexOf("50 00 00 FF FF 03 00 0E 00 10 00 01 14 00 00 64 00 00 A8 01 00 78 56");
    CHECK(answer(plc, write) ==
          hexOf("D0 00 00 FF FF 03 00 0B 00 59 C0 00 FF FF 03 00 01 14 00 00"));
    CHECK(plc.word(dev(DeviceType::D, 100)) == 0x1234);
    plc.setTypeSupported(DeviceType::D, true);
    CHECK(endCode3eBin(answer(plc, write)) == 0);
    CHECK(plc.word(dev(DeviceType::D, 100)) == 0x5678);
}

TEST_CASE("MCK-17 the type is checked before the range: an unsupported type beyond its limit "
          "gets the type code") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::D, 50);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == kOutOfRange);
    plc.setTypeSupported(DeviceType::D, false);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == kUnsupportedType);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 10, 1))) == kUnsupportedType);
    plc.setTypeSupported(DeviceType::D, true);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == kOutOfRange);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 10, 1))) == 0);
}

TEST_CASE("MCK-17 with a target, an unsupported type gets the code of that target's table") {
    // Not supported before out of range, in the code of the target: QCpu 4031 (3E Binary), Fx5 C05C
    // (any type that is neither DX nor DY in the Binary frame, the V / ZR code), Fx3 on 1E 56H and
    // on 1C 06; Generic stays C059.
    struct Row {
        MockTarget target;
        unsigned code;
    };
    for (const Row& row : {Row{MockTarget::Generic, 0xC059}, Row{MockTarget::QCpu, 0x4031},
                           Row{MockTarget::Fx5, 0xC05C}, Row{MockTarget::Fx3, 0xC059}}) {
        MockOptions opt;
        opt.target = row.target;
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
        plc.setDeviceLimit(DeviceType::D, 50);
        plc.setTypeSupported(DeviceType::D, false);
        CHECK_MESSAGE(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == row.code,
                      "3E Binary, target " << static_cast<int>(row.target));
    }
    {
        MockOptions opt;
        opt.target = MockTarget::Fx3;
        MockPlc plc(FrameConfig::frame1E(DataCode::Binary), opt);
        plc.setDeviceLimit(DeviceType::D, 50);
        plc.setTypeSupported(DeviceType::D, false);
        CHECK(answer(plc, hexOf("01 FF 0A 00 64 00 00 00 20 44 01 00")) == hexOf("81 56"));
    }
    {
        MockOptions opt;
        opt.target = MockTarget::Fx5;
        const FrameConfig cfg = FrameConfig::frame3C(SerialFormat::Format1);
        MockPlc plc(cfg, opt);
        plc.setDeviceLimit(DeviceType::D, 50);
        plc.setTypeSupported(DeviceType::D, false);
        CHECK(answer(plc, sf::request(cfg, sf::readD(cfg, 100, 1))) ==
              sf::response(cfg, sf::Kind::Nak, "7E43"));
    }
}

TEST_CASE("MCK-17 / MCK-18 setTypeSupported and setSpecialRange are noexcept") {
    MockPlc plc(FrameConfig::frame3E());
    static_assert(noexcept(plc.setTypeSupported(DeviceType::D, false)),
                  "setTypeSupported is noexcept");
    static_assert(noexcept(plc.setSpecialRange(DeviceType::D, 8000, 8512)),
                  "setSpecialRange is noexcept");
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == 0);
}

// ---- MCK-18 ------------------------------------------------------------------------------------

TEST_CASE("MCK-18 a special range contiguous with the limit: inside executes, a request crossing "
          "from the general into it is out of range") {
    // The FX3U case: D0..D7999 general (limit 8000), D8000..D8511 special.
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::D, 8000);
    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    struct Row {
        uint32_t head;
        unsigned count;
        bool ok;
    };
    const Row rows[] = {
        {7999, 1, true},                         // last general point
        {8000, 1, true},                         // first special point
        {8010, 1, true},     {8511, 1, true},    // last special point
        {8000, 512, true},                       // the whole special range
        {8512, 1, false},                        // one past the special range
        {8000, 513, false},  {7999, 2, false},   // crosses from the general into the special range
        {7990, 20, false},   {7000, 1000, true}, // ends at 7999
        {7000, 1001, false}, // ends at 8000 inside the special range, but crosses
    };
    for (const Row& r : rows) {
        const unsigned code = endCode3eBin(answer(plc, read3eBin(DeviceType::D, r.head, r.count)));
        CHECK_MESSAGE(code == (r.ok ? 0u : kOutOfRange),
                      "D" << r.head << " x" << r.count << ": end code " << std::hex << code);
    }
}

TEST_CASE("MCK-18 a gap between the limit and from is out of range") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::D, 7000);
    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    struct Row {
        uint32_t head;
        unsigned count;
        bool ok;
    };
    const Row rows[] = {
        {6999, 1, true},                                         // last general point
        {7000, 1, false},                                        // first point of the gap
        {7999, 1, false},                                        // last point of the gap
        {8000, 1, true},                                         // first special point
        {8511, 1, true},     {8512, 1, false}, {7999, 2, false}, // gap and special
        {6999, 2, false},                                        // general and gap
        {6999, 1500, false}, // general, gap and special in one request
        {8000, 512, true},
    };
    for (const Row& r : rows) {
        const unsigned code = endCode3eBin(answer(plc, read3eBin(DeviceType::D, r.head, r.count)));
        CHECK_MESSAGE(code == (r.ok ? 0u : kOutOfRange),
                      "D" << r.head << " x" << r.count << ": end code " << std::hex << code);
    }
}

TEST_CASE("MCK-18 points of a special range hold data: reads, writes and the memory view") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::D, 8000);
    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    plc.setWord(dev(DeviceType::D, 8010), 5);
    CHECK(answer(plc, read3eBin(DeviceType::D, 8010, 1)) ==
          hexOf("D0 00 00 FF FF 03 00 04 00 00 00 05 00"));
    // Write D8000 = 1234H, D8001 = 5678H.
    const Bytes write =
        hexOf("50 00 00 FF FF 03 00 10 00 10 00 01 14 00 00 40 1F 00 A8 02 00 34 12 78 56");
    CHECK(endCode3eBin(answer(plc, write)) == 0);
    CHECK(plc.word(dev(DeviceType::D, 8000)) == 0x1234);
    CHECK(plc.word(dev(DeviceType::D, 8001)) == 0x5678);
    // A write that leaves the special range changes nothing.
    const Bytes writeOut =
        hexOf("50 00 00 FF FF 03 00 10 00 10 00 01 14 00 00 3F 21 00 A8 02 00 11 11 22 22");
    CHECK(endCode3eBin(answer(plc, writeOut)) == kOutOfRange); // D8511 x2: D8512 does not exist
    CHECK(plc.word(dev(DeviceType::D, 8511)) == 0);
}

TEST_CASE("MCK-18 the range belongs to one device type") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::D, 100);
    plc.setDeviceLimit(DeviceType::M, 100);
    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::M, 8000, 1, true))) == kOutOfRange);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::M, 8000, 1, false))) == kOutOfRange);
    plc.setSpecialRange(DeviceType::M, 8000, 8512);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::M, 8000, 1, true))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == 0);
}

TEST_CASE("MCK-18 a bit device: the special points exist as bits and as 16-point words") {
    // FX3U M8000..M8511 above a general limit of 7680.
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::M, 7680);
    plc.setSpecialRange(DeviceType::M, 8000, 8512);
    plc.setBit(dev(DeviceType::M, 8000), true);
    struct Row {
        uint32_t head;
        unsigned count;
        bool bit;
        bool ok;
    };
    const Row rows[] = {
        {7679, 1, true, true},   {7680, 1, true, false},  {7999, 1, true, false},
        {8000, 1, true, true},   {8511, 1, true, true},   {8512, 1, true, false},
        {8000, 16, true, true},  {8000, 512, true, true}, {8000, 513, true, false},
        {8000, 1, false, true},  // word: M8000..M8015
        {8496, 1, false, true},  // word: M8496..M8511
        {8497, 1, false, false}, // word: M8497..M8512 leaves the range
        {7664, 1, false, true},  // word: M7664..M7679
        {7665, 1, false, false}, // word: M7665..M7680
        {7984, 1, false, false}, // word: M7984..M7999 is the gap
    };
    for (const Row& r : rows) {
        const unsigned code =
            endCode3eBin(answer(plc, read3eBin(DeviceType::M, r.head, r.count, r.bit)));
        CHECK_MESSAGE(code == (r.ok ? 0u : kOutOfRange),
                      "M" << r.head << (r.bit ? " bits x" : " words x") << r.count << ": end code "
                          << std::hex << code);
    }
    // M8000 as a word has bit 0 set.
    CHECK(answer(plc, read3eBin(DeviceType::M, 8000, 1, false)) ==
          hexOf("D0 00 00 FF FF 03 00 04 00 00 00 01 00"));
}

TEST_CASE("MCK-18 a new call replaces the range, and from >= limit clears it") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setDeviceLimit(DeviceType::D, 7000);
    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == 0);

    plc.setSpecialRange(DeviceType::D, 9000, 9100); // replaces: one range per type
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == kOutOfRange);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 9000, 100))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 9099, 1))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 9100, 1))) == kOutOfRange);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == 0); // general part intact

    plc.setSpecialRange(DeviceType::D, 9100, 9100); // from == limit: cleared
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 9000, 1))) == kOutOfRange);

    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == 0);
    plc.setSpecialRange(DeviceType::D, 9000, 8000); // from > limit: cleared
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == kOutOfRange);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 9000, 1))) == kOutOfRange);

    // A cleared type behaves as one that never had a range; a call that is already empty is fine.
    plc.setSpecialRange(DeviceType::D, 0, 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 6999, 1))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 7000, 1))) == kOutOfRange);
}

TEST_CASE("MCK-18 a special range does not change a type without a limit") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setSpecialRange(DeviceType::D, 8000, 8512);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 100, 1))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 8000, 1))) == 0);
    CHECK(endCode3eBin(answer(plc, read3eBin(DeviceType::D, 20000, 1))) == 0);
}

TEST_CASE("MCK-18 the FX3 special registers of the captures on 1E and 1C: D8010, D8000 x4, M8000 "
          "bits and word, and D7999 x2 across the boundary") {
    // CAP-fx3-eth-1e-bin-G0-02, G1-09, G2-10, G3-04 and CAP-fx3-serial-1c-f1-sum-G1-09, G3-04: the
    // PLC answers `ok` with data for numbers beyond device_end (D=7999, M=7679).
    {
        MockPlc plc(FrameConfig::frame1E(DataCode::Binary));
        plc.setDeviceLimit(DeviceType::D, 8000);
        plc.setDeviceLimit(DeviceType::M, 7680);
        plc.setSpecialRange(DeviceType::D, 8000, 8512);
        plc.setSpecialRange(DeviceType::M, 8000, 8512);
        plc.setWords(dev(DeviceType::D, 8000), {0x00C8, 0x6677, 0x0008, 0x0010});
        plc.setWord(dev(DeviceType::D, 8010), 5);
        plc.setBit(dev(DeviceType::M, 8000), true);
        plc.setBit(dev(DeviceType::M, 8003), true);
        plc.setBit(dev(DeviceType::M, 8012), true);
        CHECK(answer(plc, hexOf("01 FF 0A 00 4A 1F 00 00 20 44 01 00")) == hexOf("81 00 05 00"));
        CHECK(answer(plc, hexOf("01 FF 0A 00 40 1F 00 00 20 44 04 00")) ==
              hexOf("81 00 C8 00 77 66 08 00 10 00"));
        CHECK(answer(plc, hexOf("00 FF 0A 00 40 1F 00 00 20 4D 10 00")) ==
              hexOf("80 00 10 01 00 00 00 00 10 00"));
        // G3-04 read M8000 as a word a moment later, when the PLC's M8011 was on and M8012 off.
        plc.setBit(dev(DeviceType::M, 8012), false);
        plc.setBit(dev(DeviceType::M, 8011), true);
        CHECK(answer(plc, hexOf("01 FF 0A 00 40 1F 00 00 20 4D 01 00")) == hexOf("81 00 09 08"));
        // Across the boundary: D7999 x2 (the PLC forbids it; the mock answers 5BH + 10H).
        CHECK(answer(plc, hexOf("01 FF 0A 00 3F 1F 00 00 20 44 02 00")) == hexOf("81 5B 10"));
    }
    {
        const FrameConfig cfg = [] {
            FrameConfig c = FrameConfig::frame1C(SerialFormat::Format1);
            c.targetFamily = mc::TargetFamily::A;
            c.xyNotation = mc::XyNumbering::Octal;
            c.xyAsciiDigits = mc::XyNumbering::Octal;
            return c;
        }();
        MockPlc plc(cfg);
        plc.setDeviceLimit(DeviceType::D, 8000);
        plc.setSpecialRange(DeviceType::D, 8000, 8512);
        plc.setWords(dev(DeviceType::D, 8000), {0x00C8, 0x6677, 0x0008, 0x0010});
        plc.setWord(dev(DeviceType::D, 8010), 5);
        CHECK(answer(plc, sf::request(cfg, sf::data1c("WR", '0', "D8010", 1))) ==
              sf::response(cfg, sf::Kind::Data, "0005"));
        CHECK(answer(plc, sf::request(cfg, sf::data1c("WR", '0', "D8000", 4))) ==
              sf::response(cfg, sf::Kind::Data, "00C8667700080010"));
        CHECK(answer(plc, sf::request(cfg, sf::data1c("WR", '0', "D7999", 2))) ==
              sf::response(cfg, sf::Kind::Nak, "06"));
    }
}
