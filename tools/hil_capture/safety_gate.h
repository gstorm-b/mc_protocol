/**
 * @file safety_gate.h
 * @brief The safety gate (spec decision H2): before anything connects, every write the run could
 * make is checked against the profile's scratch area. The rule is default deny: no command goes
 * out unless it is an allowed read, or a write that lies inside scratch.
 *
 * What is checked, per step that is not skipped:
 *  - write operations and bench writes: head ... head + count - 1 lies inside ONE scratch range of
 *    that device type (a word write to a bit device covers 16 numbers per word);
 *  - poll steps: the heartbeat and every ad-hoc write; a subscription must lie in scratch too
 *    unless the plan marks it `"input": true` (a subscription only reads);
 *  - the profile's own session heartbeat, when enabled;
 *  - the `frameOverride` of the step (below);
 *  - mutate and raw frames, with the accounting below.
 *
 * Mutate and raw frames, every one, `readOnly` or not. The bytes the operation sends are searched
 * for commands by one locator per frame family, at every place a frame could start (a locator
 * finds a command, it does not trust the plan):
 *  - Ethernet: 3E and 4E subheaders, Binary and ASCII, at every offset where a whole frame fits
 *    (so a length field that swallows a second frame, or junk in front of one, hides nothing),
 *    and at the start of the bytes even when the command is cut off; plus, on a 1E profile, the
 *    1E command at the start and after every read request (1E has no start mark and no length
 *    field; a read has a fixed size of 12 bytes, 24 characters in ASCII). The command field of
 *    3E is bytes 11-12 of `50 00`, of 4E bytes 15-16 of `54 00 ss ss 00 00`, of the ASCII forms
 *    characters 22-25 and 30-33 (reference spec 5.1, 5.2); 1E the first byte, two characters
 *    (5.3).
 *  - Serial: every ENQ and every STX starts a frame, so an EOT before it or junk in front of it
 *    hides nothing. A frame id `F9` (3C, an 8 character route) or `F8` (4C, a 14 character
 *    route) is followed by a 4 character command; a frame without one is 1C (station and PC No.,
 *    then a 2 letter command). All four formats are understood, sum check on or off: format 2
 *    adds a block number, and a frame id is looked for with and without it. A port is searched
 *    for 3C, 4C and 1C frames whatever its profile says (reference spec 5.4-5.6).
 *
 * Each located command must be one of
 *  - a read-only command of its family: 0401 batch read (reference spec 4.1.1), 0403 random read
 *    (4.1.3) and 0406 batch read of multiple blocks (4.5) for 3E, 4E, 3C and 4C; 00H and 01H
 *    batch reads for 1E (4.2); BR, WR and the AnA/AnU letters JR, QR for 1C (4.3). Everything
 *    else is no read: 1401/1402/1406 writes, 0801/0802 monitor registration, 0619 loopback and
 *    0101 CPU model (neither is in the reference spec; a step that needs one waits until the
 *    spec lists it), 1E 02H-05H and every other code up to 3CH, 1C BW WW BT WT JW QW JT QT and
 *    any other letters; or
 *  - a write that the gate's `mc::MockPlc`, configured like the profile, decodes at that same
 *    offset, and whose points (words or bits) lie inside one scratch range of its device type.
 *
 * Whatever is neither refuses the whole run, naming the step, the command and its byte offset: an
 * unknown command, a write the mock cannot decode (1402, 1E 04H/05H, 1C BT/WT, any future write),
 * a frame of another family than the profile's, a frame that is cut off right after its command.
 * A frame declared `readOnly: true` is held to more: it may carry read commands only, so a write
 * is refused there even inside scratch, and a mutate whose base request is a write can never be
 * declared `readOnly`.
 *
 * A frame in which no command can be located is a fragment (bytes that are no header, a 3E
 * subheader 51H, a command cut off before its last byte, a 1E first byte above 3CH, the highest
 * 1E code in the reference spec's scope line, in a frame no longer than one read request). So is
 * a frame the mock cannot decode completely, or that ends half received (a sentinel read fed
 * after it is not decoded as that read). A fragment must be declared `readOnly: true` and name
 * the recovery that throws it away before the next frame goes out: `reconnect` on Ethernet, `eot`
 * on a serial line. The declared ones are listed for the confirmation prompt, which `--yes` does
 * not skip while the list is not empty. The mock's own view is a second net: every write it
 * decodes from any offset of the bytes is checked against scratch.
 *
 * frameOverride. A raw or mutate step may not change the frame, code, format, sum check or the
 * routing (network, pc, io, station, stationNo, selfStation) of the profile: the gate reads its
 * bytes under the profile's frame, the PLC reads them as they are. A step that writes (a write, a
 * `then` write, a poll with a heartbeat or a write action, a bench of a write) may not change the
 * routing either: a write must reach the PLC whose scratch the profile declares. Other keys stay
 * allowed (plan.h lists them); a key that keeps the profile's value changes nothing.
 *
 * Residual risk, documented and not closed:
 *  - the mock decodes like the library's reference model, not like a particular PLC; a command
 *    on the read list is trusted to be only a read;
 *  - the route fields written into raw or edited frame bytes are not compared with the profile's,
 *    so hand-written bytes could address another station (a plan author writing such a step
 *    does it on purpose; the override keys, which are the plan's documented way, are refused);
 *  - a 1E frame on a 3E, 4E or serial profile, and the binary serial format 5 (rejected by the
 *    profile loader), are not searched for: those ports take no such frame;
 *  - a 4E header whose reserved bytes are not `00 00` (Binary, or `0000` in ASCII) is not located:
 *    it shows no command and is a fragment, so it needs `readOnly` and the recovery;
 *  - a frame whose start is garbled so that no locator finds it shows no command and is a
 *    fragment; a PLC that still executed it would be outside this model;
 *  - write data that happens to look like a frame start can make a legitimate frame be refused,
 *    never admitted.
 *
 * Any violation refuses the whole run. There is no switch, option, environment variable or plan
 * key that turns the gate off: `checkGate()` has no parameter for it, and `runTool()` calls it
 * before it looks at `--yes`.
 */
#pragma once

#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"

#include <QString>
#include <QVector>

namespace mc::hil {

/// @brief One thing the gate refuses.
struct GateViolation {
    QString stepId; ///< Step (or operation) id.
    QString kind;   ///< "write", "poll heartbeat", "poll write", "poll subscription", "mutate", ...
    QString range;  ///< The resolved range, e.g. "D100-D2100 (2001 points)".
    QString why;    ///< Why it is refused.

    /// @brief One line for the console.
    QString text() const;
};

/// @brief A frame the mock could not decode but the plan declared read-only.
struct UndecodedFrame {
    QString stepId; ///< Operation id.
    QString what;   ///< Description of the frame.
    QString hex;    ///< The bytes.
};

/// @brief The gate's verdict.
struct GateReport {
    QVector<GateViolation> violations;      ///< Everything refused; empty means the run may go on.
    QVector<UndecodedFrame> readOnlyFrames; ///< Declared read-only, not decodable by the mock.

    /// @brief Whether the run is refused.
    bool refused() const { return !violations.isEmpty(); }
};

/// @brief Checks every write of the resolved plan against the profile.
/// @param[in] resolved The resolved plan (skipped steps are ignored: they send nothing).
/// @param[in] profile The profile with the scratch area.
GateReport checkGate(const ResolveResult& resolved, const Profile& profile);

/// @brief The refusal text: every offender with step id, kind, resolved range and reason.
QString refusalText(const GateReport& report);

} // namespace mc::hil
