// Negative control for MCK-HYG: never compiled. A mock source must not use the client encoder
// or response reader, even through the public header.
#include "mc/core/protocol.h"

void useClient(const mc::FrameConfig& cfg) {
    mc::McProtocol proto(cfg);
    (void)proto;
}
