// Turns the metadata of a `.vec` record (frame 3C/1C, format F1..F4, and the optional keys below;
// for 3E and 1E the data code and access route keys) into the FrameConfig and Request it stands
// for, shared by test_frame_serial.cpp, test_parser_stream.cpp, test_alloc.cpp and
// test_vectors_format.cpp. Test code only.
//
// Optional keys on a record: `series: IqR` (default Q/L), `sum: off` (sumCheck false), `block: 3A`
// (hex block number, Format 2), `checkroute: off`, `blockcheck: off`, `f3shortsum: on`, and for 1C
// `commandset: ana` (AnA/AnU command letters) and `wait: A` (message wait, one hex digit).
#pragma once

#include "doctest/doctest.h"

#include "common/vectors.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mc::test {

inline SerialFormat serialFormatOf(const std::string& text) {
    if (text == "F1") {
        return SerialFormat::Format1;
    }
    if (text == "F2") {
        return SerialFormat::Format2;
    }
    if (text == "F3") {
        return SerialFormat::Format3;
    }
    if (text == "F4") {
        return SerialFormat::Format4;
    }
    FAIL("unknown serial format '", text, "'");
    return SerialFormat::Format1;
}

inline FrameConfig serialConfigFromVector(const Vector& v) {
    const SerialFormat format = serialFormatOf(v.field("format"));
    FrameConfig cfg =
        v.field("frame") == "1C" ? FrameConfig::frame1C(format) : FrameConfig::frame3C(format);
    if (v.field("series") == "IqR") {
        cfg.series = PlcSeries::IqR;
    }
    cfg.sumCheck = v.field("sum") != "off";
    if (!v.field("block").empty()) {
        cfg.blockNo = static_cast<uint8_t>(std::stoul(v.field("block"), nullptr, 16));
    }
    cfg.checkRoute = v.field("checkroute") != "off";
    cfg.checkBlockNo = v.field("blockcheck") != "off";
    cfg.f3ShortResponseHasSum = v.field("f3shortsum") == "on";
    if (v.field("commandset") == "ana") {
        cfg.commandSet = C1CommandSet::AnA;
    }
    if (!v.field("wait").empty()) {
        cfg.messageWait = static_cast<uint8_t>(std::stoul(v.field("wait"), nullptr, 16));
    }
    return cfg;
}

// The FrameConfig a record of any family stands for: 3E and 1E (data code, `series`, and the access
// route keys `network`, `pc`, `io` in hex where a record sets them) as well as 3C and 1C.
inline FrameConfig frameConfigFromVector(const Vector& v) {
    const std::string frame = v.field("frame");
    if (frame == "3C" || frame == "1C") {
        return serialConfigFromVector(v);
    }
    const DataCode code = v.field("code") == "Ascii" ? DataCode::Ascii : DataCode::Binary;
    FrameConfig cfg = frame == "1E" ? FrameConfig::frame1E(code) : FrameConfig::frame3E(code);
    if (v.field("series") == "IqR") {
        cfg.series = PlcSeries::IqR;
    }
    if (!v.field("network").empty()) {
        cfg.network = static_cast<uint8_t>(std::stoul(v.field("network"), nullptr, 16));
    }
    if (!v.field("pc").empty()) {
        cfg.pc = static_cast<uint8_t>(std::stoul(v.field("pc"), nullptr, 16));
    }
    if (!v.field("io").empty()) {
        cfg.io = static_cast<uint16_t>(std::stoul(v.field("io"), nullptr, 16));
    }
    return cfg;
}

inline std::vector<uint32_t> csvHex(const std::string& s) {
    std::vector<uint32_t> values;
    std::string current;
    for (char c : s) {
        if (c == ',') {
            if (!current.empty()) {
                values.push_back(static_cast<uint32_t>(std::stoul(current, nullptr, 16)));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        values.push_back(static_cast<uint32_t>(std::stoul(current, nullptr, 16)));
    }
    return values;
}

// The Request a record describes. `writeStorage` keeps the write payload alive for the caller
// (Request::data is a non-owning view).
inline Request serialRequestFromVector(const Vector& v, std::vector<uint8_t>& writeStorage) {
    const std::string op = v.field("op");
    auto head = mc::parseDevice(v.field("device"));
    REQUIRE(head.hasValue());
    const uint16_t count = static_cast<uint16_t>(std::stoul(v.field("count")));

    if (op == "ReadWords") {
        return Request::readWords(head.value(), count);
    }
    if (op == "ReadBits") {
        return Request::readBits(head.value(), count);
    }
    if (op == "WriteWords") {
        std::vector<uint32_t> values = csvHex(v.field("write"));
        writeStorage.resize(values.size() * 2);
        for (size_t i = 0; i < values.size(); ++i) {
            writeStorage[2 * i] = static_cast<uint8_t>(values[i] & 0xFFu);
            writeStorage[2 * i + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFFu);
        }
        return Request::writeWords(head.value(),
                                   ByteView{writeStorage.data(), writeStorage.size()});
    }
    if (op == "WriteBits") {
        std::vector<uint32_t> values = csvHex(v.field("write"));
        writeStorage.resize(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            writeStorage[i] = static_cast<uint8_t>(values[i]);
        }
        return Request::writeBits(head.value(), ByteView{writeStorage.data(), writeStorage.size()});
    }
    FAIL("vector ", v.id, " (", v.file, ") has unrecognized op '", op, "'");
    return Request::readWords(head.value(), count);
}

} // namespace mc::test
