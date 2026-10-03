// Server direction of 3C and 1C, formats 1-4 (spec §5.4-§5.6, §4.1, §4.3 and §6.3 read from the
// server side): find the start of a request in a byte stream, work out where it ends, and decode
// it. Written from the tables of the reference spec; the golden vectors check it in the reverse
// direction (MCK-01).
#include "mock/mock_internal.h"

#include "core/protocol/field_codec.h"
#include "core/protocol/sumcheck.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace mc::detail::mock {

namespace {

// Control codes (spec §2.7).
constexpr uint8_t kStx = 0x02;
constexpr uint8_t kEtx = 0x03;
constexpr uint8_t kEot = 0x04;
constexpr uint8_t kEnq = 0x05;

// Spec §2.5: the frame ID of a 3C frame is "F9".
constexpr char kFrameId3c[] = "F9";

bool isHexChar(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

// Everything the request characters of formats 1, 2 and 4 are made of: no control code, which is
// how a stray ENQ, STX or CR inside a request is noticed.
bool isPrintable(uint8_t c) { return c >= 0x20 && c <= 0x7E; }

enum class Frame { Complete, Need, Bad };

// A cursor over the bytes of one partial request. Each take is checked as far as the bytes
// received allow, so a character that cannot belong to the field makes the request Bad at once
// instead of after the field is complete. After the first failure every call is a no-op.
class Walk {
  public:
    Walk(ByteView bytes, size_t start) : m_bytes(bytes), m_pos(start) {}

    size_t pos() const { return m_pos; }
    bool failed() const { return m_need || m_bad; }
    Frame result() const { return m_bad ? Frame::Bad : (m_need ? Frame::Need : Frame::Complete); }

    // Takes `n` characters, each accepted by `accept`.
    ByteView take(size_t n, bool (*accept)(uint8_t)) {
        if (failed()) {
            return {};
        }
        const size_t available = std::min(n, m_bytes.size - m_pos);
        for (size_t i = 0; i < available; ++i) {
            if (!accept(m_bytes.data[m_pos + i])) {
                m_bad = true;
                return {};
            }
        }
        if (available < n) {
            m_need = true;
            return {};
        }
        const ByteView out{m_bytes.data + m_pos, n};
        m_pos += n;
        return out;
    }

    // Takes `n` hexadecimal characters (n is 2 or 4) and returns their value.
    uint32_t hex(size_t n) {
        const ByteView text = take(n, &isHexChar);
        if (failed()) {
            return 0;
        }
        return n == 2 ? AsciiCodec::getU8(text).value() : AsciiCodec::getU16(text).value();
    }

    // Takes exactly the characters of `text`.
    void literal(const char* text) {
        const size_t n = std::strlen(text);
        if (failed()) {
            return;
        }
        const size_t available = std::min(n, m_bytes.size - m_pos);
        for (size_t i = 0; i < available; ++i) {
            if (m_bytes.data[m_pos + i] != static_cast<uint8_t>(text[i])) {
                m_bad = true;
                return;
            }
        }
        if (available < n) {
            m_need = true;
            return;
        }
        m_pos += n;
    }

    // The request cannot be framed.
    void fail() { m_bad = true; }

  private:
    ByteView m_bytes;
    size_t m_pos;
    bool m_need{false};
    bool m_bad{false};
};

// ---- 3C request data (spec §4.1 after the access route; no monitoring timer on serial) ----------

// Where the request data of a 3C request ends, from its command and point count. The commands the
// mock executes (0401, 1401) and the ones it only frames to answer them with an error (0403, 1402)
// have a layout; any other command has none, so nothing can say where its request ends.
void walkData3c(Walk& w) {
    const uint32_t command = w.hex(4);
    const uint32_t subcommand = w.hex(4);
    if (w.failed()) {
        return;
    }
    PlcSeries series = PlcSeries::QL;
    bool bitUnit = false;
    switch (command) {
    case 0x0401:
    case 0x1401: {
        if (!decodeQnaSubcommand(static_cast<uint16_t>(subcommand), series, bitUnit)) {
            w.fail();
            return;
        }
        w.take(qnaAsciiDeviceSize(series), &isPrintable);
        const uint32_t points = w.hex(4);
        if (command == 0x1401) {
            w.take(bitUnit ? size_t{points} : size_t{points} * 4, &isPrintable);
        }
        return;
    }
    case 0x0403: {
        // m + n devices; no data.
        if (subcommand != 0x0000 && subcommand != 0x0002) {
            w.fail();
            return;
        }
        const size_t device =
            qnaAsciiDeviceSize(subcommand == 0x0000 ? PlcSeries::QL : PlcSeries::IqR);
        const uint32_t m = w.hex(2);
        const uint32_t n = w.hex(2);
        w.take((size_t{m} + n) * device, &isPrintable);
        return;
    }
    case 0x1402: {
        if (subcommand == 0x0000 || subcommand == 0x0002) {
            // m x [device, u16] then n x [device, u32].
            const size_t device =
                qnaAsciiDeviceSize(subcommand == 0x0000 ? PlcSeries::QL : PlcSeries::IqR);
            const uint32_t m = w.hex(2);
            const uint32_t n = w.hex(2);
            w.take(size_t{m} * (device + 4) + size_t{n} * (device + 8), &isPrintable);
        } else if (subcommand == 0x0001 || subcommand == 0x0003) {
            // n x [device, set/reset]: 2 characters on Q/L, 4 on iQ-R.
            const bool ql = subcommand == 0x0001;
            const size_t device = qnaAsciiDeviceSize(ql ? PlcSeries::QL : PlcSeries::IqR);
            const uint32_t n = w.hex(2);
            w.take(size_t{n} * (device + (ql ? 2 : 4)), &isPrintable);
        } else {
            w.fail();
        }
        return;
    }
    default:
        w.fail();
        return;
    }
}

void decodeData3c(ByteView data, XyNumbering xy, SerialRequest& req) {
    QnaRequest q = decodeQnaAsciiRequestData(data, xy);
    req.executable = q.executable;
    req.op = q.op;
    req.head = q.head;
    req.count = q.count;
    req.data = std::move(q.data);
    req.series = q.series;
}

// ---- 1C request data (spec §4.3): command (2), message wait (1), character area ----------------

// The six command letters of each set: first letter B / W (ACPU) or J / Q (AnA/AnU) says bit or
// word units, second letter R, W or T says read, write or test.
struct Command1c {
    bool known{false};
    bool bit{false}; // Bit units (B, J) or word units (W, Q).
    bool ana{false}; // AnA/AnU letters: device field 7 characters, not 5.
    char kind{'\0'}; // 'R' read, 'W' write, 'T' test.
};

Command1c commandOf(uint8_t first, uint8_t second) {
    Command1c c;
    const bool firstOk = first == 'B' || first == 'W' || first == 'J' || first == 'Q';
    const bool secondOk = second == 'R' || second == 'W' || second == 'T';
    c.known = firstOk && secondOk;
    c.bit = first == 'B' || first == 'J';
    c.ana = first == 'J' || first == 'Q';
    c.kind = static_cast<char>(second);
    return c;
}

// Spec §3.3: a 1C device is its code (1 character, or 2 for the timer and counter symbols)
// followed by the number in the device's radix, 5 characters in all for the ACPU commands and 7
// for the AnA/AnU commands.
size_t deviceSize1c(const Command1c& c) { return c.ana ? 7 : 5; }

bool decodeDevice1c(ByteView field, XyNumbering xy, Device& out) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(DeviceType::Count); ++i) {
        const DeviceInfo& info = deviceInfo(static_cast<DeviceType>(i));
        const size_t codeSize = std::strlen(info.c1Code);
        if (codeSize == 0 || std::memcmp(field.data, info.c1Code, codeSize) != 0) {
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

// Spec E8: 256 points travel as 00.
size_t points1c(uint32_t field) { return field == 0 ? size_t{256} : size_t{field}; }

// Where the request data of a 1C request ends. The test commands (BT, WT, JT, QT) are framed by
// their n x item layout so that they can be answered with an error.
void walkData1c(Walk& w) {
    const ByteView letters = w.take(2, &isPrintable);
    w.take(1, &isHexChar); // message wait, 0-F (spec E7): accepted and ignored
    if (w.failed()) {
        return;
    }
    const Command1c c = commandOf(letters.data[0], letters.data[1]);
    if (!c.known) {
        w.fail();
        return;
    }
    const size_t device = deviceSize1c(c);
    if (c.kind == 'T') {
        const uint32_t n = w.hex(2);
        w.take(size_t{n} * (device + (c.bit ? 1 : 4)), &isPrintable);
        return;
    }
    w.take(device, &isPrintable);
    const uint32_t points = w.hex(2);
    if (c.kind == 'W') {
        const size_t count = points1c(points);
        w.take(c.bit ? count : count * 4, &isPrintable);
    }
}

void decodeData1c(ByteView data, XyNumbering xy, SerialRequest& req) {
    if (data.size < 3 || !isHexChar(data.data[2])) {
        return;
    }
    const Command1c c = commandOf(data.data[0], data.data[1]);
    if (!c.known || c.kind == 'T') {
        return;
    }
    const size_t device = deviceSize1c(c);
    const ByteView area{data.data + 3, data.size - 3};
    if (area.size < device + 2) {
        return;
    }
    Device head;
    auto points = AsciiCodec::getU8(ByteView{area.data + device, 2});
    if (!decodeDevice1c(ByteView{area.data, device}, xy, head) || !points.hasValue()) {
        return;
    }
    const size_t count = points1c(points.value());
    const size_t rest = area.size - device - 2;

    ByteBuf payload;
    if (c.kind == 'W') {
        const size_t wireSize = c.bit ? count : count * 4;
        if (rest != wireSize) {
            return;
        }
        payload.resize(c.bit ? count : count * 2);
        const ByteView wire{area.data + device + 2, wireSize};
        const MutableByteView dst{payload.data(), payload.size()};
        auto decoded =
            c.bit ? AsciiCodec::getBits(wire, count, dst) : AsciiCodec::getWords(wire, count, dst);
        if (!decoded.hasValue()) {
            return;
        }
    } else if (rest != 0) {
        return;
    }

    req.executable = true;
    req.op = c.kind == 'W' ? (c.bit ? Op::WriteBits : Op::WriteWords)
                           : (c.bit ? Op::ReadBits : Op::ReadWords);
    req.head = head;
    req.count = static_cast<uint16_t>(count);
    req.data = std::move(payload);
}

// ---- framing --------------------------------------------------------------------------------

// The access route that follows the start byte (and the block number of format 2): spec §5.5 for
// 3C (frame ID, station, network, PC, self-station), §5.6 for 1C (station, PC).
void readRoute(Walk& w, bool threeC, SerialRequest& req) {
    if (threeC) {
        w.literal(kFrameId3c);
        req.station = static_cast<uint8_t>(w.hex(2));
        req.network = static_cast<uint8_t>(w.hex(2));
        req.pc = static_cast<uint8_t>(w.hex(2));
        req.selfStation = static_cast<uint8_t>(w.hex(2));
    } else {
        req.station = static_cast<uint8_t>(w.hex(2));
        req.pc = static_cast<uint8_t>(w.hex(2));
    }
}

bool sumMatches(ByteView range, ByteView text) {
    if (!isHexChar(text.data[0]) || !isHexChar(text.data[1])) {
        return false;
    }
    return sumcheck(range) == AsciiCodec::getU8(text).value();
}

// Formats 1, 2 and 4: ENQ [BLK] P request-data [SUM] [CR LF]. `bytes` starts at the ENQ and ends
// before any EOT. On Complete, `end` is the size of the frame.
Frame frameEnq(const FrameConfig& cfg, ByteView bytes, SerialRequest& req, size_t& end) {
    const bool threeC = cfg.frame == FrameType::F3C;
    Walk w(bytes, 1);
    if (cfg.format == SerialFormat::Format2) {
        req.block = static_cast<uint8_t>(w.hex(2));
    }
    readRoute(w, threeC, req);
    const size_t dataBegin = w.pos();
    if (threeC) {
        walkData3c(w);
    } else {
        walkData1c(w);
    }
    const size_t dataEnd = w.pos();
    ByteView sumText;
    if (cfg.sumCheck) {
        sumText = w.take(2, &isPrintable);
    }
    if (cfg.format == SerialFormat::Format4) {
        w.literal("\r\n"); // CR LF follows the sum check and is not summed (spec §2.5)
    }
    if (w.failed()) {
        return w.result();
    }

    end = w.pos();
    const ByteView data{bytes.data + dataBegin, dataEnd - dataBegin};
    if (threeC) {
        decodeData3c(data, cfg.xyAsciiDigits, req);
    } else {
        decodeData1c(data, cfg.xyAsciiDigits, req);
    }
    // F1, F2 and F4 sum from after the ENQ to the end of the request data (spec §2.5).
    req.sumValid = !cfg.sumCheck || sumMatches(ByteView{bytes.data + 1, dataEnd - 1}, sumText);
    return Frame::Complete;
}

// Format 3: STX P request-data ETX [SUM]. The request data is ASCII and holds no control code, so
// the frame ends at the first ETX. The request data is decoded whatever it says: a command the
// mock does not know is answered with an error, not dropped.
Frame frameStx(const FrameConfig& cfg, ByteView bytes, SerialRequest& req, size_t& end) {
    const bool threeC = cfg.frame == FrameType::F3C;
    size_t etx = 0;
    for (size_t i = 1; i < bytes.size && etx == 0; ++i) {
        if (bytes.data[i] == kEtx) {
            etx = i;
        } else if (!isPrintable(bytes.data[i])) {
            return Frame::Bad; // a second STX, an ENQ or a CR cannot be inside a request
        }
    }
    if (etx == 0) {
        return Frame::Need;
    }

    Walk w(ByteView{bytes.data, etx}, 1);
    readRoute(w, threeC, req);
    if (w.failed()) {
        return Frame::Bad; // the route is cut short by the ETX, or is not hexadecimal
    }
    const size_t dataBegin = w.pos();

    Walk tail(bytes, etx + 1);
    ByteView sumText;
    if (cfg.sumCheck) {
        sumText = tail.take(2, &isPrintable);
        if (tail.failed()) {
            return tail.result(); // a control code here is the start of another request
        }
    }

    end = tail.pos();
    const ByteView data{bytes.data + dataBegin, etx - dataBegin};
    if (threeC) {
        decodeData3c(data, cfg.xyAsciiDigits, req);
    } else {
        decodeData1c(data, cfg.xyAsciiDigits, req);
    }
    // F3 sums from after the STX to and including the ETX (spec §2.5).
    req.sumValid = !cfg.sumCheck || sumMatches(ByteView{bytes.data + 1, etx}, sumText);
    return Frame::Complete;
}

} // namespace

SerialDecodeResult decodeSerialRequest(const FrameConfig& cfg, ByteView rx) {
    SerialDecodeResult result;
    if (rx.size == 0) {
        return result;
    }

    // Spec §6.3, server side: formats 1, 2 and 4 start with ENQ, format 3 with STX; any other byte
    // before it is skipped. EOT is not a skipped byte: it resets the receiver (spec §2.7).
    const bool format3 = cfg.format == SerialFormat::Format3;
    const uint8_t first = rx.data[0];
    if (first == kEot) {
        result.status = FrameStatus::Eot;
        result.consumed = 1;
        return result;
    }
    if (first != (format3 ? kStx : kEnq)) {
        result.status = FrameStatus::Junk;
        result.consumed = 1;
        return result;
    }

    // An EOT anywhere in the partial request cancels it: frame only the bytes before the first one.
    size_t limit = rx.size;
    for (size_t i = 1; i < rx.size; ++i) {
        if (rx.data[i] == kEot) {
            limit = i;
            break;
        }
    }
    const ByteView bytes{rx.data, limit};

    size_t end = 0;
    Frame frame = format3 ? frameStx(cfg, bytes, result.request, end)
                          : frameEnq(cfg, bytes, result.request, end);
    switch (frame) {
    case Frame::Complete:
        result.status = FrameStatus::Complete;
        result.consumed = end;
        break;
    case Frame::Need:
        if (limit < rx.size) {
            result.status = FrameStatus::Eot;
            result.consumed = limit + 1;
        }
        break;
    case Frame::Bad:
        // The layout cannot be followed: drop the start byte and look for the next one.
        result.status = FrameStatus::Junk;
        result.consumed = 1;
        break;
    }
    return result;
}

} // namespace mc::detail::mock
