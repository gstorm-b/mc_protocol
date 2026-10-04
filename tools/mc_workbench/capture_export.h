/**
 * @file capture_export.h
 * @brief Where a capture may be written: the rule that keeps captures of mocks and `virtual_plc`
 * out of `tests/vectors/captured/` (SPEC-gui-tool.md, "Capture and export").
 */
#pragma once

#include "mc_workbench/capture_types.h"

#include <QString>

namespace mc::workbench {

/// @brief The verdict of checkOutputTarget().
struct ExportDecision {
    bool allowed{false}; ///< The capture may be written there.
    QString reason;      ///< Why not; empty when allowed.
};

/**
 * @brief Decides whether a capture of @p source may be written under @p outputRoot.
 *
 * A folder at or under @p capturedRoot (the repository's `tests/vectors/captured/`) takes captures
 * of a real PLC only. The same holds for any path that contains the segments
 * `vectors/captured`, so the rule survives an unknown repository root. The folder is resolved by the
 * operating system (the deepest existing ancestor: links, junctions, 8.3 names, case); the part that
 * does not exist yet is normalised as Windows would (trailing dots and spaces dropped) and refused
 * when it holds `:`, `~`, a wildcard or a reserved device name. Segments are compared whole and
 * ignoring case. When anything cannot be resolved the answer is "refused", never "allowed".
 *
 * @param[in] outputRoot The capture folder itself (output root plus profile id), not the root.
 * @param[in] source What the capture was taken from.
 * @param[in] capturedRoot The protected folder; empty when unknown.
 * @return Allowed, or the reason it is refused.
 */
ExportDecision checkOutputTarget(const QString& outputRoot, CaptureSource source,
                                 const QString& capturedRoot);

/**
 * @brief Refuses a profile id or an output folder that ends in a dot or a space (Windows drops them,
 * so the folder written is not the one named). Defence in depth beside checkOutputTarget().
 * @param[in] outputRoot The output folder as typed.
 * @param[in] profileId The profile id, the name of the capture folder.
 * @return The reason it is refused; empty when both are fine.
 */
QString checkOutputInputs(const QString& outputRoot, const QString& profileId);

/// @brief Whether @p host names this computer (`localhost`, `127.x.x.x`, `::1`): not a real PLC.
/// @param[in] host Host text of a TCP transport.
/// @return true for a loopback address or name.
bool isLoopbackHost(const QString& host);

/**
 * @brief Looks for `tests/vectors/captured` in @p startDir and its parents.
 * @param[in] startDir Where to start, e.g. the application's folder.
 * @return The absolute path of the folder; empty when there is none.
 */
QString findCapturedRoot(const QString& startDir);

} // namespace mc::workbench
