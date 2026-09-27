/**
 * @file frame_3e.h
 * @brief 3E envelope: request encoding and response parsing (spec
 * `mc-protocol-frame-spec.md` §5.1). Wraps command_qna.h's request/response data with the 3E
 * header (subheader, access route, request/response data length) and, on the response side, the
 * end code and error information (spec §7.2).
 *
 * Every function is templated on `Codec` (field_codec.h's `AsciiCodec`/`BinaryCodec`);
 * frame_3e.cpp explicitly instantiates both. protocol.cpp is the only caller, and it dispatches
 * on `FrameConfig::code` to pick the right one, so a genuinely un-instantiated `Codec` here would
 * be a link error, not a runtime one -- a second layer against ever silently running unproven
 * code, still true for whichever frame family/code this module has not implemented yet.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "command_qna.h"
#include "field_codec.h"

#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// Fixed length of 3E header fields 1-6 (subheader, network, PC, I/O, station, length; spec
/// §5.1): 3 `u16`-sized fields (subheader, I/O, length) + 3 `u8`-sized fields (network, PC,
/// station). 9 bytes Binary, 18 characters ASCII.
template <class Codec> constexpr size_t frame3eHeaderSize() noexcept {
    return Codec::u16Size() * 3 + Codec::u8Size() * 3;
}

/// Wire size of 3E error information (spec §5.1 field 8b): access route (3 `u8`-sized fields +
/// 1 `u16`-sized field, same shape as the header's own route) + command (`u16`) + subcommand
/// (`u16`). 9 bytes Binary, 18 characters ASCII -- numerically the same as frame3eHeaderSize(),
/// kept as its own name because the field grouping is different (no length field; command and
/// subcommand instead).
template <class Codec> constexpr size_t frame3eErrorInfoSize() noexcept {
    return Codec::u8Size() * 3 + Codec::u16Size() * 3;
}

/// Wire size of the complete 3E request frame for `r` (spec §5.1): header + monitoring timer
/// (`u16`) + 0401/1401 request data (command_qna.h).
template <class Codec> size_t frame3eEncodedSize(const Request& r, PlcSeries series) noexcept {
    return frame3eHeaderSize<Codec>() + Codec::u16Size() + qnaRequestDataSize<Codec>(r, series);
}

/// Upper bound of the wire size of any successful or error response to `r` (spec §5.1): header +
/// end code (`u16`) + the larger of the response's own wire data size (command_qna.h) and the
/// error-information size (a PLC error response never carries both at once).
template <class Codec> size_t frame3eMaxResponseSize(const Request& r) noexcept {
    size_t dataMax = qnaResponseDataSize<Codec>(r);
    size_t infoSize = frame3eErrorInfoSize<Codec>();
    size_t lMax = Codec::u16Size() + (dataMax > infoSize ? dataMax : infoSize);
    return frame3eHeaderSize<Codec>() + lMax;
}

/**
 * @brief Encodes `r` as a complete 3E request frame (spec §5.1) into `out`.
 * @param[in] r Request being encoded.
 * @param[in] cfg Frame configuration (subheader/route defaults, `series`, `monitoringTimer`).
 * @param[out] out Destination; must hold at least `frame3eEncodedSize<Codec>(r, cfg.series)`
 * bytes.
 * @return Bytes/characters written.
 * @retval ErrorCode::InvalidDevice `r.head.type` has no QnA code for `cfg.series`
 * (device_encode.h's `qnaDevice()`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than
 * `frame3eEncodedSize<Codec>(r, cfg.series)`.
 * @par Complexity
 * O(r.count); no allocation.
 * @see frame3eTryParse
 */
template <class Codec>
Expected<size_t> frame3eEncode(const Request& r, const FrameConfig& cfg,
                                MutableByteView out) noexcept;

/**
 * @brief Tries to parse one 3E response frame out of `buffer` (spec §5.1's parse algorithm) for
 * the request `r` was encoded from, under `cfg` (route check, expected route bytes).
 *
 * Stateless and re-entrant: examines only whether `buffer` already holds enough bytes for each
 * step in turn (header, then the response data length it declares), so calling this again with a
 * longer `buffer` resumes correctly with no state of its own to carry across calls -- Parser
 * (protocol.h) is what remembers `ParseStatus::Done`/`Failed` once reached, so it does not call
 * this again for the same frame.
 *
 * @param[in] buffer Bytes received so far for this response.
 * @param[in] r Request this response answers.
 * @param[in] cfg Frame configuration this response is checked against (`checkRoute`, route
 * bytes).
 * @param[out] frameLength Set when the return value is not `NeedMore`: bytes of `buffer` this
 * frame occupies (a malformed frame whose own length field was still readable), or the bytes
 * examined before giving up (header size, when even the subheader/route did not match).
 * @param[out] error Set when the return value is `Failed`.
 * @return `NeedMore`, `Done`, or `Failed` (spec §5.1, §7.2 error mapping).
 * @par Complexity
 * O(1); no allocation.
 * @see frame3eEncode
 */
template <class Codec>
ParseStatus frame3eTryParse(ByteView buffer, const Request& r, const FrameConfig& cfg,
                             size_t& frameLength, Error& error) noexcept;

} // namespace mc::detail
