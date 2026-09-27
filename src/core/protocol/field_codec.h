/**
 * @file field_codec.h
 * @brief FieldCodec concept (spec `mc-protocol-frame-spec.md` §2.1-§2.3): `AsciiCodec` and
 * `BinaryCodec` each put/get the same field kinds in their own wire representation, with
 * identical signatures, so command/frame code (T-015 onward) is written once against a `Codec`
 * template parameter with no ASCII/Binary branches of its own (module spec "Internal design":
 * "no virtual dispatch on the hot path"; "one QnA command implementation ... no ASCII/Binary
 * branches inside command code except device encoding").
 *
 * Header-only by design (SPEC-core-protocol.md Project Structure lists no field_codec.cpp);
 * every function is defined inline, in terms of hexascii.h.
 *
 * Scope note (T-014): u8/u16/u32, bit data, and word/dword sequences were added first (what the
 * PRIM-* vectors exercise); "fixed" byte-string fields (spec E1, e.g. the 3E/4E subheaders,
 * `putFixed`/`matchesFixed` below) were added at T-017, once frame_3e.cpp gave them a real
 * caller and fixed the exact shape: two literals (binary and ASCII), matching the module spec's
 * own frame_3e.cpp Code Style example, which keeps them as separate `constexpr` arrays rather
 * than deriving one from the other.
 *
 * Field overflow (spec §2.1: "the encoder MUST reject values that do not fit the field width")
 * is deliberately not re-checked here either: `McProtocol::encode` always runs `validate()`
 * first (module spec, Open Question 3), which already rejects an out-of-range point count
 * (`ErrorCode::PointCount`) or device number (`ErrorCode::InvalidDevice`) before any codec runs
 * (core-model, already implemented and tested: tests/core/model/test_frame_config.cpp,
 * tests/core/model/test_device.cpp, tests/core/model/test_chunk.cpp). By the time a Request
 * reaches these functions its point count and device number already fit their native C++ type
 * width (uint8_t/uint16_t/uint32_t), so put functions take exactly those types, not something
 * wider "just in case".
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "hexascii.h"

#include "mc/core/frame_config.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// Encode-side failure: `out` (or, for a sequence function, the wire buffer it writes into) is
/// smaller than the field's own `*Size()` says it needs.
inline Error fieldBufferTooSmallError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::BufferTooSmall;
    e.message = "output buffer too small for this field";
    return e;
}

/// Decode-side failure: a bit character/nibble is neither 0 nor 1 (PRIM-13, PRIM-14). Invalid
/// hex characters on a numeric field are reported by hexascii.h's own hexDecode() instead; this
/// one is field_codec's own (bit data has no hex digits to delegate to).
inline Error invalidBitValueError() noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = ErrorCode::InvalidCharacter;
    e.message = "bit value is neither 0 nor 1";
    return e;
}

/**
 * @class BinaryCodec
 * @brief Wire representation used by 3E/4E Binary, 4C F5 and 1E Binary: little-endian, byte-wise
 * (spec: "no reinterpret_cast", assumption 8).
 *
 * Every `get*` has a precondition that `in` already holds at least the field's own `*Size()`
 * bytes; the frame/command layer that calls these has already sized `in` from a length field it
 * validated first (spec §5.1 etc.), so this is never re-checked here. `put*` does check the
 * output buffer, because a caller miscomputing a destination size is a real, cheap-to-catch bug.
 *
 * @see AsciiCodec
 */
struct BinaryCodec {
    /// Which `DataCode` this codec is (T-016 onward: lets command/frame code that is templated
    /// on `Codec` call a runtime-`DataCode` function, e.g. device_encode.h's `qnaDevice()`,
    /// without an ASCII/Binary branch of its own).
    static constexpr DataCode kDataCode = DataCode::Binary;

