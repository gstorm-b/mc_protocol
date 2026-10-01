// MCK-01, MCK-02, MCK-03, MCK-10, MCK-12 (end to end) and the vector parts of MCK-05 and the 3E
// part of MCK-07: MockPlc's 3E, 1E, 3C and 1C server directions against the golden vectors of
// tests/vectors/3e_*.vec, 1e_*.vec, 3c_f*.vec and 1c_f*.vec (spec Appendix A.1, A.2, A.5, A.6,
// A.12-A.19 and sections 9.6-9.8), read in the reverse direction: the request vectors go into
// MockPlc::bytesIn, the response vectors are what nextResponse must give. Frames that the vectors
// do not have are built here by hand from the tables of spec sections 5.1, 5.3 and 4.2. The serial
// stream behaviour (MCK-06, MCK-07, MCK-08) is in test_mock_stream.cpp.
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "serial_frames.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/mock/mock_plc.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using mc::ByteView;
using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::MockPlc;
using mc::MockRequestRecord;
using mc::Op;
using mc::PlcSeries;
using mc::test::loadVectors;
using mc::test::Vector;

namespace {

using Bytes = std::vector<uint8_t>;

const char* const kVectorFiles[] = {"3e_binary.vec", "3e_ascii.vec"};
const char* const k1eVectorFiles[] = {"1e_binary.vec", "1e_ascii.vec"};
const char* const kSerialVectorFiles[] = {"3c_f1.vec", "3c_f2.vec", "3c_f3.vec", "3c_f4.vec",
                                          "1c_f1.vec", "1c_f2.vec", "1c_f3.vec", "1c_f4.vec"};
const char* const kAllVectorFiles[] = {
    "3e_binary.vec", "3e_ascii.vec", "1e_binary.vec", "1e_ascii.vec", "3c_f1.vec", "3c_f2.vec",
    "3c_f3.vec",     "3c_f4.vec",    "1c_f1.vec",     "1c_f2.vec",    "1c_f3.vec", "1c_f4.vec"};

std::filesystem::path vectorsRoot() {
    return std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors";
}

std::vector<Vector> loadFile(const char* name) {
    std::vector<Vector> vectors = loadVectors(vectorsRoot() / name);
    REQUIRE_FALSE(vectors.empty());
    return vectors;
}

const Vector& byId(const std::vector<Vector>& vectors, const std::string& id) {
    for (const Vector& v : vectors) {
        if (v.id == id) {
            return v;
        }
    }
    FAIL("no vector with id ", id);
    return vectors.front();
}

bool isTaggedLater(const Vector& v) { return v.hasTag("v1.1") || v.hasTag("v2"); }

uint32_t parseHexOr(const std::string& s, uint32_t fallback) {
    return s.empty() ? fallback : static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

std::vector<uint32_t> parseCsvHex(const std::string& s) {
    std::vector<uint32_t> values;
    std::string current;
    auto flush = [&]() {
        if (!current.empty()) {
            values.push_back(static_cast<uint32_t>(std::stoul(current, nullptr, 16)));
            current.clear();
        }
    };
    for (char c : s) {
        if (c == ',') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return values;
}

mc::FrameType frameOf(const Vector& v) {
    const std::string frame = v.field("frame");
    if (frame == "1E") {
        return mc::FrameType::F1E;
    }
    if (frame == "3C") {
        return mc::FrameType::F3C;
    }
    if (frame == "1C") {
        return mc::FrameType::F1C;
    }
    return mc::FrameType::F3E;
}

// The setting a vector's metadata names, taken from the response vector when it names it and from
// the request vector otherwise (3C3-SHORTSUM-* put the f3shortsum setting on the response only).
std::string settingOf(const Vector& request, const Vector* response, const char* key) {
    if (response != nullptr && !response->field(key).empty()) {
        return response->field(key);
    }
    return request.field(key);
}

FrameConfig configFor(const Vector& v, const Vector* response = nullptr) {
    const mc::FrameType frame = frameOf(v);
    if (frame == mc::FrameType::F3C || frame == mc::FrameType::F1C) {
        const std::string f = v.field("format"); // "F1" .. "F4"
        const auto format = static_cast<mc::SerialFormat>(f.at(1) - '0');
        FrameConfig cfg = frame == mc::FrameType::F3C ? FrameConfig::frame3C(format)
                                                      : FrameConfig::frame1C(format);
        cfg.series = v.field("series") == "IqR" ? PlcSeries::IqR : PlcSeries::QL;
        cfg.sumCheck = settingOf(v, response, "sum") != "off";
        cfg.f3ShortResponseHasSum = settingOf(v, response, "f3shortsum") == "on";
        cfg.blockNo = static_cast<uint8_t>(parseHexOr(v.field("block"), cfg.blockNo));
        cfg.commandSet =
            v.field("commandset") == "ana" ? mc::C1CommandSet::AnA : mc::C1CommandSet::ACPU;
        return cfg;
    }
    const DataCode code = v.field("code") == "Ascii" ? DataCode::Ascii : DataCode::Binary;
    FrameConfig cfg =
        frame == mc::FrameType::F1E ? FrameConfig::frame1E(code) : FrameConfig::frame3E(code);
    cfg.series = v.field("series") == "IqR" ? PlcSeries::IqR : PlcSeries::QL;
    cfg.network = static_cast<uint8_t>(parseHexOr(v.field("network"), cfg.network));
    cfg.pc = static_cast<uint8_t>(parseHexOr(v.field("pc"), cfg.pc));
    cfg.io = static_cast<uint16_t>(parseHexOr(v.field("io"), cfg.io));
    cfg.station = static_cast<uint8_t>(parseHexOr(v.field("station"), cfg.station));
    return cfg;
}

// What a request vector's metadata says the mock must have decoded.
struct Meta {
    Op op;
    Device head;
    uint16_t count;
    PlcSeries series;
};

Meta metaOf(const Vector& v) {
    Meta m{};
    const std::string op = v.field("op");
    if (op == "ReadWords") {
        m.op = Op::ReadWords;
    } else if (op == "ReadBits") {
        m.op = Op::ReadBits;
    } else if (op == "WriteWords") {
        m.op = Op::WriteWords;
    } else if (op == "WriteBits") {
        m.op = Op::WriteBits;
    } else {
        FAIL("vector ", v.id, " has unrecognized op '", op, "'");
    }
    auto head = mc::parseDevice(v.field("device"));
    REQUIRE(head.hasValue());
    m.head = head.value();
    m.count = static_cast<uint16_t>(std::stoul(v.field("count")));
    m.series = v.field("series") == "IqR" ? PlcSeries::IqR : PlcSeries::QL;
    return m;
}

void feed(MockPlc& plc, const Bytes& bytes) { plc.bytesIn(ByteView{bytes.data(), bytes.size()}); }

void feedInChunks(MockPlc& plc, const Bytes& bytes, size_t chunk) {
    for (size_t pos = 0; pos < bytes.size(); pos += chunk) {
        size_t n = std::min(chunk, bytes.size() - pos);
        plc.bytesIn(ByteView{bytes.data() + pos, n});
    }
}

// Every pending response, concatenated; `count` (if given) receives how many there were.
Bytes drain(MockPlc& plc, size_t* count = nullptr) {
    Bytes all;
    size_t n = 0;
    ByteView view;
    while (plc.nextResponse(view)) {
        all.insert(all.end(), view.data, view.data + view.size);
        ++n;
    }
    if (count != nullptr) {
        *count = n;
    }
    return all;
}

// Seeds the memory a read request will see from the `expect:` list of its response vector.
void seedFromExpect(MockPlc& plc, const Meta& m, const std::string& expect) {
    std::vector<uint32_t> values = parseCsvHex(expect);
    const bool bitDevice = mc::deviceInfo(m.head.type).kind == mc::DeviceKind::Bit;
    for (size_t i = 0; i < values.size(); ++i) {
        // Word k of a bit device starts at head + 16k (spec 2.4).
        const uint32_t step = (m.op != Op::ReadBits && bitDevice) ? 16 : 1;
        Device d{m.head.type, m.head.number + static_cast<uint32_t>(i) * step};
        if (m.op == Op::ReadBits) {
            plc.setBit(d, values[i] != 0);
        } else {
            plc.setWord(d, static_cast<uint16_t>(values[i]));
        }
    }
}

// The write of a request vector must be visible in memory afterwards (`write:` metadata).
void checkWriteApplied(const MockPlc& plc, const Meta& m, const std::string& write) {
    std::vector<uint32_t> values = parseCsvHex(write);
    REQUIRE(values.size() == m.count);
    for (size_t i = 0; i < values.size(); ++i) {
        Device d{m.head.type, m.head.number + static_cast<uint32_t>(i)};
        if (m.op == Op::WriteBits) {
            CHECK_MESSAGE(plc.bit(d) == (values[i] != 0), "point ", i);
        } else {
            CHECK_MESSAGE(plc.word(d) == values[i], "word ", i);
        }
    }
}

bool sameRecord(const MockRequestRecord& a, const MockRequestRecord& b) {
    return a.frame == b.frame && a.op == b.op && a.head == b.head && a.count == b.count &&
           a.series == b.series && a.answered == b.answered &&
           a.answeredWith.code == b.answeredWith.code &&
           a.answeredWith.plcCode == b.answeredWith.plcCode &&
           a.answeredWith.info.command == b.answeredWith.info.command &&
           a.answeredWith.info.subcommand == b.answeredWith.info.subcommand;
}

bool sameLog(const std::vector<MockRequestRecord>& a, const std::vector<MockRequestRecord>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!sameRecord(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

// ---- hand-built frames, from spec section 5.1 / 4.1 (never from the client encoder) ------------

Bytes hexText(uint32_t value, int digits) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%0*X", digits, static_cast<unsigned>(value));
    return Bytes(buf, buf + digits);
}

void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }

void appendText(Bytes& dst, const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
        dst.push_back(static_cast<uint8_t>(*p));
    }
}

const Bytes kBinaryRoute = {0x00, 0xFF, 0xFF, 0x03, 0x00};

// Binary 3E request: 50 00, route, length, timer 0010, command, subcommand, device, count, data.
Bytes bin3e(uint16_t cmd, uint16_t sub, const Bytes& device, uint16_t count,
            const Bytes& payload = {}, const Bytes& route = kBinaryRoute) {
    Bytes data = {0x10, 0x00, static_cast<uint8_t>(cmd & 0xFF), static_cast<uint8_t>(cmd >> 8),
                  static_cast<uint8_t>(sub & 0xFF), static_cast<uint8_t>(sub >> 8)};
    append(data, device);
    data.push_back(static_cast<uint8_t>(count & 0xFF));
    data.push_back(static_cast<uint8_t>(count >> 8));
    append(data, payload);
    Bytes frame = {0x50, 0x00};
    append(frame, route);
    frame.push_back(static_cast<uint8_t>(data.size() & 0xFF));
    frame.push_back(static_cast<uint8_t>(data.size() >> 8));
    append(frame, data);
    return frame;
}

// ASCII 3E request: "5000", route text, length, then the same fields as text.
Bytes asc3e(uint16_t cmd, uint16_t sub, const char* device, uint16_t count,
            const char* payload = "", const char* route = "00FF03FF00") {
    Bytes data;
    appendText(data, "0010");
    append(data, hexText(cmd, 4));
    append(data, hexText(sub, 4));
    appendText(data, device);
    append(data, hexText(count, 4));
    appendText(data, payload);
    Bytes frame;
    appendText(frame, "5000");
    appendText(frame, route);
    append(frame, hexText(static_cast<uint32_t>(data.size()), 4));
    append(frame, data);
    return frame;
}

const Bytes kD100Bin = {0x64, 0x00, 0x00, 0xA8};
const Bytes kM100Bin = {0x64, 0x00, 0x00, 0x90};

// The 11-byte / 22-character "no data" success response with the default route (V-3E-B-06/A-06).
const Bytes kAckBin = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00};

// ---- hand-built 1E frames, from spec sections 5.3, 4.2 and 3.3 (never from the client encoder) ---

// Binary 1E request: command, PC, timer 000A (LE), device (number LE 4 + code LE 2), points, 00,
// then the write data.
Bytes bin1e(uint8_t cmd, const Bytes& device, uint8_t points, const Bytes& payload = {},
            uint8_t pc = 0xFF) {
    Bytes frame = {cmd, pc, 0x0A, 0x00};
    append(frame, device);
    frame.push_back(points);
    frame.push_back(0x00);
    append(frame, payload);
    return frame;
}

// ASCII 1E request: command (2 characters), PC (2), timer "000A", device (code 4 + number 8),
// points (2), "00", then the write data.
Bytes asc1e(uint8_t cmd, const char* device, uint8_t points, const char* payload = "",
            const char* pc = "FF") {
    Bytes frame;
    append(frame, hexText(cmd, 2));
    appendText(frame, pc);
    appendText(frame, "000A");
    appendText(frame, device);
    append(frame, hexText(points, 2));
    appendText(frame, "00");
    appendText(frame, payload);
    return frame;
}

const Bytes kD100Bin1e = {0x64, 0x00, 0x00, 0x00, 0x20, 0x44};
const Bytes kM100Bin1e = {0x64, 0x00, 0x00, 0x00, 0x20, 0x4D};

std::string asText(const Bytes& b) { return std::string(b.begin(), b.end()); }

} // namespace

