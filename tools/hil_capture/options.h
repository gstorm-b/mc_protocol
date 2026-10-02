/**
 * @file options.h
 * @brief The command line of `hil_capture`, parsed into one struct, and its exit codes.
 */
#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>

namespace mc::hil {

/// @brief Process exit codes, listed in `--help`.
enum class ExitCode : int {
    Ok = 0,          ///< Everything ran and matched.
    RunFailures = 1, ///< The run finished but a step failed.
    BadInput = 2,    ///< Bad arguments, an unreadable or invalid profile or plan.
    GateRefused = 3  ///< The safety gate refused the run; nothing was sent.
};

/// @brief What the command line asks for.
struct Options {
    QString profilePath;                                          ///< `--profile`.
    QString planPath;                                             ///< `--plan`.
    QString outputRoot{QStringLiteral("tests/vectors/captured")}; ///< `--output-root`.
    QStringList only;                        ///< `--only`: group ids; empty = every step.
    int benchReps{200};                      ///< `--bench-reps`.
    QString plcState{QStringLiteral("RUN")}; ///< `--plc-state`: RUN or STOP.
    bool dryRun{false};                      ///< `--dry-run`.
    bool yes{false}; ///< `--yes`: skip the confirmation prompt (never the gate; not when read-only
                     ///< frames exist).
    bool report{false}; ///< `--report`.
    QString note;       ///< `--note`: operator note recorded in run.meta.
    QString reportOut{
        QStringLiteral("docs/hil/BENCH.md")}; ///< `--report-out`: where `--report` writes.
    int reconnectSeconds{15}; ///< `--reconnect-timeout`: how long a lost link is retried.
};

/// @brief How parsing ended.
enum class CommandLineStatus : uint8_t { Run, Help, Error };

/// @brief The parse result.
struct ParseResult {
    CommandLineStatus status{CommandLineStatus::Run}; ///< Run, show help, or a bad command line.
    Options options;                                  ///< Valid when status is Run.
    QString text;                                     ///< Help text, or the error message.
};

/// @brief Parses @p args (the program name first, as QCoreApplication::arguments()).
ParseResult parseOptions(const QStringList& args);

} // namespace mc::hil