    static constexpr size_t u8Size() noexcept { return 1; }
    static constexpr size_t u16Size() noexcept { return 2; }
    static constexpr size_t u32Size() noexcept { return 4; }
    /// Nibble-packed, 2 points per byte (spec §2.2); first point in the high nibble, odd count
    /// -> low nibble of the last byte is 0.
    static constexpr size_t bitsSize(size_t count) noexcept { return (count + 1) / 2; }
    static constexpr size_t wordsSize(size_t wordCount) noexcept { return wordCount * 2; }
    static constexpr size_t dwordsSize(size_t dwordCount) noexcept { return dwordCount * 4; }

    static Expected<size_t> putU8(uint8_t v, MutableByteView out) noexcept {
        if (out.size < u8Size()) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        out.data[0] = v;
        return Expected<size_t>(u8Size());
    }

    /// @pre `in.size >= u8Size()`.
    static Expected<uint8_t> getU8(ByteView in) noexcept { return Expected<uint8_t>(in.data[0]); }

    static Expected<size_t> putU16(uint16_t v, MutableByteView out) noexcept {
        if (out.size < u16Size()) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        out.data[0] = static_cast<uint8_t>(v & 0xFFu);
        out.data[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        return Expected<size_t>(u16Size());
    }

    /// @pre `in.size >= u16Size()`.
    static Expected<uint16_t> getU16(ByteView in) noexcept {
        uint16_t v = static_cast<uint16_t>(in.data[0]) |
                     static_cast<uint16_t>(static_cast<uint16_t>(in.data[1]) << 8);
        return Expected<uint16_t>(v);
    }

    static Expected<size_t> putU32(uint32_t v, MutableByteView out) noexcept {
        if (out.size < u32Size()) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        out.data[0] = static_cast<uint8_t>(v & 0xFFu);
        out.data[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        out.data[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        out.data[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
        return Expected<size_t>(u32Size());
    }

    /// @pre `in.size >= u32Size()`.
    static Expected<uint32_t> getU32(ByteView in) noexcept {
        uint32_t v = static_cast<uint32_t>(in.data[0]) |
                     (static_cast<uint32_t>(in.data[1]) << 8) |
                     (static_cast<uint32_t>(in.data[2]) << 16) |
                     (static_cast<uint32_t>(in.data[3]) << 24);
        return Expected<uint32_t>(v);
    }

    /// @param[in] bytePerPoint One byte per point, each 0 or 1 (any nonzero byte counts as 1);
    /// `bytePerPoint.size` is the point count.
    static Expected<size_t> putBits(ByteView bytePerPoint, MutableByteView out) noexcept {
        size_t count = bytePerPoint.size;
        size_t needed = bitsSize(count);
        if (out.size < needed) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < needed; ++i) {
            uint8_t hi = bytePerPoint.data[2 * i] != 0 ? 1 : 0;
            uint8_t lo = (2 * i + 1 < count && bytePerPoint.data[2 * i + 1] != 0) ? 1 : 0;
            out.data[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return Expected<size_t>(needed);
    }

    /// @pre `in.size >= bitsSize(count)`; `bytePerPoint.size >= count`.
    /// @retval ErrorCode::InvalidCharacter a nibble among the first `count` is neither 0 nor 1
    /// (PRIM-13).
    static Expected<size_t> getBits(ByteView in, size_t count,
                                     MutableByteView bytePerPoint) noexcept {
        for (size_t i = 0; i < count; ++i) {
            uint8_t byte = in.data[i / 2];
            uint8_t nibble = (i % 2 == 0) ? static_cast<uint8_t>(byte >> 4)
                                           : static_cast<uint8_t>(byte & 0x0Fu);
            if (nibble != 0 && nibble != 1) {
                return Expected<size_t>(invalidBitValueError());
            }
            bytePerPoint.data[i] = nibble;
        }
        return Expected<size_t>(count);
    }

    /// @param[in] words Normalized payload layout (2 bytes per word, little-endian, spec §2.3) --
    /// already exactly this codec's own wire form.
    static Expected<size_t> putWords(ByteView words, MutableByteView out) noexcept {
        if (out.size < words.size) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < words.size; ++i) {
            out.data[i] = words.data[i];
        }
        return Expected<size_t>(words.size);
    }

    /// @pre `in.size >= wordsSize(wordCount)`; `words.size >= wordsSize(wordCount)`.
    static Expected<size_t> getWords(ByteView in, size_t wordCount,
                                      MutableByteView words) noexcept {
        size_t needed = wordsSize(wordCount);
        for (size_t i = 0; i < needed; ++i) {
            words.data[i] = in.data[i];
        }
        return Expected<size_t>(needed);
    }

    /// @param[in] dwords Normalized payload layout (4 bytes per dword, little-endian).
    static Expected<size_t> putDwords(ByteView dwords, MutableByteView out) noexcept {
        if (out.size < dwords.size) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < dwords.size; ++i) {
            out.data[i] = dwords.data[i];
        }
        return Expected<size_t>(dwords.size);
    }

    /// @pre `in.size >= dwordsSize(dwordCount)`; `dwords.size >= dwordsSize(dwordCount)`.
    static Expected<size_t> getDwords(ByteView in, size_t dwordCount,
                                       MutableByteView dwords) noexcept {
        size_t needed = dwordsSize(dwordCount);
        for (size_t i = 0; i < needed; ++i) {
            dwords.data[i] = in.data[i];
        }
        return Expected<size_t>(needed);
    }

    /// @brief A fixed byte-string field (spec E1, e.g. a frame subheader): writes
    /// `binaryLiteral` verbatim, ignoring `asciiLiteral` (AsciiCodec's own `putFixed` does the
    /// reverse). Never a decoded/encoded number, per spec E1.
    static Expected<size_t> putFixed(ByteView binaryLiteral, ByteView /*asciiLiteral*/,
                                      MutableByteView out) noexcept {
        if (out.size < binaryLiteral.size) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < binaryLiteral.size; ++i) {
            out.data[i] = binaryLiteral.data[i];
        }
        return Expected<size_t>(binaryLiteral.size);
    }

    /// @return Whether `in` starts with exactly `binaryLiteral` (`asciiLiteral` unused).
    /// @pre `in.size >= binaryLiteral.size`.
    static bool matchesFixed(ByteView in, ByteView binaryLiteral,
                              ByteView /*asciiLiteral*/) noexcept {
        for (size_t i = 0; i < binaryLiteral.size; ++i) {
            if (in.data[i] != binaryLiteral.data[i]) {
                return false;
            }
        }
        return true;
    }
};

/**
 * @class AsciiCodec
 * @brief Wire representation used by 3E/4E ASCII, 4C F1-F4 and 1E ASCII: each field is its
 * value's own hex digits, upper case (spec §2.1), most significant digit first; bit data is
 * `'0'`/`'1'` characters (spec §2.2). Built entirely on hexascii.h.
 *
 * Same preconditions and buffer-checking policy as BinaryCodec.
 *
 * @see BinaryCodec
 */
struct AsciiCodec {
    /// See BinaryCodec::kDataCode.
    static constexpr DataCode kDataCode = DataCode::Ascii;

    static constexpr size_t u8Size() noexcept { return 2; }
    static constexpr size_t u16Size() noexcept { return 4; }
    static constexpr size_t u32Size() noexcept { return 8; }
    /// One character per point (spec §2.2).
    static constexpr size_t bitsSize(size_t count) noexcept { return count; }
    static constexpr size_t wordsSize(size_t wordCount) noexcept { return wordCount * 4; }
    static constexpr size_t dwordsSize(size_t dwordCount) noexcept { return dwordCount * 8; }

    static Expected<size_t> putU8(uint8_t v, MutableByteView out) noexcept {
        return hexEncode(ByteView{&v, 1}, out);
    }

    /// @pre `in.size >= u8Size()`.
    /// @retval ErrorCode::InvalidCharacter a character is not `[0-9A-Fa-f]` (PRIM-06).
    static Expected<uint8_t> getU8(ByteView in) noexcept {
        uint8_t v = 0;
        auto r = hexDecode(ByteView{in.data, u8Size()}, MutableByteView{&v, 1});
        if (!r.hasValue()) {
            return Expected<uint8_t>(r.error());
        }
        return Expected<uint8_t>(v);
    }

    static Expected<size_t> putU16(uint16_t v, MutableByteView out) noexcept {
        uint8_t bytes[2] = {static_cast<uint8_t>((v >> 8) & 0xFFu),
                            static_cast<uint8_t>(v & 0xFFu)};
        return hexEncode(ByteView{bytes, 2}, out);
    }

    /// @pre `in.size >= u16Size()`.
    /// @retval ErrorCode::InvalidCharacter a character is not `[0-9A-Fa-f]` (PRIM-06).
    static Expected<uint16_t> getU16(ByteView in) noexcept {
        uint8_t bytes[2] = {0, 0};
        auto r = hexDecode(ByteView{in.data, u16Size()}, MutableByteView{bytes, 2});
        if (!r.hasValue()) {
            return Expected<uint16_t>(r.error());
        }
        uint16_t v = static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
        return Expected<uint16_t>(v);
    }

    static Expected<size_t> putU32(uint32_t v, MutableByteView out) noexcept {
        uint8_t bytes[4] = {
            static_cast<uint8_t>((v >> 24) & 0xFFu),
            static_cast<uint8_t>((v >> 16) & 0xFFu),
            static_cast<uint8_t>((v >> 8) & 0xFFu),
            static_cast<uint8_t>(v & 0xFFu),
        };
        return hexEncode(ByteView{bytes, 4}, out);
    }

    /// @pre `in.size >= u32Size()`.
    /// @retval ErrorCode::InvalidCharacter a character is not `[0-9A-Fa-f]` (PRIM-06).
    static Expected<uint32_t> getU32(ByteView in) noexcept {
        uint8_t bytes[4] = {0, 0, 0, 0};
        auto r = hexDecode(ByteView{in.data, u32Size()}, MutableByteView{bytes, 4});
        if (!r.hasValue()) {
            return Expected<uint32_t>(r.error());
        }
        uint32_t v = (static_cast<uint32_t>(bytes[0]) << 24) |
                     (static_cast<uint32_t>(bytes[1]) << 16) |
                     (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
        return Expected<uint32_t>(v);
    }

    /// @param[in] bytePerPoint One byte per point, each 0 or 1 (any nonzero byte counts as 1);
    /// `bytePerPoint.size` is the point count.
    static Expected<size_t> putBits(ByteView bytePerPoint, MutableByteView out) noexcept {
        size_t count = bytePerPoint.size;
        if (out.size < count) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < count; ++i) {
            out.data[i] = bytePerPoint.data[i] != 0 ? uint8_t{'1'} : uint8_t{'0'};
        }
        return Expected<size_t>(count);
    }

    /// @pre `in.size >= count`; `bytePerPoint.size >= count`.
    /// @retval ErrorCode::InvalidCharacter a character among the first `count` is neither `'0'`
    /// nor `'1'` (PRIM-14).
    static Expected<size_t> getBits(ByteView in, size_t count,
                                     MutableByteView bytePerPoint) noexcept {
        for (size_t i = 0; i < count; ++i) {
            uint8_t c = in.data[i];
            if (c == '0') {
                bytePerPoint.data[i] = 0;
            } else if (c == '1') {
                bytePerPoint.data[i] = 1;
            } else {
                return Expected<size_t>(invalidBitValueError());
            }
        }
        return Expected<size_t>(count);
    }

    /// @param[in] words Normalized payload layout (2 bytes per word, little-endian, spec §2.3).
    static Expected<size_t> putWords(ByteView words, MutableByteView out) noexcept {
        size_t wordCount = words.size / 2;
        size_t needed = wordsSize(wordCount);
        if (out.size < needed) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < wordCount; ++i) {
            uint16_t hi = words.data[2 * i + 1];
            uint16_t value = static_cast<uint16_t>(words.data[2 * i]) |
                              static_cast<uint16_t>(static_cast<uint16_t>(hi) << 8);
            // Capacity for this word was already confirmed above; putU16 cannot fail here.
            (void)putU16(value, MutableByteView{out.data + 4 * i, 4});
        }
        return Expected<size_t>(needed);
    }

    /// @pre `in.size >= wordsSize(wordCount)`; `words.size >= 2 * wordCount`.
    /// @retval ErrorCode::InvalidCharacter a character of any word is not `[0-9A-Fa-f]`
    /// (PRIM-06).
    static Expected<size_t> getWords(ByteView in, size_t wordCount,
                                      MutableByteView words) noexcept {
        for (size_t i = 0; i < wordCount; ++i) {
            auto r = getU16(ByteView{in.data + 4 * i, 4});
            if (!r.hasValue()) {
                return Expected<size_t>(r.error());
            }
            uint16_t value = r.value();
            words.data[2 * i] = static_cast<uint8_t>(value & 0xFFu);
            words.data[2 * i + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        }
        return Expected<size_t>(2 * wordCount);
    }

    /// @param[in] dwords Normalized payload layout (4 bytes per dword, little-endian).
    static Expected<size_t> putDwords(ByteView dwords, MutableByteView out) noexcept {
        size_t dwordCount = dwords.size / 4;
        size_t needed = dwordsSize(dwordCount);
        if (out.size < needed) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < dwordCount; ++i) {
            uint32_t b1 = dwords.data[4 * i + 1];
            uint32_t b2 = dwords.data[4 * i + 2];
            uint32_t b3 = dwords.data[4 * i + 3];
            uint32_t value =
                static_cast<uint32_t>(dwords.data[4 * i]) | (b1 << 8) | (b2 << 16) | (b3 << 24);
            (void)putU32(value, MutableByteView{out.data + 8 * i, 8});
        }
        return Expected<size_t>(needed);
    }

    /// @pre `in.size >= dwordsSize(dwordCount)`; `dwords.size >= 4 * dwordCount`.
    /// @retval ErrorCode::InvalidCharacter a character of any dword is not `[0-9A-Fa-f]`
    /// (PRIM-06).
    static Expected<size_t> getDwords(ByteView in, size_t dwordCount,
                                       MutableByteView dwords) noexcept {
        for (size_t i = 0; i < dwordCount; ++i) {
            auto r = getU32(ByteView{in.data + 8 * i, 8});
            if (!r.hasValue()) {
                return Expected<size_t>(r.error());
            }
            uint32_t value = r.value();
            dwords.data[4 * i] = static_cast<uint8_t>(value & 0xFFu);
            dwords.data[4 * i + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
            dwords.data[4 * i + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
            dwords.data[4 * i + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
        }
        return Expected<size_t>(4 * dwordCount);
    }

    /// @brief A fixed byte-string field (spec E1): writes `asciiLiteral` verbatim, ignoring
    /// `binaryLiteral` (BinaryCodec's own `putFixed` does the reverse). Never the hex encoding of
    /// a decoded number, per spec E1 -- the caller supplies the literal text directly.
    static Expected<size_t> putFixed(ByteView /*binaryLiteral*/, ByteView asciiLiteral,
                                      MutableByteView out) noexcept {
        if (out.size < asciiLiteral.size) {
            return Expected<size_t>(fieldBufferTooSmallError());
        }
        for (size_t i = 0; i < asciiLiteral.size; ++i) {
            out.data[i] = asciiLiteral.data[i];
        }
        return Expected<size_t>(asciiLiteral.size);
    }

    /// @return Whether `in` starts with exactly `asciiLiteral` (`binaryLiteral` unused).
    /// @pre `in.size >= asciiLiteral.size`.
    static bool matchesFixed(ByteView in, ByteView /*binaryLiteral*/,
                             ByteView asciiLiteral) noexcept {
        for (size_t i = 0; i < asciiLiteral.size; ++i) {
            if (in.data[i] != asciiLiteral.data[i]) {
                return false;
            }
        }
        return true;
    }
};

} // namespace mc::detail
