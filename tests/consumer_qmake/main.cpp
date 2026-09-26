// tests/consumer_qmake/main.cpp — BLD-07: prints mc::version() and qVersion() to prove the
// qmake .pri consumer path links both the library and Qt.
#include <cstdio>

#include <QtGlobal>

#include "mc/version.h"

int main() {
    const mc::Version v = mc::version();
    std::printf("mc::version() = %d.%d.%d\n", v.major, v.minor, v.patch);
    std::printf("qVersion() = %s\n", qVersion());
    return 0;
}
