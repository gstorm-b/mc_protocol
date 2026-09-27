// Internal-only header (not under include/mc/, not part of mc_core's public surface): exposes
// the random-access rows of spec §4.4 (QnA family "0403"/"1402"; 1E family "04H"/"05H"; 1C family
// "BT/JT"/"WT/QT") so they can be both compiled (referenced from a real function, avoiding an
// unused-variable warning under GCC's -Wall) and unit-tested (LIM-03,
// tests/core/model/test_limits.cpp), without adding a public header or symbol. v1's Op has no
// ReadRandom/WriteRandomBits/WriteRandomWords value, so maxPoints() (limits.h) never reads this
// data; it exists purely so v1.1 needs no re-transcription from the manual.
#pragma once

#include "mc/core/frame_config.h" // mc::TargetFamily

#include <cstddef>
#include <cstdint>

namespace mc::detail {

/// Which random-access command/subcommand/count-shape a RandomAccessLimitRow describes (spec
/// §4.4's "QnA family" table, "0403"/"1402" rows; "1E family" table, "04H"/"05H" rows; "1C
/// family" table, "BT/JT"/"WT/QT" rows).
enum class RandomAccessCommand : uint8_t {
    Read0403Sub0000,      ///< 0403 (batch read random), subcommand 0000: word + dword points.
    Read0403Sub0002,      ///< 0403, subcommand 0002: word points only.
    Write1402WordSub0000, ///< 1402 (batch write random), word part, subcommand 0000.
    Write1402WordSub0002, ///< 1402, word part, subcommand 0002.
    Write1402BitSub0001,  ///< 1402, bit part, subcommand 0001.
    Write1402BitSub0003,  ///< 1402, bit part, subcommand 0003.
    E1WriteRandomBits,         ///< 1E 04H (write random, bits).
    E1WriteRandomWordsBitDevice,  ///< 1E 05H (write random, words), bit device.
    E1WriteRandomWordsWordDevice, ///< 1E 05H, word device.
    C1WriteRandomBits,         ///< 1C BT/JT (write random, bits).
    C1WriteRandomWordsBitDevice,  ///< 1C WT/QT (write random, words), bit device.
    C1WriteRandomWordsWordDevice, ///< 1C WT/QT, word device.
};

/// One (command, target family) cell of spec §4.4's random-access rows.
///
/// The manual's own table mixes two constraint shapes here: "0403" and "1402 word" cap a
/// *weighted sum* of two separate counts (m word/dword-list items, n bit-list items); "1402 bit"
/// caps a single count outright, and so does the "A" column's alternate rule for
/// `Write1402WordSub0000` ("m ≤ 10", unrelated to n). Rather than branching on a shape flag, a
/// zero weight simply drops that term from the sum, so every row reads as one formula:
/// `weightM * m + weightN * n <= total`.
///
/// The 1E and 1C families have one "Maximum" column each, not iQ-R/Q/L, QnA and A columns the
/// way the QnA family (0403/1402) does; their rows always use `TargetFamily::IqR_Q_L` for
/// `family`, matching how the v1 table (kLimitTable, same file) stores their single-column rows.
struct RandomAccessLimitRow {
    RandomAccessCommand command; ///< Which command/subcommand this cell belongs to.
    TargetFamily family;         ///< Target family this cell applies to.
    uint16_t weightM;            ///< Multiplier on m; 0 when m does not appear in this row's
                                  ///< constraint at all.
    uint16_t weightN;            ///< Multiplier on n; 0 when n does not appear at all.
    uint16_t total;              ///< Right-hand side: weightM * m + weightN * n <= total.
};

/// Number of rows randomAccessLimitRow() can index (one per non-blank cell of spec §4.4's
/// random-access tables; a family spec §4.4 leaves blank ("—") for a command has no row at all).
/// @par Complexity
/// O(1); no allocation.
size_t randomAccessLimitRowCount() noexcept;

/// One row of the random-access table.
/// @pre index < randomAccessLimitRowCount()
/// @par Complexity
/// O(1); no allocation.
const RandomAccessLimitRow& randomAccessLimitRow(size_t index) noexcept;

} // namespace mc::detail
