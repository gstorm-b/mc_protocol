// FrameConfig's named constructors and validate()/effectiveTimeoutMs()/isSerial() (spec §8.3).
// The raw member defaults declared in frame_config.h already equal the 3E defaults (3E is the
// most-used baseline), so frame3E() only has to set `frame` and the caller's chosen `code`; the
// other named constructors override just the handful of fields that differ from that baseline.
#include "mc/core/frame_config.h"

namespace mc {

FrameConfig FrameConfig::frame3E(DataCode code) noexcept {
    FrameConfig cfg{};
    cfg.frame = FrameType::F3E;
    cfg.code = code;
    return cfg;
}

FrameConfig FrameConfig::frame1E(DataCode code) noexcept {
    FrameConfig cfg{};
    cfg.frame = FrameType::F1E;
    cfg.code = code;
    cfg.monitoringTimer = 0x000A; // spec §8.3: 1E default is 000AH (2.5 s), not 3E's 0010H.
    return cfg;
}

FrameConfig FrameConfig::frame3C(SerialFormat f) noexcept {
    FrameConfig cfg{};
    cfg.frame = FrameType::F3C;
    cfg.code = DataCode::Ascii; // 3C is ASCII-only (spec §5.5).
    cfg.format = f;
    cfg.checkRoute = true; // spec §8.3 "Serial (4C/3C/1C)" default, overriding the Ethernet false.
    return cfg;
}

FrameConfig FrameConfig::frame1C(SerialFormat f) noexcept {
    FrameConfig cfg{};
    cfg.frame = FrameType::F1C;
    cfg.code = DataCode::Ascii; // 1C is ASCII-only (spec §5.6).
    cfg.format = f;
    cfg.checkRoute = true; // spec §8.3 "Serial (4C/3C/1C)" default, overriding the Ethernet false.
    return cfg;
}

namespace {

Error invalidConfigError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::InvalidConfig;
    e.message = message;
    return e;
}

} // namespace

Expected<void> FrameConfig::validate() const noexcept {
    if (frame == FrameType::F4E || frame == FrameType::F4C) {
        return Expected<void>(invalidConfigError("F4E/F4C are reserved for v2"));
    }
    if (format == SerialFormat::Format5) {
        return Expected<void>(invalidConfigError("Format5 is reserved for v2"));
    }
    if (messageWait > 15) {
        return Expected<void>(invalidConfigError("messageWait must be 0-15"));
    }
    if (monitoringTimer == 0 && timeoutMs == 0) {
        return Expected<void>(invalidConfigError(
            "monitoringTimer == 0 (wait forever) requires an explicit timeoutMs"));
    }
    if ((frame == FrameType::F3C || frame == FrameType::F1C) && code != DataCode::Ascii) {
        return Expected<void>(invalidConfigError("3C/1C frames are ASCII-only"));
    }
    return Expected<void>();
}

bool FrameConfig::isSerial() const noexcept {
    return frame == FrameType::F3C || frame == FrameType::F1C || frame == FrameType::F4C;
}

uint32_t FrameConfig::effectiveTimeoutMs() const noexcept {
    if (timeoutMs != 0) {
        return timeoutMs;
    }
    if (isSerial()) {
        return 3000;
    }
    return static_cast<uint32_t>(monitoringTimer) * 250u + 1000u;
}

} // namespace mc
