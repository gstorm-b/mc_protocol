// Private to mc_core (mc::detail): numberBase(), and validate() with the field-maximum rule (rule
// 6) switchable for chunkCount()/chunk(). Not part of the public surface.
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <cstdint>

namespace mc::detail {

/// The base (8, 10 or 16) in which the number of device `info` is written as text or as ASCII
/// digits: its table radix, or octal for X and Y when `xy` is XyNumbering::Octal. The one place
/// that decides it for parseDevice()/formatDevice() (`xy` = FrameConfig::xyNotation), validate()
/// and the ASCII device encoder (`xy` = FrameConfig::xyAsciiDigits).
inline uint32_t numberBase(const DeviceInfo& info, XyNumbering xy) noexcept {
    if (xy == XyNumbering::Octal && (info.type == DeviceType::X || info.type == DeviceType::Y)) {
        return 8u;
    }
    return info.radix == Radix::Hex ? 16u : 10u;
}

/// validate() (request.h) with rule 6 ("count > field maximum": 256 for 1E/1C) applied only when
/// `checkFieldMaximum` is true. validate() passes true. chunkCount()/chunk() pass false: they exist
/// to split a request above the field maximum into commands that each fit maxPoints() (spec §8.5),
/// and every chunk is then within it. Rules 1-5 and 7 always apply.
Expected<void> validateRequest(const Request& r, const FrameConfig& cfg,
                               bool checkFieldMaximum) noexcept;

} // namespace mc::detail
