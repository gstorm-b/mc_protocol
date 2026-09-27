#include "harness.h"

#include <cstdio>

namespace mc::test {
namespace {

void putHex8(std::vector<uint8_t>& out, uint8_t v) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02X", v);
    out.push_back(static_cast<uint8_t>(buf[0]));
    out.push_back(static_cast<uint8_t>(buf[1]));
}

void putHex16(std::vector<uint8_t>& out, uint16_t v) {
    char buf[5];
    std::snprintf(buf, sizeof(buf), "%04X", v);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(buf[i]));
    }
}

void putU8(std::vector<uint8_t>& out, bool ascii, uint8_t v) {
    if (ascii) {
        putHex8(out, v);
    } else {
        out.push_back(v);
    }
}

void putU16(std::vector<uint8_t>& out, bool ascii, uint16_t v) {
    if (ascii) {
        putHex16(out, v);
    } else {
        out.push_back(static_cast<uint8_t>(v & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
}

// Codec-aware wire size of one endcode field (spec section 5.1: u16-sized, like every other
// field this frame envelope uses).
size_t endCodeFieldSize(bool ascii) { return ascii ? 4 : 2; }

// Builds the read data for a successful response, in `Codec`-aware wire form.
std::vector<uint8_t> buildReadData(const Request& r, bool ascii, const PeerScript& script) {
    std::vector<uint8_t> data;
    if (r.isWrite()) {
        return data; // A write's response carries no data (protocol.h's own "Payload contract").
    }
    if (!r.isBitOp()) {
        data.reserve(static_cast<size_t>(r.count) * (ascii ? 4 : 2));
        for (uint16_t i = 0; i < r.count; ++i) {
            uint16_t v = (i < script.words.size()) ? script.words[i] : 0;
            putU16(data, ascii, v);
        }
        return data;
    }
    // Bit read: ASCII is one '0'/'1' character per point; Binary is nibble-packed, 2 points per
    // byte, first point in the high nibble (spec section 2.2), odd count -> low nibble of the
    // last byte is 0.
    if (ascii) {
        data.reserve(r.count);
        for (uint16_t i = 0; i < r.count; ++i) {
            bool v = (i < script.bits.size()) && (script.bits[i] != 0);
            data.push_back(v ? uint8_t{'1'} : uint8_t{'0'});
        }
    } else {
        data.reserve((static_cast<size_t>(r.count) + 1) / 2);
        for (uint16_t i = 0; i < r.count; i += 2) {
            bool hi = (i < script.bits.size()) && (script.bits[i] != 0);
            bool lo = (i + 1 < r.count) &&
                      (static_cast<size_t>(i + 1) < script.bits.size()) &&
                      (script.bits[i + 1] != 0);
            data.push_back(static_cast<uint8_t>((hi ? 0x10u : 0u) | (lo ? 0x01u : 0u)));
        }
    }
    return data;
}

} // namespace

std::vector<uint8_t> ScriptedPeer::respond(const Request& r, const PeerScript& script) const {
    bool ascii = (m_cfg.code == DataCode::Ascii);
    std::vector<uint8_t> out;

    // Subheader D000 (spec section 5.1): the 3E response subheader, always this fixed value
    // regardless of the request's own command/subcommand.
    if (ascii) {
        out.insert(out.end(), {uint8_t{'D'}, uint8_t{'0'}, uint8_t{'0'}, uint8_t{'0'}});
    } else {
        out.insert(out.end(), {uint8_t{0xD0}, uint8_t{0x00}});
    }
    putU8(out, ascii, m_cfg.network);
    putU8(out, ascii, m_cfg.pc);
    putU16(out, ascii, m_cfg.io);
    putU8(out, ascii, m_cfg.station);

    uint16_t endCode = (script.kind == PeerScript::Kind::PlcError) ? script.plcEndCode : 0;
    std::vector<uint8_t> data =
        (script.kind == PeerScript::Kind::PlcError) ? std::vector<uint8_t>{}
                                                     : buildReadData(r, ascii, script);

    // Length: the field this response's own length-field covers, in codec-aware units (spec
    // section 5.1) -- endcode plus whatever data follows it. A PLC error response here never
    // carries the optional error-information block (protocol.h's Parser::feed() only needs the
    // length field readable to resolve `remaining < errorInfoSize`; `e.info` then simply stays
    // zero, which is a documented outcome, not a malformed frame).
    uint16_t length = static_cast<uint16_t>(endCodeFieldSize(ascii) + data.size());
    putU16(out, ascii, length);
    putU16(out, ascii, endCode);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

void OutputRecorder::drain(Session& session) {
    Output out;
    while (session.nextOutput(out)) {
        RecordedOutput r;
        r.kind = out.kind;
        if (out.bytes.size > 0) {
            r.bytes.assign(out.bytes.data, out.bytes.data + out.bytes.size);
        }
        r.deviceType = out.deviceType;
        r.round = out.round;
        if (out.changeCount > 0) {
            r.changes.assign(out.changes, out.changes + out.changeCount);
        }
        if (out.chunkCount > 0) {
            r.chunks.assign(out.chunks, out.chunks + out.chunkCount);
        }
        r.cycle = out.cycle;
        r.requestId = out.requestId;
        r.error = out.error;
        if (out.payload.size > 0) {
            r.payload.assign(out.payload.data, out.payload.data + out.payload.size);
        }
        r.fault = out.fault;
        r.reopenTransport = out.reopenTransport;
        m_items.push_back(std::move(r));
    }
}

} // namespace mc::test