TEST_CASE("MCK-01 every request vector (3E, 1E, 3C, 1C) decodes to the op, head, count, series and "
          "write data of its metadata") {
    for (const char* file : kAllVectorFiles) {
        for (const Vector& v : loadFile(file)) {
            if (v.field("kind") != "request" || isTaggedLater(v)) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            MockPlc plc(configFor(v));
            feed(plc, v.bytes);

            REQUIRE(plc.requests().size() == 1);
            const Meta m = metaOf(v);
            const MockRequestRecord& rec = plc.requests().front();
            CHECK(rec.frame == frameOf(v));
            CHECK(rec.op == m.op);
            CHECK(rec.head == m.head);
            CHECK(rec.count == m.count);
            CHECK(rec.series == m.series);
            CHECK(rec.answered);
            CHECK(rec.answeredWith.ok());

            size_t responses = 0;
            drain(plc, &responses);
            CHECK(responses == 1);
            if (m.op == Op::WriteWords || m.op == Op::WriteBits) {
                checkWriteApplied(plc, m, v.field("write"));
            }
        }
    }
}

TEST_CASE("MCK-02 with memory seeded from the metadata, every success response vector (3E, 1E, 3C, "
          "1C) is reproduced byte for byte") {
    int reproduced = 0;
    int skipped = 0;
    for (const char* file : kAllVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        for (const Vector& v : vectors) {
            // checkroute: off / blockcheck: off vectors are responses whose route or block number
            // differs from the request's, for a client that does not check it: a PLC that echoes
            // the request cannot produce them.
            if (v.field("kind") != "response" || isTaggedLater(v) ||
                !v.field("checkroute").empty() || !v.field("blockcheck").empty()) {
                if (v.field("kind") == "response" && !isTaggedLater(v)) {
                    ++skipped;
                }
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            REQUIRE_FALSE(v.field("of").empty());
            const std::string requestId = v.field("of");
            const Vector& request = byId(vectors, requestId);

            MockPlc plc(configFor(request, &v));
            const Meta m = metaOf(request);
            seedFromExpect(plc, m, v.field("expect"));
            feed(plc, request.bytes);

            size_t responses = 0;
            Bytes got = drain(plc, &responses);
            CHECK(responses == 1);
            ++reproduced;
            CHECK(got == v.bytes);
        }
    }
    // Exact counts, so the skip filter cannot silently grow: the files carry 10 responses with
    // `checkroute: off` (8) or `blockcheck: off` (2), and every other untagged success response is
    // reproduced.
    CHECK(skipped == 10);
    CHECK(reproduced == 86);
}

TEST_CASE("MCK-03 the 3E error vectors are reproduced with failRange") {
    int reproduced = 0;
    for (const char* file : kVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        for (const Vector& v : vectors) {
            // Only errors the mock itself produces: a PLC end code with error information. The
            // FrameMismatch / LengthMismatch vectors are malformed responses for the client's
            // parser, and 3E-11 carries no error information, which the mock always sends.
            if (v.field("kind") != "response-error" || v.field("error") != "Plc" ||
                v.field("errorinfo").empty() || isTaggedLater(v)) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            const std::string requestId = v.field("of");
            const Vector& request = byId(vectors, requestId);
            const Meta m = metaOf(request);
            const uint16_t code = static_cast<uint16_t>(parseHexOr(v.field("plccode"), 0));

            MockPlc plc(configFor(request));
            plc.failRange(m.head.type, m.head.number, m.head.number, code);
            feed(plc, request.bytes);

            CHECK(drain(plc) == v.bytes);
            REQUIRE(plc.requests().size() == 1);
            const mc::Error& err = plc.requests().front().answeredWith;
            CHECK(plc.requests().front().answered);
            CHECK(err.category == mc::ErrorCategory::Plc);
            CHECK(err.code == mc::ErrorCode::PlcError);
            CHECK(err.plcCode == code);
            std::vector<uint32_t> info = parseCsvHex(v.field("errorinfo"));
            REQUIRE(info.size() == 6);
            CHECK(err.info.network == info[0]);
            CHECK(err.info.pc == info[1]);
            CHECK(err.info.io == info[2]);
            CHECK(err.info.station == info[3]);
            CHECK(err.info.command == info[4]);
            CHECK(err.info.subcommand == info[5]);
            ++reproduced;
        }
    }
    CHECK(reproduced == 2); // V-3E-B-10 and V-3E-A-10
}

// The error information ends with the request's command and subcommand (spec section 5.1, field
// 8b). The vectors only carry 0401/0000, so these cases pin the subcommand of a Q/L bit read
// (0001) and of an iQ-R word read (0002), in both codes.
TEST_CASE("MCK-03 the error information carries the request's command and subcommand") {
    struct Case {
        const char* name;
        bool ascii;
        Bytes request;
        Device faulted;
        uint16_t subcommand;
        Bytes tail; // command + subcommand as they appear at the end of the response
    };
    const Bytes iqrD100 = {0x64, 0x00, 0x00, 0x00, 0xA8, 0x00};
    const Bytes tailBitQl = {0x01, 0x04, 0x01, 0x00};
    const Bytes tailWordIqr = {0x01, 0x04, 0x02, 0x00};
    Bytes tailBitQlAscii;
    Bytes tailWordIqrAscii;
    appendText(tailBitQlAscii, "04010001");
    appendText(tailWordIqrAscii, "04010002");
    const std::vector<Case> cases = {
        {"binary Q/L bit read", false, bin3e(0x0401, 0x0001, kM100Bin, 8),
         Device{DeviceType::M, 100}, 0x0001, tailBitQl},
        {"binary iQ-R word read", false, bin3e(0x0401, 0x0002, iqrD100, 1),
         Device{DeviceType::D, 100}, 0x0002, tailWordIqr},
        {"ASCII Q/L bit read", true, asc3e(0x0401, 0x0001, "M*000100", 8),
         Device{DeviceType::M, 100}, 0x0001, tailBitQlAscii},
        {"ASCII iQ-R word read", true, asc3e(0x0401, 0x0002, "D***00000100", 1),
         Device{DeviceType::D, 100}, 0x0002, tailWordIqrAscii},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        MockPlc plc(FrameConfig::frame3E(c.ascii ? DataCode::Ascii : DataCode::Binary));
        plc.failRange(c.faulted.type, c.faulted.number, c.faulted.number, 0xC051);
        feed(plc, c.request);
        const Bytes response = drain(plc);
        REQUIRE(response.size() > c.tail.size());
        CHECK(Bytes(response.end() - static_cast<std::ptrdiff_t>(c.tail.size()), response.end()) ==
              c.tail);
        REQUIRE(plc.requests().size() == 1);
        const mc::Error& err = plc.requests().front().answeredWith;
        CHECK(err.plcCode == 0xC051);
        CHECK(err.info.command == 0x0401);
        CHECK(err.info.subcommand == c.subcommand);
    }
}

TEST_CASE("MCK-03 a faulted request changes no memory and a fault elsewhere does not touch it") {
    MockPlc plc(FrameConfig::frame3E());
    plc.failRange(DeviceType::D, 100, 100, 0xC051);

    feed(plc, bin3e(0x1401, 0x0000, kD100Bin, 3, {0x95, 0x19, 0x02, 0x12, 0x30, 0x11}));
    Bytes failed = drain(plc);
    CHECK(failed.size() == 20);
    CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
    CHECK(plc.word(Device{DeviceType::D, 101}) == 0);

    // D101..D103 does not touch [100, 100]: the write goes through.
    feed(plc, bin3e(0x1401, 0x0000, {0x65, 0x00, 0x00, 0xA8}, 1, {0x34, 0x12}));
    CHECK(drain(plc) == kAckBin);
    CHECK(plc.word(Device{DeviceType::D, 101}) == 0x1234);

    // A range that starts below the fault and reaches it is touched.
    feed(plc, bin3e(0x0401, 0x0000, {0x63, 0x00, 0x00, 0xA8}, 2));
    CHECK(drain(plc).size() == 20);
    // Another device type with the same numbers is not.
    feed(plc, bin3e(0x0401, 0x0000, {0x64, 0x00, 0x00, 0xB4}, 1)); // W64
    CHECK(drain(plc).size() == 13);
}

TEST_CASE("MCK-03 a word request on a bit device touches 16 points per word") {
    MockPlc plc(FrameConfig::frame3E());
    plc.failRange(DeviceType::M, 115, 115, 0xC051);
    feed(plc, bin3e(0x0401, 0x0000, kM100Bin, 1)); // M100..M115
    CHECK(drain(plc).size() == 20);
    feed(plc, bin3e(0x0401, 0x0000, {0x74, 0x00, 0x00, 0x90}, 1)); // M116..M131
    CHECK(drain(plc).size() == 13);
}

TEST_CASE("MCK-12 a request reaching the device limit gets outOfRangeQna, one below it succeeds") {
    for (const char* file : kVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        const bool ascii = std::string(file) == "3e_ascii.vec";
        const std::string readDId = ascii ? "V-3E-A-01" : "V-3E-B-01"; // D100 x 3
        const std::string readMId = ascii ? "V-3E-A-03" : "V-3E-B-03"; // M100 x 8 bits
        const Vector& readD = byId(vectors, readDId);
        const Vector& readM = byId(vectors, readMId);
        INFO(file);

        MockPlc plc(configFor(readD));
        plc.setDeviceLimit(DeviceType::D, 102); // D100..D102 reaches D102
        feed(plc, readD.bytes);
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests().back().answeredWith.plcCode == 0xC051);
        CHECK(plc.requests().back().answeredWith.category == mc::ErrorCategory::Plc);
        CHECK_FALSE(drain(plc).empty());

        plc.setDeviceLimit(DeviceType::D, 103); // D100..D102 ends below D103
        feed(plc, readD.bytes);
        CHECK(plc.requests().back().answeredWith.ok());
        CHECK_FALSE(drain(plc).empty());

        plc.setDeviceLimit(DeviceType::M, 107); // M100..M107 reaches M107
        feed(plc, readM.bytes);
        CHECK(plc.requests().back().answeredWith.plcCode == 0xC051);
        drain(plc);
        plc.setDeviceLimit(DeviceType::M, 108);
        feed(plc, readM.bytes);
        CHECK(plc.requests().back().answeredWith.ok());
    }
}

