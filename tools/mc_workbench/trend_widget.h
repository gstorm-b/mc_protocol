/**
 * @file trend_widget.h
 * @brief `TrendWidget`: a small line chart of selected values over time, drawn with QPainter
 * (no Qt Charts).
 */
#pragma once

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <deque>
#include <vector>

namespace mc::workbench {

/**
 * @brief Draws one polyline per series over a sliding time window, with an automatic value axis
 * and a legend.
 *
 * Every series is a bounded ring (kMaxSamples); the oldest samples fall out first, so a long run
 * cannot grow memory. Time is whatever the caller passes in milliseconds (the device tab uses
 * milliseconds since the tab was created); the window ends at the newest sample.
 *
 * @note GUI thread only.
 */
class TrendWidget : public QWidget {
    Q_OBJECT
public:
    /// @brief The most samples kept per series.
    static constexpr int kMaxSamples = 4096;

    /// @brief Creates an empty chart with a 30 second window.
    /// @param[in] parent Parent widget.
    explicit TrendWidget(QWidget* parent = nullptr);

    /// @brief Adds a series; nothing happens when it exists.
    /// @param[in] key Name shown in the legend, e.g. "D100".
    void addSeries(const QString& key);

    /// @brief Removes a series and its samples.
    /// @param[in] key The series.
    void removeSeries(const QString& key);

    /// @brief Appends a sample to a series; ignored when the series does not exist.
    /// @param[in] key The series.
    /// @param[in] tMs Time of the sample in milliseconds.
    /// @param[in] value The value.
    void addSample(const QString& key, qint64 tMs, double value);

    /// @brief Whether a series exists.
    /// @param[in] key The series.
    /// @return true when it does.
    bool hasSeries(const QString& key) const;

    /// @brief The names of the series, in the order they were added.
    /// @return The keys.
    QStringList seriesKeys() const;

    /// @brief How many samples a series holds.
    /// @param[in] key The series.
    /// @return The count; 0 for an unknown series.
    int sampleCount(const QString& key) const;

    /// @brief The samples of a series (x = time in ms, y = value).
    /// @param[in] key The series.
    /// @return A copy, oldest first.
    QVector<QPointF> samples(const QString& key) const;

    /// @brief Removes every sample but keeps the series.
    void clearSamples();

    /// @brief Sets the width of the time axis.
    /// @param[in] seconds Seconds shown; at least 1.
    void setWindowSeconds(int seconds);

    /// @brief The width of the time axis.
    /// @return Seconds shown.
    int windowSeconds() const noexcept { return m_windowSeconds; }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Series {
        QString key;
        std::deque<QPointF> samples;
    };

    Series* find(const QString& key);
    const Series* find(const QString& key) const;

    std::vector<Series> m_series;
    int m_windowSeconds{30};
};

} // namespace mc::workbench
