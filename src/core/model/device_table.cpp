// The constexpr device table (spec §3.2) and deviceInfo(). Content is transcribed verbatim from
// the reference spec, including footnotes: SM/SD have no 1E/1C code (footnote 1); L and S have
// no 1E code of their own and alias M's code (footnote 2, see FrameConfig::e1AliasLS, T-008); RD
// exists only for iQ-R (no QnA Q/L code).
#include "mc/core/device.h"

#include <cstddef>

namespace mc {
namespace detail {

// clang-format off
//                     type              symbol  kind              radix       asciiQL binQL  asciiIqr  binIqr  e1Code   c1Code
constexpr DeviceInfo kDeviceTable[] = {
    {DeviceType::SM,  "SM",  DeviceKind::Bit,  Radix::Dec, "SM", 0x91,   "SM**", 0x0091, kNoCode, ""   },
    {DeviceType::SD,  "SD",  DeviceKind::Word, Radix::Dec, "SD", 0xA9,   "SD**", 0x00A9, kNoCode, ""   },
    {DeviceType::X,   "X",   DeviceKind::Bit,  Radix::Hex, "X*", 0x9C,   "X***", 0x009C, 0x5820,  "X"  },
    {DeviceType::Y,   "Y",   DeviceKind::Bit,  Radix::Hex, "Y*", 0x9D,   "Y***", 0x009D, 0x5920,  "Y"  },
    {DeviceType::M,   "M",   DeviceKind::Bit,  Radix::Dec, "M*", 0x90,   "M***", 0x0090, 0x4D20,  "M"  },
    {DeviceType::L,   "L",   DeviceKind::Bit,  Radix::Dec, "L*", 0x92,   "L***", 0x0092, 0x4D20,  "L"  },
    {DeviceType::F,   "F",   DeviceKind::Bit,  Radix::Dec, "F*", 0x93,   "F***", 0x0093, 0x4620,  "F"  },
    {DeviceType::V,   "V",   DeviceKind::Bit,  Radix::Dec, "V*", 0x94,   "V***", 0x0094, kNoCode, ""   },
    {DeviceType::B,   "B",   DeviceKind::Bit,  Radix::Hex, "B*", 0xA0,   "B***", 0x00A0, 0x4220,  "B"  },
    {DeviceType::D,   "D",   DeviceKind::Word, Radix::Dec, "D*", 0xA8,   "D***", 0x00A8, 0x4420,  "D"  },
    {DeviceType::W,   "W",   DeviceKind::Word, Radix::Hex, "W*", 0xB4,   "W***", 0x00B4, 0x5720,  "W"  },
    {DeviceType::TS,  "TS",  DeviceKind::Bit,  Radix::Dec, "TS", 0xC1,   "TS**", 0x00C1, 0x5453,  "TS" },
    {DeviceType::TC,  "TC",  DeviceKind::Bit,  Radix::Dec, "TC", 0xC0,   "TC**", 0x00C0, 0x5443,  "TC" },
    {DeviceType::TN,  "TN",  DeviceKind::Word, Radix::Dec, "TN", 0xC2,   "TN**", 0x00C2, 0x544E,  "TN" },
    {DeviceType::STS, "STS", DeviceKind::Bit,  Radix::Dec, "SS", 0xC7,   "STS*", 0x00C7, kNoCode, ""   },
    {DeviceType::STC, "STC", DeviceKind::Bit,  Radix::Dec, "SC", 0xC6,   "STC*", 0x00C6, kNoCode, ""   },
    {DeviceType::STN, "STN", DeviceKind::Word, Radix::Dec, "SN", 0xC8,   "STN*", 0x00C8, kNoCode, ""   },
    {DeviceType::CS,  "CS",  DeviceKind::Bit,  Radix::Dec, "CS", 0xC4,   "CS**", 0x00C4, 0x4353,  "CS" },
    {DeviceType::CC,  "CC",  DeviceKind::Bit,  Radix::Dec, "CC", 0xC3,   "CC**", 0x00C3, 0x4343,  "CC" },
    {DeviceType::CN,  "CN",  DeviceKind::Word, Radix::Dec, "CN", 0xC5,   "CN**", 0x00C5, 0x434E,  "CN" },
    {DeviceType::SB,  "SB",  DeviceKind::Bit,  Radix::Hex, "SB", 0xA1,   "SB**", 0x00A1, kNoCode, ""   },
    {DeviceType::SW,  "SW",  DeviceKind::Word, Radix::Hex, "SW", 0xB5,   "SW**", 0x00B5, kNoCode, ""   },
    {DeviceType::S,   "S",   DeviceKind::Bit,  Radix::Dec, "S*", 0x98,   "S***", 0x0098, 0x4D20,  "S"  },
    {DeviceType::DX,  "DX",  DeviceKind::Bit,  Radix::Hex, "DX", 0xA2,   "DX**", 0x00A2, kNoCode, ""   },
    {DeviceType::DY,  "DY",  DeviceKind::Bit,  Radix::Hex, "DY", 0xA3,   "DY**", 0x00A3, kNoCode, ""   },
    {DeviceType::Z,   "Z",   DeviceKind::Word, Radix::Dec, "Z*", 0xCC,   "Z***", 0x00CC, kNoCode, ""   },
    {DeviceType::R,   "R",   DeviceKind::Word, Radix::Dec, "R*", 0xAF,   "R***", 0x00AF, 0x5220,  "R"  },
    {DeviceType::ZR,  "ZR",  DeviceKind::Word, Radix::Hex, "ZR", 0xB0,   "ZR**", 0x00B0, kNoCode, ""   },
    {DeviceType::RD,  "RD",  DeviceKind::Word, Radix::Dec, "",   kNoCode,"RD**", 0x002C, kNoCode, ""   },
};
// clang-format on

static_assert(std::size(kDeviceTable) == static_cast<size_t>(DeviceType::Count),
              "kDeviceTable must have exactly one row per DeviceType");

// A relaxed-constexpr loop (C++17) checking that every row's own `type` equals its index, i.e.
// the table is declared in DeviceType's own order: deviceInfo() below indexes it directly, with
// no separate lookup step.
constexpr bool rowIndexMatchesDeclarationOrder() {
    for (size_t i = 0; i < std::size(kDeviceTable); ++i) {
        if (static_cast<size_t>(kDeviceTable[i].type) != i) {
            return false;
        }
    }
    return true;
}
static_assert(rowIndexMatchesDeclarationOrder(),
              "kDeviceTable row order must match DeviceType declaration order");

} // namespace detail

// Not constexpr (see device.h): a constexpr/inline function's out-of-line body is emitted only
// in a translation unit that itself calls it in a potentially-evaluated expression, and nothing
// in this file does — every other translation unit would otherwise link against a symbol no
// object file ever emits (verified: MSVC LNK2019 across mc_core.lib and its callers).
const DeviceInfo& deviceInfo(DeviceType t) noexcept {
    return detail::kDeviceTable[static_cast<size_t>(t)];
}

} // namespace mc
