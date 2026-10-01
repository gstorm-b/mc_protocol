/**
 * @file frame_serial.h
 * @brief 3C and 1C envelopes: request encoding and response parsing for the serial formats (spec
 * `mc-protocol-frame-spec.md` §5.5 and §5.6, with the format table and checks of §5.4 and the sum
 * check ranges of §2.5). Wraps command_qna.h's request/response data (3C: frame ID `"F9"` and
 * access route `P` = station, network, PC, self-station) or command_a1c.h's (1C: no frame ID, `P` =
 * station, PC) with the format's control codes, the block number (format 2), the SUM and, for
 * format 4, CR LF.
 *
 * Finding where a response frame starts and ends, its SUM and its terminator is serial_parser.h's
 * job; this file judges what the parts say (frame ID, access route, data size) and maps it to the
 * error table of spec §7.2.
 *
 * Serial frames are always ASCII, so nothing here is templated on a codec. Every function here has
 * the precondition that `cfg.frame` is `F3C` or `F1C` and `frameSerialFormatSupported(cfg.format)`.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "command_qna.h"
#include "field_codec.h"
#include "serial_parser.h"

#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// The serial formats this envelope implements (1-4, both 3C and 1C); protocol.cpp reports format
/// 5, which `FrameConfig::validate()` rejects in v1, as not implemented.
constexpr bool frameSerialFormatSupported(SerialFormat format) noexcept {
    return format >= SerialFormat::Format1 && format <= SerialFormat::Format4;
}

/**
 * @brief Wire size of the complete 3C or 1C request frame for `r` (spec §5.4 table, request
 * column): the start byte (ENQ, or STX in format 3), the block number (format 2), `P`, the request
 * data (0401/1401 in 3C, BR/WR/BW/WW or JR/QR/JW/QW in 1C), ETX (format 3), the SUM when
 * `cfg.sumCheck`, and CR LF in format 4.
 * @param[in] r Request being encoded.
 * @param[in] cfg Frame configuration (`format`, `sumCheck`, `series`, `commandSet`).
 * @pre `frameSerialFormatSupported(cfg.format)`.
 * @return The size in bytes.
 * @par Complexity
 * O(1); no allocation.
 */
size_t frameSerialEncodedSize(const Request& r, const FrameConfig& cfg) noexcept;

/**
 * @brief Encodes `r` as a complete 3C or 1C request frame into `out`.
 * @param[in] r Request being encoded.
 * @param[in] cfg Frame configuration (access route, `format`, `sumCheck`, `series`, and for 1C
 * `commandSet` and `messageWait`).
 * @param[out] out Destination; must hold at least `frameSerialEncodedSize(r, cfg)` bytes.
 * @return Bytes written.
 * @retval ErrorCode::InvalidDevice `r.head.type` has no code for this family (3C: `cfg.series`).
 * @retval ErrorCode::InvalidConfig 1C only: `cfg.messageWait` is above 15.
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `frameSerialEncodedSize(r, cfg)`.
 * @par Complexity
 * O(r.count); no allocation.
 * @see frameSerialTryParse
 */
Expected<size_t> frameSerialEncode(const Request& r, const FrameConfig& cfg,
                                   MutableByteView out) noexcept;

/**
 * @brief Upper bound of the wire size of any successful or error response to `r`, junk before
 * the frame excluded: the larger of the expected response (data frame for a read, ACK for a
 * write) and the NAK frame.
 * @param[in] r Request whose response is being sized.
 * @param[in] cfg Frame configuration.
 * @return The bound in bytes.
 * @par Complexity
 * O(1); no allocation.
 */
size_t frameSerialMaxResponseSize(const Request& r, const FrameConfig& cfg) noexcept;

/**
 * @brief Advances the parse of one 3C or 1C response to `r` over the new bytes of `buffer`:
 * serialFeed() finds the frame, then the frame ID (3C only), the access route (when
 * `cfg.checkRoute`), the block number (format 2, when `cfg.checkBlockNo`) and the data size are
 * checked and a NAK becomes a PLC error (spec §5.4 "Checks when parsing", §7.2).
 *
 * A read needs a response with data of exactly the command layer's response data size; a write
 * needs an ACK (format 3: `QACK`/`GG` without data); a NAK (format 3: `QNAK`/`NN`) answers either.
 * The PLC error code is 4 characters in 3C and 2 in 1C.
 *
 * @param[in] buffer Every byte received so far, from the first byte examined.
 * @param[in] r Request this response answers.
 * @param[in] cfg Frame configuration this response is checked against.
 * @param[in,out] cursor Progress of the scan; value-initialized for a new response.
 * @param[out] frameLength Set when the return value is not `NeedMore`: bytes of `buffer` the frame
 * occupies, skipped junk included.
 * @param[out] error Set when the return value is `Failed`.
 * @return `NeedMore`, `Done` or `Failed` (`FrameMismatch`, `LengthMismatch`, `SumCheck`,
 * `InvalidCharacter` or a `Plc` error, spec §7.2).
 * @par Complexity
 * O(k) in the bytes newly examined; no allocation.
 * @see frameSerialPayload, frameSerialEncode
 */
ParseStatus frameSerialTryParse(ByteView buffer, const Request& r, const FrameConfig& cfg,
                                SerialCursor& cursor, size_t& frameLength, Error& error) noexcept;

/**
 * @brief Decodes the response data of a frame that parsed as `Done` into `out`, in the
 * normalized layout of protocol.h's "Payload contract".
 * @param[in] buffer The same bytes that produced `Done`.
 * @param[in] skipped Junk bytes before the frame, as counted by the parse.
 * @param[in] r Request the response answers.
 * @param[in] cfg Frame configuration.
 * @param[out] out Destination; must hold at least `payloadSize(r)` bytes.
 * @return Bytes written.
 * @retval ErrorCode::InvalidCharacter a response data character is not valid for the request.
 * @par Complexity
 * O(r.count); no allocation.
 */
Expected<size_t> frameSerialPayload(ByteView buffer, size_t skipped, const Request& r,
                                    const FrameConfig& cfg, MutableByteView out) noexcept;

} // namespace mc::detail
