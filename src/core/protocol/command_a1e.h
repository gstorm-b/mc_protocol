/**
 * @file command_a1e.h
 * @brief 1E batch commands 00H-03H (spec `mc-protocol-frame-spec.md` §4.2): request data and
 * response-data decoding, written once against `Codec` (field_codec.h's `AsciiCodec` /
 * `BinaryCodec`) like command_qna.h.
 *
 * In 1E the command code is not part of the request data: it travels in the frame subheader
 * (spec §5.3), so `a1eCommandCode()` hands it to frame_1e.h and `a1eRequestData()` starts at the
 * head device. 1E has no length field, so `a1eResponseDataSize()` is what tells the frame layer
 * how many bytes/characters a successful response carries (spec §4.2 "Response data size").
 *
 * The test commands 04H (random write, bit units) and 05H (random write, word units) have their
 * encoders below (`a1eTestBitsRequestData()`, `a1eTestWordsRequestData()`) so their CMD-22..24 and
 * CMD-37 vectors need no restructuring later, but they are not reachable through `Op` in v1
 * (module spec "Commands", decision 2); only tests call them. Their command codes (04H, 05H) are
 * frame subheader values like the four above and belong to the frame layer once they are exposed.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "bit_payload.h"
#include "device_encode.h"
#include "field_codec.h"

#include "mc/core/device.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// 1E command code (spec §4.2) of `r`, sent as the frame subheader: 00H bit read, 01H word read,
/// 02H bit write, 03H word write.
inline uint8_t a1eCommandCode(const Request& r) noexcept {
    if (r.isWrite()) {
        return r.isBitOp() ? uint8_t{0x02} : uint8_t{0x03};
    }
    return r.isBitOp() ? uint8_t{0x00} : uint8_t{0x01};
}

/**
 * @brief Wire size of `r`'s 00H-03H request data (spec §4.2): head device + points (`u8`) + fixed
 * `00` (`u8`) + write data (writes only; Binary bits are nibble-packed with a zero last nibble on
 * an odd count, ASCII bits are exactly `count` characters).
 * @par Complexity
 * O(1); no allocation.
 */
template <class Codec> size_t a1eRequestDataSize(const Request& r) noexcept {
    size_t size = e1DeviceSize(Codec::kDataCode) + Codec::u8Size() * 2;
    if (r.isWrite()) {
        size += r.isBitOp() ? Codec::bitsSize(r.count) : Codec::wordsSize(r.count);
    }
    return size;
}

/**
 * @brief Encodes `r`'s 00H-03H request data (spec §4.2) into `out`.
 *
 * The points field is a `u8`: 256 is written as `00` (spec E8); `validate()` has already rejected
 * anything above 256.
 *
 * @param[in] r Request being encoded; a read or a write, bits or words.
 * @param[out] out Destination; must hold at least `a1eRequestDataSize<Codec>(r)` bytes.
 * @param[in] xyDigits Base of the ASCII digits of an X/Y head device
 * (`FrameConfig::xyAsciiDigits`).
 * @return Bytes/characters written.
 * @retval ErrorCode::InvalidDevice `r.head.type` has no 1E code (device_encode.h's `e1Device()`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `a1eRequestDataSize<Codec>(r)`.
 * @par Complexity
 * O(r.count); no allocation.
 * @see a1eResponseData
 */
template <class Codec>
Expected<size_t> a1eRequestData(const Request& r, MutableByteView out,
                                XyNumbering xyDigits = XyNumbering::Hex) noexcept;

/**
 * @brief Wire size of the 00H-03H response DATA for `r` (spec §4.2 "Response data size"): 0 for a
 * write; word read `Codec::wordsSize(count)`; bit read `Codec::bitsSize` of the count rounded up
 * to even, which is ceil(N/2) bytes Binary and N + (N mod 2) characters ASCII (the last one a
 * dummy on an odd N, 1E-10).
 * @par Complexity
 * O(1); no allocation.
 */
template <class Codec> size_t a1eResponseDataSize(const Request& r) noexcept {
    if (r.isWrite()) {
        return 0;
    }
    size_t count = r.count;
    return r.isBitOp() ? Codec::bitsSize(count + count % 2) : Codec::wordsSize(count);
}

