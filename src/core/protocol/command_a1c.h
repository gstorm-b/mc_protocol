/**
 * @file command_a1c.h
 * @brief 1C batch commands BR/WR/BW/WW (ACPU) and JR/QR/JW/QW (AnA/AnU) (spec
 * `mc-protocol-frame-spec.md` §4.3): request data and response-data decoding. 1C is always ASCII,
 * so nothing here is templated on a codec; the field codec is `AsciiCodec` throughout.
 *
 * Request data is the command (2 characters), the message wait (1 hex character, spec E7), then
 * the character area: head device (device_encode.h's `c1Device()`), the number of points (2
 * characters, 256 written as `00`, spec E8) and, for a write, the data. The command letters depend
 * on `FrameConfig::commandSet`; the response data size is computed from the request, as the frame
 * layer has no length field to read it from.
 *
 * The test commands BT/JT (bit units) and WT/QT (word units) have their encoders below
 * (`a1cTestBitsRequestData()`, `a1cTestWordsRequestData()`) so their CMD-31 and CMD-32 vectors need
 * no restructuring later, but they are not reachable through `Op` in v1 (module spec "Commands",
 * decision 2); only tests call them.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "bit_payload.h"
#include "device_encode.h"
#include "field_codec.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// Characters of the command (`BR`, `QW`, ...).
inline constexpr size_t kA1cCommandSize = 2;
/// Characters of the message wait: one hex digit, unit 10 ms (spec E7).
inline constexpr size_t kA1cMessageWaitSize = 1;

/// Two command letters of a 1C command.
struct A1cCommand {
    char letters[2]; ///< The command as sent, e.g. `{'B', 'R'}`.
};

/**
 * @brief The 1C command of `r` for a command set (spec §4.3 table): ACPU `BR` (bit read), `WR`
 * (word read), `BW` (bit write), `WW` (word write); AnA/AnU `JR`, `QR`, `JW`, `QW`.
 * @param[in] r Request being encoded.
 * @param[in] commandSet ACPU or AnA/AnU (`FrameConfig::commandSet`).
 * @return The two command letters.
 * @par Complexity
 * O(1); no allocation.
 */
inline A1cCommand a1cCommand(const Request& r, C1CommandSet commandSet) noexcept {
    const bool ana = commandSet == C1CommandSet::AnA;
    if (r.isBitOp()) {
        return r.isWrite() ? A1cCommand{{ana ? 'J' : 'B', 'W'}}
                           : A1cCommand{{ana ? 'J' : 'B', 'R'}};
    }
    return r.isWrite() ? A1cCommand{{ana ? 'Q' : 'W', 'W'}} : A1cCommand{{ana ? 'Q' : 'W', 'R'}};
}

/**
 * @brief Wire size of `r`'s request data (spec §4.3): command + message wait + head device +
 * points + write data (writes only: `count` characters for bits, `4 * count` for words).
 * @param[in] r Request being encoded.
 * @param[in] commandSet ACPU or AnA/AnU; the device field widths differ (spec §3.3).
 * @return The size in characters.
 * @par Complexity
 * O(1); no allocation.
 */
inline size_t a1cRequestDataSize(const Request& r, C1CommandSet commandSet) noexcept {
    size_t size = kA1cCommandSize + kA1cMessageWaitSize + c1DeviceSize(r.head, commandSet) +
                  AsciiCodec::u8Size();
    if (r.isWrite()) {
        size += r.isBitOp() ? AsciiCodec::bitsSize(r.count) : AsciiCodec::wordsSize(r.count);
    }
    return size;
}

/**
 * @brief Encodes `r`'s request data (spec §4.3) into `out`.
 *
 * The points field is 2 hex characters: 256 is written as `00` (spec E8); `validate()` has already
 * rejected anything above 256.
 *
 * @param[in] r Request being encoded; a read or a write, bits or words.
 * @param[in] commandSet ACPU or AnA/AnU.
 * @param[in] messageWait Message wait, 0-15 (unit 10 ms), written as one upper-case hex digit.
 * @param[out] out Destination; must hold at least `a1cRequestDataSize(r, commandSet)` characters.
 * @return Characters written.
 * @retval ErrorCode::InvalidConfig `messageWait` is above 15.
 * @retval ErrorCode::InvalidDevice `r.head.type` has no 1C code (device_encode.h's `c1Device()`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `a1cRequestDataSize(r, commandSet)`.
 * @par Complexity
 * O(r.count); no allocation.
 * @see a1cResponseData
 */
