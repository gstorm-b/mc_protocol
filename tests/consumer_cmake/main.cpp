// tests/consumer_cmake/main.cpp — BLD-06: proves a plain CMake consumer that only links
// mc::core gets a working header and library, with no library test or example target pulled
// into its own build tree.
#include <cstdio>

#include "mc/version.h"

int main() {
    const mc::Version v = mc::version();
    std::printf("mc::version() = %d.%d.%d\n", v.major, v.minor, v.patch);
    return 0;
}
