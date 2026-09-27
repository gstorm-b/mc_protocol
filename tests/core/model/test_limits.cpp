#include "doctest/doctest.h"

#include "mc/core/limits.h"

// Private, src/-only header (not part of mc_core's public surface): exposes the random-access
// rows of spec §4.4 for LIM-03 below. tests/CMakeLists.txt gives this binary its own include
// path to src/ so this resolves.
#include "core/model/limits_table.h"

using mc::DataCode;
using mc::DeviceKind;
using mc::FrameConfig;
using mc::FrameType;
using mc::Op;
using mc::TargetFamily;

namespace {

// Builds the FrameConfig a QnA-family cell needs: F3C is always Ascii (frame3C() forces it);
// F3E takes whichever code the cell names (E71 ASCII vs E71 Binary are separate table rows).
FrameConfig qnaConfig(FrameType frame, DataCode code, TargetFamily family) {
    FrameConfig cfg =
        (frame == FrameType::F3C) ? FrameConfig::frame3C() : FrameConfig::frame3E(code);
    cfg.targetFamily = family;
    return cfg;
}

struct Cell {
    const char* label;
    FrameType frame;
    DataCode code; // ignored when frame == F3C.
    Op op;
    DeviceKind kind;
    TargetFamily family;
    uint16_t expected;
};

} // namespace

TEST_CASE("LIM-01 QnA family (spec section 4.4): every cell of the v1 table") {
    // clang-format off
    const Cell cells[] = {
        // "0401/1401 word | word device": Read and Write share this row.
        {"0401 word / word device, IqR_Q_L", FrameType::F3E, DataCode::Binary, Op::ReadWords,  DeviceKind::Word, TargetFamily::IqR_Q_L, 960},
        {"0401 word / word device, QnA",     FrameType::F3E, DataCode::Binary, Op::ReadWords,  DeviceKind::Word, TargetFamily::QnA,      480},
        {"0401 word / word device, A",       FrameType::F3E, DataCode::Binary, Op::ReadWords,  DeviceKind::Word, TargetFamily::A,         64},
        {"1401 word / word device, IqR_Q_L", FrameType::F3E, DataCode::Binary, Op::WriteWords, DeviceKind::Word, TargetFamily::IqR_Q_L, 960},
        {"1401 word / word device, QnA",     FrameType::F3E, DataCode::Binary, Op::WriteWords, DeviceKind::Word, TargetFamily::QnA,      480},
        {"1401 word / word device, A",       FrameType::F3E, DataCode::Binary, Op::WriteWords, DeviceKind::Word, TargetFamily::A,         64},

        // "0401 word | bit device" (words): ReadWords on a bit device.
        {"0401 word / bit device, IqR_Q_L", FrameType::F3E, DataCode::Binary, Op::ReadWords, DeviceKind::Bit, TargetFamily::IqR_Q_L, 960},
        {"0401 word / bit device, QnA",     FrameType::F3E, DataCode::Binary, Op::ReadWords, DeviceKind::Bit, TargetFamily::QnA,      480},
        {"0401 word / bit device, A",       FrameType::F3E, DataCode::Binary, Op::ReadWords, DeviceKind::Bit, TargetFamily::A,         32},

        // "1401 word | bit device" (words): WriteWords on a bit device.
        {"1401 word / bit device, IqR_Q_L", FrameType::F3E, DataCode::Binary, Op::WriteWords, DeviceKind::Bit, TargetFamily::IqR_Q_L, 960},
        {"1401 word / bit device, QnA",     FrameType::F3E, DataCode::Binary, Op::WriteWords, DeviceKind::Bit, TargetFamily::QnA,      480},
        {"1401 word / bit device, A",       FrameType::F3E, DataCode::Binary, Op::WriteWords, DeviceKind::Bit, TargetFamily::A,         10},

        // "0401 bit | C24 (4C/3C)".
        {"0401 bit / C24, IqR_Q_L", FrameType::F3C, DataCode::Ascii, Op::ReadBits, DeviceKind::Bit, TargetFamily::IqR_Q_L, 7904},
        {"0401 bit / C24, QnA",     FrameType::F3C, DataCode::Ascii, Op::ReadBits, DeviceKind::Bit, TargetFamily::QnA,     3952},
        {"0401 bit / C24, A",       FrameType::F3C, DataCode::Ascii, Op::ReadBits, DeviceKind::Bit, TargetFamily::A,       256},

        // "0401 bit | E71 ASCII".
        {"0401 bit / E71 ASCII, IqR_Q_L", FrameType::F3E, DataCode::Ascii, Op::ReadBits, DeviceKind::Bit, TargetFamily::IqR_Q_L, 3584},
        {"0401 bit / E71 ASCII, QnA",     FrameType::F3E, DataCode::Ascii, Op::ReadBits, DeviceKind::Bit, TargetFamily::QnA,     1792},
        {"0401 bit / E71 ASCII, A",       FrameType::F3E, DataCode::Ascii, Op::ReadBits, DeviceKind::Bit, TargetFamily::A,       256},

        // "0401 bit | E71 Binary".
        {"0401 bit / E71 Binary, IqR_Q_L", FrameType::F3E, DataCode::Binary, Op::ReadBits, DeviceKind::Bit, TargetFamily::IqR_Q_L, 7168},
        {"0401 bit / E71 Binary, QnA",     FrameType::F3E, DataCode::Binary, Op::ReadBits, DeviceKind::Bit, TargetFamily::QnA,     3584},
        {"0401 bit / E71 Binary, A",       FrameType::F3E, DataCode::Binary, Op::ReadBits, DeviceKind::Bit, TargetFamily::A,       256},

        // "1401 bit | C24 / E71 ASCII / E71 Binary": the A column (160) is shared by all three.
        {"1401 bit / C24, IqR_Q_L",        FrameType::F3C, DataCode::Ascii,  Op::WriteBits, DeviceKind::Bit, TargetFamily::IqR_Q_L, 7904},
        {"1401 bit / C24, QnA",            FrameType::F3C, DataCode::Ascii,  Op::WriteBits, DeviceKind::Bit, TargetFamily::QnA,     3952},
        {"1401 bit / C24, A",              FrameType::F3C, DataCode::Ascii,  Op::WriteBits, DeviceKind::Bit, TargetFamily::A,       160},
        {"1401 bit / E71 ASCII, IqR_Q_L",  FrameType::F3E, DataCode::Ascii,  Op::WriteBits, DeviceKind::Bit, TargetFamily::IqR_Q_L, 3584},
        {"1401 bit / E71 ASCII, QnA",      FrameType::F3E, DataCode::Ascii,  Op::WriteBits, DeviceKind::Bit, TargetFamily::QnA,     1792},
        {"1401 bit / E71 ASCII, A",        FrameType::F3E, DataCode::Ascii,  Op::WriteBits, DeviceKind::Bit, TargetFamily::A,       160},
        {"1401 bit / E71 Binary, IqR_Q_L", FrameType::F3E, DataCode::Binary, Op::WriteBits, DeviceKind::Bit, TargetFamily::IqR_Q_L, 7168},
        {"1401 bit / E71 Binary, QnA",     FrameType::F3E, DataCode::Binary, Op::WriteBits, DeviceKind::Bit, TargetFamily::QnA,     3584},
        {"1401 bit / E71 Binary, A",       FrameType::F3E, DataCode::Binary, Op::WriteBits, DeviceKind::Bit, TargetFamily::A,       160},
    };
    // clang-format on

    for (const Cell& cell : cells) {
        SUBCASE(cell.label) {
            FrameConfig cfg = qnaConfig(cell.frame, cell.code, cell.family);
            CHECK(mc::maxPoints(cfg, cell.op, cell.kind) == cell.expected);
        }
    }
}

