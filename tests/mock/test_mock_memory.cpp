// MCK-04 (writes change memory; bit and word views of one memory agree) and the memory side of
// MCK-12 (setDeviceLimit); the out-of-range *response* is checked end-to-end in
// test_mock_vectors.cpp. Reaches the private src/mock/memory_image.h for the range helper.
#include "doctest/doctest.h"

#include "mc/core/device.h"
#include "mc/mock/mock_plc.h"
#include "mock/memory_image.h"

using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::MockPlc;
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
