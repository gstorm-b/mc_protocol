// tests/consumer_qmake/main.cpp — BLD-07: prints mc::version() and qVersion() and builds an
// McDevice, to prove the qmake .pri consumer path links the library (core and device) and Qt.
#include <cstdio>

#include <QtGlobal>

#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"
#include "mc/version.h"

int main() {
    const mc::Version v = mc::version();
    std::printf("mc::version() = %d.%d.%d\n", v.major, v.minor, v.patch);
    std::printf("qVersion() = %s\n", qVersion());

    mc::McDeviceConfig cfg;
    cfg.subscriptions.append(mc::SubscriptionSpec{QStringLiteral("D100"), 4});
    mc::McDevice device(cfg);
    if (!device.configStatus() || device.linkState() != mc::LinkState::Disconnected) {
        std::printf("McDevice is not usable\n");
        return 1;
    }
    std::printf("McDevice ready, link state = Disconnected\n");
    return 0;
}
