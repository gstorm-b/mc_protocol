/**
 * @file limits.h
 * @brief Point limits per command (spec `mc-protocol-frame-spec.md` §4.4) and request splitting
 * into chunks that fit them (spec §8.5).
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstddef>
#include <cstdint>

namespace mc {

/**
 * @brief Maximum points a single command may carry for this frame, code, operation, device kind,
 * series and target family.
 *
 * The constexpr transcription of spec §4.4 (v1 subset: `Op::ReadBits`, `Op::ReadWords`,
 * `Op::WriteBits`, `Op::WriteWords` only; the random-access rows of the reference table are kept
 * as data in `src/core/model/limits_table.cpp` for v1.1, unreachable through this function until
 * `Op` grows the random-access values). The 1E column is transcribed from PDF Appendix 5 and has
 * not gone through the reference document's own verification process (spec §10.1 Q7); the QnA and
 * 1C columns are cross-checked against the command pages there and are not flagged.
 *
 * For a word operation on a bit device, the returned maximum is in **words**, matching
 * `Request::count`'s own unit for that combination (spec §2.4: each word covers 16 bit points).
 *
 * A nonzero per-target cap of the operation (`FrameConfig::maxReadWords`, `maxWriteWords`,
 * `maxReadBits`, `maxWriteBits`) lowers the table value to `min(table, cap)`; a cap of 0, or one
 * above the table value, changes nothing.
 *
 * @param[in] cfg Frame this command would be encoded for; reads `frame`, `code`, `series`,
 * `targetFamily` and the four per-target caps.
 * @param[in] op Operation the command performs.
 * @param[in] kind Device kind (`DeviceKind::Bit` or `DeviceKind::Word`) the command targets.
 * @return The maximum, or 0 when spec §4.4 has no cell for this combination.
 * @par Complexity
 * O(1); no allocation.
 * @see chunkCount, chunk
 */
uint16_t maxPoints(const FrameConfig& cfg, Op op, DeviceKind kind) noexcept;

/**
 * @struct Chunk
 * @brief One command's share of a larger Request, as chunk() would split it (spec §8.5).
 *
 * Value type, trivially copyable.
 *
 * @see chunk, chunkCount
 */
struct Chunk {
    uint32_t headNumber; ///< Device number this chunk starts at; the device type is the
                          ///< originating Request's own `head.type`.
    uint16_t count;       ///< Points this chunk carries.
    uint32_t dataOffset;  ///< Byte offset into the originating Request's `data` for a write chunk;
                          ///< 0 for a read chunk.
};

/**
 * @brief The number of chunks chunk() would produce for @p r under @p cfg, without writing them.
 *
 * @param[in] r Request that would be split.
 * @param[in] cfg Frame @p r would be encoded for.
 * @return The chunk count chunk() would produce.
 * @retval ErrorCode::InvalidDevice Any failure of `validate(r, cfg)` (request.h) except its field
 * maximum rule 6, which does not apply here: splitting is what brings a 1E or 1C request above
 * 256 units within it.
 * @retval ErrorCode::DataSizeMismatch @p r is a write whose payload size does not match its
 * count.
 * @retval ErrorCode::PointCount @p r has no points, no spec §4.4 cell applies to this
 * operation/device kind combination, @p r is a write that needs more than one chunk and
 * `cfg.splitWrites` is false, or @p r is a bit operation with `BitLayout::PackedLsbFirst` that
 * needs more than one chunk while maxPoints() is not a multiple of 8 (a per-target cap; a chunk
 * could not start on a byte boundary).
 * @par Complexity
 * O(1); no allocation.
 * @see chunk
 */
Expected<size_t> chunkCount(const Request& r, const FrameConfig& cfg) noexcept;

/**
 * @brief Splits @p r into chunks that each fit within maxPoints() (spec §8.5).
 *
 * Every chunk but the last carries exactly maxPoints() points; the head number steps by 16 per
 * point of the chunk size for a word operation on a bit device (spec §2.4), by 1 otherwise. A
 * write that needs more than one chunk is rejected with `ErrorCode::PointCount` unless
 * `cfg.splitWrites` is set, since splitting a write is not atomic (part of the data may already be
 * written when a later chunk's command fails).
 *
 * @param[in] r Request to split.
 * @param[in] cfg Frame @p r would be encoded for.
 * @param[out] out Destination array for the chunks; unwritten when this returns an Error.
 * @param[in] capacity Number of Chunk slots available at @p out.
 * @return The number of chunks written to @p out; always equal to chunkCount(r, cfg)'s value on
 * success.
 * @retval ErrorCode::InvalidDevice Any failure of `validate(r, cfg)` (request.h) except its field
 * maximum rule 6, which does not apply here: splitting is what brings a 1E or 1C request above
 * 256 units within it.
 * @retval ErrorCode::DataSizeMismatch @p r is a write whose payload size does not match its
 * count.
 * @retval ErrorCode::PointCount @p r has no points, no spec §4.4 cell applies to this
 * operation/device kind combination, @p r is a write that needs more than one chunk and
 * `cfg.splitWrites` is false, or @p r is a bit operation with `BitLayout::PackedLsbFirst` that
 * needs more than one chunk while maxPoints() is not a multiple of 8 (a per-target cap; a chunk
 * could not start on a byte boundary).
 * @retval ErrorCode::BufferTooSmall `capacity` is smaller than the number of chunks needed.
 * @par Complexity
 * O(C) in the number of chunks produced; no allocation.
 * @see chunkCount, maxPoints
 */
Expected<size_t> chunk(const Request& r, const FrameConfig& cfg, Chunk* out,
                        size_t capacity) noexcept;

} // namespace mc