Expected<size_t> a1cRequestData(const Request& r, C1CommandSet commandSet, uint8_t messageWait,
                                MutableByteView out) noexcept;

/**
 * @brief Wire size of the response DATA for `r` (spec §4.3 table): 0 for a write; a bit read `N`
 * characters; a word read `4N` characters.
 * @par Complexity
 * O(1); no allocation.
 */
inline size_t a1cResponseDataSize(const Request& r) noexcept {
    if (r.isWrite()) {
        return 0;
    }
    return r.isBitOp() ? AsciiCodec::bitsSize(r.count) : AsciiCodec::wordsSize(r.count);
}

/**
 * @brief Decodes response data `in` for `r` into `payloadOut`, in the normalized layout of
 * protocol.h's "Payload contract". A write's response carries no data; `payloadOut` is untouched
 * and 0 is returned.
 * @param[in] r Request the response answers.
 * @param[in] in Response data characters (not the whole frame).
 * @param[out] payloadOut Destination; must hold the normalized payload size of `r`.
 * @return Bytes written to `payloadOut`.
 * @retval ErrorCode::LengthMismatch `in.size` does not equal `a1cResponseDataSize(r)`.
 * @retval ErrorCode::InvalidCharacter a data character is not `[0-9A-Fa-f]` (words) or not
 * `'0'`/`'1'` (bits).
 * @par Complexity
 * O(r.count); no allocation.
 * @see a1cRequestData
 */
Expected<size_t> a1cResponseData(const Request& r, ByteView in,
                                 MutableByteView payloadOut) noexcept;

/// One entry of a BT/JT test command: a bit device and its ON/OFF state.
struct A1cTestBit {
    Device device;
    bool on;
};

/// One entry of a WT/QT test command: a word device and the word to write to it.
struct A1cTestWord {
    Device device;
    uint16_t value;
};

/// Wire size of BT/JT request data for `n` entries (spec §4.3): command + message wait + n (2
/// characters) + n x [device + `'0'`/`'1'` (1 character)].
size_t a1cTestBitsRequestDataSize(const A1cTestBit* items, size_t n,
                                  C1CommandSet commandSet) noexcept;

/// Wire size of WT/QT request data for `n` entries: command + message wait + n (2 characters) + n x
/// [device + 4 characters].
size_t a1cTestWordsRequestDataSize(const A1cTestWord* items, size_t n,
                                   C1CommandSet commandSet) noexcept;

/**
 * @brief Encodes BT/JT (test, random write of bits) request data (spec §4.3) into `out`.
 * @param[in] items The `n` entries, in wire order.
 * @param[in] n Number of entries; at most 255 (the `n` field is 2 characters).
 * @param[in] commandSet ACPU (`BT`) or AnA/AnU (`JT`).
 * @param[in] messageWait Message wait, 0-15.
 * @param[out] out Destination; must hold at least `a1cTestBitsRequestDataSize()` characters.
 * @return Characters written.
 * @retval ErrorCode::InvalidConfig `messageWait` is above 15.
 * @retval ErrorCode::PointCount `n` is above 255.
 * @retval ErrorCode::InvalidDevice an entry's device type has no 1C code.
 * @retval ErrorCode::BufferTooSmall `out` is too small.
 * @par Complexity
 * O(n); no allocation.
 */
Expected<size_t> a1cTestBitsRequestData(const A1cTestBit* items, size_t n, C1CommandSet commandSet,
                                        uint8_t messageWait, MutableByteView out) noexcept;

/**
 * @brief Encodes WT/QT (test, random write of words) request data (spec §4.3) into `out`.
 * @param[in] items The `n` entries, in wire order.
 * @param[in] n Number of entries; at most 255.
 * @param[in] commandSet ACPU (`WT`) or AnA/AnU (`QT`).
 * @param[in] messageWait Message wait, 0-15.
 * @param[out] out Destination; must hold at least `a1cTestWordsRequestDataSize()` characters.
 * @return Characters written.
 * @retval ErrorCode::InvalidConfig `messageWait` is above 15.
 * @retval ErrorCode::PointCount `n` is above 255.
 * @retval ErrorCode::InvalidDevice an entry's device type has no 1C code.
 * @retval ErrorCode::BufferTooSmall `out` is too small.
 * @par Complexity
 * O(n); no allocation.
 */
Expected<size_t> a1cTestWordsRequestData(const A1cTestWord* items, size_t n,
                                         C1CommandSet commandSet, uint8_t messageWait,
                                         MutableByteView out) noexcept;

} // namespace mc::detail
