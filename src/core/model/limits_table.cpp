// The constexpr transcription of spec §4.4 (v1 subset: Op::ReadBits/ReadWords/WriteBits/
// WriteWords only) and maxPoints(). See limits.h for what this function guarantees; see the
// bottom of this file for the random-access rows (0403, 1402), kept as real (internal-linkage-
// free, tested) data in mc::detail for v1.1 — see limits_table.h.
#include "mc/core/limits.h"

#include "limits_table.h"

#include <iterator>

namespace mc {
namespace {

// One named condition per row of spec §4.4 that v1's Op/DeviceKind combinations can reach.
enum class LimitCondition : uint8_t {
    // QnA family (3E/4E/3C/4C): "0401"/"1401" are the read/write commands (spec §4.4 "QnA
    // family" table).
    QnaWordDevice,          // 0401/1401 word, word device: Read and Write share this row.
    QnaReadBitDeviceWords,  // 0401 word, bit device (in words).
    QnaWriteBitDeviceWords, // 1401 word, bit device (in words).
    QnaReadBits3C,          // 0401 bit, C24 (3C/4C).
    QnaReadBits3EAscii,     // 0401 bit, E71 ASCII (3E/4E).
    QnaReadBits3EBinary,    // 0401 bit, E71 Binary (3E/4E).
    QnaWriteBits3C,         // 1401 bit, C24 (3C/4C).
    QnaWriteBits3EAscii,    // 1401 bit, E71 ASCII (3E/4E).
    QnaWriteBits3EBinary,   // 1401 bit, E71 Binary (3E/4E).
    // 1E family: one "Maximum" column only (no per-target-family split); from PDF Appendix 5,
    // not verified against the md files (spec §10.1 Q7).
    E1ReadBits,               // 00H.
    E1ReadWordsBitDeviceWords, // 01H, bit device (in words).
    E1ReadWordsWordDevice,     // 01H, word device.
    E1WriteBits,               // 02H.
    E1WriteWordsBitDeviceWords, // 03H, bit device (in words).
    E1WriteWordsWordDevice,     // 03H, word device.
    // 1C family: one "Maximum" column; cross-checked against the command pages, not flagged by
    // spec §10.1 Q7.
    C1ReadBits,               // BR/JR.
    C1ReadWordsBitDeviceWords, // WR/QR, bit device (in words).
    C1ReadWordsWordDevice,     // WR/QR, word device.
    C1WriteBits,               // BW/JW.
    C1WriteWordsBitDeviceWords, // WW/QW, bit device (in words).
    C1WriteWordsWordDevice,     // WW/QW, word device.
};

struct LimitRow {
    LimitCondition condition;
    uint16_t iqrQl; ///< iQ-R/Q/L column; the only column 1E and 1C rows populate.
    uint16_t qna;   ///< QnA column; 0 where spec §4.4 leaves the cell blank ("—").
    uint16_t a;     ///< A column; 0 where spec §4.4 leaves the cell blank ("—").
};

// clang-format off
constexpr LimitRow kLimitTable[] = {
    {LimitCondition::QnaWordDevice,             960, 480,  64},
    {LimitCondition::QnaReadBitDeviceWords,      960, 480,  32},
    {LimitCondition::QnaWriteBitDeviceWords,     960, 480,  10},
    {LimitCondition::QnaReadBits3C,             7904, 3952, 256},
    {LimitCondition::QnaReadBits3EAscii,        3584, 1792, 256},
    {LimitCondition::QnaReadBits3EBinary,       7168, 3584, 256},
    {LimitCondition::QnaWriteBits3C,            7904, 3952, 160},
    {LimitCondition::QnaWriteBits3EAscii,       3584, 1792, 160},
    {LimitCondition::QnaWriteBits3EBinary,      7168, 3584, 160},
    {LimitCondition::E1ReadBits,                  256,    0,   0},
    {LimitCondition::E1ReadWordsBitDeviceWords,   128,    0,   0},
    {LimitCondition::E1ReadWordsWordDevice,       256,    0,   0},
    {LimitCondition::E1WriteBits,                 256,    0,   0},
    {LimitCondition::E1WriteWordsBitDeviceWords,   40,    0,   0},
    {LimitCondition::E1WriteWordsWordDevice,       256,    0,   0},
    {LimitCondition::C1ReadBits,                   256,    0,   0},
    {LimitCondition::C1ReadWordsBitDeviceWords,     32,    0,   0},
    {LimitCondition::C1ReadWordsWordDevice,          64,    0,   0},
    {LimitCondition::C1WriteBits,                   160,    0,   0},
    {LimitCondition::C1WriteWordsBitDeviceWords,     10,    0,   0},
    {LimitCondition::C1WriteWordsWordDevice,         64,    0,   0},
};
// clang-format on

const LimitRow* findRow(LimitCondition condition) noexcept {
    for (const LimitRow& row : kLimitTable) {
        if (row.condition == condition) {
            return &row;
        }
    }
    return nullptr;
}

uint16_t columnFor(const LimitRow& row, TargetFamily family) noexcept {
    switch (family) {
    case TargetFamily::IqR_Q_L:
        return row.iqrQl;
    case TargetFamily::QnA:
        return row.qna;
    case TargetFamily::A:
        return row.a;
    }
    return 0;
}

} // namespace

uint16_t maxPoints(const FrameConfig& cfg, Op op, DeviceKind kind) noexcept {
    if (cfg.frame == FrameType::F1E) {
        LimitCondition condition;
        switch (op) {
        case Op::ReadBits:
            condition = LimitCondition::E1ReadBits;
            break;
        case Op::ReadWords:
            condition = (kind == DeviceKind::Bit) ? LimitCondition::E1ReadWordsBitDeviceWords
                                                   : LimitCondition::E1ReadWordsWordDevice;
            break;
        case Op::WriteBits:
            condition = LimitCondition::E1WriteBits;
            break;
        case Op::WriteWords:
            condition = (kind == DeviceKind::Bit) ? LimitCondition::E1WriteWordsBitDeviceWords
                                                   : LimitCondition::E1WriteWordsWordDevice;
            break;
        default:
            return 0;
        }
        const LimitRow* row = findRow(condition);
        return row ? row->iqrQl : 0;
    }

    if (cfg.frame == FrameType::F1C) {
        LimitCondition condition;
        switch (op) {
        case Op::ReadBits:
            condition = LimitCondition::C1ReadBits;
            break;
        case Op::ReadWords:
            condition = (kind == DeviceKind::Bit) ? LimitCondition::C1ReadWordsBitDeviceWords
                                                   : LimitCondition::C1ReadWordsWordDevice;
            break;
        case Op::WriteBits:
            condition = LimitCondition::C1WriteBits;
            break;
        case Op::WriteWords:
            condition = (kind == DeviceKind::Bit) ? LimitCondition::C1WriteWordsBitDeviceWords
                                                   : LimitCondition::C1WriteWordsWordDevice;
            break;
        default:
            return 0;
        }
        const LimitRow* row = findRow(condition);
        return row ? row->iqrQl : 0;
    }

    // QnA family: F3E/F4E (Ethernet, "E71") or F3C/F4C (serial, "C24"); F4E/F4C are reserved in
    // v1 but share the same §4.4 columns as F3E/F3C, so they are not special-cased out here.
    bool isEthernet = (cfg.frame == FrameType::F3E || cfg.frame == FrameType::F4E);
    bool isSerial = (cfg.frame == FrameType::F3C || cfg.frame == FrameType::F4C);
    if (!isEthernet && !isSerial) {
        return 0;
    }

    if (kind != DeviceKind::Bit && kind != DeviceKind::Word) {
        return 0; // DWord: no v1 device uses it; no spec §4.4 row covers it.
    }

    LimitCondition condition;
    if (op == Op::ReadWords || op == Op::WriteWords) {
        if (kind == DeviceKind::Word) {
            condition = LimitCondition::QnaWordDevice;
        } else {
            condition = (op == Op::ReadWords) ? LimitCondition::QnaReadBitDeviceWords
                                               : LimitCondition::QnaWriteBitDeviceWords;
        }
    } else if (op == Op::ReadBits) {
        if (kind != DeviceKind::Bit) {
            return 0;
        }
        if (isSerial) {
            condition = LimitCondition::QnaReadBits3C;
        } else {
            condition = (cfg.code == DataCode::Ascii) ? LimitCondition::QnaReadBits3EAscii
                                                       : LimitCondition::QnaReadBits3EBinary;
        }
    } else { // Op::WriteBits
        if (kind != DeviceKind::Bit) {
            return 0;
        }
        if (isSerial) {
            condition = LimitCondition::QnaWriteBits3C;
        } else {
            condition = (cfg.code == DataCode::Ascii) ? LimitCondition::QnaWriteBits3EAscii
                                                       : LimitCondition::QnaWriteBits3EBinary;
        }
    }

    const LimitRow* row = findRow(condition);
    if (!row) {
        return 0;
    }
    return columnFor(*row, cfg.targetFamily);
}

} // namespace mc

