/**
 * @file frame_1e.h
 * @brief 1E envelope: request encoding and response parsing (spec `mc-protocol-frame-spec.md`
 * §5.3). Wraps command_a1e.h's request/response data with the 1E subheader (the command code),
 * PC No. and monitoring timer on the request side, and the response subheader, end code and
 * abnormal code on the response side (spec §7.2).
 *
 * 1E has no length field, so a response is delimited by what the request asked for: the parse is
 * driven by the end code (spec §5.3 "Parse algorithm"). Every function is templated on `Codec`
 * (field_codec.h); frame_1e.cpp explicitly instantiates both, and protocol.cpp dispatches on
 * `FrameConfig::code`.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "command_a1e.h"
#include "field_codec.h"

#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// Wire size of the 1E request head (spec §5.3 fields 1-3): subheader (`u8`) + PC No. (`u8`) +
/// monitoring timer (`u16`). 4 bytes Binary, 8 characters ASCII.
template <class Codec> constexpr size_t frame1eRequestHeadSize() noexcept {
    return Codec::u8Size() * 2 + Codec::u16Size();
}

/// Wire size of the 1E response head (spec §5.3 fields 1-2): subheader (`u8`) + end code (`u8`).
/// 2 bytes Binary, 4 characters ASCII.
template <class Codec> constexpr size_t frame1eResponseHeadSize() noexcept {
    return Codec::u8Size() * 2;
}

/// Wire size of the complete 1E request frame for `r` (spec §5.3): head + 00H-03H request data.
template <class Codec> size_t frame1eEncodedSize(const Request& r) noexcept {
    return frame1eRequestHeadSize<Codec>() + a1eRequestDataSize<Codec>(r);
}

/// Upper bound of the wire size of any successful or error response to `r` (spec §5.3): the head
/// plus the larger of the response data and the abnormal code (`u8`, only after end code 5BH).
template <class Codec> size_t frame1eMaxResponseSize(const Request& r) noexcept {
    size_t dataMax = a1eResponseDataSize<Codec>(r);
    size_t abnormal = Codec::u8Size();
    return frame1eResponseHeadSize<Codec>() + (dataMax > abnormal ? dataMax : abnormal);
}

/**
 * @brief Encodes `r` as a complete 1E request frame (spec §5.3) into `out`.
 * @param[in] r Request being encoded.
 * @param[in] cfg Frame configuration (`pc`, `monitoringTimer`).
 * @param[out] out Destination; must hold at least `frame1eEncodedSize<Codec>(r)` bytes.
 * @return Bytes/characters written.
 * @retval ErrorCode::InvalidDevice `r.head.type` has no 1E code (device_encode.h's `e1Device()`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `frame1eEncodedSize<Codec>(r)`.
 * @par Complexity
 * O(r.count); no allocation.
 * @see frame1eTryParse
 */
template <class Codec>
Expected<size_t> frame1eEncode(const Request& r, const FrameConfig& cfg,
                                MutableByteView out) noexcept;

/**
 * @brief Tries to parse one 1E response frame out of `buffer` (spec §5.3's parse algorithm) for
 * the request `r` was encoded from.
 *
 * Stateless and re-entrant, like frame3eTryParse(): it only asks whether `buffer` already holds
 * what each step needs, so a later call with a longer `buffer` resumes correctly.
 *
 * @param[in] buffer Bytes received so far for this response.
 * @param[in] r Request this response answers (its command code and size fix the expected
 * subheader and data length).
 * @param[out] frameLength Set when the return value is not `NeedMore`: bytes of `buffer` this
 * frame occupies (2/4 for a bad subheader or an end code other than 00H/5BH, 3/6 for 5BH, the
 * computed response size for a normal end, all in bytes/characters).
 * @param[out] error Set when the return value is `Failed`.
 * @return `NeedMore`, `Done`, or `Failed` (spec §5.3, §7.2 error mapping).
 * @par Complexity
 * O(1); no allocation.
 * @see frame1eEncode
 */
template <class Codec>
ParseStatus frame1eTryParse(ByteView buffer, const Request& r, size_t& frameLength,
                             Error& error) noexcept;

} // namespace mc::detail
