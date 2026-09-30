// Request's own member functions (builders, isWrite(), isBitOp()) and validate(Request,
// FrameConfig) (spec §3.4 item 4, §3.5, §4.4 field maxima). request.h has no other .cpp of its
// own (SPEC-core-model.md's Project Structure assigns only this file to it).
#include "mc/core/request.h"

#include "core/model/validate_internal.h"

namespace mc {

Request Request::readBits(Device head, uint16_t count) noexcept {
    Request r{};
    r.op = Op::ReadBits;
    r.head = head;
    r.count = count;
    return r;
}

Request Request::readWords(Device head, uint16_t count) noexcept {
    Request r{};
    r.op = Op::ReadWords;
    r.head = head;
    r.count = count;
    return r;
}

Request Request::writeBits(Device head, ByteView data, BitLayout layout) noexcept {
    Request r{};
    r.op = Op::WriteBits;
    r.head = head;
    r.data = data;
    r.bitLayout = layout;
    // count is derived (request.h): one point per byte, or eight points per byte when packed.
    r.count = static_cast<uint16_t>(layout == BitLayout::PackedLsbFirst ? data.size * 8
                                                                         : data.size);
    return r;
}

Request Request::writeWords(Device head, ByteView data) noexcept {
    Request r{};
    r.op = Op::WriteWords;
    r.head = head;
    r.data = data;
    r.count = static_cast<uint16_t>(data.size / 2);
    return r;
}

bool Request::isWrite() const noexcept { return op == Op::WriteBits || op == Op::WriteWords; }

bool Request::isBitOp() const noexcept { return op == Op::ReadBits || op == Op::WriteBits; }

namespace {

Error invalidDeviceError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::InvalidDevice;
    e.message = message;
    return e;
}

Error pointCountError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::PointCount;
    e.message = message;
    return e;
}

Error dataSizeMismatchError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::DataSizeMismatch;
    e.message = message;
    return e;
}

// The six Timer/Counter symbols (TS, TC, TN, CS, CC, CN) are the only devices with a 1C code
// longer than one character (spec §3.3's "T/C" column); every other device with a 1C code has a
// single-character one.
bool isTimerOrCounterDevice(const DeviceInfo& info) noexcept {
    return info.c1Code[0] != '\0' && info.c1Code[1] != '\0';
}

// base^digits - 1: the largest value representable in `digits` digits of `radix`. Accumulates in
// uint64_t, not uint32_t: the widest case this module calls with (8 hex digits, iQ-R) computes
// 16^8 == 2^32, one past uint32_t's own range. A uint32_t accumulator still lands on the right
// final answer (0xFFFFFFFF) here, but only because it wraps twice in a row (2^32 mod 2^32 == 0,
// then 0 - 1 == 0xFFFFFFFF) -- correct by coincidence, not by construction, and silently wrong
// for any future caller whose base^digits doesn't happen to be an exact multiple of 2^32 (found
// in Checkpoint A review, T-008). Every digits/radix pair this module ever calls with keeps
// base^digits - 1 within uint32_t's range (the widest is exactly 0xFFFFFFFF), so the final
// narrowing cast below never truncates.
uint32_t digitLimit(int digits, Radix radix) noexcept {
    uint64_t base = (radix == Radix::Hex) ? 16u : 10u;
    uint64_t limit = 1;
    for (int i = 0; i < digits; ++i) {
        limit *= base;
    }
    return static_cast<uint32_t>(limit - 1);
}

bool isQnaFamily(FrameType frame) noexcept {
    return frame == FrameType::F3E || frame == FrameType::F3C || frame == FrameType::F4E ||
           frame == FrameType::F4C;
}

// Rule 3: is `type` supported at all by the frame family `cfg` describes. 1E has no code of its
// own for L/S (spec §3.2 footnote 2): supported there only when cfg.e1AliasLS opts in, regardless
// of the table's own e1Code entry for L/S (which records what the alias would encode, not
// whether it applies by default).
bool deviceSupportedByFamily(const DeviceInfo& info, DeviceType type,
                              const FrameConfig& cfg) noexcept {
    if (isQnaFamily(cfg.frame)) {
        return (cfg.series == PlcSeries::IqR) ? info.qnaBinIqr != kNoCode
                                               : info.qnaBinQL != kNoCode;
    }
    if (cfg.frame == FrameType::F1E) {
        if (type == DeviceType::L || type == DeviceType::S) {
            return cfg.e1AliasLS;
        }
        return info.e1Code != kNoCode;
    }
    if (cfg.frame == FrameType::F1C) {
        return info.c1Code[0] != '\0';
    }
    return false;
}

