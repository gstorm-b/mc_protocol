/**
 * @file tool.h
 * @brief The program behind `main()`: load, resolve, gate, confirm, run. Kept apart from
 * `main.cpp` so the tests can call it with their own streams.
 */
#pragma once

#include "hil_capture/options.h"

#include <QString>
#include <QTextStream>

#include <functional>

namespace mc::hil {

/// @brief The streams the tool talks through.
struct ToolIo {
    QTextStream* out{nullptr}; ///< Normal output.
    QTextStream* err{nullptr}; ///< Errors and refusals.
    QTextStream* in{nullptr};  ///< Operator input (the confirmation prompt).
    /// Called with the id of each step just before it starts (tests use it to break the link).
    std::function<void(const QString&)> stepStarted{};
};

/// @brief Runs the tool for @p options.
/// @param[in] options The parsed command line.
/// @param[in] io The streams to use.
/// @return The process exit code.
ExitCode runTool(const Options& options, const ToolIo& io);

} // namespace mc::hil
