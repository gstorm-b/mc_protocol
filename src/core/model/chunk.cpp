// chunkCount() and chunk() (spec §8.5): splitting a Request into commands that each fit within
// maxPoints(). Both call validate(Request, FrameConfig) (request.h) first, so an already-invalid
// request is rejected the same way whether the caller calls validate() itself or goes straight to
// chunking.
#include "mc/core/limits.h"

namespace mc {
namespace {

Error pointCountError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::PointCount;
    e.message = message;
    return e;
}

Error bufferTooSmallError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::BufferTooSmall;
    e.message = message;
    return e;
}

} // namespace

Expected<size_t> chunkCount(const Request& r, const FrameConfig& cfg) noexcept {
    Expected<void> validation = validate(r, cfg);
    if (!validation.hasValue()) {
        return Expected<size_t>(validation.error());
    }

    const DeviceInfo& info = deviceInfo(r.head.type);
    uint16_t max = maxPoints(cfg, r.op, info.kind);
    if (max == 0) {
        return Expected<size_t>(
            pointCountError("no spec section 4.4 limit for this frame/operation/device kind"));
    }

    if (r.isWrite() && r.count > max && !cfg.splitWrites) {
        return Expected<size_t>(pointCountError(
            "write exceeds the single-command point limit and splitWrites is disabled"));
    }

    size_t n = (static_cast<size_t>(r.count) + max - 1) / max; // ceiling division.
    return Expected<size_t>(n);
}

Expected<size_t> chunk(const Request& r, const FrameConfig& cfg, Chunk* out,
                        size_t capacity) noexcept {
    Expected<size_t> countResult = chunkCount(r, cfg);
    if (!countResult.hasValue()) {
        return Expected<size_t>(countResult.error());
    }
    size_t n = countResult.value();
    if (n > capacity) {
        return Expected<size_t>(
            bufferTooSmallError("capacity is smaller than the number of chunks needed"));
    }

    const DeviceInfo& info = deviceInfo(r.head.type);
    uint16_t max = maxPoints(cfg, r.op, info.kind);
    bool isWordOp = (r.op == Op::ReadWords || r.op == Op::WriteWords);
    // Spec §2.4: bit i of word k is device head + 16k + i, so a word-unit step over a bit device
    // advances the device number by 16 per word; every other combination steps by 1 per point.
    uint32_t stepPerUnit = (isWordOp && info.kind == DeviceKind::Bit) ? 16u : 1u;

    for (size_t i = 0; i < n; ++i) {
        size_t off = i * static_cast<size_t>(max);
        size_t remaining = static_cast<size_t>(r.count) - off;
        uint16_t chunkPoints =
            static_cast<uint16_t>(remaining < static_cast<size_t>(max) ? remaining : max);

        Chunk c{};
        c.headNumber = r.head.number + static_cast<uint32_t>(off * stepPerUnit);
        c.count = chunkPoints;
        if (r.isWrite()) {
            if (r.op == Op::WriteWords) {
                c.dataOffset = static_cast<uint32_t>(off * 2); // two bytes per word.
            } else {
                // WriteBits: BytePerPoint is one byte per point; PackedLsbFirst is eight points
                // per byte. Every v1 WriteBits limit (7904, 3584, 7168, 256, 160) is a multiple
                // of 8, so `off` (always 0 or a multiple of `max`) is always byte-aligned here.
                c.dataOffset =
                    static_cast<uint32_t>(r.bitLayout == BitLayout::PackedLsbFirst ? off / 8 : off);
            }
        } else {
            c.dataOffset = 0;
        }
        out[i] = c;
    }

    return Expected<size_t>(n);
}

} // namespace mc