// ---------------------------------------------------------------------------------------------
// Random-access rows of spec §4.4 (QnA family "0403"/"1402"; 1E family "04H"/"05H"; 1C family
// "BT/JT"/"WT/QT"): real, tested data (limits_table.h), kept for v1.1 so
// ReadRandom/WriteRandomBits/WriteRandomWords need no re-transcription from the manual. Every
// non-blank cell of the reference tables is one row here; a family/column the reference table
// leaves blank ("—") for a command has no row at all (17 cells, not stored as 0 sentinels the
// way the v1 table above does, since these rows are never looked up by frame/op/kind the way
// kLimitTable is — there is no "not applicable" case to distinguish from "zero" for a caller to
// trip over).
//
// 0403 with ZR of a High Performance model QCPU (Q/L subcommand): each ZR point counts double
// before the m + n <= total check (spec §4.4 note; FrameConfig::highPerformanceQcpu). Not
// represented as data: it changes how m is computed by the caller, not the limit itself.
//
// The 1E rows (04H, 05H), like the v1 1E rows above, are transcribed from PDF Appendix 5 and
// have not gone through the reference document's own verification process (spec §10.1 Q7); the
// 1C rows (BT/JT, WT/QT) are not flagged.
namespace mc::detail {
namespace {

// clang-format off
constexpr RandomAccessLimitRow kRandomAccessLimitTable[] = {
    // command                                       family                  weightM  weightN  total
    {RandomAccessCommand::Read0403Sub0000,           TargetFamily::IqR_Q_L,  1,       1,       192}, // "0403 | m + n, sub 0000 | 192"
    {RandomAccessCommand::Read0403Sub0000,           TargetFamily::QnA,      1,       1,        96}, // "... | 96 |" (A: "—", no row)
    {RandomAccessCommand::Read0403Sub0002,           TargetFamily::IqR_Q_L,  1,       1,        96}, // "0403 | m + n, sub 0002 | 96" (QnA/A: "—", no rows)
    {RandomAccessCommand::Write1402WordSub0000,      TargetFamily::IqR_Q_L, 12,      14,      1920}, // "1402 word | m*12+n*14, sub 0000 | <=1920"
    {RandomAccessCommand::Write1402WordSub0000,      TargetFamily::QnA,     12,      14,       960}, // "... | <=960 |"
    {RandomAccessCommand::Write1402WordSub0000,      TargetFamily::A,       1,        0,        10}, // "... | m <= 10 |" (n absent: weightN = 0)
    {RandomAccessCommand::Write1402WordSub0002,      TargetFamily::IqR_Q_L, 12,      14,       960}, // "1402 word | m*12+n*14, sub 0002 | <=960" (QnA/A: "—", no rows)
    {RandomAccessCommand::Write1402BitSub0001,       TargetFamily::IqR_Q_L,  0,       1,       188}, // "1402 bit | n, sub 0001 | 188" (m absent: weightM = 0)
    {RandomAccessCommand::Write1402BitSub0001,       TargetFamily::QnA,      0,       1,        94}, // "... | 94 |"
    {RandomAccessCommand::Write1402BitSub0001,       TargetFamily::A,        0,       1,        20}, // "... | 20 |"
    {RandomAccessCommand::Write1402BitSub0003,       TargetFamily::IqR_Q_L,  0,       1,        94}, // "1402 bit | n, sub 0003 | 94" (QnA/A: "—", no rows)
    {RandomAccessCommand::E1WriteRandomBits,         TargetFamily::IqR_Q_L,  0,       1,        80}, // 1E "04H | — | 80 points"
    {RandomAccessCommand::E1WriteRandomWordsBitDevice,  TargetFamily::IqR_Q_L, 0,     1,        40}, // 1E "05H | bit device | 40 words"
    {RandomAccessCommand::E1WriteRandomWordsWordDevice, TargetFamily::IqR_Q_L, 0,     1,        40}, // 1E "05H | word device | 40 points"
    {RandomAccessCommand::C1WriteRandomBits,         TargetFamily::IqR_Q_L,  0,       1,        20}, // 1C "BT/JT | — | 20 points"
    {RandomAccessCommand::C1WriteRandomWordsBitDevice,  TargetFamily::IqR_Q_L, 0,     1,        10}, // 1C "WT/QT | bit device | 10 words"
    {RandomAccessCommand::C1WriteRandomWordsWordDevice, TargetFamily::IqR_Q_L, 0,     1,        10}, // 1C "WT/QT | word device | 10 points"
};
// clang-format on

} // namespace

size_t randomAccessLimitRowCount() noexcept {
    return std::size(kRandomAccessLimitTable);
}

const RandomAccessLimitRow& randomAccessLimitRow(size_t index) noexcept {
    return kRandomAccessLimitTable[index];
}

} // namespace mc::detail
