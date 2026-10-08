// Server direction of 3E and 1E (spec §5.1, §5.3, §4.1, §4.2): frame and decode a request. Written
// from the tables of the reference spec; the golden vectors check it in the reverse direction
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

bool digitValue(uint8_t c, uint32_t base, uint32_t& v) {
    if (c == ' ') { // spec §3.2: leading zeros of ASCII device numbers may be spaces
        v = 0;
        return true;
    }
    if (c >= '0' && c <= '9') {
        v = static_cast<uint32_t>(c - '0');
        return v < base;
    }
    if (base == 16) {
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

} // namespace

// The device and subcommand decoding below is shared with the serial decoder
// (request_decode_serial.cpp).

uint32_t asciiNumberBase(const DeviceInfo& info, XyNumbering xy) {
    if (xy == XyNumbering::Octal && (info.type == DeviceType::X || info.type == DeviceType::Y)) {
        return 8;
    }
    return info.radix == Radix::Hex ? 16 : 10;
}

bool parseDeviceNumber(ByteView digits, uint32_t base, uint64_t& number) {
    number = 0;
    for (size_t i = 0; i < digits.size; ++i) {
        uint32_t digit = 0;
        if (!digitValue(digits.data[i], base, digit)) {
            return false;
        }
        number = number * base + digit;
    }
    return true;
}

size_t qnaAsciiDeviceSize(PlcSeries series) { return deviceFieldSize(DataCode::Ascii, series); }

bool decodeQnaAsciiDevice(PlcSeries series, ByteView field, XyNumbering xy, Device& out) {
    const size_t codeSize = series == PlcSeries::QL ? 2 : 4;
    const ByteView codeText{field.data, codeSize};
    for (uint8_t i = 0; i < static_cast<uint8_t>(DeviceType::Count); ++i) {
        const DeviceInfo& info = deviceInfo(static_cast<DeviceType>(i));
        const char* tableCode = series == PlcSeries::QL ? info.qnaAsciiQL : info.qnaAsciiIqr;
        if (!asciiCodeMatches(tableCode, codeText)) {
            continue;
        }
        uint64_t number = 0;
        if (!parseDeviceNumber(ByteView{field.data + codeSize, field.size - codeSize},
                               asciiNumberBase(info, xy), number)) {
            return false;
        }
        out = Device{info.type, static_cast<uint32_t>(number)};
        return true;
    }
    return false;
}

bool decodeQnaSubcommand(uint16_t subcommand, PlcSeries& series, bool& bitUnit) {
    switch (subcommand) {
    case 0x0000:
        series = PlcSeries::QL;
        bitUnit = false;
        return true;
    case 0x0001:
        series = PlcSeries::QL;
        bitUnit = true;
        return true;
    case 0x0002:
        series = PlcSeries::IqR;
        bitUnit = false;
        return true;
    case 0x0003:
        series = PlcSeries::IqR;
        bitUnit = true;
        return true;
    default:
        return false;
    }
}

namespace {

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

bool decodeDevice(DataCode code, PlcSeries series, XyNumbering xy, ByteView field, Device& out) {
    return code == DataCode::Ascii ? decodeQnaAsciiDevice(series, field, xy, out)
                                   : decodeBinaryDevice(series, field, out);
}

// Decodes the request data of one frame: monitoring timer (3E only, so `withTimer`), command,
// subcommand, command data.
template <class Codec>
QnaRequest decodeBody(const Route& route, ByteView body, bool withTimer, XyNumbering xy) {
    QnaRequest req;
    req.route = route;

    Reader<Codec> rd(body);
    uint16_t timer = 0;
    uint16_t command = 0;
    uint16_t subcommand = 0;
    if ((withTimer && !rd.u16(timer)) || !rd.u16(command)) {
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

    PlcSeries series = PlcSeries::QL;
    bool bitUnit = false;
    if (!decodeQnaSubcommand(subcommand, series, bitUnit)) {
        return req;
    }

    ByteView deviceField;
    Device head;
    uint16_t count = 0;
    if (!rd.bytes(deviceFieldSize(Codec::kDataCode, series), deviceField) ||
        !decodeDevice(Codec::kDataCode, series, xy, deviceField, head) || !rd.u16(count) ||
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

template <class Codec> DecodeResult decodeFrame(XyNumbering xy, ByteView rx) {
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
    result.request = decodeBody<Codec>(route, ByteView{rx.data + headerSize, length}, true, xy);
    return result;
}

// ---- 1E (spec §5.3, §4.2) ------------------------------------------------------------------

// Command codes of the 1E request subheader (spec §4.2).
constexpr uint8_t kE1BitRead = 0x00;
constexpr uint8_t kE1WordRead = 0x01;
constexpr uint8_t kE1BitWrite = 0x02;
constexpr uint8_t kE1WordWrite = 0x03;
constexpr uint8_t kE1TestBits = 0x04;  // random write, bit units: not executed (v1.1)
constexpr uint8_t kE1TestWords = 0x05; // random write, word units: not executed (v1.1)

// Spec §3.3: 1E Binary is number (LE 4) + code (LE 2); 1E ASCII is code (4 hex characters) +
// number (8 hex characters), whatever the device's own radix; X and Y carry 8 octal characters
// when `xy` is Octal. Spec §3.2 footnote 2: L and S have no code of their own and are reached as
// M, so a code never decodes to L or S.
size_t e1DeviceFieldSize(DataCode code) { return code == DataCode::Ascii ? 12 : 6; }

template <class Codec> bool decodeE1Device(ByteView field, XyNumbering xy, Device& out) {
    constexpr bool ascii = Codec::kDataCode == DataCode::Ascii;
    const ByteView numberField = ascii ? ByteView{field.data + 4, 8} : ByteView{field.data, 4};
    const ByteView codeField = ascii ? ByteView{field.data, 4} : ByteView{field.data + 4, 2};
    auto number = Codec::getU32(numberField);
    auto code = Codec::getU16(codeField);
    if (!number.hasValue() || !code.hasValue()) {
        return false;
    }
    for (uint8_t i = 0; i < static_cast<uint8_t>(DeviceType::Count); ++i) {
        const DeviceInfo& info = deviceInfo(static_cast<DeviceType>(i));
        if (info.type == DeviceType::L || info.type == DeviceType::S) {
            continue;
        }
        if (info.e1Code != kNoCode && info.e1Code == code.value()) {
            uint32_t index = number.value();
            if constexpr (ascii) {
                if (asciiNumberBase(info, xy) == 8) {
                    uint64_t octal = 0;
                    if (!parseDeviceNumber(numberField, 8, octal)) {
                        return false;
                    }
                    index = static_cast<uint32_t>(octal);
                }
            }
            out = Device{info.type, index};
            return true;
        }
    }
    return false;
}

// Frames one 1E request from the start of `rx`. There is no length field: the frame ends where the
// command's fixed layout says, given its point count (00H-03H) or its item count (04H, 05H).
template <class Codec> DecodeResult1e decodeFrame1e(XyNumbering xy, ByteView rx) {
    constexpr bool ascii = Codec::kDataCode == DataCode::Ascii;
    // Subheader (command), PC No., monitoring timer (spec §5.3 fields 1-3).
    constexpr size_t headSize = Codec::u8Size() + Codec::u8Size() + Codec::u16Size();
    const size_t deviceSize = e1DeviceFieldSize(Codec::kDataCode);

    DecodeResult1e result;
    if (rx.size == 0) {
        return result;
    }

    // A first byte that cannot start a request is decided at once (Binary: a command above 05H;
    // ASCII: the two characters must read "00".."05"), so a wrong subheader never waits for more.
    const bool junk = ascii ? (rx.data[0] != '0' ||
                               (rx.size >= 2 && (rx.data[1] < '0' || rx.data[1] > '5')))
                            : rx.data[0] > kE1TestWords;
    if (junk) {
        result.status = FrameStatus::Junk;
        result.consumed = 1;
        return result;
    }
    if (rx.size < headSize) {
        return result;
    }

    Reader<Codec> head(ByteView{rx.data, headSize});
    uint8_t command = 0;
    uint8_t pc = 0;
    uint16_t timer = 0;
    if (!head.u8(command) || !head.u8(pc) || !head.u16(timer) || command > kE1TestWords) {
        result.status = FrameStatus::Junk;
        result.consumed = 1;
        return result;
    }

    // Points (or the item count n of 04H/05H) and the fixed 00: two u8 fields after the device
    // field (batch access) or at the start of the request data (04H, 05H).
    const bool batch = command <= kE1WordWrite;
    const size_t countAt = headSize + (batch ? deviceSize : 0);
    const size_t bodyHead = countAt + 2 * Codec::u8Size();
    if (rx.size < bodyHead) {
        return result;
    }
    Reader<Codec> counts(ByteView{rx.data + countAt, 2 * Codec::u8Size()});
    uint8_t points = 0;
    uint8_t fixed = 0;
    if (!counts.u8(points) || !counts.u8(fixed)) {
        result.status = FrameStatus::Junk;
        result.consumed = 1;
        return result;
    }

    E1Request req;
    req.pc = pc;
    req.command = command;

    if (!batch) {
        // 04H: n x (device + u8); 05H: n x (device + u16). Framed, never executed.
        const size_t itemSize =
            deviceSize + (command == kE1TestBits ? Codec::u8Size() : Codec::u16Size());
        const size_t total = bodyHead + size_t{points} * itemSize;
        if (rx.size < total) {
            return result;
        }
        result.status = FrameStatus::Complete;
        result.consumed = total;
        result.request = std::move(req);
        return result;
    }

    // Spec E8: 256 points travel as 00.
    const uint16_t count = points == 0 ? uint16_t{256} : uint16_t{points};
    const bool bitUnit = command == kE1BitRead || command == kE1BitWrite;
    const bool write = command == kE1BitWrite || command == kE1WordWrite;
    const size_t dataSize =
        !write ? 0 : (bitUnit ? Codec::bitsSize(count) : Codec::wordsSize(count));
    const size_t total = bodyHead + dataSize;
    if (rx.size < total) {
        return result;
    }

    result.status = FrameStatus::Complete;
    result.consumed = total;

    Device dev;
    bool executable =
        fixed == 0 && decodeE1Device<Codec>(ByteView{rx.data + headSize, deviceSize}, xy, dev);
    ByteBuf data;
    if (executable && write) {
        data.resize(bitUnit ? count : size_t{count} * 2);
        const MutableByteView dst{data.data(), data.size()};
        const ByteView wire{rx.data + bodyHead, dataSize};
        auto decoded =
            bitUnit ? Codec::getBits(wire, count, dst) : Codec::getWords(wire, count, dst);
        executable = decoded.hasValue();
    }
    if (executable) {
        req.executable = true;
        req.op = write ? (bitUnit ? Op::WriteBits : Op::WriteWords)
                       : (bitUnit ? Op::ReadBits : Op::ReadWords);
        req.head = dev;
        req.count = count;
        req.data = std::move(data);
    }
    result.request = std::move(req);
    return result;
}

} // namespace

DecodeResult decode3eRequest(DataCode code, XyNumbering xy, ByteView rx) {
    return code == DataCode::Ascii ? decodeFrame<AsciiCodec>(xy, rx)
                                   : decodeFrame<BinaryCodec>(xy, rx);
}

DecodeResult1e decode1eRequest(DataCode code, XyNumbering xy, ByteView rx) {
    return code == DataCode::Ascii ? decodeFrame1e<AsciiCodec>(xy, rx)
                                   : decodeFrame1e<BinaryCodec>(xy, rx);
}

QnaRequest decodeQnaAsciiRequestData(ByteView data, XyNumbering xy) {
    return decodeBody<AsciiCodec>(Route{}, data, false, xy);
}

} // namespace mc::detail::mock