TEST_CASE("MCK-12 the out-of-range response is the error frame with the request's error "
          "information, and a limit-stopped write changes no memory") {
    MockPlc plc(FrameConfig::frame3E());
    plc.setDeviceLimit(DeviceType::D, 101);
    feed(plc, bin3e(0x1401, 0x0000, kD100Bin, 2, {0x01, 0x00, 0x02, 0x00}));
    const Bytes expected = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0B, 0x00, 0x51, 0xC0,
                            0x00, 0xFF, 0xFF, 0x03, 0x00, 0x01, 0x14, 0x00, 0x00};
    CHECK(drain(plc) == expected);
    CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
}

TEST_CASE("MCK-12 a word read of a bit device counts 16 points per word against the limit") {
    MockPlc plc(FrameConfig::frame3E());
    plc.setDeviceLimit(DeviceType::M, 8190);
    // M8180 x 1 word covers M8180..M8195: reaches the limit. As bits x 10 it ends at M8189.
    feed(plc, bin3e(0x0401, 0x0000, {0xF4, 0x1F, 0x00, 0x90}, 1));
    CHECK(plc.requests().back().answeredWith.plcCode == 0xC051);
    drain(plc);
    feed(plc, bin3e(0x0401, 0x0001, {0xF4, 0x1F, 0x00, 0x90}, 10));
    CHECK(plc.requests().back().answeredWith.ok());
}

TEST_CASE(
    "MCK-05 every request vector (3E, 1E, 3C, 1C) fed one byte at a time, in 3-byte pieces, or "
    "twice in one buffer decodes identically") {
    for (const char* file : kAllVectorFiles) {
        for (const Vector& v : loadFile(file)) {
            if (v.field("kind") != "request" || isTaggedLater(v)) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            const Meta m = metaOf(v);
            const bool write = m.op == Op::WriteWords || m.op == Op::WriteBits;

            MockPlc whole(configFor(v));
            feed(whole, v.bytes);
            Bytes reference = drain(whole);
            REQUIRE_FALSE(reference.empty());

            for (size_t chunk : {size_t{1}, size_t{3}}) {
                MockPlc plc(configFor(v));
                feedInChunks(plc, v.bytes, chunk);
                CHECK(drain(plc) == reference);
                CHECK(sameLog(plc.requests(), whole.requests()));
                if (write) {
                    checkWriteApplied(plc, m, v.field("write"));
                }
            }

            MockPlc twice(configFor(v));
            Bytes both = v.bytes;
            append(both, v.bytes);
            feed(twice, both);
            size_t responses = 0;
            Bytes got = drain(twice, &responses);
            CHECK(responses == 2);
            Bytes expected = reference;
            append(expected, reference);
            CHECK(got == expected);
            REQUIRE(twice.requests().size() == 2);
            CHECK(sameRecord(twice.requests()[0], whole.requests()[0]));
            CHECK(sameRecord(twice.requests()[1], whole.requests()[0]));
        }
    }
}

