/**
 * @file hexascii.h
 * @brief ASCII hex encode/decode (spec `mc-protocol-frame-spec.md` §2.1): the shared primitive
 * every `AsciiCodec` numeric field (field_codec.h) and the sum check (sumcheck.h) build on.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>

namespace mc::detail {

/// Chars needed to hex-encode `byteCount` bytes: exactly 2 per byte.
constexpr size_t hexEncodedSize(size_t byteCount) noexcept { return byteCount * 2; }

/**
 * @brief Encodes every byte of `bytes`, in order, as two upper-case ASCII hex characters (spec
 * §2.1: "the encoder MUST use uppercase"), high nibble first.
 * @param[in] bytes Bytes to encode.
 * @param[out] out Destination; must hold at least `hexEncodedSize(bytes.size)` characters.
 * @return Chars written.
 * @retval ErrorCode::BufferTooSmall `out.size < hexEncodedSize(bytes.size)`; nothing is written.
 * @par Complexity
 * O(n) in `bytes.size`; no allocation.
 */
Expected<size_t> hexEncode(ByteView bytes, MutableByteView out) noexcept;

/**
 * @brief Decodes `text`, in order, into `out`. Upper or lower case digits are both accepted
 * (spec §2.1: "the decoder SHOULD also accept lowercase"); the encoder side (hexEncode) always
 * produces upper case.
 * @param[in] text An even number of ASCII hex characters.
 * @param[out] out Destination; must hold at least `text.size / 2` bytes.
 * @return Bytes written.
 * @retval ErrorCode::InvalidCharacter `text.size` is odd, or a character is not `[0-9A-Fa-f]`;
 * nothing is written.
 * @retval ErrorCode::BufferTooSmall `out.size < text.size / 2`; nothing is written.
 * @par Complexity
 * O(n) in `text.size`; no allocation.
 */
Expected<size_t> hexDecode(ByteView text, MutableByteView out) noexcept;

} // namespace mc::detail
