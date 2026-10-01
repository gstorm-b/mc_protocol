// Server direction of 3E, 1E, 3C and 1C (spec §5.1, §5.3 "Response", §5.4-§5.6): build the response
// frame of a request.
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"
#include "core/protocol/sumcheck.h"

#include <cstring>

namespace mc::detail::mock {

namespace {

// Spec §2.1.1 E1: the response subheader is a fixed byte sequence, not a u16.
constexpr uint8_t kSubheaderBinary[] = {0xD0, 0x00};
constexpr uint8_t kSubheaderAscii[] = {'D', '0', '0', '0'};

// Appends fields to a frame through the codec of the wire representation. Every put writes into a
// scratch buffer sized for the largest field, so it cannot fail.
template <class Codec> class Writer {
public:
    explicit Writer(ByteBuf& out) : m_out(out) {}

    void fixed(ByteView binary, ByteView ascii) {
        uint8_t tmp[8];
        auto r = Codec::putFixed(binary, ascii, MutableByteView{tmp, sizeof(tmp)});
        append(tmp, r.value());
    }

    void u8(uint8_t v) {
        uint8_t tmp[8];
        auto r = Codec::putU8(v, MutableByteView{tmp, sizeof(tmp)});
        append(tmp, r.value());
    }

    void u16(uint16_t v) {
        uint8_t tmp[8];
        auto r = Codec::putU16(v, MutableByteView{tmp, sizeof(tmp)});
        append(tmp, r.value());
    }

    // `words` is the normalized form: little-endian, two bytes per word.
    void words(ByteView normalized) {
        ByteBuf tmp(Codec::wordsSize(normalized.size / 2));
        auto r = Codec::putWords(normalized, MutableByteView{tmp.data(), tmp.size()});
        append(tmp.data(), r.value());
    }

    // `bits` is the normalized form: one byte per point, 0 or 1.
    void bits(ByteView normalized) {
        ByteBuf tmp(Codec::bitsSize(normalized.size));
        auto r = Codec::putBits(normalized, MutableByteView{tmp.data(), tmp.size()});
        append(tmp.data(), r.value());
    }

private:
    void append(const uint8_t* data, size_t size) { m_out.insert(m_out.end(), data, data + size); }

    ByteBuf& m_out;
};

template <class Codec>
ByteBuf buildResponse(const QnaRequest& req, const Outcome& outcome) {
    const ByteView subheaderBinary{kSubheaderBinary, sizeof(kSubheaderBinary)};
    const ByteView subheaderAscii{kSubheaderAscii, sizeof(kSubheaderAscii)};

    // Everything after the response data length: end code, then read data or error information.
    ByteBuf body;
    Writer<Codec> bodyWriter(body);
    if (outcome.ok) {
        bodyWriter.u16(0x0000);
        if (req.op == Op::ReadWords) {
            bodyWriter.words(ByteView{outcome.data.data(), outcome.data.size()});
        } else if (req.op == Op::ReadBits) {
            bodyWriter.bits(ByteView{outcome.data.data(), outcome.data.size()});
        }
    } else {
        bodyWriter.u16(outcome.plcCode);
        // Spec §5.1 field 8b: access route, command, subcommand. The route is the request's.
        bodyWriter.u8(req.route.network);
        bodyWriter.u8(req.route.pc);
        bodyWriter.u16(req.route.io);
        bodyWriter.u8(req.route.station);
        bodyWriter.u16(req.command);
        bodyWriter.u16(req.subcommand);
    }

    ByteBuf frame;
    Writer<Codec> writer(frame);
    writer.fixed(subheaderBinary, subheaderAscii);
    writer.u8(req.route.network);
    writer.u8(req.route.pc);
    writer.u16(req.route.io);
    writer.u8(req.route.station);
    writer.u16(static_cast<uint16_t>(body.size()));
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

template <class Codec>
ByteBuf buildResponse1e(const E1Request& req, const Outcome& outcome) {
    ByteBuf frame;
    Writer<Codec> writer(frame);
    // Spec §5.3: the response subheader is the command code OR 80H.
    writer.u8(static_cast<uint8_t>(req.command | 0x80u));
    if (!outcome.ok) {
        const uint8_t endCode = static_cast<uint8_t>(outcome.plcCode & 0xFFu);
        writer.u8(endCode);
        if (endCode == kE1EndCodeWithAbnormal) {
            writer.u8(outcome.abnormal);
        }
        return frame;
    }
    writer.u8(0x00);
    if (req.op == Op::ReadWords) {
        writer.words(ByteView{outcome.data.data(), outcome.data.size()});
    } else if (req.op == Op::ReadBits) {
        // Spec §4.2: an odd point count is padded to a whole byte (Binary, low nibble 0) or with
        // one dummy character (ASCII); both are one more zero point before the codec packs them.
        ByteBuf padded = outcome.data;
        if (padded.size() % 2 != 0) {
            padded.push_back(0);
        }
        writer.bits(ByteView{padded.data(), padded.size()});
    }
    return frame;
}

// Control codes (spec §2.7).
constexpr uint8_t kStx = 0x02;
constexpr uint8_t kEtx = 0x03;
constexpr uint8_t kAck = 0x06;
constexpr uint8_t kLf = 0x0A;
constexpr uint8_t kCr = 0x0D;
constexpr uint8_t kNak = 0x15;

} // namespace

ByteBuf build3eResponse(DataCode code, const QnaRequest& request, const Outcome& outcome) {
    return code == DataCode::Ascii ? buildResponse<AsciiCodec>(request, outcome)
                                   : buildResponse<BinaryCodec>(request, outcome);
}

ByteBuf build1eResponse(DataCode code, const E1Request& request, const Outcome& outcome) {
    return code == DataCode::Ascii ? buildResponse1e<AsciiCodec>(request, outcome)
                                   : buildResponse1e<BinaryCodec>(request, outcome);
}

// Spec §5.4 table (formats 1-4 of 4C) with the 3C access route of §5.5 or the 1C one of §5.6.
ByteBuf buildSerialResponse(const FrameConfig& cfg, const SerialRequest& req,
                            const Outcome& outcome) {
    const bool threeC = cfg.frame == FrameType::F3C;
    const bool hasData = outcome.ok && (req.op == Op::ReadWords || req.op == Op::ReadBits);

    ByteBuf frame;
    Writer<AsciiCodec> w(frame);
    auto text = [&frame](const char* s) {
        const size_t n = std::strlen(s);
        for (size_t k = 0; k < n; ++k) {
            frame.push_back(static_cast<uint8_t>(s[k]));
        }
    };
    // The access route of the request is echoed: frame ID, station, network, PC, self-station on
    // 3C; station and PC on 1C.
    auto route = [&]() {
        if (threeC) {
            text("F9");
            w.u8(req.station);
            w.u8(req.network);
            w.u8(req.pc);
            w.u8(req.selfStation);
        } else {
            w.u8(req.station);
            w.u8(req.pc);
        }
    };
    auto data = [&]() {
        const ByteView normalized{outcome.data.data(), outcome.data.size()};
        if (req.op == Op::ReadWords) {
            w.words(normalized);
        } else {
            w.bits(normalized);
        }
    };
    auto errorCode = [&]() {
        if (threeC) {
            w.u16(outcome.plcCode);
        } else {
            w.u8(static_cast<uint8_t>(outcome.plcCode & 0xFFu));
        }
    };
    // Every format sums from the byte after the first one to the end of what is there (spec
    // §2.5: after STX, or from the block number, including ETX).
    auto sum = [&]() {
        uint8_t digits[2];
        auto r = sumcheckEncode(ByteView{frame.data() + 1, frame.size() - 1},
                                MutableByteView{digits, sizeof(digits)});
        frame.insert(frame.end(), digits, digits + r.value());
    };

    if (cfg.format == SerialFormat::Format3) {
        // STX P end-code [data | error code] ETX [SUM]
        frame.push_back(kStx);
        route();
        text(outcome.ok ? (threeC ? "QACK" : "GG") : (threeC ? "QNAK" : "NN"));
        if (hasData) {
            data();
        } else if (!outcome.ok) {
            errorCode();
        }
        frame.push_back(kEtx);
        // Spec §10 Q1: the short responses carry no SUM as printed, unless the setting says so.
        if (cfg.sumCheck && (hasData || cfg.f3ShortResponseHasSum)) {
            sum();
        }
        return frame;
    }

    // Formats 1, 2 and 4: STX [BLK] P data ETX [SUM], ACK [BLK] P, or NAK [BLK] P error-code.
    // ACK and NAK carry no SUM.
    frame.push_back(hasData ? kStx : (outcome.ok ? kAck : kNak));
    if (cfg.format == SerialFormat::Format2) {
        w.u8(req.block);
    }
    route();
    if (hasData) {
        data();
        frame.push_back(kEtx);
        if (cfg.sumCheck) {
            sum();
        }
    } else if (!outcome.ok) {
        errorCode();
    }
    if (cfg.format == SerialFormat::Format4) {
        frame.push_back(kCr);
        frame.push_back(kLf);
    }
    return frame;
}

} // namespace mc::detail::mock
