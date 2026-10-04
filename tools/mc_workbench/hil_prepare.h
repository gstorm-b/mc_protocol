/**
 * @file hil_prepare.h
 * @brief The load, resolve and gate pipeline of the HIL runner view: the very code `hil_capture`
 * runs (`loadProfileFile`, `loadPlanFile`, `resolvePlan`, `checkGate`, `dryRunText`,
 * `confirmationSummary`), in the same order, with the same texts, and the confirmation rule.
 *
 * The GUI has no gate of its own. Whatever `checkGate()` refuses is refused here, and
 * `HilRunner` calls prepareHil() again on the runner thread right before a run, so a run never
 * rests on a verdict the GUI thread produced or on files that changed after the operator looked.
 */
#pragma once

#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/safety_gate.h"
#include "mc_workbench/hil_types.h"

namespace mc::workbench {

/// @brief A profile and a plan, loaded, resolved and passed through the safety gate.
struct HilPrepared {
    int exitCode{2};                 ///< `mc::hil::ExitCode`: 0 ok, 2 bad input, 3 gate refused.
    QString errorText;               ///< Bad input: one line per problem (no program-name prefix).
    mc::hil::Profile profile;        ///< Valid when the files loaded.
    mc::hil::ResolveResult resolved; ///< Valid when the plan resolved.
    mc::hil::GateReport gate;        ///< The gate's verdict (when the plan resolved).
    QString refusalText;             ///< `mc::hil::refusalText(gate)` when refused.
    QString dryRunText;              ///< `mc::hil::dryRunText(...)` when the gate passed.
    QString confirmationText;        ///< `mc::hil::confirmationSummary(...)` when the gate passed.
    QString planId;                  ///< The plan's id.

    /// @brief Whether the gate passed.
    bool ok() const { return exitCode == 0; }

    /// @brief The value copy that crosses to the GUI thread.
    /// @return Texts, counts and the digest; no pointers.
    HilCheckResult toResult() const;
};

/**
 * @brief Loads, resolves and gates, as `runTool()` does before it prints the dry run.
 * @param[in] input Profile, plan, groups and PLC state.
 * @return The outcome; never throws for a bad file.
 */
HilPrepared prepareHil(const HilCheckInput& input);

/// @brief Whether "confirm without typing" may be offered and honoured for @p check: the profile
/// talks TCP to this computer (loopback host) and the run holds no read-only frame.
/// @param[in] check The check the operator looked at.
/// @return true only for a loopback profile without read-only frames.
bool skipTypingAllowed(const HilCheckResult& check);

/**
 * @brief The confirmation rule of `runTool()`, narrowed for the GUI: the profile id must be typed,
 * except when the run is repeated with `--yes`, holds no read-only frame and the profile is a
 * loopback TCP one (`skipTypingAllowed()`). Another host or a COM port always needs the id.
 * @param[in] check The check the operator looked at.
 * @param[in] typedId What was typed (surrounding blanks ignored).
 * @param[in] skipTyping The operator asked to confirm without typing (`--yes`).
 * @param[out] why Why it does not hold (may be null).
 * @return true when the run is confirmed.
 */
bool confirmationAccepted(const HilCheckResult& check, const QString& typedId, bool skipTyping,
                          QString* why = nullptr);

/// @brief Whether a confirmation must be typed for @p check (always, when read-only frames exist
/// or the profile is not a loopback TCP one).
/// @param[in] check The check.
/// @param[in] skipTyping The operator's `--yes`.
/// @return true when the profile id has to be typed.
bool mustTypeProfileId(const HilCheckResult& check, bool skipTyping);

} // namespace mc::workbench