TEST_CASE("LIM-01 1C family (spec section 4.4): every cell of the v1 table") {
    FrameConfig cfg = FrameConfig::frame1C();

    CHECK(mc::maxPoints(cfg, Op::ReadBits, DeviceKind::Bit) == 256);   // BR/JR.
    CHECK(mc::maxPoints(cfg, Op::ReadWords, DeviceKind::Bit) == 32);   // WR/QR, bit device (words).
    CHECK(mc::maxPoints(cfg, Op::ReadWords, DeviceKind::Word) == 64);  // WR/QR, word device.
    CHECK(mc::maxPoints(cfg, Op::WriteBits, DeviceKind::Bit) == 160);  // BW/JW.
    CHECK(mc::maxPoints(cfg, Op::WriteWords, DeviceKind::Bit) == 10);  // WW/QW, bit device (words).
    CHECK(mc::maxPoints(cfg, Op::WriteWords, DeviceKind::Word) == 64); // WW/QW, word device.
}

TEST_CASE("maxPoints: ReadBits/WriteBits on a word device has no spec section 4.4 cell "
          "(Checkpoint A coverage gap)") {
    // maxPoints() is a public, standalone function: a caller can ask for a combination
    // validate() would never let through (a bit-op Op paired with DeviceKind::Word), and it must
    // report "no such cell" (0) rather than fabricate a number. No real (Request, FrameConfig)
    // pair reaches this via chunk()/chunkCount(), since validate() rejects it first (DEV-13); this
    // test calls maxPoints() directly to reach it.
    FrameConfig cfg = FrameConfig::frame3E();
    CHECK(mc::maxPoints(cfg, Op::ReadBits, DeviceKind::Word) == 0);
    CHECK(mc::maxPoints(cfg, Op::WriteBits, DeviceKind::Word) == 0);
}

TEST_CASE("LIM-02 1E family (PDF Appendix 5, not md-verified per spec section 10.1 Q7)") {
    FrameConfig cfg = FrameConfig::frame1E();

    CHECK(mc::maxPoints(cfg, Op::ReadBits, DeviceKind::Bit) == 256);    // 00H.
    CHECK(mc::maxPoints(cfg, Op::ReadWords, DeviceKind::Bit) == 128);   // 01H, bit device (words).
    CHECK(mc::maxPoints(cfg, Op::ReadWords, DeviceKind::Word) == 256);  // 01H, word device.
    CHECK(mc::maxPoints(cfg, Op::WriteBits, DeviceKind::Bit) == 256);   // 02H.
    CHECK(mc::maxPoints(cfg, Op::WriteWords, DeviceKind::Bit) == 40);   // 03H, bit device (words).
    CHECK(mc::maxPoints(cfg, Op::WriteWords, DeviceKind::Word) == 256); // 03H, word device.
}

