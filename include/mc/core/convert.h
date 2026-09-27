/**
 * @file convert.h
 * @brief Application-level conversion between normalized payload bytes and values (spec
 * `mc-protocol-frame-spec.md` §2.3, §2.4, §8.7).
 */
#pragma once

#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace mc::convert {

/**
 * @namespace mc::convert
 * @brief Word/dword/float/string/bit conversions between normalized payload bytes and
 * application values.
 *
 * Every accessor and writer is bounds-checked: an out-of-range index or an undersized view never
 * reads or writes past the view, and reports failure instead (the zero value for an accessor,
 * `false` for a writer). Word order is **low word at the lower device number** (spec §2.3, §8.7):
 * for a multi-word value, `words[wordIndex]` holds the low bits and each following word holds the
 * next 16 bits up.
 */

/**
 * @brief Reads the word at @p wordIndex.
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the word to read.
 * @return The word, or 0 when @p wordIndex is out of range.
 * @par Complexity
 * O(1); no allocation.
 * @see putWord
 */
uint16_t wordAt(ByteView words, size_t wordIndex) noexcept;

/**
 * @brief Reads the word at @p wordIndex as a signed, two's-complement value.
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the word to read.
 * @return The value, or 0 when @p wordIndex is out of range.
 * @par Complexity
 * O(1); no allocation.
 * @see putInt16
 */
int16_t int16At(ByteView words, size_t wordIndex) noexcept;

/**
 * @brief Reads the double word (two words) at @p wordIndex.
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the low word; `words[wordIndex]` is the low 16 bits,
 * `words[wordIndex + 1]` the high 16 bits.
 * @return The value, or 0 when the two-word range is out of range.
 * @par Complexity
 * O(1); no allocation.
 * @see putUint32
 */
uint32_t uint32At(ByteView words, size_t wordIndex) noexcept;

/**
 * @brief Reads the double word at @p wordIndex as a signed, two's-complement value.
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the low word (see uint32At()).
 * @return The value, or 0 when the two-word range is out of range.
 * @par Complexity
 * O(1); no allocation.
 * @see putInt32
 */
int32_t int32At(ByteView words, size_t wordIndex) noexcept;

/**
 * @brief Reads the double word at @p wordIndex as an IEEE-754 single-precision float.
 *
 * The bit pattern is exactly what uint32At() would return at the same index.
 *
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the low word (see uint32At()).
 * @return The value, or 0 when the two-word range is out of range.
 * @par Complexity
 * O(1); no allocation.
 * @see putFloat32
 */
float float32At(ByteView words, size_t wordIndex) noexcept;

/**
 * @brief Reads the quad word (four words) at @p wordIndex as an IEEE-754 double-precision float.
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the lowest word; each following word up to `wordIndex + 3` holds
 * the next 16 bits up.
 * @return The value, or 0 when the four-word range is out of range.
 * @par Complexity
 * O(1); no allocation.
 * @see putFloat64
 */
double float64At(ByteView words, size_t wordIndex) noexcept;

/**
 * @brief Writes @p v as the word at @p wordIndex.
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the word to write.
 * @param[in] v Value to write.
 * @return true on success; false when @p wordIndex is out of range (nothing is written).
 * @par Complexity
 * O(1); no allocation.
 * @see wordAt
 */
bool putWord(MutableByteView words, size_t wordIndex, uint16_t v) noexcept;

/**
 * @brief Writes @p v as the word at @p wordIndex.
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the word to write.
 * @param[in] v Value to write.
 * @return true on success; false when @p wordIndex is out of range (nothing is written).
 * @par Complexity
 * O(1); no allocation.
 * @see int16At
 */
bool putInt16(MutableByteView words, size_t wordIndex, int16_t v) noexcept;

/**
 * @brief Writes @p v as the double word at @p wordIndex.
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the low word (see uint32At()).
 * @param[in] v Value to write.
 * @return true on success; false when the two-word range is out of range (nothing is written).
 * @par Complexity
 * O(1); no allocation.
 * @see uint32At
 */
