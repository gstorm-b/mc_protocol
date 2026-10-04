#include "mc_workbench/trend_widget.h"

#include <QColor>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>

#include <algorithm>
#include <iterator>
#include <limits>

namespace mc::workbench {

namespace {

const QColor kPalette[] = {QColor(31, 119, 180), QColor(214, 39, 40),  QColor(44, 160, 44),
                           QColor(255, 127, 14), QColor(148, 103, 189), QColor(140, 86, 75),
                           QColor(227, 119, 194), QColor(23, 190, 207)};

} // namespace

TrendWidget::TrendWidget(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
}

TrendWidget::Series* TrendWidget::find(const QString& key) {
    for (Series& s : m_series) {
        if (s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

const TrendWidget::Series* TrendWidget::find(const QString& key) const {
    for (const Series& s : m_series) {
        if (s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

void TrendWidget::addSeries(const QString& key) {
    if (find(key) == nullptr) {
        m_series.push_back(Series{key, {}});
        update();
    }
}

void TrendWidget::removeSeries(const QString& key) {
    m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                  [&key](const Series& s) { return s.key == key; }),
                   m_series.end());
    update();
}

void TrendWidget::addSample(const QString& key, qint64 tMs, double value) {
    Series* series = find(key);
    if (series == nullptr) {
        return;
    }
    series->samples.emplace_back(static_cast<double>(tMs), value);
    while (series->samples.size() > static_cast<size_t>(kMaxSamples)) {
        series->samples.pop_front();
    }
    update();
}

bool TrendWidget::hasSeries(const QString& key) const {
    return find(key) != nullptr;
}

QStringList TrendWidget::seriesKeys() const {
    QStringList keys;
    for (const Series& s : m_series) {
        keys.push_back(s.key);
    }
    return keys;
}

int TrendWidget::sampleCount(const QString& key) const {
    const Series* series = find(key);
    return series != nullptr ? static_cast<int>(series->samples.size()) : 0;
}

QVector<QPointF> TrendWidget::samples(const QString& key) const {
    QVector<QPointF> out;
    if (const Series* series = find(key)) {
        out.reserve(static_cast<int>(series->samples.size()));
        for (const QPointF& p : series->samples) {
            out.push_back(p);
        }
    }
    return out;
}

void TrendWidget::clearSamples() {
    for (Series& s : m_series) {
        s.samples.clear();
    }
    update();
}

void TrendWidget::setWindowSeconds(int seconds) {
    m_windowSeconds = std::max(1, seconds);
    update();
}

QSize TrendWidget::minimumSizeHint() const {
    return {160, 100};
}

QSize TrendWidget::sizeHint() const {
    return {420, 200};
}

void TrendWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().base());

    const QFontMetrics fm(font());
    const int left = fm.horizontalAdvance(QStringLiteral("-32768.0")) + 8;
    const QRect plot(left, 8 + fm.height(), width() - left - 8, height() - 16 - fm.height() * 2);
    if (plot.width() < 20 || plot.height() < 20) {
        return;
    }
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(plot);

    // Window and value range over the visible samples.
    double newest = -std::numeric_limits<double>::infinity();
    for (const Series& s : m_series) {
        if (!s.samples.empty()) {
            newest = std::max(newest, s.samples.back().x());
        }
    }
    if (newest == -std::numeric_limits<double>::infinity()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("Tick the Trend box of a point"));
        return;
    }
    const double windowMs = static_cast<double>(m_windowSeconds) * 1000.0;
    const double oldest = newest - windowMs;
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (const Series& s : m_series) {
        for (const QPointF& pt : s.samples) {
            if (pt.x() >= oldest) {
                low = std::min(low, pt.y());
                high = std::max(high, pt.y());
            }
        }
    }
    if (low > high) {
        low = 0;
        high = 1;
    }
    if (high - low < 1.0) {
        const double middle = (high + low) / 2.0;
        low = middle - 0.5;
        high = middle + 0.5;
    }

    // Value axis labels and grid.
    p.setPen(palette().color(QPalette::Text));
    p.drawText(QRect(2, plot.top() - fm.height() / 2, left - 6, fm.height()),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(high, 'g', 6));
    p.drawText(QRect(2, plot.bottom() - fm.height() / 2, left - 6, fm.height()),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(low, 'g', 6));
    p.drawText(QRect(plot.left(), plot.bottom() + 2, plot.width(), fm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("-%1 s").arg(m_windowSeconds));
    p.drawText(QRect(plot.left(), plot.bottom() + 2, plot.width(), fm.height()),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("now"));
    p.setPen(QPen(palette().color(QPalette::Midlight), 1, Qt::DotLine));
    p.drawLine(plot.left(), plot.center().y(), plot.right(), plot.center().y());

    p.setClipRect(plot.adjusted(1, 1, -1, -1));
    int colorIndex = 0;
    int legendX = plot.left();
    for (const Series& s : m_series) {
        const QColor color = kPalette[colorIndex % static_cast<int>(std::size(kPalette))];
        ++colorIndex;
        QPainterPath path;
        bool started = false;
        for (const QPointF& pt : s.samples) {
            if (pt.x() < oldest) {
                continue;
            }
            const double x = plot.left() + (pt.x() - oldest) / windowMs * plot.width();
            const double y = plot.bottom() - (pt.y() - low) / (high - low) * plot.height();
            if (!started) {
                path.moveTo(x, y);
                started = true;
            } else {
                path.lineTo(x, y);
            }
        }
        p.setPen(QPen(color, 1.5));
        p.drawPath(path);
        if (started) {
            p.setBrush(color);
            p.drawEllipse(path.currentPosition(), 2.5, 2.5);
            p.setBrush(Qt::NoBrush);
        }
        p.setClipping(false);
        p.setPen(color);
        p.drawText(QPoint(legendX, plot.top() - 4), s.key);
        legendX += fm.horizontalAdvance(s.key) + 12;
        p.setClipRect(plot.adjusted(1, 1, -1, -1));
    }
}

} // namespace mc::workbench
