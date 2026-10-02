/**
 * @file dry_run.h
 * @brief `--dry-run` and the confirmation prompt: what the run would send, and what the operator
 * is asked to confirm.
 *
 * The dry-run text is meant to be read and to be parsed by tests. Every frame is one line
 *
 *     <four spaces>tx <upper-case hex bytes separated by spaces>
 *
 * and nothing else is on that line; the lines around it say which step and operation it belongs
 * to. API operations are encoded by `McProtocol` (a request wider than one command shows one
 * frame per chunk, as `McDevice` would split it), a mutate step shows its edited bytes, a raw step
 * its literal bytes, a poll step the read plan `ReadPlan` builds for its subscriptions plus its
 * writes. Nothing here opens a transport.
 */
#pragma once

#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/safety_gate.h"

#include <QString>

namespace mc::hil {

/// @brief The text `--dry-run` prints for the resolved plan.
QString dryRunText(const ResolveResult& resolved, const Profile& profile);

/// @brief The pre-run summary: PLC identity, transport, scratch area and every
/// read-only frame the gate could not decode.
/// @param[in] plcState RUN or STOP, as the operator set it by hand; the tool never changes it.
QString confirmationSummary(const Profile& profile, const GateReport& gate,
                            const ResolveResult& resolved,
                            const QString& plcState = QStringLiteral("RUN"));

} // namespace mc::hil
