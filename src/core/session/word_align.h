// Private to mc_core (mc::detail): the word alignment of a bit subscription polled with word reads
// (PlanOptions::bitsAsWords), shared by ReadPlan::build() and Session::subscribe()'s immediate
// check so both see the same range.
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"

#include <cstdint>

namespace mc::detail {

/// Half-open interval [start, end) in native device-number units.
struct Interval {
    uint64_t start;
    uint64_t end;
};

/// Whether word access to a bit device on `cfg` must land on a 16-point boundary (spec §3.5).
/// Mirrors the condition src/core/model/validate.cpp checks before rejecting an unaligned
/// request (that file's own needsWordAlignmentCheck(), private to it) -- kept in sync by hand
/// since neither module exposes it to the other; SPEC-core-session.md ties the two together
/// explicitly ("M9000-M9255 on 1E/1C").
inline bool needsWordAlignment(const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1E || cfg.frame == FrameType::F1C || cfg.aSeriesTarget;
}

/// Aligns one subscription's native point range down/up to a 16-point (word) boundary for
/// `PlanOptions::bitsAsWords` planning. The M9000-M9255 special case (spec §10 Q8: boundaries at
/// 9000 + 16k, since 9000 itself is not a multiple of 16) applies only where `needsWordAlignment`
/// would also enforce it on send; every other bit device, and M outside that range, aligns to a
/// plain multiple of 16. `count` must be at least 1.
inline Interval alignToWordBoundary(DeviceType type, uint32_t start, uint32_t count,
                                    const FrameConfig& cfg) noexcept {
    uint64_t origin = 0;
    if (type == DeviceType::M && needsWordAlignment(cfg) && start >= 9000 && start <= 9255) {
        origin = 9000;
    }
    uint64_t s = start;
    uint64_t last = s + count - 1;
    uint64_t alignedStart = origin + ((s - origin) / 16) * 16;
    uint64_t alignedEnd = origin + (((last - origin) / 16) + 1) * 16;
    return Interval{alignedStart, alignedEnd};
}

} // namespace mc::detail
