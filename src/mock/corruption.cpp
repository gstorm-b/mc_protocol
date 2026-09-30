// The Corruption modes: damage one finished response, byte for byte as the mode's documentation in
// mc/mock/mock_plc.h says.
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"

namespace mc::detail::mock {

namespace {

// Adds one to the u8 field at `offset`, in the wire representation `code` (wraps at 0xFF).
void incrementU8(DataCode code, ByteBuf& frame, size_t offset) {
    if (code == DataCode::Ascii) {
        auto v = AsciiCodec::getU8(ByteView{frame.data() + offset, frame.size() - offset});
        if (v.hasValue()) {
            (void)AsciiCodec::putU8(static_cast<uint8_t>(v.value() + 1),
                                    MutableByteView{frame.data() + offset, AsciiCodec::u8Size()});
        }
    } else {
        ++frame[offset];
    }
}

// 3E response header (spec §5.1): subheader, network, PC, I/O, station, length. Offsets of the
// route fields it starts with.
struct RouteOffsets {
    size_t network;
    size_t pc;
    size_t station;
};

RouteOffsets routeOffsets3e(DataCode code) {
    if (code == DataCode::Ascii) {
        return RouteOffsets{4, 6, 12}; // "D000" + "NN" "PP" "IIII" "SS"
    }
    return RouteOffsets{2, 3, 6}; // D0 00, NN, PP, II II, SS
}

} // namespace

void corrupt3eResponse(Corruption mode, DataCode code, ByteBuf& response) {
    switch (mode) {
    case Corruption::WrongSubheader:
        if (code == DataCode::Ascii) {
            response[0] = 'E'; // "D000" -> "E000"
        } else {
            response[0] = static_cast<uint8_t>(response[0] ^ 0x01u); // D0 -> D1
        }
        break;
    case Corruption::WrongRoute: {
        const RouteOffsets at = routeOffsets3e(code);
        incrementU8(code, response, at.network);
        incrementU8(code, response, at.pc);
        incrementU8(code, response, at.station);
        break;
    }
    case Corruption::Truncate:
        response.pop_back();
        break;
    case Corruption::JunkPrefix:
        response.insert(response.begin(), 3, uint8_t{0x55});
        break;
    case Corruption::ExtraByte:
        response.push_back(0x00);
        break;
    case Corruption::WrongSumCheck: // serial only: an Ethernet frame has no sum check
    case Corruption::WrongBlockNo:  // serial format 2 only
        break;
    }
}

} // namespace mc::detail::mock
