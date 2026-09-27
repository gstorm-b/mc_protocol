#include "doctest/doctest.h"

#include "mc/core/convert.h"

#include <string_view>
#include <vector>

using mc::ByteBuf;
using mc::ByteView;
using mc::MutableByteView;

namespace {

ByteView view(const std::vector<uint8_t>& v) { return ByteView{v.data(), v.size()}; }
MutableByteView mview(std::vector<uint8_t>& v) { return MutableByteView{v.data(), v.size()}; }

} // namespace

TEST_CASE("CNV-01 uint32At/putUint32: D350=56ABH, D351=170FH -> 170F56ABH") {
    std::vector<uint8_t> bytes{0xAB, 0x56, 0x0F, 0x17};
    CHECK(mc::convert::wordAt(view(bytes), 0) == 0x56AB);  // D350
    CHECK(mc::convert::wordAt(view(bytes), 1) == 0x170F);  // D351
    CHECK(mc::convert::uint32At(view(bytes), 0) == 0x170F56ABu);

    std::vector<uint8_t> written(4, 0);
    CHECK(mc::convert::putUint32(mview(written), 0, 0x170F56ABu));
    CHECK(written == bytes);
}

TEST_CASE("CNV-02 float32At/putFloat32: D0=0000H, D1=3F40H -> 0.75f") {
    std::vector<uint8_t> bytes{0x00, 0x00, 0x40, 0x3F};
    CHECK(mc::convert::float32At(view(bytes), 0) == doctest::Approx(0.75f));

    std::vector<uint8_t> written(4, 0);
    CHECK(mc::convert::putFloat32(mview(written), 0, 0.75f));
    CHECK(written == bytes);
}

TEST_CASE("CNV-03 stringAt/putString: \"ABCD\" <-> D0=4241H, D1=4443H") {
    std::vector<uint8_t> bytes{0x41, 0x42, 0x43, 0x44};
    char buf[8]{};
    size_t n = mc::convert::stringAt(view(bytes), 0, 2, buf, sizeof(buf));
    CHECK(n == 4);
    CHECK(std::string_view(buf, n) == "ABCD");

    std::vector<uint8_t> written(4, 0xAA);
    CHECK(mc::convert::putString(mview(written), 0, 2, "ABCD"));
    CHECK(written == bytes);
}

TEST_CASE("CNV-04 int16At: FFFFH -> -1") {
    std::vector<uint8_t> bytes{0xFF, 0xFF};
    CHECK(mc::convert::int16At(view(bytes), 0) == -1);

    std::vector<uint8_t> written(2, 0);
    CHECK(mc::convert::putInt16(mview(written), 0, -1));
    CHECK(written == bytes);
}

TEST_CASE("CNV-05 float64At/putFloat64: round trip over four words") {
    constexpr double kValue = 123456.789;
    ByteBuf buf = mc::convert::fromFloat64({kValue});
    REQUIRE(buf.size() == 8);
    CHECK(mc::convert::float64At(ByteView{buf.data(), buf.size()}, 0) == kValue);

    std::vector<uint8_t> written(8, 0);
    CHECK(mc::convert::putFloat64(mview(written), 0, kValue));
    CHECK(mc::convert::float64At(view(written), 0) == kValue);
}

TEST_CASE("CNV-06 (PRIM-15) wordsToBits([1234H, 0002H]): ON exactly at 2,4,5,9,12,17") {
    std::vector<uint8_t> words{0x34, 0x12, 0x02, 0x00}; // 0x1234, 0x0002, little-endian.
    std::vector<uint8_t> bits(32, 0xAA);                // sentinel; every slot must be overwritten.
    REQUIRE(mc::convert::wordsToBits(view(words), mview(bits)));

    bool expectedOn[32] = {};
    for (int i : {2, 4, 5, 9, 12, 17}) {
        expectedOn[i] = true;
    }
    for (size_t i = 0; i < 32; ++i) {
        INFO("bit index ", i);
        CHECK(bits[i] == (expectedOn[i] ? 1 : 0));
    }

    // Round trip back.
    std::vector<uint8_t> roundTrip(4, 0);
    CHECK(mc::convert::bitsToWords(view(bits), mview(roundTrip)));
    CHECK(roundTrip == words);
}

