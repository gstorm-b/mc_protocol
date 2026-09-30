// tests/consumer_cmake/main.cpp — BLD-06: proves a plain CMake consumer that links mc::device gets
// working headers and libraries (core through mc::device, Qt through its link interface), with no
// library test or example target pulled into its own build tree. It builds an McDevice from a
// config and checks that the config is usable; no event loop or socket is needed for that.
#include <cstdio>

#include <QString>

#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"
#include "mc/version.h"

int main() {
    const mc::Version v = mc::version();
    std::printf("mc::version() = %d.%d.%d\n", v.major, v.minor, v.patch);

    mc::McDeviceConfig cfg;
    cfg.subscriptions.append(mc::SubscriptionSpec{QStringLiteral("D100"), 4});
    QString where;
    if (!cfg.validate(&where)) {
        std::printf("McDeviceConfig is not valid: %s\n", qPrintable(where));
        return 1;
    }
    mc::McDevice device(cfg);
    if (!device.configStatus() || device.linkState() != mc::LinkState::Disconnected) {
        std::printf("McDevice is not usable\n");
        return 1;
    }
    std::printf("McDevice ready, link state = Disconnected\n");
    return 0;
}