TEST_CASE("MCK-05 3E: responses come out in request order, one per request") {
    MockPlc plc(FrameConfig::frame3E());
    plc.setWords(Device{DeviceType::D, 100}, {0x1111, 0x2222});
    Bytes stream = bin3e(0x0401, 0x0000, kD100Bin, 1);
    append(stream, bin3e(0x0401, 0x0000, {0x65, 0x00, 0x00, 0xA8}, 1));
    feed(plc, stream);

    ByteView view;
    REQUIRE(plc.nextResponse(view));
    CHECK(view.size == 13);
    CHECK(view.data[11] == 0x11);
    REQUIRE(plc.nextResponse(view));
    CHECK(view.data[11] == 0x22);
    CHECK_FALSE(plc.nextResponse(view));
}

TEST_CASE("MCK-07 a non-default request route is echoed in the response header") {
    int checked = 0;
    for (const char* file : kVectorFiles) {
        const bool ascii = std::string(file) == "3e_ascii.vec";
        // Route field: binary bytes 2..6, ASCII characters 4..13 (after the subheader).
        const size_t begin = ascii ? 4 : 2;
        const size_t end = ascii ? 14 : 7;
        for (const Vector& v : loadFile(file)) {
            if (v.field("kind") != "request" || isTaggedLater(v) ||
                (v.field("network").empty() && v.field("pc").empty() && v.field("io").empty() &&
                 v.field("station").empty())) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            const Bytes requestRoute(v.bytes.begin() + begin, v.bytes.begin() + end);

            // The same request with the default route, and what the mock answers to it.
            Bytes defaultRoute = kBinaryRoute;
            if (ascii) {
                defaultRoute.clear();
                appendText(defaultRoute, "00FF03FF00");
            }
            Bytes defaultRequest = v.bytes;
            std::copy(defaultRoute.begin(), defaultRoute.end(), defaultRequest.begin() + begin);

            MockPlc a(configFor(v));
            a.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
            feed(a, v.bytes);
            const Bytes got = drain(a);

            MockPlc b(configFor(v));
            b.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
            feed(b, defaultRequest);
            const Bytes defaultAnswer = drain(b);
            REQUIRE(defaultAnswer.size() > end);
            Bytes expected = defaultAnswer;
            std::copy(requestRoute.begin(), requestRoute.end(), expected.begin() + begin);

            CHECK(got == expected);
            CHECK(got != defaultAnswer); // the route really differs from the default one
            ++checked;
        }
    }
    CHECK(checked == 4); // 3E-12, 3E-13 and their ASCII twins
}

TEST_CASE("MCK-07 an error response echoes the request route in the error information") {
    MockPlc plc(FrameConfig::frame3E());
    plc.failRange(DeviceType::D, 100, 100, 0xC051);
    const Bytes route = {0x02, 0x03, 0xE1, 0x03, 0x05};
    feed(plc, bin3e(0x0401, 0x0000, kD100Bin, 3, {}, route));
    const Bytes expected = {0xD0, 0x00, 0x02, 0x03, 0xE1, 0x03, 0x05, 0x0B, 0x00, 0x51, 0xC0,
                            0x02, 0x03, 0xE1, 0x03, 0x05, 0x01, 0x04, 0x00, 0x00};
    CHECK(drain(plc) == expected);
    REQUIRE(plc.requests().size() == 1);
    CHECK(plc.requests()[0].answeredWith.info.network == 0x02);
    CHECK(plc.requests()[0].answeredWith.info.pc == 0x03);
    CHECK(plc.requests()[0].answeredWith.info.io == 0x03E1);
    CHECK(plc.requests()[0].answeredWith.info.station == 0x05);
}

TEST_CASE("MCK-10 3E odd bit read: binary pads the low nibble with zero, ASCII has no dummy") {
    {
        std::vector<Vector> vectors = loadFile("3e_binary.vec");
        MockPlc plc(configFor(byId(vectors, "V-3E-B-08")));
        plc.setBits(Device{DeviceType::M, 100}, {true, false, true, false, true});
        feed(plc, byId(vectors, "V-3E-B-08").bytes);
        Bytes got = drain(plc);
        REQUIRE(got.size() == 14); // V-3E-B-09
        CHECK((got.back() & 0x0F) == 0);
        CHECK(got == byId(vectors, "V-3E-B-09").bytes);
    }
    {
        std::vector<Vector> vectors = loadFile("3e_ascii.vec");
        MockPlc plc(configFor(byId(vectors, "V-3E-A-08")));
        plc.setBits(Device{DeviceType::M, 100}, {true, false, true, false, true});
        feed(plc, byId(vectors, "V-3E-A-08").bytes);
        Bytes got = drain(plc);
        REQUIRE(got.size() == 27); // V-3E-A-09: 22 header characters + 5 points
        CHECK(std::string(got.end() - 5, got.end()) == "10101");
        CHECK(got == byId(vectors, "V-3E-A-09").bytes);
    }
}

TEST_CASE("MCK-01 word access to a bit device: word k covers head + 16k .. head + 16k + 15") {
    MockPlc plc(FrameConfig::frame3E());
    // Spec section 2.4: M100 read as 2 words returns 1234H, 0002H.
    plc.setWords(Device{DeviceType::M, 100}, {0x1234, 0x0002});
    feed(plc, bin3e(0x0401, 0x0000, kM100Bin, 2));
    const Bytes expected = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
                            0x34, 0x12, 0x02, 0x00};
    CHECK(drain(plc) == expected);

    feed(plc, bin3e(0x1401, 0x0000, kM100Bin, 1, {0x01, 0x80}));
    CHECK(drain(plc) == kAckBin);
    CHECK(plc.bit(Device{DeviceType::M, 100}));
    CHECK(plc.bit(Device{DeviceType::M, 115}));
    CHECK_FALSE(plc.bit(Device{DeviceType::M, 101}));
    CHECK(plc.word(Device{DeviceType::M, 116}) == 0x0002); // the second word is untouched
}

TEST_CASE("MCK-01 hex-radix and iQ-R devices are decoded in their own radix and width") {
    // ASCII Q/L: W*00001F x 1, and X*00001F x 1 bit.
    MockPlc ascii(FrameConfig::frame3E(DataCode::Ascii));
    ascii.setWord(Device{DeviceType::W, 0x1F}, 0xABCD);
    ascii.setBit(Device{DeviceType::X, 0x1F}, true);
    feed(ascii, asc3e(0x0401, 0x0000, "W*00001F", 1));
    Bytes got = drain(ascii);
    CHECK(std::string(got.end() - 4, got.end()) == "ABCD");
    feed(ascii, asc3e(0x0401, 0x0001, "X*00001F", 1));
    got = drain(ascii);
    CHECK(std::string(got.end() - 1, got.end()) == "1");
    CHECK(ascii.requests().back().head == Device{DeviceType::X, 0x1F});

    // ASCII iQ-R: X***0000001F.
    feed(ascii, asc3e(0x0401, 0x0003, "X***0000001F", 1));
    CHECK(ascii.requests().back().series == PlcSeries::IqR);
    CHECK(ascii.requests().back().head == Device{DeviceType::X, 0x1F});
    drain(ascii);

    // The star and the leading zeros may be spaces (spec section 3.2).
    feed(ascii, asc3e(0x0401, 0x0001, "X 00001F", 1));
    CHECK(ascii.requests().back().head == Device{DeviceType::X, 0x1F});
    drain(ascii);
    feed(ascii, asc3e(0x0401, 0x0001, "X*    1F", 1));
    CHECK(ascii.requests().back().head == Device{DeviceType::X, 0x1F});
    drain(ascii);
    feed(ascii, asc3e(0x0401, 0x0001, "X     1F", 1));
    CHECK(ascii.requests().back().answeredWith.ok());
    CHECK(ascii.requests().back().head == Device{DeviceType::X, 0x1F});
    drain(ascii);

    // Binary iQ-R: 4-byte number, 2-byte code (X = 009C).
    MockPlc plc(FrameConfig::frame3E());
    plc.setBit(Device{DeviceType::X, 0x1F}, true);
    feed(plc, bin3e(0x0401, 0x0003, {0x1F, 0x00, 0x00, 0x00, 0x9C, 0x00}, 1));
    CHECK(plc.requests().back().head == Device{DeviceType::X, 0x1F});
    CHECK(drain(plc).back() == 0x10);
}

