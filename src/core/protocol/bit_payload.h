/**
 * @file bit_payload.h
 * @brief Bit data of a request or response in the normalized payload layouts (protocol.h's
 * "Payload contract") against the wire form of a `Codec`: `encodeBits()` / `decodeBits()`, shared
 * by the QnA (command_qna.h) and 1E (command_a1e.h) command layers so the chunked packing logic
 * exists once.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "field_codec.h"

#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace mc::detail {

// Encodes r.data (write payload, r.bitLayout) as `count` bits of wire data into `out`, in chunks
// of up to 8 points: field_codec.h's Codec::putBits() always treats index 0 of a call as the
// high nibble of its first byte (Binary) / its first character (Ascii), so a chunk boundary must
// itself be nibble-aligned. Every chunk here is a full 8 points except possibly the last, so
// `pointsDone` is always even where it matters (Binary); Ascii has no alignment constraint at
// all. No allocation: `chunk` is a fixed 8-byte stack array, not sized by `count`.
template <class Codec>
Expected<size_t> encodeBits(const Request& r, MutableByteView out) noexcept {
    size_t count = r.count;
    if (r.bitLayout == BitLayout::BytePerPoint) {
        return Codec::putBits(r.data, out);
    }

    size_t pointsDone = 0;
    while (pointsDone < count) {
        size_t chunkCount = std::min<size_t>(8, count - pointsDone);
        uint8_t chunk[8] = {};
        uint8_t packedByte = r.data.data[pointsDone / 8];
        for (size_t b = 0; b < chunkCount; ++b) {
            chunk[b] = static_cast<uint8_t>((packedByte >> b) & 1u);
        }
        size_t offset = Codec::bitsSize(pointsDone);
        auto putResult = Codec::putBits(ByteView{chunk, chunkCount},
                                         MutableByteView{out.data + offset, out.size - offset});
        if (!putResult.hasValue()) {
            return putResult;
        }
        pointsDone += chunkCount;
    }
    return Expected<size_t>(Codec::bitsSize(count));
}

// Inverse of encodeBits(): decodes `count` bits of wire data `in` into payloadOut, in r.bitLayout
// (same chunking rationale as encodeBits()). Reads only the first `count` points of `in`; any
// padding after them (a 1E ASCII dummy character, a zero last nibble) is not inspected.
template <class Codec>
Expected<size_t> decodeBits(const Request& r, ByteView in, MutableByteView payloadOut) noexcept {
    size_t count = r.count;
    if (r.bitLayout == BitLayout::BytePerPoint) {
        return Codec::getBits(in, count, payloadOut);
    }

    size_t packedSize = (count + 7) / 8;
    for (size_t i = 0; i < packedSize; ++i) {
        payloadOut.data[i] = 0;
    }
    size_t pointsDone = 0;
    while (pointsDone < count) {
        size_t chunkCount = std::min<size_t>(8, count - pointsDone);
        uint8_t chunk[8] = {};
        size_t offset = Codec::bitsSize(pointsDone);
        auto getResult = Codec::getBits(ByteView{in.data + offset, in.size - offset}, chunkCount,
                                         MutableByteView{chunk, chunkCount});
        if (!getResult.hasValue()) {
            return getResult;
        }
        for (size_t b = 0; b < chunkCount; ++b) {
            if (chunk[b] != 0) {
                payloadOut.data[pointsDone / 8] |= static_cast<uint8_t>(1u << b);
            }
        }
        pointsDone += chunkCount;
    }
    return Expected<size_t>(packedSize);
}

} // namespace mc::detail
