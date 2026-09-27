/**
 * @file command_qna.h
 * @brief QnA batch read/write commands 0401 (read) / 1401 (write) (spec
 * `mc-protocol-frame-spec.md` §4.1.1): request data and response-data decoding, written once
 * against `Codec` (field_codec.h's `AsciiCodec`/`BinaryCodec`) and shared by 3E and 3C (module
 * spec "Internal design": "one QnA command implementation used by 3E and 3C through the field
 * codec; no ASCII/Binary branches inside command code except device encoding").
 *
 * "Request data" / "response data" here are the command layer's own bytes (spec §4, "Command
 * layer"): the part of a frame between its envelope (subheader/route/length, spec §5) and its
 * end. `qnaRequestData()` is frame-agnostic; whichever frame envelope wraps it (3E, spec §5.1;
 * 3C, spec §5.5) adds nothing this file needs to know about.
 *
 * Every function takes `Codec` explicitly (`AsciiCodec` or `BinaryCodec`) rather than a runtime
 * `DataCode`, per the module spec's own "Internal design" rule quoted above; `Codec::kDataCode`
 * (field_codec.h) is what lets this file still call `qnaDevice()` (device_encode.h), which does
 * take a runtime `DataCode` (device encoding is the one place that branch is allowed).
 *
 * The random-access commands 0403 (read) / 1402 (write) share this command family (spec §4.1.2)
 * but are not reachable through `Op` in v1 (module spec "Commands", decision 2); their CMD-11..14
 * golden vectors are transcribed (tagged `v1.1`) but nothing here encodes or decodes them yet.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "device_encode.h"
#include "field_codec.h"

#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// QnA subcommand (spec §4.1.1): Q/L `0000` (word)/`0001` (bit); iQ-R `0002` (word)/`0003` (bit).
constexpr uint16_t qnaSubcommand(bool isBitOp, PlcSeries series) noexcept {
    uint16_t base = series == PlcSeries::QL ? 0x0000u : 0x0002u;
    return isBitOp ? static_cast<uint16_t>(base + 1u) : base;
}

/// QnA command code (spec §4.1.1): `0401` (read) or `1401` (write).
constexpr uint16_t qnaCommandCode(bool isWrite) noexcept { return isWrite ? 0x1401u : 0x0401u; }

/**
 * @brief Wire size of `r`'s 0401/1401 request data (spec §4.1.1): command(u16) +
 * subcommand(u16) + device + points(u16) + write data (writes only).
 * @param[in] r Request being encoded.
 * @param[in] series Q/L or iQ-R device-code column (`FrameConfig::series`).
 * @par Complexity
 * O(1); no allocation.
 */
template <class Codec> size_t qnaRequestDataSize(const Request& r, PlcSeries series) noexcept {
    size_t size = Codec::u16Size() * 3 + qnaDeviceSize(Codec::kDataCode, series);
    if (r.isWrite()) {
        size += r.isBitOp() ? Codec::bitsSize(r.count) : Codec::wordsSize(r.count);
    }
    return size;
}

/**
 * @brief Encodes `r`'s 0401/1401 request data (spec §4.1.1) into `out`.
 * @param[in] r Request being encoded; a read or a write, bits or words.
 * @param[in] series Q/L or iQ-R device-code column.
 * @param[out] out Destination; must hold at least `qnaRequestDataSize<Codec>(r, series)` bytes.
 * @return Bytes/characters written.
 * @retval ErrorCode::InvalidDevice `r.head.type` has no QnA code for `series` (device_encode.h's
 * `qnaDevice()`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `qnaRequestDataSize<Codec>(r, series)`.
 * @par Complexity
 * O(r.count); no allocation.
 * @see qnaResponseData
 */
template <class Codec>
Expected<size_t> qnaRequestData(const Request& r, PlcSeries series, MutableByteView out) noexcept;

/**
 * @brief Wire size of the 0401/1401 response DATA for `r` (spec §4.1.1): 0 for a write; for a
 * read, `Codec::bitsSize(r.count)` or `Codec::wordsSize(r.count)`.
 * @par Complexity
 * O(1); no allocation.
 */
template <class Codec> size_t qnaResponseDataSize(const Request& r) noexcept {
    if (r.isWrite()) {
        return 0;
    }
    return r.isBitOp() ? Codec::bitsSize(r.count) : Codec::wordsSize(r.count);
}

/**
 * @brief Decodes 0401/1401 response data `in` for `r` into `payloadOut`, in the normalized
 * layout of protocol.h's "Payload contract": `ReadBits`/`BytePerPoint` one byte per point;
 * `ReadBits`/`PackedLsbFirst` `ceil(count / 8)` bytes, LSB first; `ReadWords` `2 * count` bytes,
 * little-endian, regardless of device kind (Open Question 2: raw words, no bit conversion here).
 * A write's response carries no data; `payloadOut` is untouched and 0 is returned.
 * @param[in] r Request the response answers.
 * @param[in] in Response data bytes/characters (not the whole frame).
 * @param[out] payloadOut Destination; must hold at least `qnaResponseDataSize<Codec>(r)` bytes
 * for a word read, or the packed/unpacked size `r.bitLayout` implies for a bit read.
 * @return Bytes written to `payloadOut`.
 * @retval ErrorCode::LengthMismatch `in.size` does not equal `qnaResponseDataSize<Codec>(r)`
 * (CMDD-17).
 * @retval ErrorCode::InvalidCharacter an ASCII character is not `[0-9A-Fa-f]`, or a Binary
 * nibble/bit value is neither 0 nor 1 (CMDD-18).
 * @par Complexity
 * O(r.count); no allocation.
 * @see qnaRequestData
 */
template <class Codec>
Expected<size_t> qnaResponseData(const Request& r, ByteView in,
                                  MutableByteView payloadOut) noexcept;

} // namespace mc::detail