/**
 * @brief Decodes 00H-03H response data `in` for `r` into `payloadOut`, in the normalized layout
 * of protocol.h's "Payload contract". A write's response carries no data; `payloadOut` is
 * untouched and 0 is returned. The ASCII dummy character of an odd bit read is ignored.
 * @param[in] r Request the response answers.
 * @param[in] in Response data bytes/characters (not the whole frame).
 * @param[out] payloadOut Destination; must hold the normalized payload size of `r`.
 * @return Bytes written to `payloadOut`.
 * @retval ErrorCode::LengthMismatch `in.size` does not equal `a1eResponseDataSize<Codec>(r)`
 * (CMDD-13).
 * @retval ErrorCode::InvalidCharacter an ASCII character is not `[0-9A-Fa-f]`, or a bit
 * character/nibble is neither 0 nor 1.
 * @par Complexity
 * O(r.count); no allocation.
 * @see a1eRequestData
 */
template <class Codec>
Expected<size_t> a1eResponseData(const Request& r, ByteView in,
                                  MutableByteView payloadOut) noexcept;

/// One entry of a 04H test command: a bit device and its ON/OFF state.
struct A1eTestBit {
    Device device;
    bool on;
};

/// One entry of a 05H test command: a word device and the word to write to it.
struct A1eTestWord {
    Device device;
    uint16_t value;
};

/// Wire size of 04H request data for `n` entries (spec §4.2): n (`u8`) + fixed `00` (`u8`) + n x
/// [device + ON/OFF (`u8`)].
template <class Codec> size_t a1eTestBitsRequestDataSize(size_t n) noexcept {
    return Codec::u8Size() * 2 + n * (e1DeviceSize(Codec::kDataCode) + Codec::u8Size());
}

/// Wire size of 05H request data for `n` entries: n (`u8`) + fixed `00` (`u8`) + n x [device +
/// write data (`u16`)].
template <class Codec> size_t a1eTestWordsRequestDataSize(size_t n) noexcept {
    return Codec::u8Size() * 2 + n * (e1DeviceSize(Codec::kDataCode) + Codec::u16Size());
}

/**
 * @brief Encodes 04H (test, random write of bits) request data (spec §4.2) into `out`.
 *
 * X/Y digits are always hex (unreachable through McProtocol in v1).
 *
 * @param[in] items The `n` entries, in wire order.
 * @param[in] n Number of entries; at most 255 (the `n` field is a `u8`).
 * @param[out] out Destination; must hold at least `a1eTestBitsRequestDataSize<Codec>(n)`.
 * @return Bytes/characters written.
 * @retval ErrorCode::PointCount `n` is above 255 (checked before `items` is read).
 * @retval ErrorCode::InvalidDevice an entry's device type has no 1E code.
 * @retval ErrorCode::BufferTooSmall `out` is too small.
 * @par Complexity
 * O(n); no allocation.
 */
template <class Codec>
Expected<size_t> a1eTestBitsRequestData(const A1eTestBit* items, size_t n,
                                         MutableByteView out) noexcept;

/**
 * @brief Encodes 05H (test, random write of words) request data (spec §4.2) into `out`.
 *
 * X/Y digits are always hex (unreachable through McProtocol in v1).
 *
 * @param[in] items The `n` entries, in wire order.
 * @param[in] n Number of entries; at most 255 (the `n` field is a `u8`).
 * @param[out] out Destination; must hold at least `a1eTestWordsRequestDataSize<Codec>(n)`.
 * @return Bytes/characters written.
 * @retval ErrorCode::PointCount `n` is above 255 (checked before `items` is read).
 * @retval ErrorCode::InvalidDevice an entry's device type has no 1E code.
 * @retval ErrorCode::BufferTooSmall `out` is too small.
 * @par Complexity
 * O(n); no allocation.
 */
template <class Codec>
Expected<size_t> a1eTestWordsRequestData(const A1eTestWord* items, size_t n,
                                          MutableByteView out) noexcept;

} // namespace mc::detail
