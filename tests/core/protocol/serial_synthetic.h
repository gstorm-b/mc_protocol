// Synthetic serial response frames, built from the format table of the reference spec (section
// 5.4, 5.5, 5.6 and the sum check ranges of 2.5), for the tests of the serial receive state
// machine (test_serial_parser.cpp, and the allocation check in test_alloc.cpp). The builder knows
// where every part of the frame lies because it is the one that puts them there; it shares no
// code with the parser under test, and it computes the sum check with its own loop.
#pragma once

#include "core/protocol/serial_parser.h"

#include "mc/core/frame_config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mc::test {

struct SyntheticSpec {
    FrameType frame{FrameType::F3C};
    SerialFormat format{SerialFormat::Format1};
    bool sumCheck{true};
    bool f3ShortSum{false};
    detail::SerialResponseKind kind{detail::SerialResponseKind::Data};
    std::string data;  // Data: the response data characters.
    std::string err;   // Nak: the error code characters (3C: 4, 1C: 2).
    std::string block; // Format 2: the block number characters (2).
};

struct SyntheticFrame {
    SyntheticSpec spec;
    std::string name;           // for INFO() in a failing assertion
    std::vector<uint8_t> bytes; // the complete frame, starting at its start byte
    size_t blockOffset{0};
    size_t routeOffset{0};
    size_t dataOffset{0};
    size_t dataSize{0};
};

inline std::string syntheticRoute(FrameType frame) {
    return frame == FrameType::F1C ? "00FF" : "F90000FF00";
}

// The sum check of the spec: the low 8 bits of the byte sum, as 2 upper-case hex characters.
inline std::string syntheticSum(const std::string& range) {
    unsigned sum = 0;
    for (char c : range) {
        sum += static_cast<unsigned char>(c);
    }
    sum &= 0xFFu;
    const char* digits = "0123456789ABCDEF";
    std::string out;
    out += digits[sum >> 4];
    out += digits[sum & 0xFu];
    return out;
}

inline SyntheticFrame buildSyntheticFrame(const SyntheticSpec& spec) {
    using detail::SerialResponseKind;
    const bool is1C = spec.frame == FrameType::F1C;
    const std::string route = syntheticRoute(spec.frame);
    const std::string ackCode = is1C ? "GG" : "QACK";
    const std::string nakCode = is1C ? "NN" : "QNAK";
    const std::string stx(1, '\x02');
    const std::string etx(1, '\x03');
    const std::string ack(1, '\x06');
    const std::string nak(1, '\x15');
    const std::string crlf = "\r\n";
    const bool f3 = spec.format == SerialFormat::Format3;
    const bool f4 = spec.format == SerialFormat::Format4;
    const std::string blk = spec.format == SerialFormat::Format2 ? spec.block : std::string();

    SyntheticFrame frame;
    frame.spec = spec;
    frame.blockOffset = 1;
    frame.routeOffset = 1 + blk.size();
    std::string s;

    if (f3) {
        // STX P code [data | err] ETX [SUM]
        const bool isNak = spec.kind == SerialResponseKind::Nak;
        const std::string tail = isNak ? spec.err : spec.data;
        const std::string body = route + (isNak ? nakCode : ackCode) + tail;
        s = stx + body + etx;
        const bool hasData = spec.kind == SerialResponseKind::Data;
        if (spec.sumCheck && (hasData || spec.f3ShortSum)) {
            s += syntheticSum(body + etx);
        }
        frame.dataOffset = 1 + route.size() + (isNak ? nakCode.size() : ackCode.size());
        frame.dataSize = tail.size();
    } else {
        frame.dataOffset = 1 + blk.size() + route.size();
        if (spec.kind == SerialResponseKind::Data) {
            const std::string body = blk + route + spec.data;
            s = stx + body + etx;
            if (spec.sumCheck) {
                s += syntheticSum(body + etx);
            }
            frame.dataSize = spec.data.size();
        } else if (spec.kind == SerialResponseKind::Ack) {
            s = ack + blk + route;
            frame.dataSize = 0;
        } else {
            s = nak + blk + route + spec.err;
            frame.dataSize = spec.err.size();
        }
        if (f4) {
            s += crlf;
        }
    }
    frame.bytes.assign(s.begin(), s.end());
    return frame;
}

inline const char* syntheticKindName(detail::SerialResponseKind k) {
    switch (k) {
    case detail::SerialResponseKind::Data:
        return "data";
    case detail::SerialResponseKind::Ack:
        return "ack";
    case detail::SerialResponseKind::Nak:
        return "nak";
    }
    return "?";
}

// Every response kind of every format, for 3C and 1C, with the sum check on and off, and (Format 3
// only, the one place it matters) with and without a SUM on the short responses; two different
// response data lengths; two block numbers in Format 2.
inline std::vector<SyntheticFrame> syntheticSerialFrames() {
    using detail::SerialResponseKind;
    std::vector<SyntheticFrame> all;
    for (FrameType frame : {FrameType::F3C, FrameType::F1C}) {
        const std::string err = frame == FrameType::F1C ? "06" : "7151";
        for (SerialFormat format : {SerialFormat::Format1, SerialFormat::Format2,
                                    SerialFormat::Format3, SerialFormat::Format4}) {
            for (bool sum : {true, false}) {
                for (bool shortSum : {false, true}) {
                    if (shortSum && format != SerialFormat::Format3) {
                        continue;
                    }
                    for (const char* block : {"00", "3A"}) {
                        if (std::string(block) != "00" && format != SerialFormat::Format2) {
                            continue;
                        }
                        struct Variant {
                            SerialResponseKind kind;
                            const char* data;
                        };
                        for (const Variant& v : {Variant{SerialResponseKind::Data, "199512021130"},
                                                 Variant{SerialResponseKind::Data, "00010011"},
                                                 Variant{SerialResponseKind::Ack, ""},
                                                 Variant{SerialResponseKind::Nak, ""}}) {
                            SyntheticSpec spec;
                            spec.frame = frame;
                            spec.format = format;
                            spec.sumCheck = sum;
                            spec.f3ShortSum = shortSum;
                            spec.kind = v.kind;
                            spec.data = v.data;
                            spec.err = err;
                            spec.block = block;
                            SyntheticFrame built = buildSyntheticFrame(spec);
                            built.name = std::string(frame == FrameType::F1C ? "1C" : "3C") + " F" +
                                         std::to_string(static_cast<int>(format)) +
                                         (sum ? " sum" : " nosum") + (shortSum ? " shortsum" : "") +
                                         " " + syntheticKindName(v.kind) + " " + v.data + " blk " +
                                         block;
                            all.push_back(std::move(built));
                        }
                    }
                }
            }
        }
    }
    return all;
}

} // namespace mc::test
