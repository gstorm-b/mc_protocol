// Hand-built 3C and 1C frames for the serial tests of tests/mock: request and response frames of
// formats 1 to 4 written from the tables of reference spec sections 5.4-5.6 and 2.5 (never from the
// client encoder), so a test can build the variants the golden vectors do not have: another station
// or route, a wrong SUM, no SUM, an unsupported command. serial_frames_selfcheck in
// test_mock_stream.cpp pins these builders to the golden vectors.
//
// Test code only: header-only, may allocate.
#pragma once

#include "mc/core/frame_config.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace mc::test::serial {

using Bytes = std::vector<uint8_t>;

inline constexpr uint8_t kStx = 0x02;
inline constexpr uint8_t kEtx = 0x03;
inline constexpr uint8_t kEot = 0x04;
inline constexpr uint8_t kEnq = 0x05;
inline constexpr uint8_t kAck = 0x06;
inline constexpr uint8_t kLf = 0x0A;
inline constexpr uint8_t kCr = 0x0D;
inline constexpr uint8_t kNak = 0x15;

/// The access route of a frame as the numbers the spec lists (3C: station, network, PC,
/// self-station; 1C: station, PC) and the block number of format 2. The defaults are the ones of
/// the Appendix A vectors.
struct Route {
    unsigned station{0x00};
    unsigned network{0x00};
    unsigned pc{0xFF};
    unsigned self{0x00};
    unsigned block{0x00};
};

inline std::string hex2(unsigned v) {
    char text[8];
    std::snprintf(text, sizeof(text), "%02X", v & 0xFFu);
    return text;
}

inline std::string hex4(unsigned v) {
    char text[8];
    std::snprintf(text, sizeof(text), "%04X", v & 0xFFFFu);
    return text;
}

inline Bytes bytesOf(const std::string& text) { return Bytes(text.begin(), text.end()); }

inline void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }

inline void append(Bytes& dst, const std::string& text) { append(dst, bytesOf(text)); }

/// Spec 2.5: the low 8 bits of the sum of bytes [from, to).
inline unsigned byteSum(const Bytes& b, size_t from, size_t to) {
    unsigned sum = 0;
    for (size_t i = from; i < to; ++i) {
        sum += b[i];
    }
    return sum & 0xFFu;
}

inline bool isThreeC(const FrameConfig& cfg) { return cfg.frame == FrameType::F3C; }

/// P: the access route text (spec 5.5: "F9" station network PC self-station; spec 5.6: station
/// PC).
inline std::string routeText(const FrameConfig& cfg, const Route& r) {
    if (isThreeC(cfg)) {
        return "F9" + hex2(r.station) + hex2(r.network) + hex2(r.pc) + hex2(r.self);
    }
    return hex2(r.station) + hex2(r.pc);
}

/// A request frame in the format of `cfg`: `data` is the request data text (3C: command,
/// subcommand, device, points, data; 1C: command, message wait, character area). The SUM is
/// appended when `cfg.sumCheck`, as the spec 2.5 range table says: F1, F2 and F4 from after the ENQ
/// to the end of the request data, F3 from after the STX through the ETX.
inline Bytes request(const FrameConfig& cfg, const std::string& data, const Route& r = {}) {
    Bytes f;
    const bool f3 = cfg.format == SerialFormat::Format3;
    f.push_back(f3 ? kStx : kEnq);
    if (cfg.format == SerialFormat::Format2) {
        append(f, hex2(r.block));
    }
    append(f, routeText(cfg, r));
    append(f, data);
    if (f3) {
        f.push_back(kEtx);
    }
    if (cfg.sumCheck) {
        append(f, hex2(byteSum(f, 1, f.size())));
    }
    if (cfg.format == SerialFormat::Format4) {
        f.push_back(kCr);
        f.push_back(kLf);
    }
    return f;
}

enum class Kind {
    Data, ///< A response with data (`text` = the data characters).
    Ack,  ///< A response without data.
    Nak   ///< An error response (`text` = the error code characters).
};

/// A response frame (spec 5.4 table; 3C end codes QACK / QNAK, 1C GG / NN in format 3). The SUM
/// follows the rules of spec 2.5 and 10 Q1: data responses carry one when `cfg.sumCheck`; ACK and
/// NAK never; the format 3 short responses only with `cfg.f3ShortResponseHasSum`.
inline Bytes response(const FrameConfig& cfg, Kind kind, const std::string& text,
                      const Route& r = {}) {
    Bytes f;
    const std::string p = routeText(cfg, r);
    const bool threeC = isThreeC(cfg);
    if (cfg.format == SerialFormat::Format3) {
        f.push_back(kStx);
        append(f, p);
        append(f, std::string(kind == Kind::Nak ? (threeC ? "QNAK" : "NN")
                                                : (threeC ? "QACK" : "GG")));
        append(f, text);
        f.push_back(kEtx);
        if (cfg.sumCheck && (kind == Kind::Data || cfg.f3ShortResponseHasSum)) {
            append(f, hex2(byteSum(f, 1, f.size())));
        }
        return f;
    }
    f.push_back(kind == Kind::Data ? kStx : (kind == Kind::Ack ? kAck : kNak));
    if (cfg.format == SerialFormat::Format2) {
        append(f, hex2(r.block));
    }
    append(f, p);
    append(f, text);
    if (kind == Kind::Data) {
        f.push_back(kEtx);
        if (cfg.sumCheck) {
            append(f, hex2(byteSum(f, 1, f.size())));
        }
    }
    if (cfg.format == SerialFormat::Format4) {
        f.push_back(kCr);
        f.push_back(kLf);
    }
    return f;
}

// ---- request data text, from spec 4.1 and 4.3 -------------------------------------------------

/// 3C 0401 / 1401 request data: command, subcommand, device field (already text), points, data.
inline std::string data3c(unsigned command, unsigned subcommand, const std::string& device,
                          unsigned points, const std::string& payload = "") {
    return hex4(command) + hex4(subcommand) + device + hex4(points) + payload;
}

/// 1C request data: command letters, message wait character, device field, points (2 digits),
/// data.
inline std::string data1c(const std::string& command, char wait, const std::string& device,
                          unsigned points, const std::string& payload = "") {
    return command + std::string(1, wait) + device + hex2(points) + payload;
}

// ---- the two requests the tests use most, in the dialect of each family
// ---------------------------

inline std::string padded(unsigned v, int digits) {
    char text[16];
    std::snprintf(text, sizeof(text), "%0*u", digits, v);
    return text;
}

/// Read `count` words from D`number`: 3C 0401/0000 (device "D*" + 6 digits), 1C WR (ACPU, "D" + 4
/// digits, message wait 0).
inline std::string readD(const FrameConfig& cfg, unsigned number, unsigned count) {
    if (isThreeC(cfg)) {
        return data3c(0x0401, 0x0000, "D*" + padded(number, 6), count);
    }
    return data1c("WR", '0', "D" + padded(number, 4), count);
}

/// Write the words in `words` (4 hex characters each) to D`number`: 3C 1401/0000, 1C WW.
inline std::string writeD(const FrameConfig& cfg, unsigned number, const std::string& words) {
    const unsigned count = static_cast<unsigned>(words.size() / 4);
    if (isThreeC(cfg)) {
        return data3c(0x1401, 0x0000, "D*" + padded(number, 6), count, words);
    }
    return data1c("WW", '0', "D" + padded(number, 4), count, words);
}

} // namespace mc::test::serial