TEST_CASE("LIM-03 Random-access rows (spec section 4.4, kept for v1.1): every non-blank cell") {
    using mc::detail::RandomAccessCommand;
    using mc::detail::RandomAccessLimitRow;

    // Values typed fresh from spec section 4.4's "0403"/"1402" (QnA), "04H"/"05H" (1E) and
    // "BT/JT"/"WT/QT" (1C) rows, not copied from limits_table.cpp: weightM/weightN are the
    // multipliers of "weightM*m + weightN*n <= total" (0 when that term is absent from the
    // manual's own condition); a family the reference table leaves blank ("-") for a row has no
    // entry here at all -- there are 17 non-blank cells across the twelve rows, not one cell per
    // row per family. The 1E rows are from PDF Appendix 5, not verified against the md files
    // (spec section 10.1 Q7), same as the v1 1E rows in LIM-02 above.
    struct ExpectedCell {
        const char* label;
        RandomAccessCommand command;
        TargetFamily family;
        uint16_t weightM;
        uint16_t weightN;
        uint16_t total;
    };

    // clang-format off
    const ExpectedCell expected[] = {
        {"0403 m+n sub0000, IqR_Q_L",     RandomAccessCommand::Read0403Sub0000,      TargetFamily::IqR_Q_L,  1,  1,  192},
        {"0403 m+n sub0000, QnA",         RandomAccessCommand::Read0403Sub0000,      TargetFamily::QnA,      1,  1,   96},
        {"0403 m+n sub0002, IqR_Q_L",     RandomAccessCommand::Read0403Sub0002,      TargetFamily::IqR_Q_L,  1,  1,   96},
        {"1402 word sub0000, IqR_Q_L",    RandomAccessCommand::Write1402WordSub0000, TargetFamily::IqR_Q_L, 12, 14, 1920},
        {"1402 word sub0000, QnA",        RandomAccessCommand::Write1402WordSub0000, TargetFamily::QnA,     12, 14,  960},
        {"1402 word sub0000, A (m<=10)",  RandomAccessCommand::Write1402WordSub0000, TargetFamily::A,        1,  0,   10},
        {"1402 word sub0002, IqR_Q_L",    RandomAccessCommand::Write1402WordSub0002, TargetFamily::IqR_Q_L, 12, 14,  960},
        {"1402 bit sub0001, IqR_Q_L",     RandomAccessCommand::Write1402BitSub0001,  TargetFamily::IqR_Q_L,  0,  1,  188},
        {"1402 bit sub0001, QnA",         RandomAccessCommand::Write1402BitSub0001,  TargetFamily::QnA,      0,  1,   94},
        {"1402 bit sub0001, A",           RandomAccessCommand::Write1402BitSub0001,  TargetFamily::A,        0,  1,   20},
        {"1402 bit sub0003, IqR_Q_L",     RandomAccessCommand::Write1402BitSub0003,  TargetFamily::IqR_Q_L,  0,  1,   94},
        {"1E 04H",                        RandomAccessCommand::E1WriteRandomBits,             TargetFamily::IqR_Q_L, 0, 1, 80},
        {"1E 05H bit device",             RandomAccessCommand::E1WriteRandomWordsBitDevice,   TargetFamily::IqR_Q_L, 0, 1, 40},
        {"1E 05H word device",            RandomAccessCommand::E1WriteRandomWordsWordDevice,  TargetFamily::IqR_Q_L, 0, 1, 40},
        {"1C BT/JT",                      RandomAccessCommand::C1WriteRandomBits,             TargetFamily::IqR_Q_L, 0, 1, 20},
        {"1C WT/QT bit device",           RandomAccessCommand::C1WriteRandomWordsBitDevice,   TargetFamily::IqR_Q_L, 0, 1, 10},
        {"1C WT/QT word device",          RandomAccessCommand::C1WriteRandomWordsWordDevice,  TargetFamily::IqR_Q_L, 0, 1, 10},
    };
    // clang-format on
    const size_t expectedCount = sizeof(expected) / sizeof(expected[0]);

    CHECK(mc::detail::randomAccessLimitRowCount() == expectedCount);

    for (const ExpectedCell& cell : expected) {
        SUBCASE(cell.label) {
            bool found = false;
            for (size_t i = 0; i < mc::detail::randomAccessLimitRowCount(); ++i) {
                const RandomAccessLimitRow& row = mc::detail::randomAccessLimitRow(i);
                if (row.command == cell.command && row.family == cell.family) {
                    found = true;
                    CHECK(row.weightM == cell.weightM);
                    CHECK(row.weightN == cell.weightN);
                    CHECK(row.total == cell.total);
                    break;
                }
            }
            CHECK(found);
        }
    }
}
