// mc::convert (spec §2.3, §2.4, §8.7): normalized-payload bytes <-> application values. Every
// reader/writer checks its bounds before touching anything, so a failure never reads or writes
// past the view (and a writer's failure never partially writes either). Multi-byte values are
// little-endian, word by word, low word at the lower device number.
#include "mc/core/convert.h"

#include <cstring>

namespace mc::convert {
namespace {

bool inRange(size_t viewSize, size_t byteOffset, size_t byteLen) noexcept {
    return byteOffset + byteLen <= viewSize;
}

} // namespace

uint16_t wordAt(ByteView words, size_t wordIndex) noexcept {
    size_t offset = wordIndex * 2;
    if (!inRange(words.size, offset, 2)) {
        return 0;
    }
    return static_cast<uint16_t>(words.data[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(words.data[offset + 1]) << 8);
}

int16_t int16At(ByteView words, size_t wordIndex) noexcept {
    return static_cast<int16_t>(wordAt(words, wordIndex));
}

uint32_t uint32At(ByteView words, size_t wordIndex) noexcept {
    if (!inRange(words.size, wordIndex * 2, 4)) {
        return 0;
    }
    uint32_t low = wordAt(words, wordIndex);
    uint32_t high = wordAt(words, wordIndex + 1);
    return low | (high << 16);
}

int32_t int32At(ByteView words, size_t wordIndex) noexcept {
    return static_cast<int32_t>(uint32At(words, wordIndex));
}

float float32At(ByteView words, size_t wordIndex) noexcept {
    uint32_t bits = uint32At(words, wordIndex);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

double float64At(ByteView words, size_t wordIndex) noexcept {
    if (!inRange(words.size, wordIndex * 2, 8)) {
        return 0.0;
    }
    uint64_t bits = 0;
    for (size_t i = 0; i < 4; ++i) {
        bits |= static_cast<uint64_t>(wordAt(words, wordIndex + i)) << (16 * i);
    }
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

bool putWord(MutableByteView words, size_t wordIndex, uint16_t v) noexcept {
    size_t offset = wordIndex * 2;
    if (!inRange(words.size, offset, 2)) {
        return false;
    }
    words.data[offset] = static_cast<uint8_t>(v & 0xFFu);
    words.data[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    return true;
}

bool putInt16(MutableByteView words, size_t wordIndex, int16_t v) noexcept {
    return putWord(words, wordIndex, static_cast<uint16_t>(v));
}

bool putUint32(MutableByteView words, size_t wordIndex, uint32_t v) noexcept {
    if (!inRange(words.size, wordIndex * 2, 4)) {
        return false;
    }
    putWord(words, wordIndex, static_cast<uint16_t>(v & 0xFFFFu));
    putWord(words, wordIndex + 1, static_cast<uint16_t>((v >> 16) & 0xFFFFu));
    return true;
}

bool putInt32(MutableByteView words, size_t wordIndex, int32_t v) noexcept {
    return putUint32(words, wordIndex, static_cast<uint32_t>(v));
}

bool putFloat32(MutableByteView words, size_t wordIndex, float v) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return putUint32(words, wordIndex, bits);
}

bool putFloat64(MutableByteView words, size_t wordIndex, double v) noexcept {
    if (!inRange(words.size, wordIndex * 2, 8)) {
        return false;
    }
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (size_t i = 0; i < 4; ++i) {
        putWord(words, wordIndex + i, static_cast<uint16_t>((bits >> (16 * i)) & 0xFFFFu));
    }
    return true;
}

size_t stringAt(ByteView words, size_t wordIndex, size_t wordCount, char* out,
                 size_t capacity) noexcept {
    size_t offset = wordIndex * 2;
    size_t byteLen = wordCount * 2;
    if (!inRange(words.size, offset, byteLen)) {
        return 0;
    }
    size_t found = 0;
    while (found < byteLen && words.data[offset + found] != 0) {
        ++found;
    }
    size_t written = (found < capacity) ? found : capacity;
    for (size_t i = 0; i < written; ++i) {
        out[i] = static_cast<char>(words.data[offset + i]);
    }
    if (written < capacity) {
        out[written] = '\0';
    }
    return written;
}

bool putString(MutableByteView words, size_t wordIndex, size_t wordCount,
                std::string_view s) noexcept {
    size_t offset = wordIndex * 2;
    size_t byteLen = wordCount * 2;
    if (!inRange(words.size, offset, byteLen)) {
        return false;
    }
    if (s.size() > byteLen) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        words.data[offset + i] = static_cast<uint8_t>(s[i]);
    }
    for (size_t i = s.size(); i < byteLen; ++i) {
        words.data[offset + i] = 0;
    }
    return true;
}

bool packBits(ByteView bytePerPoint, MutableByteView packedLsbFirst) noexcept {
    size_t n = bytePerPoint.size;
    size_t required = (n + 7) / 8;
    if (packedLsbFirst.size < required) {
        return false;
    }
    for (size_t byteIdx = 0; byteIdx < required; ++byteIdx) {
        size_t base = byteIdx * 8;
        size_t bitsInByte = (n - base < 8) ? (n - base) : 8;
        uint8_t packed = 0;
        for (size_t bit = 0; bit < bitsInByte; ++bit) {
            if (bytePerPoint.data[base + bit] != 0) {
                packed |= static_cast<uint8_t>(1u << bit);
            }
        }
        packedLsbFirst.data[byteIdx] = packed;
    }
    return true;
}

bool unpackBits(ByteView packedLsbFirst, size_t n, MutableByteView bytePerPoint) noexcept {
    size_t required = (n + 7) / 8;
    if (packedLsbFirst.size < required || bytePerPoint.size < n) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        uint8_t byte = packedLsbFirst.data[i / 8];
        bytePerPoint.data[i] = static_cast<uint8_t>((byte >> (i % 8)) & 0x01u);
    }
    return true;
}

bool wordsToBits(ByteView words, MutableByteView bytePerPoint) noexcept {
    if (words.size % 2 != 0) {
        return false;
    }
    size_t wordCount = words.size / 2;
    if (bytePerPoint.size != wordCount * 16) {
        return false;
    }
    for (size_t w = 0; w < wordCount; ++w) {
        uint16_t word = wordAt(words, w);
        for (size_t bit = 0; bit < 16; ++bit) {
            bytePerPoint.data[w * 16 + bit] = static_cast<uint8_t>((word >> bit) & 0x01u);
        }
    }
    return true;
}

bool bitsToWords(ByteView bytePerPoint, MutableByteView words) noexcept {
    if (bytePerPoint.size % 16 != 0) {
        return false;
    }
    size_t wordCount = bytePerPoint.size / 16;
    if (words.size != wordCount * 2) {
        return false;
    }
    for (size_t w = 0; w < wordCount; ++w) {
        uint16_t word = 0;
        for (size_t bit = 0; bit < 16; ++bit) {
            if (bytePerPoint.data[w * 16 + bit] != 0) {
                word |= static_cast<uint16_t>(1u << bit);
            }
        }
        putWord(words, w, word);
    }
    return true;
}

ByteBuf fromWords(std::initializer_list<uint16_t> values) {
    ByteBuf buf(values.size() * 2);
    MutableByteView view{buf.data(), buf.size()};
    size_t i = 0;
    for (uint16_t v : values) {
        putWord(view, i, v);
        ++i;
    }
    return buf;
}

ByteBuf fromInt16(std::initializer_list<int16_t> values) {
    ByteBuf buf(values.size() * 2);
    MutableByteView view{buf.data(), buf.size()};
    size_t i = 0;
    for (int16_t v : values) {
        putInt16(view, i, v);
        ++i;
    }
    return buf;
}

ByteBuf fromInt32(std::initializer_list<int32_t> values) {
    ByteBuf buf(values.size() * 4);
    MutableByteView view{buf.data(), buf.size()};
    size_t i = 0;
    for (int32_t v : values) {
        putInt32(view, i * 2, v);
        ++i;
    }
    return buf;
}

ByteBuf fromFloat32(std::initializer_list<float> values) {
    ByteBuf buf(values.size() * 4);
    MutableByteView view{buf.data(), buf.size()};
    size_t i = 0;
    for (float v : values) {
        putFloat32(view, i * 2, v);
        ++i;
    }
    return buf;
}

ByteBuf fromFloat64(std::initializer_list<double> values) {
    ByteBuf buf(values.size() * 8);
    MutableByteView view{buf.data(), buf.size()};
    size_t i = 0;
    for (double v : values) {
        putFloat64(view, i * 4, v);
        ++i;
    }
    return buf;
}

ByteBuf fromString(std::string_view s, size_t wordCount) {
    ByteBuf buf(wordCount * 2);
    // Truncate s to fit first, so putString() below (which fails outright on an oversized s)
    // always succeeds: this builder's contract is "truncated to fit", not "fails to fit".
    size_t maxLen = (s.size() < buf.size()) ? s.size() : buf.size();
    putString(MutableByteView{buf.data(), buf.size()}, 0, wordCount, s.substr(0, maxLen));
    return buf;
}

ByteBuf fromBits(std::initializer_list<bool> values) {
    ByteBuf buf(values.size());
    size_t i = 0;
    for (bool v : values) {
        buf[i] = v ? 1 : 0;
        ++i;
    }
    return buf;
}

} // namespace mc::convert