TEST_CASE("MCK-01 request data the mock cannot execute is answered with unsupportedQna") {
    struct Case {
        const char* name;
        Bytes frame;
    };
    const Case cases[] = {
        {"bit read of a word device", bin3e(0x0401, 0x0001, kD100Bin, 1)},
        {"unknown subcommand", bin3e(0x0401, 0x0004, kD100Bin, 1)},
        {"unknown device code", bin3e(0x0401, 0x0000, {0x64, 0x00, 0x00, 0x01}, 1)},
        {"zero points", bin3e(0x0401, 0x0000, kD100Bin, 0)},
        {"read with trailing data", bin3e(0x0401, 0x0000, kD100Bin, 1, {0x00})},
        {"write with short data", bin3e(0x1401, 0x0000, kD100Bin, 2, {0x01, 0x00, 0x02})},
        {"bit write with a nibble of 2", bin3e(0x1401, 0x0001, kM100Bin, 2, {0x21})},
        {"Q/L subcommand with an iQ-R device field",
         bin3e(0x0401, 0x0000, {0x64, 0x00, 0x00, 0x00, 0xA8, 0x00}, 1)},
        {"device field missing", bin3e(0x0401, 0x0000, {}, 0)},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        MockPlc plc(FrameConfig::frame3E());
        plc.setWord(Device{DeviceType::D, 100}, 7);
        feed(plc, c.frame);
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests()[0].answered);
        CHECK(plc.requests()[0].answeredWith.plcCode == 0xC059);
        Bytes got = drain(plc);
        REQUIRE(got.size() >= 11);
        CHECK(got[9] == 0x59);
        CHECK(got[10] == 0xC0);
        CHECK(plc.word(Device{DeviceType::D, 100}) == 7);
    }

    // Too short to hold even the command: error information carries zeros for it.
    MockPlc plc(FrameConfig::frame3E());
    Bytes tiny = {0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x10, 0x00};
    feed(plc, tiny);
    REQUIRE(plc.requests().size() == 1);
    CHECK(plc.requests()[0].answeredWith.info.command == 0);
    CHECK(drain(plc).size() == 20);
}

TEST_CASE("MCK-01 ASCII request data with a bad character is answered, not executed") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Ascii));
    feed(plc, asc3e(0x0401, 0x0000, "D*000100", 1, "")); // sanity: fine
    drain(plc);
    plc.setWord(Device{DeviceType::D, 100}, 5);
    feed(plc, asc3e(0x1401, 0x0000, "D*000100", 1, "12G4"));
    CHECK(plc.requests().back().answeredWith.plcCode == 0xC059);
    CHECK(plc.word(Device{DeviceType::D, 100}) == 5);
    drain(plc);
    feed(plc, asc3e(0x1401, 0x0001, "M*000100", 2, "12"));
    CHECK(plc.requests().back().answeredWith.plcCode == 0xC059); // bit characters must be 0/1
    drain(plc);
    feed(plc, asc3e(0x0401, 0x0000, "D*00G100", 1));
    CHECK(plc.requests().back().answeredWith.plcCode == 0xC059); // digit outside the radix
}

TEST_CASE("MCK-05 3E: bytes that cannot start a request are dropped and logged once as "
          "unanswered") {
    for (DataCode code : {DataCode::Binary, DataCode::Ascii}) {
        INFO((code == DataCode::Ascii ? "ASCII" : "Binary"));
        const Bytes good = code == DataCode::Ascii ? asc3e(0x0401, 0x0000, "D*000100", 1)
                                                   : bin3e(0x0401, 0x0000, kD100Bin, 1);
        // Junk in front of a good request, delivered in one piece and byte by byte.
        for (size_t chunk : {size_t{64}, size_t{1}}) {
            MockPlc plc(FrameConfig::frame3E(code));
            Bytes stream = {0x01, 0x02, 0x03, 0x04, 0x05};
            append(stream, good);
            feedInChunks(plc, stream, chunk);
            REQUIRE(plc.requests().size() == 2);
            CHECK_FALSE(plc.requests()[0].answered);
            CHECK(plc.requests()[0].answeredWith.ok());
            CHECK(plc.requests()[1].answered);
            size_t responses = 0;
            drain(plc, &responses);
            CHECK(responses == 1);
        }
        // Junk alone is never answered; a second run after a good request is a second record.
        MockPlc plc(FrameConfig::frame3E(code));
        feed(plc, Bytes{0xFF, 0xFF});
        feed(plc, Bytes{0xFF});
        CHECK(plc.requests().size() == 1);
        CHECK(drain(plc).empty());
        feed(plc, good);
        feed(plc, Bytes{0xFF});
        CHECK(plc.requests().size() == 3);
        CHECK_FALSE(plc.requests()[2].answered);
    }
}

TEST_CASE("MCK-05 3E ASCII: a header with a non-hex character is junk, and a half subheader "
          "waits") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Ascii));
    feed(plc, Bytes{'5', '0'});
    CHECK(plc.requests().empty());
    feed(plc, Bytes{'0', '0'});
    CHECK(plc.requests().empty()); // "5000" so far: a frame is still being received
    // Non-hex route: the first '5' is dropped, and no later position starts a subheader.
    feed(plc, Bytes{'0', '0', 'F', 'F', '0', '3', 'F', 'G', '0', '0', '0', '0', '0', '0'});
    CHECK(plc.requests().size() == 1);
    CHECK_FALSE(plc.requests()[0].answered);
    CHECK(drain(plc).empty());
}

TEST_CASE("MCK-05 3E: a request split anywhere is not executed before it is complete") {
    MockPlc plc(FrameConfig::frame3E());
    const Bytes frame = bin3e(0x1401, 0x0000, kD100Bin, 1, {0x34, 0x12});
    plc.bytesIn(ByteView{frame.data(), frame.size() - 1});
    CHECK(plc.requests().empty());
    CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
    ByteView view;
    CHECK_FALSE(plc.nextResponse(view));
    plc.bytesIn(ByteView{frame.data() + frame.size() - 1, 1});
    CHECK(plc.requests().size() == 1);
    CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1234);
}

TEST_CASE("MCK-01 a response longer than the u16 length field is refused") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Ascii));
    // 16384 words are 65536 characters plus the end code: cannot be described by the length field.
    feed(plc, asc3e(0x0401, 0x0000, "D*000000", 16384));
    CHECK(plc.requests().back().answeredWith.plcCode == 0xC059);
    drain(plc);
    feed(plc, asc3e(0x0401, 0x0000, "D*000000", 960));
    CHECK(plc.requests().back().answeredWith.ok());
}


// ---- 1E (spec sections 5.3, 4.2, appendix A.5 / A.6) ------------------------------------------

TEST_CASE("MCK-03 the 1E error vectors are reproduced with failRange") {
    int reproduced = 0;
    for (const char* file : k1eVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        for (const Vector& v : vectors) {
            // Only errors the mock itself produces: a PLC end code. 1E-07 is a malformed response
            // for the client's parser.
            if (v.field("kind") != "response-error" || v.field("error") != "Plc" ||
                isTaggedLater(v)) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            const std::string requestId = v.field("of");
            const Vector& request = byId(vectors, requestId);
            const Meta m = metaOf(request);
            const uint16_t code = static_cast<uint16_t>(parseHexOr(v.field("plccode"), 0));
            const uint8_t abnormal = static_cast<uint8_t>(parseHexOr(v.field("abnormal"), 0));

            MockPlc plc(configFor(request));
            plc.failRange(m.head.type, m.head.number, m.head.number, code, abnormal);
            feed(plc, request.bytes);

            CHECK(drain(plc) == v.bytes);
            REQUIRE(plc.requests().size() == 1);
            const mc::Error& err = plc.requests().front().answeredWith;
            CHECK(plc.requests().front().answered);
            CHECK(err.category == mc::ErrorCategory::Plc);
            CHECK(err.code == mc::ErrorCode::PlcError);
            CHECK(err.plcCode == code);
            CHECK(err.abnormalCode == abnormal);
            ++reproduced;
        }
    }
    CHECK(reproduced == 4); // V-1E-B-11, V-1E-B-12 and their ASCII twins
}

TEST_CASE("MCK-03 1E: an abnormal code follows the end code 5BH only, and a fault changes no "
          "memory") {
    for (DataCode code : {DataCode::Binary, DataCode::Ascii}) {
        const bool ascii = code == DataCode::Ascii;
        INFO((ascii ? "1E ASCII" : "1E Binary"));
        MockPlc plc(FrameConfig::frame1E(code));
        plc.failRange(DeviceType::D, 100, 100, 0x5B, 0x22);
        plc.failRange(DeviceType::D, 200, 200, 0x60, 0x22); // the abnormal code is not sent
        plc.failRange(DeviceType::D, 300, 300, 0xC051, 0x22); // the end code of 1E is a u8: 51H

        feed(plc, ascii ? asc1e(0x03, "442000000064", 1, "1234")
                        : bin1e(0x03, kD100Bin1e, 1, {0x34, 0x12}));
        Bytes got = drain(plc);
        CHECK((ascii ? asText(got) == "835B22" : got == Bytes{0x83, 0x5B, 0x22}));
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
        CHECK(plc.requests().back().answeredWith.abnormalCode == 0x22);

        feed(plc, ascii ? asc1e(0x01, "4420000000C8", 1)
                        : bin1e(0x01, {0xC8, 0x00, 0x00, 0x00, 0x20, 0x44}, 1));
        got = drain(plc);
        CHECK((ascii ? asText(got) == "8160" : got == Bytes{0x81, 0x60}));
        CHECK(plc.requests().back().answeredWith.plcCode == 0x60);
        CHECK(plc.requests().back().answeredWith.abnormalCode == 0); // not on the wire

        // A code that does not fit the u8 end code is sent as its low byte.
        feed(plc, ascii ? asc1e(0x01, "44200000012C", 1)
                        : bin1e(0x01, {0x2C, 0x01, 0x00, 0x00, 0x20, 0x44}, 1));
        got = drain(plc);
        CHECK((ascii ? asText(got) == "8151" : got == Bytes{0x81, 0x51}));
        CHECK(plc.requests().back().answeredWith.plcCode == 0x51);
    }
}

