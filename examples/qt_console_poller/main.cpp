// qt_console_poller: connects an mc::McDevice to a PLC, subscribes to devices given on the command
// line and prints what the device reports.
//
//   qt_console_poller --host 127.0.0.1 --port 5000 --frame 3E --sub D100:64 --sub M0:32
//
// What it shows:
//   * McDevice never blocks: connectToPlc() returns at once and everything else arrives as a
//     signal in the Qt event loop. There is no waitFor*() anywhere in this file.
//   * After the first round, one snapshotReady() per subscribed device type, all together;
//     nothing is reported as "changed" in that round.
//   * From the second round on, valuesChanged() carries exactly the points that changed.
//   * The device never reconnects by itself. When the link faults or is lost, this program
//     decides what to do: it gives up and exits with 1. Another application might call
//     connectToPlc() again instead.
//
// Options: --rounds N exits with 0 after N polling rounds (1 on a link fault or when the
// connection cannot be opened), so a script can run it; without it the program runs until Ctrl+C.
#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <cstdint>
#include <cstdio>

namespace {

void say(const QString& line) {
    std::printf("%s\n", qPrintable(line));
    std::fflush(stdout);
}

QString nameOf(mc::Device d) {
    char text[16];
    mc::formatDevice(d, text, sizeof text);
    return QString::fromLatin1(text);
}

const char* stateName(mc::LinkState s) {
    switch (s) {
    case mc::LinkState::Disconnected: return "Disconnected";
    case mc::LinkState::Connecting: return "Connecting";
    case mc::LinkState::Connected: return "Connected";
    case mc::LinkState::Faulted: return "Faulted";
    }
    return "?";
}

const char* reasonName(mc::LinkReason r) {
    switch (r) {
    case mc::LinkReason::Requested: return "Requested";
    case mc::LinkReason::OpenFailed: return "OpenFailed";
    case mc::LinkReason::PeerClosed: return "PeerClosed";
    case mc::LinkReason::TransportError: return "TransportError";
    case mc::LinkReason::Fault: return "Fault";
    }
    return "?";
}

// One snapshot segment as rows of eight points: words as decimal, bits as 0/1. `values` uses the
// normalized layout of the library: two bytes little-endian per word, one byte per bit.
void printSegment(const mc::SnapshotSegment& seg) {
    const bool words = mc::deviceInfo(seg.head.type).kind != mc::DeviceKind::Bit;
    for (quint32 row = 0; row < seg.count; row += 8) {
        QString line = QStringLiteral("  %1:").arg(nameOf(mc::Device{seg.head.type, seg.head.number + row}), -8);
        for (quint32 k = row; k < seg.count && k < row + 8; ++k) {
            if (words) {
                const auto lo = static_cast<uint8_t>(seg.values.at(static_cast<qsizetype>(k) * 2));
                const auto hi = static_cast<uint8_t>(seg.values.at(static_cast<qsizetype>(k) * 2 + 1));
                line += QStringLiteral(" %1").arg(static_cast<unsigned>(lo | (hi << 8)), 5);
            } else {
                line += QStringLiteral(" %1").arg(static_cast<int>(seg.values.at(k)));
            }
        }
        say(line);
    }
}

mc::FrameConfig frameFor(const QString& name, mc::DataCode code, bool& ok) {
    ok = true;
    if (name.compare(QLatin1String("3E"), Qt::CaseInsensitive) == 0) {
        return mc::FrameConfig::frame3E(code);
    }
    if (name.compare(QLatin1String("1E"), Qt::CaseInsensitive) == 0) {
        return mc::FrameConfig::frame1E(code);
    }
    if (name.compare(QLatin1String("3C"), Qt::CaseInsensitive) == 0) {
        return mc::FrameConfig::frame3C();
    }
    if (name.compare(QLatin1String("1C"), Qt::CaseInsensitive) == 0) {
        return mc::FrameConfig::frame1C();
    }
    ok = false;
    return mc::FrameConfig::frame3E();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Polls a PLC through mc::McDevice and prints it."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("host"), QStringLiteral("PLC host name or address."),
                      QStringLiteral("HOST"), QStringLiteral("127.0.0.1")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("PLC TCP port."), QStringLiteral("N"),
                      QStringLiteral("5000")});
    parser.addOption({QStringLiteral("frame"), QStringLiteral("3E, 1E, 3C or 1C."),
                      QStringLiteral("FRAME"), QStringLiteral("3E")});
    parser.addOption({QStringLiteral("code"), QStringLiteral("Binary or ASCII (3E and 1E)."),
                      QStringLiteral("Binary|ASCII"), QStringLiteral("Binary")});
    parser.addOption({QStringLiteral("sub"), QStringLiteral("Subscribe to COUNT points from DEV."),
                      QStringLiteral("DEV:COUNT")});
    parser.addOption({QStringLiteral("rounds"), QStringLiteral("Exit 0 after N rounds."),
                      QStringLiteral("N")});
    if (!parser.parse(app.arguments())) {
        std::fprintf(stderr, "%s\n", qPrintable(parser.errorText()));
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        std::printf("%s", qPrintable(parser.helpText()));
        return 0;
    }

    // Build the configuration from the arguments; McDeviceConfig is plain data.
    const QString codeText = parser.value(QStringLiteral("code"));
    mc::DataCode code = mc::DataCode::Binary;
    if (codeText.compare(QLatin1String("ASCII"), Qt::CaseInsensitive) == 0) {
        code = mc::DataCode::Ascii;
    } else if (codeText.compare(QLatin1String("Binary"), Qt::CaseInsensitive) != 0) {
        std::fprintf(stderr, "qt_console_poller: --code is Binary or ASCII, got \"%s\".\n",
                     qPrintable(codeText));
        return 2;
    }
    bool frameOk = false;
    mc::McDeviceConfig cfg;
    cfg.frame = frameFor(parser.value(QStringLiteral("frame")), code, frameOk);
    if (!frameOk) {
        std::fprintf(stderr, "qt_console_poller: --frame is 3E, 1E, 3C or 1C.\n");
        return 2;
    }
    bool portOk = false;
    const int port = parser.value(QStringLiteral("port")).toInt(&portOk);
    if (!portOk || port <= 0 || port > 65535) {
        std::fprintf(stderr, "qt_console_poller: --port is 1..65535.\n");
        return 2;
    }
    cfg.tcp.host = parser.value(QStringLiteral("host"));
    cfg.tcp.port = static_cast<quint16>(port);
    for (const QString& text : parser.values(QStringLiteral("sub"))) {
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        bool countOk = false;
        const quint32 count = colon > 0 ? text.mid(colon + 1).toUInt(&countOk) : 0;
        if (!countOk) {
            std::fprintf(stderr, "qt_console_poller: --sub expects DEV:COUNT, got \"%s\".\n",
                         qPrintable(text));
            return 2;
        }
        cfg.subscriptions.append(mc::SubscriptionSpec{text.left(colon), count});
    }
    int rounds = 0; // 0 = run until Ctrl+C
    if (parser.isSet(QStringLiteral("rounds"))) {
        bool roundsOk = false;
        rounds = parser.value(QStringLiteral("rounds")).toInt(&roundsOk);
        if (!roundsOk || rounds <= 0) {
            std::fprintf(stderr, "qt_console_poller: --rounds is a positive number.\n");
            return 2;
        }
    }

    QString where;
    if (const mc::Expected<void> valid = cfg.validate(&where); !valid) {
        std::fprintf(stderr, "qt_console_poller: invalid configuration: %s: %s\n",
                     qPrintable(where), valid.error().message);
        return 2;
    }

    mc::McDevice device(cfg);

    // McDevice emits its signals only after it has drained the engine underneath, so a slot may
    // call the device again (here: disconnectFromPlc()); the nested call cannot corrupt anything.
    QObject::connect(&device, &mc::McDevice::linkStateChanged, &app,
                     [&](mc::LinkState state, mc::LinkReason reason, const QString& detail) {
                         say(QStringLiteral("link: %1 (%2) %3")
                                 .arg(QLatin1String(stateName(state)), QLatin1String(reasonName(reason)), detail));
                         // This program's own policy: a lost or refused link ends it. The device
                         // itself would sit in Disconnected until asked again.
                         if (state == mc::LinkState::Disconnected && reason != mc::LinkReason::Requested) {
                             QCoreApplication::exit(1);
                         }
                     });
    QObject::connect(&device, &mc::McDevice::linkFault, &app, [](const mc::LinkFaultInfo& fault) {
        say(QStringLiteral("link fault: %1 (%2)")
                .arg(fault.kind == mc::LinkFaultKind::Timeout ? QLatin1String("timeout")
                                                              : QLatin1String("protocol error"),
                     QString::fromUtf8(fault.error.message)));
        QCoreApplication::exit(1);
    });
    QObject::connect(&device, &mc::McDevice::snapshotReady, &app, [](const mc::DeviceSnapshot& snap) {
        if (snap.round != 1) { // later rounds would print the same values again
            return;
        }
        say(QStringLiteral("snapshot %1, round 1").arg(QLatin1String(mc::deviceInfo(snap.type).symbol)));
        for (const mc::ChunkStatus& chunk : snap.chunks) {
            if (chunk.state == mc::ChunkState::Failed) {
                say(QStringLiteral("  chunk %1 x%2 failed: %3")
                        .arg(nameOf(chunk.request.head))
                        .arg(chunk.request.count)
                        .arg(QString::fromUtf8(chunk.error.message)));
            }
        }
        for (const mc::SnapshotSegment& seg : snap.segments) {
            printSegment(seg);
        }
    });
    QObject::connect(&device, &mc::McDevice::valuesChanged, &app,
                     [](mc::DeviceType, quint32 round, const QVector<mc::Change>& changes) {
                         for (const mc::Change& c : changes) {
                             say(QStringLiteral("changed %1: %2 -> %3 (round %4)")
                                     .arg(nameOf(c.device))
                                     .arg(c.oldValue)
                                     .arg(c.newValue)
                                     .arg(round));
                         }
                     });
    QObject::connect(&device, &mc::McDevice::cycleDone, &app, [&](const mc::CycleInfo& cycle) {
        if (rounds > 0 && static_cast<int>(cycle.round) >= rounds) {
            say(QStringLiteral("done after %1 rounds").arg(cycle.round));
            device.disconnectFromPlc();
            QCoreApplication::exit(0);
        }
    });

    // Start from inside the event loop, so a QCoreApplication::exit() from a slot always has a
    // running loop to stop. connectToPlc() returns at once; the rest happens in app.exec().
    QTimer::singleShot(0, &device, &mc::McDevice::connectToPlc);
    return app.exec();
}
