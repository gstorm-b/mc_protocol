#include "mc/version.h"

namespace {

// Keeps mc::version() and the MC_VERSION_* macros honest at compile time; the runtime half of
// BLD-08 (tests/build/test_version.cpp) checks the same fact through the compiled function call.
static_assert(mc::version().major == MC_VERSION_MAJOR, "mc::version() disagrees with MC_VERSION_MAJOR");
static_assert(mc::version().minor == MC_VERSION_MINOR, "mc::version() disagrees with MC_VERSION_MINOR");
static_assert(mc::version().patch == MC_VERSION_PATCH, "mc::version() disagrees with MC_VERSION_PATCH");

} // namespace