TEST_CASE("MCK-12 1E: a request reaching the device limit gets outOfRange1e + abnormal code, one "
          "below it succeeds") {
    for (const char* file : k1eVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        const bool ascii = std::string(file) == "1e_ascii.vec";
        const std::string readDId = ascii ? "V-1E-A-01" : "V-1E-B-01"; // D100 x 3
        const std::string readMId = ascii ? "V-1E-A-03" : "V-1E-B-03"; // M100 x 8 bits
        const std::string errorId = ascii ? "V-1E-A-11" : "V-1E-B-11"; // 5BH + 10H
        const Vector& readD = byId(vectors, readDId);
        const Vector& readM = byId(vectors, readMId);
        const Vector& error = byId(vectors, errorId);
        INFO(file);

        MockPlc plc(configFor(readD));
        plc.setDeviceLimit(DeviceType::D, 102); // D100..D102 reaches D102
        feed(plc, readD.bytes);
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests().back().answeredWith.plcCode == 0x5B);
        CHECK(plc.requests().back().answeredWith.abnormalCode == 0x10);
        CHECK(plc.requests().back().answeredWith.category == mc::ErrorCategory::Plc);
        CHECK(drain(plc) == error.bytes);

        plc.setDeviceLimit(DeviceType::D, 103); // D100..D102 ends below D103
        feed(plc, readD.bytes);
        CHECK(plc.requests().back().answeredWith.ok());
        CHECK_FALSE(drain(plc).empty());

        plc.setDeviceLimit(DeviceType::M, 107); // M100..M107 reaches M107
        feed(plc, readM.bytes);
        CHECK(plc.requests().back().answeredWith.plcCode == 0x5B);
        drain(plc);
        plc.setDeviceLimit(DeviceType::M, 108);
        feed(plc, readM.bytes);
        CHECK(plc.requests().back().answeredWith.ok());
    }
}

TEST_CASE("MCK-12 1E: a limit-stopped write changes no memory, and a word read of a bit device "
          "counts 16 points per word against the limit") {
    MockPlc plc(FrameConfig::frame1E());
    plc.setDeviceLimit(DeviceType::D, 101);
    feed(plc, bin1e(0x03, kD100Bin1e, 2, {0x01, 0x00, 0x02, 0x00}));
    CHECK(drain(plc) == Bytes{0x83, 0x5B, 0x10});
    CHECK(plc.word(Device{DeviceType::D, 100}) == 0);

    plc.setDeviceLimit(DeviceType::M, 8190);
    // M8180 x 1 word covers M8180..M8195: reaches the limit. As bits x 10 it ends at M8189.
    const Bytes m8180 = {0xF4, 0x1F, 0x00, 0x00, 0x20, 0x4D};
    feed(plc, bin1e(0x01, m8180, 1));
    CHECK(drain(plc) == Bytes{0x81, 0x5B, 0x10});
    feed(plc, bin1e(0x00, m8180, 10));
    CHECK(plc.requests().back().answeredWith.ok());
}

TEST_CASE("MCK-10 1E odd bit read: binary pads the low nibble with zero, ASCII ends in a dummy "
          "character") {
    // The point after the five read is set, so a padding taken from memory would show.
    {
        std::vector<Vector> vectors = loadFile("1e_binary.vec");
        MockPlc plc(configFor(byId(vectors, "V-1E-B-09")));
        plc.setBits(Device{DeviceType::M, 100}, {true, false, true, false, true, true});
        feed(plc, byId(vectors, "V-1E-B-09").bytes);
        Bytes got = drain(plc);
        REQUIRE(got.size() == 5); // V-1E-B-10
        CHECK((got.back() & 0x0F) == 0);
        CHECK(got == byId(vectors, "V-1E-B-10").bytes);
    }
    {
        std::vector<Vector> vectors = loadFile("1e_ascii.vec");
        MockPlc plc(configFor(byId(vectors, "V-1E-A-09")));
        plc.setBits(Device{DeviceType::M, 100}, {true, false, true, false, true, true});
        feed(plc, byId(vectors, "V-1E-A-09").bytes);
        Bytes got = drain(plc);
        REQUIRE(got.size() == 10); // V-1E-A-10: "80" + "00" + 5 points + the dummy
        CHECK(std::string(got.end() - 6, got.end()) == "101010");
        CHECK(got == byId(vectors, "V-1E-A-10").bytes);
    }
}

TEST_CASE("MCK-01 1E: devices are decoded from the 1E codes and 8-digit hex numbers of spec 3.3") {
    struct Case {
        const char* name;
        uint8_t cmd;
        Bytes binary;
        const char* ascii;
        Device head;
    };
    const std::vector<Case> cases = {
        {"X1F", 0x00, {0x1F, 0x00, 0x00, 0x00, 0x20, 0x58}, "58200000001F",
         Device{DeviceType::X, 0x1F}},
        {"TN10", 0x01, {0x0A, 0x00, 0x00, 0x00, 0x4E, 0x54}, "544E0000000A",
         Device{DeviceType::TN, 10}},
        {"M1234", 0x00, {0xD2, 0x04, 0x00, 0x00, 0x20, 0x4D}, "4D20000004D2",
         Device{DeviceType::M, 1234}},
        {"M9000", 0x00, {0x28, 0x23, 0x00, 0x00, 0x20, 0x4D}, "4D2000002328",
         Device{DeviceType::M, 9000}},
        {"D100", 0x01, kD100Bin1e, "442000000064", Device{DeviceType::D, 100}},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        MockPlc bin(FrameConfig::frame1E(DataCode::Binary));
        feed(bin, bin1e(c.cmd, c.binary, 1));
        REQUIRE(bin.requests().size() == 1);
        CHECK(bin.requests()[0].head == c.head);
        CHECK(bin.requests()[0].answeredWith.ok());

        MockPlc ascii(FrameConfig::frame1E(DataCode::Ascii));
        feed(ascii, asc1e(c.cmd, c.ascii, 1));
        REQUIRE(ascii.requests().size() == 1);
        CHECK(ascii.requests()[0].head == c.head);
        CHECK(ascii.requests()[0].answeredWith.ok());
    }
}

TEST_CASE("MCK-01 1E: word access to a bit device, points 00 = 256, and the PC number is not "
          "checked") {
    // Spec section 2.4: M100 read as 2 words returns 1234H, 0002H.
    MockPlc plc(FrameConfig::frame1E(DataCode::Binary));
    plc.setWords(Device{DeviceType::M, 100}, {0x1234, 0x0002});
    feed(plc, bin1e(0x01, kM100Bin1e, 2));
    CHECK(drain(plc) == Bytes{0x81, 0x00, 0x34, 0x12, 0x02, 0x00});
    feed(plc, bin1e(0x03, kM100Bin1e, 1, {0x01, 0x80}));
    CHECK(drain(plc) == Bytes{0x83, 0x00});
    CHECK(plc.bit(Device{DeviceType::M, 100}));
    CHECK(plc.bit(Device{DeviceType::M, 115}));
    CHECK_FALSE(plc.bit(Device{DeviceType::M, 101}));
    CHECK(plc.word(Device{DeviceType::M, 116}) == 0x0002); // the second word is untouched

    // Points 00 is 256 bits: 128 data bytes in Binary, 256 characters in ASCII.
    plc.setBit(Device{DeviceType::M, 355}, true);
    feed(plc, bin1e(0x00, kM100Bin1e, 0));
    REQUIRE(plc.requests().back().count == 256);
    Bytes bits = drain(plc);
    REQUIRE(bits.size() == 2 + 128);
    CHECK(bits[2] == 0x10);     // M100 = 1, M101 = 0
    CHECK(bits.back() == 0x01); // M354 = 0, M355 = 1

    MockPlc ascii(FrameConfig::frame1E(DataCode::Ascii));
    ascii.setBit(Device{DeviceType::M, 355}, true);
    feed(ascii, asc1e(0x00, "4D2000000064", 0));
    REQUIRE(ascii.requests().back().count == 256);
    Bytes text = drain(ascii);
    REQUIRE(text.size() == 4 + 256);
    CHECK(asText(Bytes(text.begin(), text.begin() + 4)) == "8000");
    CHECK(text.back() == '1');

    // Another PC number than the configured one is answered all the same.
    MockPlc other(FrameConfig::frame1E(DataCode::Binary));
    other.setWord(Device{DeviceType::D, 100}, 0x1995);
    feed(other, bin1e(0x01, kD100Bin1e, 1, {}, 0x03));
    CHECK(drain(other) == Bytes{0x81, 0x00, 0x95, 0x19});
}

