/**
 * @file request.h
 * @brief A read or write of consecutive points, its builders, and validate() (spec §3.4 item 4,
 * §3.5, §4.4 field maxima).
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstdint>

namespace mc {

/**
 * @enum Op
 * @brief Which operation a Request performs.
 */
enum class Op : uint8_t {
    ReadBits,  ///< Read `count` bits starting at `head`.
    ReadWords, ///< Read `count` words starting at `head`.
    WriteBits, ///< Write `count` bits starting at `head`.
    WriteWords ///< Write `count` words starting at `head`.
    // v1.1: ReadRandom, WriteRandomBits, WriteRandomWords.
};

/**
 * @enum BitLayout
 * @brief Layout of bit data in a normalized payload (capability-map assumption 5).
 */
enum class BitLayout : uint8_t {
    BytePerPoint,  ///< One byte per point, value 0 or 1, device order. Default.
    PackedLsbFirst ///< Eight points per byte, point (head+i) at bit (i % 8) of byte (i / 8).
};

/**
 * @struct Request
 * @brief A read or write of `count` consecutive points starting at `head`.
 *
 * Non-owning: `data` must outlive the call it is passed to (encode); `Session::submit`
 * (core-session) copies it.
 *
 * @see validate, Op, BitLayout
 */
struct Request {
    Op op{Op::ReadWords};        ///< Which operation this request performs.
    Device head{};                ///< First point to read or write.
    uint16_t count{0}; ///< Points: bits for `*Bits` ops, words for `*Words` ops.
    ByteView data{};    ///< Write payload in normalized form; empty for reads.
    BitLayout bitLayout{BitLayout::BytePerPoint}; ///< Applies to bit data in `data` and in the
                                                   ///< response.

    /**
     * @brief Builds a bit-read request.
     * @param[in] head First bit device to read.
     * @param[in] count Number of bits to read.
     * @return The built request.
     * @par Complexity
     * O(1); no allocation.
     * @see validate
     */
    static Request readBits(Device head, uint16_t count) noexcept;

    /**
     * @brief Builds a word-read request.
     * @param[in] head First word device to read.
     * @param[in] count Number of words to read.
     * @return The built request.
     * @par Complexity
     * O(1); no allocation.
     * @see validate
     */
    static Request readWords(Device head, uint16_t count) noexcept;

    /**
     * @brief Builds a bit-write request.
     *
     * `count` is derived from `data` and `layout`: for `BytePerPoint`, one point per byte
     * (`count == data.size`); for `PackedLsbFirst`, eight points per byte
     * (`count == data.size * 8`).
     *
     * @param[in] head First bit device to write.
     * @param[in] data Write payload; must outlive the call it is passed to.
     * @param[in] layout Layout `data` is packed in.
     * @return The built request.
     * @par Complexity
     * O(1); no allocation.
     * @see validate
     */
    static Request writeBits(Device head, ByteView data,
                              BitLayout layout = BitLayout::BytePerPoint) noexcept;

    /**
     * @brief Builds a word-write request.
     * @param[in] head First word device to write.
     * @param[in] data Write payload, two bytes per word; must outlive the call it is passed to.
     * `count` is derived as `data.size / 2`.
     * @return The built request.
     * @par Complexity
     * O(1); no allocation.
     * @see validate
     */
    static Request writeWords(Device head, ByteView data) noexcept;

    /**
     * @brief Whether this request writes rather than reads.
     * @return true for `WriteBits` and `WriteWords`.
     * @par Complexity
     * O(1); no allocation.
     */
    bool isWrite() const noexcept;

    /**
     * @brief Whether this request operates on bits rather than words.
     * @return true for `ReadBits` and `WriteBits`.
     * @par Complexity
     * O(1); no allocation.
     */
    bool isBitOp() const noexcept;
};

/**
 * @brief Checks everything about @p r that can be decided from data alone, before encoding.
 *
 * Applies the following rules, in order; the first failure wins:
 * -# `count >= 1`.
 * -# A bit operation (`isBitOp()`) on a non-bit device.
 * -# `head.type` unsupported by `cfg`'s frame family, honouring `cfg.e1AliasLS` for L/S on 1E.
 * -# `head.number` exceeds the frame family's field width (spec §3.4 item 4). An ASCII field
 *    is counted in the digits actually written: X and Y take octal digits when
 *    `cfg.xyAsciiDigits` is `XyNumbering::Octal`.
 * -# A word operation on a bit device with `head.number` not a multiple of 16, when `cfg.frame`
 *    is 1E or 1C or `cfg.aSeriesTarget`; in M9000-M9255 the rule is `head.number == 9000 + 16k`
 *    (spec §10 Q8), which replaces the plain multiple-of-16 rule in that range.
 * -# `count` exceeds the frame family's field maximum (256 for 1E/1C; QnA's u16 field never
 *    exceeds `count`'s own type).
 * -# For a write, `data.size` does not match `count` and `bitLayout`.
 *
 * @param[in] r Request to check.
 * @param[in] cfg Frame this request would be encoded for.
 * @return Success when every rule above passes.
 * @retval ErrorCode::PointCount `count == 0`, or `count` exceeds the frame family's field
 * maximum.
 * @retval ErrorCode::InvalidDevice A bit operation on a non-bit device, an unsupported device for
 * this frame family, a device number outside the field width, or misaligned word access to a
 * bit device.
 * @retval ErrorCode::DataSizeMismatch `data.size` does not match `count` and `bitLayout` for a
 * write.
 * @par Complexity
 * O(1); no allocation.
 * @see Request, FrameConfig
 */
Expected<void> validate(const Request& r, const FrameConfig& cfg) noexcept;

} // namespace mc
