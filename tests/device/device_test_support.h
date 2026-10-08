// device_test_support.h -- helpers shared by the McDevice tests: a config that points at a
// MockPlcServer, the standard memory image and small wait/convert functions.
#pragma once

#include "mc/core/device.h"
#include "mc/core/log.h"
#include "mc/device/mc_device_config.h"
#include "mc/mock/mock_plc.h"
#include "mock_plc_server.h"

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QVector>

#include <cstdint>

namespace testing {

constexpr int kWaitMs = 5000;

inline mc::Device dev(const char* text) { return mc::parseDevice(text).value(); }

// D100..D103 = 10, 20, 30, 40 and M3 = 1: the image every server starts with.
inline void seedMemory(mc::MockPlc& plc) {
    plc.setWords(dev("D100"), {10, 20, 30, 40});
    plc.setBit(dev("M3"), true);
}

// A config for one MockPlcServer: 3E Binary (or the given frame), fast rounds, subscriptions
// D100 x4 and M0 x16 (two device types, so snapshot order is visible).
inline mc::McDeviceConfig configFor(quint16 port, const mc::FrameConfig& frame = mc::FrameConfig::frame3E(),
                                    bool subscribe = true) {
    mc::McDeviceConfig cfg;
    cfg.frame = frame;
    cfg.session.cycleIntervalMs = 20;
    cfg.tcp.host = QStringLiteral("127.0.0.1");
    cfg.tcp.port = port;
    cfg.tcp.connectTimeoutMs = 2000;
    if (subscribe) {
        cfg.subscriptions = {{QStringLiteral("D100"), 4}, {QStringLiteral("M0"), 16}};
    }
    return cfg;
}

// Two bytes little-endian per word, the normalized payload layout.
inline QByteArray wordsLe(const QVector<quint16>& words) {
    QByteArray out;
    for (quint16 w : words) {
        out.append(static_cast<char>(w & 0xFF));
        out.append(static_cast<char>((w >> 8) & 0xFF));
    }
    return out;
}

// Removes the "cycle(n)" entries: with an empty plan a round ends at once and its position among
// other signals depends on timing.
inline QStringList withoutCycles(QStringList trace) {
    QStringList out;
    for (const QString& t : trace) {
        if (!t.startsWith(QLatin1String("cycle("))) {
            out << t;
        }
    }
    return out;
}

} // namespace testing

namespace testing {

// A port on which nobody listens: bind an OS-chosen one, then release it.
inline quint16 unusedPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

// Collects every log line, all levels.
class CaptureSink final : public mc::LogSink {
  public:
    struct Line {
        mc::LogLevel level;
        QString category;
        QString message;
    };

    bool enabled(mc::LogLevel) const noexcept override { return true; }

    void write(mc::LogLevel level, std::string_view category,
               std::string_view message) noexcept override {
        lines.append(Line{
            level,
            QString::fromUtf8(category.data(), static_cast<QString::size_type>(category.size())),
            QString::fromUtf8(message.data(), static_cast<QString::size_type>(message.size()))});
    }

    // The lines of one level and category.
    QVector<Line> of(mc::LogLevel level, const QString& category) const {
        QVector<Line> out;
        for (const Line& l : lines) {
            if (l.level == level && l.category == category) {
                out.append(l);
            }
        }
        return out;
    }

    QVector<Line> lines;
};

} // namespace testing
