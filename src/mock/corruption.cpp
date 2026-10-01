// The Corruption modes: damage one finished response, byte for byte as the mode's documentation in
// mc/mock/mock_plc.h says.
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"
#include "core/protocol/sumcheck.h"

#include <algorithm>

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

// The modes that damage a frame as a whole and read nothing of its layout, the same on every
// frame family. Returns false for any other mode.
bool damageWholeFrame(Corruption mode, ByteBuf& response) {
    switch (mode) {
    case Corruption::Truncate:
        response.pop_back();
        return true;
    case Corruption::JunkPrefix:
        response.insert(response.begin(), 3, uint8_t{0x55});
        return true;
    case Corruption::ExtraByte:
        response.push_back(0x00);
        return true;
    default:
        return false;
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
    if (damageWholeFrame(mode, response)) {
        return;
    }
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
    default:
        // WrongSumCheck is serial only (an Ethernet frame has no sum check) and WrongBlockNo is
        // serial format 2 only; the whole-frame modes were handled above.
        break;
    }
}

void corrupt1eResponse(Corruption mode, DataCode code, ByteBuf& response) {
    // A 1E response has no network, PC or station field, so WrongRoute has nothing to change. The
    // other modes act on the frame as a whole (its first byte, its end, both ends) and are the same
    // as on 3E; the first byte XOR 01H is never the subheader the client expects.
    if (mode != Corruption::WrongRoute) {
        corrupt3eResponse(mode, code, response);
    }
}

void corruptSerialResponse(const FrameConfig& cfg, Corruption mode, ByteBuf& response) {
    if (damageWholeFrame(mode, response)) {
        return;
    }

    // A serial response (spec §5.4-§5.6) is a start byte (STX, ACK or NAK), the block number in
    // format 2, the access route, then the rest. Where there is a SUM it follows the ETX, and CR LF
    // follows it in format 4. ASCII data holds no ETX, so the first one in the frame is the ETX.
    constexpr uint8_t kEtx = 0x03;
    const bool format2 = cfg.format == SerialFormat::Format2;
    const bool format4 = cfg.format == SerialFormat::Format4;
    const size_t routeAt = format2 ? 3 : 1;
    const auto etxAt = std::find(response.begin(), response.end(), kEtx);
    const size_t etx = static_cast<size_t>(etxAt - response.begin());
    const size_t crLf = format4 ? 2 : 0;
    const bool hasSum = etxAt != response.end() && response.size() == etx + 1 + 2 + crLf;

    // Every format sums from the byte after the first one up to and including the ETX.
    auto summed = [&]() { return ByteView{response.data() + 1, etx}; };
    auto sumAt = [&]() { return MutableByteView{response.data() + etx + 1, 2}; };

    switch (mode) {
    case Corruption::WrongSumCheck:
        if (hasSum) {
            (void)AsciiCodec::putU8(static_cast<uint8_t>(sumcheck(summed()) + 1), sumAt());
        }
        break;
    case Corruption::WrongRoute: {
        // 3C: frame ID, then station, network, PC (+1 each) and self-station (left alone). 1C:
        // station and PC.
        const bool threeC = cfg.frame == FrameType::F3C;
        const size_t station = routeAt + (threeC ? 2 : 0);
        incrementU8(DataCode::Ascii, response, station);
        incrementU8(DataCode::Ascii, response, station + 2); // 3C: network; 1C: PC
        if (threeC) {
            incrementU8(DataCode::Ascii, response, station + 4); // PC
        }
        if (hasSum) {
            (void)sumcheckEncode(summed(), sumAt()); // the route is the only damage
        }
        break;
    }
    case Corruption::WrongBlockNo:
        if (format2) {
            incrementU8(DataCode::Ascii, response, 1);
            if (hasSum) {
                (void)sumcheckEncode(summed(), sumAt()); // the block number is the only damage
            }
        }
        break;
    default:
        // WrongSubheader is for the Ethernet subheaders; the whole-frame modes were handled above.
        break;
    }
}

} // namespace mc::detail::mock