TEST_CASE("MCK-01 1E: request data the mock cannot execute is answered with unsupported1e") {
    struct Case {
        const char* name;
        uint8_t cmd;
        Bytes binary;
        Bytes ascii;
    };
    Bytes fixedNotZeroBin = bin1e(0x01, kD100Bin1e, 1);
    fixedNotZeroBin.back() = 0x01;
    Bytes fixedNotZeroAscii = asc1e(0x01, "442000000064", 1);
    fixedNotZeroAscii.back() = '1';
    const std::vector<Case> cases = {
        {"bit read of a word device", 0x00, bin1e(0x00, kD100Bin1e, 1),
         asc1e(0x00, "442000000064", 1)},
        {"bit write to a word device", 0x02, bin1e(0x02, kD100Bin1e, 1, {0x10}),
         asc1e(0x02, "442000000064", 1, "1")},
        {"unknown device code", 0x01, bin1e(0x01, {0x64, 0x00, 0x00, 0x00, 0x01, 0x00}, 1),
         asc1e(0x01, "000100000064", 1)},
        {"fixed field not 00", 0x01, fixedNotZeroBin, fixedNotZeroAscii},
        {"bit write with a nibble of 2", 0x02, bin1e(0x02, kM100Bin1e, 2, {0x21}),
         asc1e(0x02, "4D2000000064", 2, "12")},
    };
    for (const Case& c : cases) {
        for (DataCode code : {DataCode::Binary, DataCode::Ascii}) {
            const bool ascii = code == DataCode::Ascii;
            INFO(c.name, ascii ? " (ASCII)" : " (Binary)");
            MockPlc plc(FrameConfig::frame1E(code));
            plc.setWord(Device{DeviceType::D, 100}, 7);
            feed(plc, ascii ? c.ascii : c.binary);
            REQUIRE(plc.requests().size() == 1);
            CHECK(plc.requests()[0].answered);
            CHECK(plc.requests()[0].answeredWith.plcCode == 0x50);
            CHECK(plc.requests()[0].answeredWith.abnormalCode == 0);
            Bytes got = drain(plc);
            if (ascii) {
                CHECK(asText(got) == "8" + std::to_string(c.cmd) + "50");
            } else {
                CHECK(got == Bytes{static_cast<uint8_t>(0x80 | c.cmd), 0x50});
            }
            CHECK(plc.word(Device{DeviceType::D, 100}) == 7);
            CHECK_FALSE(plc.bit(Device{DeviceType::M, 100}));
        }
    }

    // ASCII only: a character outside the field's alphabet.
    MockPlc plc(FrameConfig::frame1E(DataCode::Ascii));
    plc.setWord(Device{DeviceType::D, 100}, 5);
    feed(plc, asc1e(0x03, "442000000064", 1, "12G4"));
    CHECK(asText(drain(plc)) == "8350");
    CHECK(plc.word(Device{DeviceType::D, 100}) == 5);
    feed(plc, asc1e(0x02, "4D2000000064", 2, "12")); // bit characters must be 0 or 1
    CHECK(asText(drain(plc)) == "8250");
    feed(plc, asc1e(0x01, "44200000G064", 1)); // a digit outside hexadecimal
    CHECK(asText(drain(plc)) == "8150");
    CHECK(plc.requests().size() == 3);
}

TEST_CASE("MCK-05 1E: bytes that cannot start a request are dropped and logged once as "
          "unanswered") {
    for (DataCode code : {DataCode::Binary, DataCode::Ascii}) {
        const bool ascii = code == DataCode::Ascii;
        INFO((ascii ? "ASCII" : "Binary"));
        const Bytes good = ascii ? asc1e(0x01, "442000000064", 1) : bin1e(0x01, kD100Bin1e, 1);
        // A command above 05H cannot start a frame (ASCII: "X", "Y" and "06").
        const Bytes junk = ascii ? Bytes{'X', 'Y', '0', '6'} : Bytes{0x06, 0x80, 0xFF};
        for (size_t chunk : {size_t{64}, size_t{1}}) {
            MockPlc plc(FrameConfig::frame1E(code));
            Bytes stream = junk;
            append(stream, good);
            feedInChunks(plc, stream, chunk);
            REQUIRE(plc.requests().size() == 2);
            CHECK_FALSE(plc.requests()[0].answered);
            CHECK(plc.requests()[0].answeredWith.ok());
            CHECK(plc.requests()[1].answered);
            size_t responses = 0;
            drain(plc, &responses);
            CHECK(responses == 1);
        }
    }

    // A half subheader waits: "0" alone is a frame still being received.
    MockPlc plc(FrameConfig::frame1E(DataCode::Ascii));
    feed(plc, Bytes{'0'});
    CHECK(plc.requests().empty());
    feed(plc, Bytes{'1', 'F', 'F'});
    CHECK(plc.requests().empty());

    // A wrong subheader is decided at once, never waiting for the rest of a header (Binary: a
    // command above 05H is one byte; ASCII: "06" is two characters, the header is eight).
    MockPlc binary(FrameConfig::frame1E(DataCode::Binary));
    feed(binary, Bytes{0x06});
    CHECK(binary.requests().size() == 1);
    MockPlc ascii(FrameConfig::frame1E(DataCode::Ascii));
    feed(ascii, Bytes{'0', '6'});
    CHECK(ascii.requests().size() == 1);
}

TEST_CASE("MCK-05 1E: a request split anywhere is not executed before it is complete") {
    for (DataCode code : {DataCode::Binary, DataCode::Ascii}) {
        const bool ascii = code == DataCode::Ascii;
        INFO((ascii ? "ASCII" : "Binary"));
        MockPlc plc(FrameConfig::frame1E(code));
        const Bytes frame = ascii ? asc1e(0x03, "442000000064", 1, "1234")
                                  : bin1e(0x03, kD100Bin1e, 1, {0x34, 0x12});
        plc.bytesIn(ByteView{frame.data(), frame.size() - 1});
        CHECK(plc.requests().empty());
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
        ByteView view;
        CHECK_FALSE(plc.nextResponse(view));
        plc.bytesIn(ByteView{frame.data() + frame.size() - 1, 1});
        CHECK(plc.requests().size() == 1);
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1234);
    }
}

TEST_CASE("MCK-05 1E: responses come out in request order, one per request") {
    MockPlc plc(FrameConfig::frame1E(DataCode::Binary));
    plc.setWords(Device{DeviceType::D, 100}, {0x1111, 0x2222});
    Bytes stream = bin1e(0x01, kD100Bin1e, 1);
    append(stream, bin1e(0x01, {0x65, 0x00, 0x00, 0x00, 0x20, 0x44}, 1));
    feed(plc, stream);

    ByteView view;
    REQUIRE(plc.nextResponse(view));
    REQUIRE(view.size == 4);
    CHECK(view.data[2] == 0x11);
    REQUIRE(plc.nextResponse(view));
    CHECK(view.data[2] == 0x22);
    CHECK_FALSE(plc.nextResponse(view));
}

// ---- 3C and 1C (spec sections 5.4-5.6, appendix A.12-A.19) -----------------------------------

TEST_CASE("MCK-03 the 3C and 1C error vectors are reproduced with failRange") {
    int reproduced = 0;
    for (const char* file : kSerialVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        for (const Vector& v : vectors) {
            // Only errors the mock itself produces: a PLC error code. FrameMismatch / SumCheck
            // vectors are malformed responses for the client's parser.
            if (v.field("kind") != "response-error" || v.field("error") != "Plc" ||
                isTaggedLater(v)) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            const std::string requestId = v.field("of");
            const Vector& request = byId(vectors, requestId);
            const Meta m = metaOf(request);
            const uint16_t code = static_cast<uint16_t>(parseHexOr(v.field("plccode"), 0));

            MockPlc plc(configFor(request, &v));
            plc.failRange(m.head.type, m.head.number, m.head.number, code);
            feed(plc, request.bytes);

            CHECK(drain(plc) == v.bytes);
            REQUIRE(plc.requests().size() == 1);
            const mc::Error& err = plc.requests().front().answeredWith;
            CHECK(plc.requests().front().answered);
            CHECK(err.category == mc::ErrorCategory::Plc);
            CHECK(err.code == mc::ErrorCode::PlcError);
            CHECK(err.plcCode == code);
            ++reproduced;
        }
    }
    // Per family: V-xCn-05 / V-1Cn-08 (x4), the NAKWRITE twins of formats 2 and 3 (x2), BLK-NAK of
    // format 2 and SHORTSUM-NAK of format 3: ten each.
    CHECK(reproduced == 20);
}