bool putUint32(MutableByteView words, size_t wordIndex, uint32_t v) noexcept;

/**
 * @brief Writes @p v as the double word at @p wordIndex.
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the low word (see uint32At()).
 * @param[in] v Value to write.
 * @return true on success; false when the two-word range is out of range (nothing is written).
 * @par Complexity
 * O(1); no allocation.
 * @see int32At
 */
bool putInt32(MutableByteView words, size_t wordIndex, int32_t v) noexcept;

/**
 * @brief Writes @p v as the double word at @p wordIndex.
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the low word (see uint32At()).
 * @param[in] v Value to write.
 * @return true on success; false when the two-word range is out of range (nothing is written).
 * @par Complexity
 * O(1); no allocation.
 * @see float32At
 */
bool putFloat32(MutableByteView words, size_t wordIndex, float v) noexcept;

/**
 * @brief Writes @p v as the quad word at @p wordIndex.
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the lowest word (see float64At()).
 * @param[in] v Value to write.
 * @return true on success; false when the four-word range is out of range (nothing is written).
 * @par Complexity
 * O(1); no allocation.
 * @see float64At
 */
bool putFloat64(MutableByteView words, size_t wordIndex, double v) noexcept;

/**
 * @brief Reads a string out of @p wordCount words (spec §8.7: the first character is in the low
 * byte of the first word).
 *
 * Stops at the first NUL byte or after @p wordCount words, whichever comes first, and copies at
 * most @p capacity characters to @p out. Appends a terminating NUL only when a character slot
 * remains after the copy (i.e. never when the copy itself used the last slot in @p capacity).
 *
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the first word.
 * @param[in] wordCount Number of words the string occupies.
 * @param[out] out Destination buffer.
 * @param[in] capacity Number of bytes available at @p out.
 * @return The number of characters copied to @p out; 0 when the word range is out of range.
 * @par Complexity
 * O(wordCount); no allocation.
 * @see putString
 */
size_t stringAt(ByteView words, size_t wordIndex, size_t wordCount, char* out,
                 size_t capacity) noexcept;

/**
 * @brief Writes @p s into @p wordCount words, NUL-padded (spec §8.7).
 * @param[in,out] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[in] wordIndex Index of the first word.
 * @param[in] wordCount Number of words to write; `wordCount * 2` bytes.
 * @param[in] s Text to write.
 * @return true on success; false when @p s does not fit in `wordCount * 2` bytes, or the word
 * range is out of range (nothing is written either way).
 * @par Complexity
 * O(wordCount); no allocation.
 * @see stringAt
 */
bool putString(MutableByteView words, size_t wordIndex, size_t wordCount,
                std::string_view s) noexcept;

/**
 * @brief Packs one-byte-per-point bit data into eight-points-per-byte, LSB first
 * (`BitLayout::PackedLsbFirst`: point i at bit `i % 8` of byte `i / 8`).
 * @param[in] bytePerPoint One byte per point (`BitLayout::BytePerPoint`); its size is the point
 * count n.
 * @param[out] packedLsbFirst Destination; must hold at least `(n + 7) / 8` bytes. Any bits beyond
 * n in the last byte are set to 0.
 * @return true on success; false when @p packedLsbFirst is too small (nothing is written).
 * @par Complexity
 * O(n) in the number of points; no allocation.
 * @see unpackBits
 */
bool packBits(ByteView bytePerPoint, MutableByteView packedLsbFirst) noexcept;

/**
 * @brief Unpacks eight-points-per-byte bit data into one-byte-per-point.
 * @param[in] packedLsbFirst Source; must hold at least `(n + 7) / 8` bytes.
 * @param[in] n Number of points to unpack.
 * @param[out] bytePerPoint Destination; must hold at least n bytes. Each byte is written 0 or 1.
 * @return true on success; false when either view is too small (nothing is written).
 * @par Complexity
 * O(n) in the number of points; no allocation.
 * @see packBits
 */