TEST_CASE("CNV-07 packBits/unpackBits round trip") {
    // 11 points, not a multiple of 8, so packing needs the (n + 7) / 8 = 2 bytes and the last
    // byte has 5 padding bits that must come back 0.
    std::vector<uint8_t> points{1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    std::vector<uint8_t> packed(2, 0xAA);
    REQUIRE(mc::convert::packBits(view(points), mview(packed)));
    CHECK(packed[0] == 0x4D); // p0..p7 = 1,0,1,1,0,0,1,0 -> bit0..bit7.
    CHECK(packed[1] == 0x03); // p8..p10 = 1,1,0; bits 3-7 of this byte are padding, must be 0.

    std::vector<uint8_t> unpacked(11, 0xAA);
    REQUIRE(mc::convert::unpackBits(view(packed), 11, mview(unpacked)));
    CHECK(unpacked == points);
}

TEST_CASE("CNV-08 boundary: the last valid index/capacity works; the next fails and touches "
          "nothing") {
    SUBCASE("wordAt") {
        std::vector<uint8_t> bytes{0x01, 0x00}; // exactly one word.
        CHECK(mc::convert::wordAt(view(bytes), 0) == 1);
        CHECK(mc::convert::wordAt(view(bytes), 1) == 0); // one word past the view.
    }
    SUBCASE("int16At") {
        std::vector<uint8_t> bytes{0x01, 0x00};
        CHECK(mc::convert::int16At(view(bytes), 0) == 1);
        CHECK(mc::convert::int16At(view(bytes), 1) == 0);
    }
    SUBCASE("uint32At") {
        std::vector<uint8_t> bytes{0x01, 0x00, 0x00, 0x00}; // exactly one dword.
        CHECK(mc::convert::uint32At(view(bytes), 0) == 1u);
        CHECK(mc::convert::uint32At(view(bytes), 1) == 0u); // needs 2 more bytes than exist.
    }
    SUBCASE("int32At") {
        std::vector<uint8_t> bytes{0x01, 0x00, 0x00, 0x00};
        CHECK(mc::convert::int32At(view(bytes), 0) == 1);
        CHECK(mc::convert::int32At(view(bytes), 1) == 0);
    }
    SUBCASE("float32At") {
        std::vector<uint8_t> bytes{0x00, 0x00, 0x40, 0x3F}; // 0.75f.
        CHECK(mc::convert::float32At(view(bytes), 0) == doctest::Approx(0.75f));
        CHECK(mc::convert::float32At(view(bytes), 1) == 0.0f);
    }
    SUBCASE("float64At") {
        std::vector<uint8_t> bytes = mc::convert::fromFloat64({1.5});
        REQUIRE(bytes.size() == 8);
        CHECK(mc::convert::float64At(view(bytes), 0) == 1.5);
        CHECK(mc::convert::float64At(view(bytes), 1) == 0.0); // needs 2 more bytes than exist.
    }
    SUBCASE("putWord") {
        std::vector<uint8_t> dest(2, 0xAA);
        CHECK(mc::convert::putWord(mview(dest), 0, 0x1234));
        CHECK(dest[0] == 0x34);
        CHECK(dest[1] == 0x12);

        std::vector<uint8_t> sentinel(2, 0xAA);
        CHECK_FALSE(mc::convert::putWord(mview(sentinel), 1, 0x1234));
        CHECK(sentinel == std::vector<uint8_t>(2, 0xAA)); // untouched.
    }
    SUBCASE("putInt16") {
        std::vector<uint8_t> sentinel(2, 0xAA);
        CHECK_FALSE(mc::convert::putInt16(mview(sentinel), 1, -1));
        CHECK(sentinel == std::vector<uint8_t>(2, 0xAA));
    }
    SUBCASE("putUint32") {
        std::vector<uint8_t> dest(4, 0xAA);
        CHECK(mc::convert::putUint32(mview(dest), 0, 0x11223344u));
        CHECK(dest == std::vector<uint8_t>{0x44, 0x33, 0x22, 0x11});

        std::vector<uint8_t> sentinel(4, 0xAA);
        CHECK_FALSE(mc::convert::putUint32(mview(sentinel), 1, 1u));
        CHECK(sentinel == std::vector<uint8_t>(4, 0xAA));
    }
    SUBCASE("putInt32") {
        std::vector<uint8_t> sentinel(4, 0xAA);
        CHECK_FALSE(mc::convert::putInt32(mview(sentinel), 1, -1));
        CHECK(sentinel == std::vector<uint8_t>(4, 0xAA));
    }
    SUBCASE("putFloat32") {
        std::vector<uint8_t> sentinel(4, 0xAA);
        CHECK_FALSE(mc::convert::putFloat32(mview(sentinel), 1, 1.0f));
        CHECK(sentinel == std::vector<uint8_t>(4, 0xAA));
    }
    SUBCASE("putFloat64") {
        std::vector<uint8_t> dest(8, 0xAA);
        CHECK(mc::convert::putFloat64(mview(dest), 0, 2.5));
        CHECK(mc::convert::float64At(view(dest), 0) == 2.5);

        std::vector<uint8_t> sentinel(8, 0xAA);
        CHECK_FALSE(mc::convert::putFloat64(mview(sentinel), 1, 2.5)); // needs 2 more bytes.
        CHECK(sentinel == std::vector<uint8_t>(8, 0xAA));
    }
    SUBCASE("stringAt") {
        std::vector<uint8_t> bytes{'A', 'B', 'C', 'D'}; // wordCount = 2, exactly 4 bytes.
        char buf[8];
        for (char& c : buf) {
            c = 'x';
        }
        size_t n = mc::convert::stringAt(view(bytes), 0, 2, buf, sizeof(buf));
        CHECK(n == 4);

        char sentinel[8];
        for (char& c : sentinel) {
            c = 'x';
        }
        // wordIndex 1 with wordCount 2 needs bytes [2, 6), the view only has 4.
        size_t failedN = mc::convert::stringAt(view(bytes), 1, 2, sentinel, sizeof(sentinel));
        CHECK(failedN == 0);
        for (char c : sentinel) {
            CHECK(c == 'x'); // untouched.
        }
    }
    SUBCASE("putString") {
        std::vector<uint8_t> dest(4, 0xAA);
        CHECK(mc::convert::putString(mview(dest), 0, 2, "ABCD"));
        CHECK(dest == std::vector<uint8_t>{'A', 'B', 'C', 'D'});

        std::vector<uint8_t> sentinel(4, 0xAA);
        CHECK_FALSE(mc::convert::putString(mview(sentinel), 1, 2, "AB"));
        CHECK(sentinel == std::vector<uint8_t>(4, 0xAA));

        // Checkpoint A coverage gap: the destination view itself is large enough (inRange()
        // passes), but the string is longer than wordCount * 2 bytes can hold.
        std::vector<uint8_t> tooLong(4, 0xAA);
        CHECK_FALSE(mc::convert::putString(mview(tooLong), 0, 2, "ABCDE"));
        CHECK(tooLong == std::vector<uint8_t>(4, 0xAA));
    }
    SUBCASE("packBits") {
        // 9 points needs ceil(9/8) = 2 output bytes.
        std::vector<uint8_t> points(9, 1);
        std::vector<uint8_t> packed(2, 0xAA);
        CHECK(mc::convert::packBits(view(points), mview(packed)));

        std::vector<uint8_t> sentinel(1, 0xAA); // one byte short of the 2 required.
        CHECK_FALSE(mc::convert::packBits(view(points), mview(sentinel)));
        CHECK(sentinel == std::vector<uint8_t>(1, 0xAA));
    }
    SUBCASE("unpackBits") {
        std::vector<uint8_t> packed{0xFF, 0x01}; // 9 bits set.
        std::vector<uint8_t> unpacked(9, 0xAA);
        CHECK(mc::convert::unpackBits(view(packed), 9, mview(unpacked)));

        std::vector<uint8_t> sentinel(8, 0xAA); // one byte short of the 9 required.
        CHECK_FALSE(mc::convert::unpackBits(view(packed), 9, mview(sentinel)));
        CHECK(sentinel == std::vector<uint8_t>(8, 0xAA));
    }
    SUBCASE("wordsToBits") {
        std::vector<uint8_t> words{0x01, 0x00}; // one word.
        std::vector<uint8_t> exact(16, 0xAA);
        CHECK(mc::convert::wordsToBits(view(words), mview(exact)));

        std::vector<uint8_t> sentinel(15, 0xAA); // one byte short of the required 16.
        CHECK_FALSE(mc::convert::wordsToBits(view(words), mview(sentinel)));
        CHECK(sentinel == std::vector<uint8_t>(15, 0xAA));

        // Checkpoint A coverage gap: an odd-sized words view is not whole words at all, checked
        // before the output-size comparison above even runs.
        std::vector<uint8_t> odd{0x01, 0x00, 0x02};
        std::vector<uint8_t> oddOut(48, 0xAA);
        CHECK_FALSE(mc::convert::wordsToBits(view(odd), mview(oddOut)));
        CHECK(oddOut == std::vector<uint8_t>(48, 0xAA));
    }
    SUBCASE("bitsToWords") {
        std::vector<uint8_t> bits(16, 1); // one word's worth of points.
        std::vector<uint8_t> exact(2, 0xAA);
        CHECK(mc::convert::bitsToWords(view(bits), mview(exact)));

        std::vector<uint8_t> sentinel(1, 0xAA); // one byte short of the required 2.
        CHECK_FALSE(mc::convert::bitsToWords(view(bits), mview(sentinel)));
        CHECK(sentinel == std::vector<uint8_t>(1, 0xAA));

        // Checkpoint A coverage gap: bytePerPoint.size not a multiple of 16 at all, checked
        // before the output-size comparison above even runs.
        std::vector<uint8_t> notMultipleOf16(17, 1);
        std::vector<uint8_t> notMultipleOut(2, 0xAA);
        CHECK_FALSE(mc::convert::bitsToWords(view(notMultipleOf16), mview(notMultipleOut)));
        CHECK(notMultipleOut == std::vector<uint8_t>(2, 0xAA));
    }
}

TEST_CASE("convert::from* builders") {
    ByteBuf words = mc::convert::fromWords({0x1234, 0xABCD});
    CHECK(words == ByteBuf{0x34, 0x12, 0xCD, 0xAB});

    ByteBuf int16s = mc::convert::fromInt16({-1, 1});
    CHECK(int16s == ByteBuf{0xFF, 0xFF, 0x01, 0x00});

    ByteBuf int32s = mc::convert::fromInt32({-1});
    CHECK(int32s == ByteBuf{0xFF, 0xFF, 0xFF, 0xFF});

    ByteBuf floats = mc::convert::fromFloat32({0.75f});
    CHECK(floats == ByteBuf{0x00, 0x00, 0x40, 0x3F});

    ByteBuf str = mc::convert::fromString("AB", 2);
    CHECK(str == ByteBuf{'A', 'B', 0x00, 0x00});

    ByteBuf truncated = mc::convert::fromString("ABCDEF", 2);
    CHECK(truncated == ByteBuf{'A', 'B', 'C', 'D'}); // truncated to fit, not rejected.

    ByteBuf bits = mc::convert::fromBits({true, false, true});
    CHECK(bits == ByteBuf{1, 0, 1});
}