// Rule 4: the largest device number the frame family accepts (spec §3.4 item 4).
uint32_t deviceNumberLimit(const DeviceInfo& info, const FrameConfig& cfg) noexcept {
    if (isQnaFamily(cfg.frame)) {
        int digits = (cfg.series == PlcSeries::IqR) ? 8 : 6;
        // Binary always caps at the raw byte width, regardless of the device's own radix; only
        // ASCII's digit count depends on it (spec §3.4 item 4 table).
        Radix effectiveRadix = (cfg.code == DataCode::Binary) ? Radix::Hex : info.radix;
        return digitLimit(digits, effectiveRadix);
    }
    if (cfg.frame == FrameType::F1E) {
        return 0xFFFFFFFFu; // Always representable in a uint32_t; no narrower cap exists.
    }
    if (cfg.frame == FrameType::F1C) {
        bool isTC = isTimerOrCounterDevice(info);
        int digits;
        if (cfg.commandSet == C1CommandSet::ACPU) {
            digits = isTC ? 3 : 4;
        } else {
            digits = isTC ? 5 : 6;
        }
        return digitLimit(digits, info.radix);
    }
    return 0xFFFFFFFFu;
}

// Rule 5: word access to a bit device must start on a 16-point boundary, but only for the frame
// families spec §3.5 names (1E, 1C, or a QnA frame explicitly targeting an A-series CPU).
bool needsWordAlignmentCheck(const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1E || cfg.frame == FrameType::F1C || cfg.aSeriesTarget;
}

} // namespace

Expected<void> validate(const Request& r, const FrameConfig& cfg) noexcept {
    return detail::validateRequest(r, cfg, true);
}

namespace detail {

Expected<void> validateRequest(const Request& r, const FrameConfig& cfg,
                               bool checkFieldMaximum) noexcept {
    // Rule 1.
    if (r.count == 0) {
        return Expected<void>(pointCountError("count must be at least 1"));
    }

    const DeviceInfo& info = deviceInfo(r.head.type);

    // Rule 2.
    if (r.isBitOp() && info.kind != DeviceKind::Bit) {
        return Expected<void>(invalidDeviceError("bit operation on a non-bit device"));
    }

    // Rule 3.
    if (!deviceSupportedByFamily(info, r.head.type, cfg)) {
        return Expected<void>(invalidDeviceError("device not supported by this frame family"));
    }

    // Rule 4.
    if (r.head.number > deviceNumberLimit(info, cfg)) {
        return Expected<void>(
            invalidDeviceError("device number exceeds the frame family's field width"));
    }

    // Rule 5.
    bool isWordOp = (r.op == Op::ReadWords || r.op == Op::WriteWords);
    if (isWordOp && info.kind == DeviceKind::Bit && needsWordAlignmentCheck(cfg)) {
        uint32_t n = r.head.number;
        bool aligned;
        if (r.head.type == DeviceType::M && n >= 9000 && n <= 9255) {
            // Spec §10 Q8: in M9000-M9255 the boundary is 9000 + 16k, which replaces the plain
            // multiple-of-16 rule (M9008 is rejected although 9008 itself is 16 x 563).
            aligned = ((n - 9000) % 16) == 0;
        } else {
            aligned = (n % 16) == 0;
        }
        if (!aligned) {
            return Expected<void>(invalidDeviceError(
                "word access to a bit device must start on a 16-point boundary"));
        }
    }

    // Rule 6. QnA's u16 field maximum is never exceeded: count is itself a uint16_t.
    if (checkFieldMaximum && (cfg.frame == FrameType::F1E || cfg.frame == FrameType::F1C) &&
        r.count > 256) {
        return Expected<void>(pointCountError("count exceeds the frame family's field maximum"));
    }

    // Rule 7.
    if (r.isWrite()) {
        size_t expected;
        if (r.op == Op::WriteBits) {
            expected = (r.bitLayout == BitLayout::PackedLsbFirst)
                           ? (static_cast<size_t>(r.count) + 7) / 8
                           : static_cast<size_t>(r.count);
        } else {
            expected = static_cast<size_t>(r.count) * 2;
        }
        if (r.data.size != expected) {
            return Expected<void>(
                dataSizeMismatchError("write payload size does not match count and layout"));
        }
    }

    return Expected<void>();
}

} // namespace detail

} // namespace mc
