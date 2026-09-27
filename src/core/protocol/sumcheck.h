/**
 * @file sumcheck.h
 * @brief 8-bit sum check, encoded as 2 ASCII hex characters (spec `mc-protocol-frame-spec.md`
 * §2.5, E6).
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/**
 * @brief The 8-bit sum check of `data` (spec §2.5: "add all bytes in the sum check range, keep
 * the low 8 bits").
 * @param[in] data Bytes in the frame's sum check range (spec §2.5's per-format table decides
 * which bytes that is; this function just sums what it is given).
 * @return The low 8 bits of the sum.
 * @par Complexity
 * O(n) in `data.size`; no allocation.
 */
uint8_t sumcheck(ByteView data) noexcept;

/**
 * @brief Encodes `sumcheck(data)` as 2 upper-case ASCII hex characters, most significant digit
 * first (spec E6: "always 2 ASCII hex characters ... even in binary Format 5").
 * @param[in] data Same as sumcheck().
 * @param[out] out Destination; must hold at least 2 characters.
 * @return Chars written (always 2 on success).
 * @retval ErrorCode::BufferTooSmall `out.size < 2`; nothing is written.
 * @par Complexity
 * O(n) in `data.size`; no allocation.
 */
Expected<size_t> sumcheckEncode(ByteView data, MutableByteView out) noexcept;

} // namespace mc::detail
