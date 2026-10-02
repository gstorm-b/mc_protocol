/**
 * @file bench_report.h
 * @brief `hil_capture --report`: turns every `<root>/<profile>/bench.csv` into `BENCH.md`
 * (spec "Timing benchmark").
 *
 * One table per profile; per request the bytes out and in and, for each of `ttfb`, `rx` and `rtt`,
 * the min / median / p95 / max in a RUN column and a STOP column (milliseconds, three decimals).
 * The median is the statistical median (the mean of the two middle values for an even count); p95
 * is the **nearest-rank** percentile: the value at rank ceil(0.95 * n) of the sorted series. The
 * caveats of the spec are printed at the top. Nothing is judged: the numbers are reference data.
 */
#pragma once

#include <QString>
#include <QVector>

namespace mc::hil {

/// @brief Summary statistics of one series.
struct SeriesStats {
    int n{0};         ///< Number of values.
    double min{0};    ///< Smallest value.
    double median{0}; ///< Median (mean of the middle two for an even n).
    double p95{0};    ///< Nearest-rank 95th percentile.
    double max{0};    ///< Largest value.
};

/// @brief The statistics of @p values (all zero for an empty series).
SeriesStats seriesStats(QVector<double> values);

/// @brief The report text for every bench.csv under @p root.
/// @param[in] root Folder holding `<profile id>/bench.csv`.
/// @param[out] error Set when no bench.csv is found or one cannot be read.
QString benchReportText(const QString& root, QString* error);

/// @brief Writes the report to @p outFile (its folder is created).
bool writeBenchReport(const QString& root, const QString& outFile, QString* error);

} // namespace mc::hil
