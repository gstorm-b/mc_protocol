#include "hil_capture/bench_report.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace mc::hil {

namespace {

struct Cell {
    QVector<double> ttfb;
    QVector<double> rx;
    QVector<double> rtt;
    qint64 reqBytes{0};
    qint64 respBytes{0};
};

struct RequestKey {
    QString step;
    QString op;
    QString device;
    QString count;
    bool operator<(const RequestKey& o) const {
        return std::tie(step, op, device, count) < std::tie(o.step, o.op, o.device, o.count);
    }
};

QString fmt(double v) { return QString::number(v, 'f', 3); }

QString cellText(const QVector<double>& values) {
    if (values.isEmpty()) {
        return QStringLiteral("-");
    }
    const SeriesStats s = seriesStats(values);
    return QStringLiteral("%1 / %2 / %3 / %4 (n=%5)")
        .arg(fmt(s.min), fmt(s.median), fmt(s.p95), fmt(s.max))
        .arg(s.n);
}

} // namespace

SeriesStats seriesStats(QVector<double> v) {
    SeriesStats s;
    if (v.isEmpty()) {
        return s;
    }
    std::sort(v.begin(), v.end());
    const int n = static_cast<int>(v.size());
    s.n = n;
    s.min = v.first();
    s.max = v.last();
    s.median = (n % 2 == 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
    const int rank = static_cast<int>(std::ceil(0.95 * n - 1e-9)); // nearest rank, 1-based
    s.p95 = v[std::max(rank, 1) - 1];
    return s;
}

QString benchReportText(const QString& root, QString* error) {
    const QDir dir(root);
    QStringList profiles = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QString body;
    int found = 0;
    for (const QString& profile : profiles) {
        QFile file(dir.filePath(profile + QStringLiteral("/bench.csv")));
        if (!file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot read %1").arg(file.fileName());
            }
            return QString();
        }
        const QStringList lines =
            QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QMap<RequestKey, QMap<QString, Cell>> table; // request -> state -> cell
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList c = lines[i].split(QLatin1Char(','));
            if (c.size() < 13) {
                continue;
            }
            RequestKey key{c[2], c[3], c[4], c[5]};
            Cell& cell = table[key][c[1]];
            cell.reqBytes = c[6].toLongLong();
            cell.respBytes = c[7].toLongLong();
            const auto add = [](QVector<double>& list, const QString& text) {
                bool ok = false;
                const double v = text.toDouble(&ok);
                if (ok) {
                    list.push_back(v);
                }
            };
            add(cell.ttfb, c[9]);
            add(cell.rx, c[10]);
            add(cell.rtt, c[11]);
        }
        ++found;
        body += QStringLiteral("## %1\n\n").arg(profile);
        body += QStringLiteral("| Step | Request | Bytes out / in | Metric | RUN: min / median / "
                               "p95 / max | STOP: min / median / p95 / max |\n");
        body += QStringLiteral("|---|---|---|---|---|---|\n");
        for (auto it = table.begin(); it != table.end(); ++it) {
            const Cell run = it.value().value(QStringLiteral("RUN"));
            const Cell stop = it.value().value(QStringLiteral("STOP"));
            const Cell& sizes = it.value().contains(QStringLiteral("RUN")) ? run : stop;
            const QString request =
                QStringLiteral("%1 %2 x%3").arg(it.key().op, it.key().device, it.key().count);
            const struct {
                const char* name;
                const QVector<double>& (*pick)(const Cell&);
            } metrics[] = {
                {"ttfb", [](const Cell& c) -> const QVector<double>& { return c.ttfb; }},
                {"rx", [](const Cell& c) -> const QVector<double>& { return c.rx; }},
                {"rtt", [](const Cell& c) -> const QVector<double>& { return c.rtt; }},
            };
            bool first = true;
            for (const auto& m : metrics) {
                body +=
                    QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 |\n")
                        .arg(
                            first ? it.key().step : QString(), first ? request : QString(),
                            first
                                ? QStringLiteral("%1 / %2").arg(sizes.reqBytes).arg(sizes.respBytes)
                                : QString(),
                            QLatin1String(m.name), cellText(m.pick(run)), cellText(m.pick(stop)));
                first = false;
            }
        }
        body += QLatin1Char('\n');
    }
    if (found == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("no bench.csv under %1").arg(root);
        }
        return QString();
    }
    QString head;
    head += QStringLiteral("# HIL timing benchmark\n\n");
    head += QStringLiteral("Generated by `hil_capture --report` from every `bench.csv`. Reference "
                           "data: nothing passes or fails on a timing.\n\n");
    head += QStringLiteral(
        "- Times are milliseconds. `ttfb` is request written to first byte, `rx` first to last "
        "byte, `rtt` written to last byte. Median is the statistical median (mean of the two "
        "middle values for an even count); p95 is the **nearest-rank** percentile (the value at "
        "rank ceil(0.95 n) of the sorted series).\n");
    head += QStringLiteral(
        "- Timestamps are taken in the Qt event loop, so they include OS and driver latency.\n");
    head += QStringLiteral("- USB-serial adapters buffer received bytes (e.g. an FTDI latency "
                           "timer), which inflates `ttfb` and hides `rx` on serial links; the "
                           "adapter and its latency setting are part of the profile.\n");
    head += QStringLiteral("- RUN is the PLC running (the application's condition); STOP is the "
                           "same request with the PLC stopped, so the scan's share of `ttfb` can "
                           "be separated from the communication cost. `-` means no row.\n\n");
    return head + body;
}

bool writeBenchReport(const QString& root, const QString& outFile, QString* error) {
    const QString text = benchReportText(root, error);
    if (text.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(outFile).absolutePath());
    QFile f(outFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2").arg(outFile, f.errorString());
        }
        return false;
    }
    f.write(text.toUtf8());
    return true;
}

} // namespace mc::hil
