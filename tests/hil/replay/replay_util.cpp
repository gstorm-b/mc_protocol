#include "replay_util.h"

#include "mc/core/device.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace mc::replay::util {

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) {
        out.push_back(w);
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

namespace {

std::string hexOf(const uint8_t* data, size_t size, size_t limit) {
    std::string out;
    char buf[4];
    const size_t shown = std::min(size, limit);
    for (size_t i = 0; i < shown; ++i) {
        std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned>(data[i]));
        if (!out.empty()) {
            out += ' ';
        }
        out += buf;
    }
    if (shown < size) {
        out += " ...";
    }
    return out;
}

} // namespace

std::string hex(const std::vector<uint8_t>& bytes, size_t limit) {
    return hexOf(bytes.data(), bytes.size(), limit);
}

std::string hex(ByteView bytes, size_t limit) { return hexOf(bytes.data, bytes.size, limit); }

std::optional<uint32_t> number(const std::string& s, int base) {
    if (s.empty()) {
        return std::nullopt;
    }
    uint64_t v = 0;
    for (const char ch : s) {
        int d = -1;
        if (ch >= '0' && ch <= '9') {
            d = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            d = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            d = ch - 'A' + 10;
        }
        if (d < 0 || d >= base) {
            return std::nullopt;
        }
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
        if (v > 0xFFFFFFFFull) {
            return std::nullopt;
        }
    }
    return static_cast<uint32_t>(v);
}

std::optional<DeviceType> typeBySymbol(const std::string& symbol) {
    for (size_t i = 0; i < static_cast<size_t>(DeviceType::Count); ++i) {
        const DeviceType t = static_cast<DeviceType>(i);
        if (symbol == deviceInfo(t).symbol) {
            return t;
        }
    }
    return std::nullopt;
}

std::string deviceText(const Device& d, XyNumbering xy) {
    char buf[32];
    const size_t n = formatDevice(d, buf, sizeof buf, xy);
    return std::string(buf, std::min(n, sizeof buf - 1));
}

std::optional<Op> opByName(const std::string& name) {
    if (name == "ReadWords") {
        return Op::ReadWords;
    }
    if (name == "ReadBits") {
        return Op::ReadBits;
    }
    if (name == "WriteWords") {
        return Op::WriteWords;
    }
    if (name == "WriteBits") {
        return Op::WriteBits;
    }
    return std::nullopt;
}

bool isBitDevice(DeviceType t) { return deviceInfo(t).kind == DeviceKind::Bit; }

size_t firstDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return i;
        }
    }
    return n;
}

std::string describeDifference(const std::vector<uint8_t>& captured,
                               const std::vector<uint8_t>& other, const char* otherName) {
    const size_t at = firstDifference(captured, other);
    std::ostringstream os;
    os << "first difference at byte " << at;
    if (at < captured.size() && at < other.size()) {
        os << ": captured " << hex(std::vector<uint8_t>{captured[at]}) << ", " << otherName << ' '
           << hex(std::vector<uint8_t>{other[at]});
    }
    os << " (captured " << captured.size() << " bytes, " << otherName << ' ' << other.size()
       << " bytes)";
    return os.str();
}

std::vector<Decoded> decode(const FrameConfig& cfg, const std::vector<uint8_t>& bytes) {
    std::vector<Decoded> out;
    MockPlc mock(cfg);
    mock.bytesIn(view(bytes));
    for (const MockRequestRecord& rec : mock.requests()) {
        if (rec.count == 0) {
            // A request the mock could not read (a bad device number, a short frame, ...) is logged
            // with default fields; a decoded request always has at least one point.
            continue;
        }
        Decoded d;
        d.op = rec.op;
        d.head = rec.head;
        d.count = rec.count;
        d.answeredOk = rec.answered && rec.answeredWith.code == ErrorCode::Ok;
        if (isWriteOp(rec.op)) {
            if (isBitOp(rec.op)) {
                for (uint32_t i = 0; i < rec.count; ++i) {
                    d.data.push_back(mock.bit(Device{rec.head.type, rec.head.number + i}) ? 1 : 0);
                }
            } else {
                const bool bitDevice = isBitDevice(rec.head.type);
                for (uint32_t k = 0; k < rec.count; ++k) {
                    const uint32_t at = rec.head.number + (bitDevice ? 16 * k : k);
                    const uint16_t w = mock.word(Device{rec.head.type, at});
                    d.data.push_back(static_cast<uint8_t>(w & 0xFF));
                    d.data.push_back(static_cast<uint8_t>(w >> 8));
                }
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

Request toRequest(const Decoded& d) {
    switch (d.op) {
    case Op::ReadBits:
        return Request::readBits(d.head, d.count);
    case Op::ReadWords:
        return Request::readWords(d.head, d.count);
    case Op::WriteBits:
        return Request::writeBits(d.head, view(d.data));
    case Op::WriteWords:
        return Request::writeWords(d.head, view(d.data));
    }
    return Request{};
}

uint32_t pointsTouched(const Decoded& d) {
    if (isBitOp(d.op)) {
        return d.count;
    }
    return isBitDevice(d.head.type) ? 16u * d.count : d.count;
}

} // namespace mc::replay::util