TEST_CASE("MCK-01 1C: AnA/AnU commands, message wait, T/C devices, hex devices and points 00") {
    namespace sf = mc::test::serial;
    struct Case {
        const char* name;
        std::string data;
        Op op;
        Device head;
        uint16_t count;
    };
    const std::vector<Case> cases = {
        {"JR bit read, 7-character device", sf::data1c("JR", '0', "M000100", 8), Op::ReadBits,
         Device{DeviceType::M, 100}, 8},
        {"QR word read", sf::data1c("QR", 'A', "D000100", 3), Op::ReadWords,
         Device{DeviceType::D, 100}, 3},
        {"JW bit write", sf::data1c("JW", '0', "M000100", 2, "10"), Op::WriteBits,
         Device{DeviceType::M, 100}, 2},
        {"QW word write", sf::data1c("QW", '0', "D000100", 1, "1234"), Op::WriteWords,
         Device{DeviceType::D, 100}, 1},
        {"timer current value, ACPU (code 2, number 3)", sf::data1c("WR", '0', "TN123", 2),
         Op::ReadWords, Device{DeviceType::TN, 123}, 2},
        {"timer current value, AnA (code 2, number 5)", sf::data1c("QR", '0', "TN00123", 2),
         Op::ReadWords, Device{DeviceType::TN, 123}, 2},
        {"counter contact", sf::data1c("BR", '0', "CS012", 1), Op::ReadBits,
         Device{DeviceType::CS, 12}, 1},
        {"hexadecimal X, lower case digits", sf::data1c("BR", '0', "X001f", 4), Op::ReadBits,
         Device{DeviceType::X, 0x1F}, 4},
        {"hexadecimal W", sf::data1c("WR", '0', "W00A0", 1), Op::ReadWords,
         Device{DeviceType::W, 0xA0}, 1},
        {"latch relay and step relay are their own devices", sf::data1c("BR", '0', "L0010", 1),
         Op::ReadBits, Device{DeviceType::L, 10}, 1},
        {"step relay", sf::data1c("BR", '0', "S0010", 1), Op::ReadBits, Device{DeviceType::S, 10},
         1},
        {"leading zeros of the number as spaces", sf::data1c("WR", '0', "D  10", 1), Op::ReadWords,
         Device{DeviceType::D, 10}, 1},
        {"file register", sf::data1c("WR", '0', "R0007", 1), Op::ReadWords,
         Device{DeviceType::R, 7}, 1},
        {"points 00 are 256 (bit read)", sf::data1c("BR", '0', "M0000", 0), Op::ReadBits,
         Device{DeviceType::M, 0}, 256},
        {"points 00 are 256 (word read)", sf::data1c("WR", '0', "D0000", 0), Op::ReadWords,
         Device{DeviceType::D, 0}, 256},
        {"points in lower case hex", sf::data1c("WR", '0', "D0000", 0x0A), Op::ReadWords,
         Device{DeviceType::D, 0}, 10},
    };
    for (mc::SerialFormat format : {mc::SerialFormat::Format1, mc::SerialFormat::Format2,
                                    mc::SerialFormat::Format3, mc::SerialFormat::Format4}) {
        const FrameConfig cfg = FrameConfig::frame1C(format);
        for (const Case& c : cases) {
            INFO(c.name, ", format ", static_cast<int>(format));
            MockPlc plc(cfg);
            feed(plc, sf::request(cfg, c.data));
            REQUIRE(plc.requests().size() == 1);
            const MockRequestRecord& rec = plc.requests()[0];
            CHECK(rec.frame == mc::FrameType::F1C);
            CHECK(rec.op == c.op);
            CHECK(rec.head == c.head);
            CHECK(rec.count == c.count);
            CHECK(rec.series == PlcSeries::QL);
            CHECK(rec.answeredWith.ok());
            size_t responses = 0;
            drain(plc, &responses);
            CHECK(responses == 1);
        }
    }
}

TEST_CASE("MCK-04 1C and 3C: writes reach memory, bits one character per point, words 4 hex") {
    namespace sf = mc::test::serial;
    for (bool threeC : {true, false}) {
        const FrameConfig cfg = threeC ? FrameConfig::frame3C() : FrameConfig::frame1C();
        MockPlc plc(cfg);
        feed(plc, sf::request(cfg, sf::writeD(cfg, 100, "199512021130")));
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1995);
        CHECK(plc.word(Device{DeviceType::D, 102}) == 0x1130);

        // M100 = 1,0,1,1,0 written as 5 characters: no padding on an ASCII serial frame.
        const std::string bits = threeC ? sf::data3c(0x1401, 0x0001, "M*000100", 5, "10110")
                                        : sf::data1c("BW", '0', "M0100", 5, "10110");
        feed(plc, sf::request(cfg, bits));
        CHECK(plc.bit(Device{DeviceType::M, 100}));
        CHECK_FALSE(plc.bit(Device{DeviceType::M, 101}));
        CHECK(plc.bit(Device{DeviceType::M, 102}));
        CHECK(plc.bit(Device{DeviceType::M, 103}));
        CHECK_FALSE(plc.bit(Device{DeviceType::M, 104}));
        size_t responses = 0;
        drain(plc, &responses);
        CHECK(responses == 2);
    }
}

TEST_CASE("MCK-10 serial odd bit read: one character per point, nothing is padded or read past the "
          "count") {
    namespace sf = mc::test::serial;
    for (bool threeC : {true, false}) {
        const FrameConfig cfg = threeC ? FrameConfig::frame3C() : FrameConfig::frame1C();
        MockPlc plc(cfg);
        plc.setBits(Device{DeviceType::M, 100}, {true, false, true, false, true, true});
        const std::string data =
            threeC ? sf::data3c(0x0401, 0x0001, "M*000100", 5) : sf::data1c("BR", '0', "M0100", 5);
        feed(plc, sf::request(cfg, data));
        const Bytes got = drain(plc);
        CHECK(got == sf::response(cfg, sf::Kind::Data, "10101"));
    }
}

TEST_CASE("MCK-12 serial: a request reaching the device limit gets outOfRange, one below it "
          "succeeds") {
    namespace sf = mc::test::serial;
    for (bool threeC : {true, false}) {
        const FrameConfig cfg = threeC ? FrameConfig::frame3C() : FrameConfig::frame1C();
        const uint16_t code = threeC ? 0xC051 : 0x06;
        const std::string codeText = threeC ? "C051" : "06";
        MockPlc plc(cfg);
        plc.setDeviceLimit(DeviceType::D, 102); // D100..D102 reaches D102
        feed(plc, sf::request(cfg, sf::readD(cfg, 100, 3)));
        CHECK(drain(plc) == sf::response(cfg, sf::Kind::Nak, codeText));
        CHECK(plc.requests().back().answeredWith.plcCode == code);
        feed(plc, sf::request(cfg, sf::writeD(cfg, 101, "00010002"))); // a write is stopped too
        CHECK(drain(plc) == sf::response(cfg, sf::Kind::Nak, codeText));
        CHECK(plc.word(Device{DeviceType::D, 101}) == 0);
        plc.setDeviceLimit(DeviceType::D, 103);
        feed(plc, sf::request(cfg, sf::readD(cfg, 100, 3)));
        CHECK(plc.requests().back().answeredWith.ok());
        drain(plc);

        // A word read of a bit device counts 16 points per word: M100 x 1 word reaches M115.
        plc.setDeviceLimit(DeviceType::M, 115);
        const std::string word =
            threeC ? sf::data3c(0x0401, 0x0000, "M*000100", 1) : sf::data1c("WR", '0', "M0100", 1);
        feed(plc, sf::request(cfg, word));
        CHECK(plc.requests().back().answeredWith.plcCode == code);
        drain(plc);
        plc.setDeviceLimit(DeviceType::M, 116);
        feed(plc, sf::request(cfg, word));
        CHECK(plc.requests().back().answeredWith.ok());
    }
}

TEST_CASE("MCK-01 1C: a write with points 00 carries 256 points, and the next request is found "
          "after them") {
    namespace sf = mc::test::serial;
    for (mc::SerialFormat format : {mc::SerialFormat::Format1, mc::SerialFormat::Format2,
                                    mc::SerialFormat::Format3, mc::SerialFormat::Format4}) {
        const FrameConfig cfg = FrameConfig::frame1C(format);
        INFO("format ", static_cast<int>(format));
        MockPlc plc(cfg);
        Bytes stream = sf::request(cfg, sf::data1c("BW", '0', "M0000", 0, std::string(256, '1')));
        sf::append(stream,
                   sf::request(cfg, sf::data1c("WW", '0', "D0000", 0, std::string(1024, 'F'))));
        sf::append(stream, sf::request(cfg, sf::data1c("BR", '0', "M0000", 1)));
        feed(plc, stream);
        REQUIRE(plc.requests().size() == 3);
        CHECK(plc.requests()[0].count == 256);
        CHECK(plc.requests()[1].count == 256);
        CHECK(plc.requests()[2].count == 1);
        CHECK(plc.bit(Device{DeviceType::M, 255}));
        CHECK(plc.word(Device{DeviceType::D, 255}) == 0xFFFF);
        size_t responses = 0;
        drain(plc, &responses);
        CHECK(responses == 3);
    }
}