bool unpackBits(ByteView packedLsbFirst, size_t n, MutableByteView bytePerPoint) noexcept;

/**
 * @brief Expands a bit device read in word units into one byte per point (spec §2.4: bit i of
 * word k is device `head + 16k + i`).
 * @param[in] words Normalized payload bytes, 2 bytes per word, little-endian.
 * @param[out] bytePerPoint Destination; its size must equal `16 * (words.size / 2)` exactly.
 * @return true on success; false when @p words has an odd size or @p bytePerPoint's size does
 * not match (nothing is written).
 * @par Complexity
 * O(n) in the number of words; no allocation.
 * @see bitsToWords
 */
bool wordsToBits(ByteView words, MutableByteView bytePerPoint) noexcept;

/**
 * @brief Packs one-byte-per-point bit data back into word units (the inverse of wordsToBits()).
 * @param[in] bytePerPoint Source; its size must be a multiple of 16.
 * @param[out] words Destination; its size must equal `bytePerPoint.size / 16 * 2` exactly.
 * @return true on success; false when @p bytePerPoint's size is not a multiple of 16 or
 * @p words's size does not match (nothing is written).
 * @par Complexity
 * O(n) in the number of words; no allocation.
 * @see wordsToBits
 */
bool bitsToWords(ByteView bytePerPoint, MutableByteView words) noexcept;

/**
 * @brief Builds a normalized payload from a list of words.
 * @param[in] values Words to encode, low word first.
 * @return A buffer holding `values.size() * 2` bytes.
 * @par Complexity
 * O(n) in the number of words; allocates one ByteBuf.
 */
ByteBuf fromWords(std::initializer_list<uint16_t> values);

/**
 * @brief Builds a normalized payload from a list of signed words.
 * @param[in] values Values to encode, low word first.
 * @return A buffer holding `values.size() * 2` bytes.
 * @par Complexity
 * O(n) in the number of words; allocates one ByteBuf.
 */
ByteBuf fromInt16(std::initializer_list<int16_t> values);

/**
 * @brief Builds a normalized payload from a list of signed double words.
 * @param[in] values Values to encode, low word first per value.
 * @return A buffer holding `values.size() * 4` bytes.
 * @par Complexity
 * O(n) in the number of double words; allocates one ByteBuf.
 */
ByteBuf fromInt32(std::initializer_list<int32_t> values);

/**
 * @brief Builds a normalized payload from a list of single-precision floats.
 * @param[in] values Values to encode, low word first per value.
 * @return A buffer holding `values.size() * 4` bytes.
 * @par Complexity
 * O(n) in the number of double words; allocates one ByteBuf.
 */
ByteBuf fromFloat32(std::initializer_list<float> values);

/**
 * @brief Builds a normalized payload from a list of double-precision floats.
 * @param[in] values Values to encode, low word first per value.
 * @return A buffer holding `values.size() * 8` bytes.
 * @par Complexity
 * O(n) in the number of quad words; allocates one ByteBuf.
 */
ByteBuf fromFloat64(std::initializer_list<double> values);

/**
 * @brief Builds a normalized payload holding @p s in @p wordCount words, NUL-padded.
 * @param[in] s Text to encode.
 * @param[in] wordCount Number of words to allocate; `wordCount * 2` bytes. @p s is truncated to
 * fit when it is longer.
 * @return A buffer holding `wordCount * 2` bytes.
 * @par Complexity
 * O(wordCount); allocates one ByteBuf.
 */
ByteBuf fromString(std::string_view s, size_t wordCount);

/**
 * @brief Builds a normalized payload from a list of bits, one byte per point
 * (`BitLayout::BytePerPoint`, the layout `Request::writeBits()` defaults to).
 * @param[in] values Bit values, device order.
 * @return A buffer holding `values.size()` bytes, each 0x01 (true) or 0x00 (false).
 * @par Complexity
 * O(n) in the number of points; allocates one ByteBuf.
 */
ByteBuf fromBits(std::initializer_list<bool> values);

} // namespace mc::convert
