// Defines the doctest main() for this binary; other sources joining mc_build_tests
// (BLD-04/05, T03) must not repeat DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "mc/version.h"

TEST_CASE("BLD-08 mc::version() agrees with the MC_VERSION_* macros") {
    constexpr mc::Version v = mc::version();
    CHECK(v.major == MC_VERSION_MAJOR);
    CHECK(v.minor == MC_VERSION_MINOR);
    CHECK(v.patch == MC_VERSION_PATCH);
}
