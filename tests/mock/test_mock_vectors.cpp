// MCK-01, MCK-02, MCK-03, MCK-12 (end to end) and the 3E parts of MCK-05, MCK-07 and MCK-10:
// MockPlc's 3E server direction against the golden vectors of tests/vectors/3e_binary.vec and
// 3e_ascii.vec (spec Appendix A.1 / A.2 and section 9.6), read in the reverse direction: the
// request vectors go into MockPlc::bytesIn, the response vectors are what nextResponse must give.
// Frames that the vectors do not have are built here by hand from the tables of spec section 5.1.
#include "doctest/doctest.h"

#include "common/vectors.h"

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

FrameConfig configFor(const Vector& v) {
    FrameConfig cfg =
        FrameConfig::frame3E(v.field("code") == "Ascii" ? DataCode::Ascii : DataCode::Binary);
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
    for (size_t i = 0; i < values.size(); ++i) {
        Device d{m.head.type, m.head.number + static_cast<uint32_t>(i)};
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

} // namespace

TEST_CASE("MCK-01 every 3E request vector decodes to the op, head, count, series and write data "
          "of its metadata") {
    for (const char* file : kVectorFiles) {
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
            CHECK(rec.frame == mc::FrameType::F3E);
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

TEST_CASE("MCK-02 with memory seeded from the metadata, every 3E success response vector is "
          "reproduced byte for byte") {
    for (const char* file : kVectorFiles) {
        std::vector<Vector> vectors = loadFile(file);
        for (const Vector& v : vectors) {
            if (v.field("kind") != "response" || isTaggedLater(v)) {
                continue;
            }
            INFO("vector ", v.id, " (", v.file, ":", v.line, ")");
            REQUIRE_FALSE(v.field("of").empty());
            const std::string requestId = v.field("of");
            const Vector& request = byId(vectors, requestId);

            MockPlc plc(configFor(request));
            const Meta m = metaOf(request);
            seedFromExpect(plc, m, v.field("expect"));
            feed(plc, request.bytes);

            size_t responses = 0;
            Bytes got = drain(plc, &responses);
            CHECK(responses == 1);
            CHECK(got == v.bytes);
        }
    }
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

TEST_CASE("MCK-05 3E: every request vector fed one byte at a time, in 3-byte pieces, or twice in "
          "one buffer decodes identically") {
    for (const char* file : kVectorFiles) {
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

