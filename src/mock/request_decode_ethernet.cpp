// Server direction of 3E (spec §5.1, §4.1.1, §4.1.2): frame and decode a request. Written from
// the tables of the reference spec; the golden vectors check it in the reverse direction
// (MCK-01).
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace mc::detail::mock {

namespace {

constexpr uint16_t kBatchRead = 0x0401;
constexpr uint16_t kBatchWrite = 0x1401;

// Spec §2.1.1 E1: the request subheader is a fixed byte sequence, not a u16.
constexpr uint8_t kSubheaderBinary[] = {0x50, 0x00};
constexpr uint8_t kSubheaderAscii[] = {'5', '0', '0', '0'};

// Cursor over one frame's bytes that reads fields through the codec of the wire representation.
template <class Codec> class Reader {
public:
    explicit Reader(ByteView view) : m_view(view) {}

    size_t remaining() const { return m_view.size - m_pos; }

    bool u8(uint8_t& v) {
        if (remaining() < Codec::u8Size()) {
            return false;
        }
        auto r = Codec::getU8(ByteView{m_view.data + m_pos, m_view.size - m_pos});
        if (!r.hasValue()) {
            return false;
        }
        v = r.value();
        m_pos += Codec::u8Size();
        return true;
    }

    bool u16(uint16_t& v) {
        if (remaining() < Codec::u16Size()) {
            return false;
        }
        auto r = Codec::getU16(ByteView{m_view.data + m_pos, m_view.size - m_pos});
        if (!r.hasValue()) {
            return false;
        }
        v = r.value();
        m_pos += Codec::u16Size();
        return true;
    }

    bool bytes(size_t n, ByteView& out) {
        if (remaining() < n) {
            return false;
        }
        out = ByteView{m_view.data + m_pos, n};
        m_pos += n;
        return true;
    }

private:
    ByteView m_view;
    size_t m_pos{0};
};

// Spec §3.3, "QnA ..." rows: Binary Q/L is number (LE 3) + code (1); Binary iQ-R is number (LE 4)
// + code (LE 2); ASCII is the text code ("D*", "D***") followed by 6 (Q/L) or 8 (iQ-R) digits in
// the device's own radix. This is the one place that branches on the wire representation.
size_t deviceFieldSize(DataCode code, PlcSeries series) {
    if (code == DataCode::Ascii) {
        return series == PlcSeries::QL ? 8 : 12;
    }
    return series == PlcSeries::QL ? 4 : 6;
}

bool digitValue(uint8_t c, Radix radix, uint32_t& v) {
    if (c == ' ') { // spec §3.2: leading zeros of ASCII device numbers may be spaces
        v = 0;
        return true;
    }
    if (c >= '0' && c <= '9') {
        v = static_cast<uint32_t>(c - '0');
        return true;
    }
    if (radix == Radix::Hex) {
        if (c >= 'A' && c <= 'F') {
            v = static_cast<uint32_t>(c - 'A' + 10);
            return true;
        }
        if (c >= 'a' && c <= 'f') {
            v = static_cast<uint32_t>(c - 'a' + 10);
            return true;
        }
    }
    return false;
}

bool asciiCodeMatches(const char* tableCode, ByteView text) {
    size_t len = std::strlen(tableCode);
    if (len == 0 || len != text.size) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        // Spec §3.2: the '*' of a QnA ASCII device code may be replaced by a space.
        char c = text.data[i] == ' ' ? '*' : static_cast<char>(text.data[i]);
        if (c != tableCode[i]) {
            return false;
        }
    }
    return true;
}

bool decodeAsciiDevice(PlcSeries series, ByteView field, Device& out) {
    const size_t codeSize = series == PlcSeries::QL ? 2 : 4;
    const ByteView codeText{field.data, codeSize};
    for (uint8_t i = 0; i < static_cast<uint8_t>(DeviceType::Count); ++i) {
        const DeviceInfo& info = deviceInfo(static_cast<DeviceType>(i));
        const char* tableCode = series == PlcSeries::QL ? info.qnaAsciiQL : info.qnaAsciiIqr;
        if (!asciiCodeMatches(tableCode, codeText)) {
            continue;
        }
        uint64_t number = 0;
        for (size_t d = codeSize; d < field.size; ++d) {
            uint32_t digit = 0;
            if (!digitValue(field.data[d], info.radix, digit)) {
                return false;
            }
            number = number * (info.radix == Radix::Hex ? 16u : 10u) + digit;
        }
        out = Device{info.type, static_cast<uint32_t>(number)};
        return true;
    }
    return false;
}

bool decodeBinaryDevice(PlcSeries series, ByteView field, Device& out) {
    uint32_t number = 0;
    uint16_t code = 0;
    if (series == PlcSeries::QL) {
        number = static_cast<uint32_t>(field.data[0]) |
                 (static_cast<uint32_t>(field.data[1]) << 8) |
                 (static_cast<uint32_t>(field.data[2]) << 16);
        code = field.data[3];
    } else {
        number = BinaryCodec::getU32(field).value();
        code = BinaryCodec::getU16(ByteView{field.data + 4, 2}).value();
    }
    for (uint8_t i = 0; i < static_cast<uint8_t>(DeviceType::Count); ++i) {
        const DeviceInfo& info = deviceInfo(static_cast<DeviceType>(i));
        uint16_t tableCode = series == PlcSeries::QL ? info.qnaBinQL : info.qnaBinIqr;
        if (tableCode != kNoCode && tableCode == code) {
            out = Device{info.type, number};
            return true;
        }
    }
    return false;
}

bool decodeDevice(DataCode code, PlcSeries series, ByteView field, Device& out) {
    return code == DataCode::Ascii ? decodeAsciiDevice(series, field, out)
                                   : decodeBinaryDevice(series, field, out);
}

// Decodes the request data of one frame (monitoring timer, command, subcommand, command data).
template <class Codec> QnaRequest decodeBody(const Route& route, ByteView body) {
    QnaRequest req;
    req.route = route;

    Reader<Codec> rd(body);
    uint16_t timer = 0;
    uint16_t command = 0;
    uint16_t subcommand = 0;
    if (!rd.u16(timer) || !rd.u16(command)) {
        return req;
    }
    req.command = command;
    if (!rd.u16(subcommand)) {
        return req;
    }
    req.subcommand = subcommand;
    if (command != kBatchRead && command != kBatchWrite) {
        return req;
    }

    // Spec §4.1: subcommand 0000/0001 are Q/L word/bit units, 0002/0003 iQ-R word/bit units.
    PlcSeries series = PlcSeries::QL;
    bool bitUnit = false;
    switch (subcommand) {
    case 0x0000:
        break;
    case 0x0001:
        bitUnit = true;
        break;
    case 0x0002:
        series = PlcSeries::IqR;
        break;
    case 0x0003:
        series = PlcSeries::IqR;
        bitUnit = true;
        break;
    default:
        return req;
    }

    ByteView deviceField;
    Device head;
    uint16_t count = 0;
    if (!rd.bytes(deviceFieldSize(Codec::kDataCode, series), deviceField) ||
        !decodeDevice(Codec::kDataCode, series, deviceField, head) || !rd.u16(count) ||
        count == 0) {
        return req;
    }

    const bool write = command == kBatchWrite;
    ByteBuf data;
    if (write) {
        const size_t wireSize = bitUnit ? Codec::bitsSize(count) : Codec::wordsSize(count);
        ByteView wire;
        if (rd.remaining() != wireSize || !rd.bytes(wireSize, wire)) {
            return req;
        }
        data.resize(bitUnit ? count : size_t{count} * 2);
        const MutableByteView dst{data.data(), data.size()};
        auto decoded =
            bitUnit ? Codec::getBits(wire, count, dst) : Codec::getWords(wire, count, dst);
        if (!decoded.hasValue()) {
            return req;
        }
    } else if (rd.remaining() != 0) {
        return req;
    }

    req.executable = true;
    req.op = write ? (bitUnit ? Op::WriteBits : Op::WriteWords)
                   : (bitUnit ? Op::ReadBits : Op::ReadWords);
    req.head = head;
    req.count = count;
    req.series = series;
    req.data = std::move(data);
    return req;
}

template <class Codec> DecodeResult decodeFrame(ByteView rx) {
    constexpr bool ascii = Codec::kDataCode == DataCode::Ascii;
    const uint8_t* subheader = ascii ? kSubheaderAscii : kSubheaderBinary;
    constexpr size_t subheaderSize = ascii ? sizeof(kSubheaderAscii) : sizeof(kSubheaderBinary);
    // Network, PC, I/O, station, request data length (spec §5.1 fields 2-6).
    constexpr size_t routeAndLengthSize =
        Codec::u8Size() + Codec::u8Size() + Codec::u16Size() + Codec::u8Size() + Codec::u16Size();
    constexpr size_t headerSize = subheaderSize + routeAndLengthSize;

    DecodeResult result;

    // A first byte that cannot start a frame is decided at once, so a wrong subheader never
    // waits for more bytes.
    for (size_t i = 0; i < std::min(rx.size, subheaderSize); ++i) {
        if (rx.data[i] != subheader[i]) {
            result.status = FrameStatus::Junk;
            result.consumed = 1;
            return result;
        }
    }
    if (rx.size < headerSize) {
        return result;
    }

    Reader<Codec> rd(ByteView{rx.data + subheaderSize, routeAndLengthSize});
    Route route;
    uint16_t length = 0;
    if (!rd.u8(route.network) || !rd.u8(route.pc) || !rd.u16(route.io) || !rd.u8(route.station) ||
        !rd.u16(length)) {
        result.status = FrameStatus::Junk;
        result.consumed = 1;
        return result;
    }
    if (rx.size < headerSize + length) {
        return result;
    }

    result.status = FrameStatus::Complete;
    result.consumed = headerSize + length;
    result.request = decodeBody<Codec>(route, ByteView{rx.data + headerSize, length});
    return result;
}

} // namespace

DecodeResult decode3eRequest(DataCode code, ByteView rx) {
    return code == DataCode::Ascii ? decodeFrame<AsciiCodec>(rx) : decodeFrame<BinaryCodec>(rx);
}

} // namespace mc::detail::mock
