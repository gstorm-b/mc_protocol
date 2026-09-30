// Private to mc_core (mc::detail): validate() with the field-maximum rule (rule 6) switchable, for
// chunkCount()/chunk(). Not part of the public surface.
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

namespace mc::detail {

/// validate() (request.h) with rule 6 ("count > field maximum": 256 for 1E/1C) applied only when
/// `checkFieldMaximum` is true. validate() passes true. chunkCount()/chunk() pass false: they exist
/// to split a request above the field maximum into commands that each fit maxPoints() (spec §8.5),
/// and every chunk is then within it. Rules 1-5 and 7 always apply.
Expected<void> validateRequest(const Request& r, const FrameConfig& cfg,
                               bool checkFieldMaximum) noexcept;

} // namespace mc::detail
