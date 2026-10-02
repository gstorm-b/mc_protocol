// tests/hil/replay/replay_options.h: the one command-line option of mc_replay_tests.
//
//   mc_replay_tests --replay-root=<dir>     (or the environment variable MC_REPLAY_ROOT)
//
// <dir> holds one capture folder per profile (<dir>/<profile id>/run.meta ...). Default:
// tests/vectors/captured/ of the source tree, the folder of hardware captures. The test case
// "RPL-sweep ..." replays every capture folder under it; the HIL-04 end-to-end test points it at
// the capture it has just written under the build tree (captures of virtual_plc never go to the
// default folder).
#pragma once

#include <string>

namespace mc::replay {

/// The root given by `--replay-root` or MC_REPLAY_ROOT; empty = the default.
extern std::string g_rootOption;

} // namespace mc::replay
