// MCK-09 (unsupported commands) and MCK-11 (mute, muteNext and every Corruption mode) on 3E and
// 1E, Binary and ASCII, and on 3C and 1C in formats 1 to 4, asserted byte for byte against a clean
// response; plus clearFaults() and the serial parts of MCK-12. The expected bytes are derived here
// from the documented rule of each mode (mc/mock/mock_plc.h) applied to a clean response, never by
// calling the mock a second time for the damage itself. MCK-16 (the error codes of a MockTarget)
// takes its bytes from the hardware captures instead.
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
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

using mc::ByteView;
using mc::Corruption;
using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::FrameType;
using mc::MockOptions;
using mc::MockPlc;
using mc::MockTarget;
using mc::SerialFormat;
using mc::TargetFamily;
using mc::XyNumbering;
using mc::test::loadVectors;
using mc::test::Vector;

namespace sf = mc::test::serial;

namespace {

using Bytes = std::vector<uint8_t>;

const Bytes kJunk = {0x55, 0x55, 0x55};

const Vector& byId(const std::vector<Vector>& vectors, const std::string& id) {
    for (const Vector& v : vectors) {
        if (v.id == id) {
            return v;
        }
    }
    FAIL("no vector with id ", id);
    return vectors.front();
}

std::vector<Vector> loadFile(const char* name) {
    std::vector<Vector> vectors =
        loadVectors(std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors" / name);
    REQUIRE_FALSE(vectors.empty());
    return vectors;
}

// One wire representation under test: the configuration of the mock, a request for D100 x 3 words
// (the vector of the spec's example G1), the same request with a route that carries hex digits (3E:
// 0F, FE, 1F; 1E has only the PC number: 03; 3C: network 0F, PC 3E, self-station 1F; 1C: PC 3E), and
// a write of D100 = 1234H.
struct Wire {
    const char* name;
    FrameConfig cfg;
    Bytes read;
    Bytes readOtherRoute;
    Bytes write;

    bool is1e() const { return cfg.frame == FrameType::F1E; }
    bool serial() const { return cfg.frame == FrameType::F3C || cfg.frame == FrameType::F1C; }
    bool ascii() const { return cfg.code == DataCode::Ascii; }
};

FrameConfig configOf(const Wire& w) { return w.cfg; }

// The route of readOtherRoute for the serial frames.
sf::Route otherSerialRoute() {
    sf::Route r;
    r.network = 0x0F;
    r.pc = 0x3E;
    r.self = 0x1F;
    r.block = 0x3A;
    return r;
}

std::vector<Wire> wires() {
    const Bytes binRead = byId(loadFile("3e_binary.vec"), "V-3E-B-01").bytes;
    const Bytes ascRead = byId(loadFile("3e_ascii.vec"), "V-3E-A-01").bytes;

    Bytes binOther = binRead;
    const Bytes binRoute = {0x0F, 0xFE, 0xFF, 0x03, 0x1F};
    std::copy(binRoute.begin(), binRoute.end(), binOther.begin() + 2);

    Bytes ascOther = ascRead;
    const std::string ascRoute = "0FFE03FF1F";
    std::copy(ascRoute.begin(), ascRoute.end(), ascOther.begin() + 4);

    // D100 = 1234H, written by hand from the tables of spec 5.1 / 5.3 and 4.1.2 / 4.2.
    const Bytes binWrite3e = {0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0E, 0x00, 0x10, 0x00,
                              0x01, 0x14, 0x00, 0x00, 0x64, 0x00, 0x00, 0xA8, 0x01, 0x00, 0x34, 0x12};
    const std::string ascWrite3eText = "500000FF03FF00" "001C" "0010" "14010000" "D*000100" "0001" "1234";
    const Bytes binWrite1e = {0x03, 0xFF, 0x0A, 0x00, 0x64, 0x00, 0x00, 0x00,
                              0x20, 0x44, 0x01, 0x00, 0x34, 0x12};
    const std::string ascWrite1eText = "03FF000A" "442000000064" "0100" "1234";

    // 1E: the PC number (byte 1, characters 2-3) is the only route field of the request.
    const Bytes bin1eRead = byId(loadFile("1e_binary.vec"), "V-1E-B-01").bytes;
    const Bytes asc1eRead = byId(loadFile("1e_ascii.vec"), "V-1E-A-01").bytes;
    Bytes bin1eOther = bin1eRead;
    bin1eOther[1] = 0x03;
    Bytes asc1eOther = asc1eRead;
    asc1eOther[2] = '0';
    asc1eOther[3] = '3';

    std::vector<Wire> list = {
        {"3E Binary", FrameConfig::frame3E(DataCode::Binary), binRead, binOther, binWrite3e},
        {"3E ASCII", FrameConfig::frame3E(DataCode::Ascii), ascRead, ascOther,
         Bytes(ascWrite3eText.begin(), ascWrite3eText.end())},
        {"1E Binary", FrameConfig::frame1E(DataCode::Binary), bin1eRead, bin1eOther, binWrite1e},
        {"1E ASCII", FrameConfig::frame1E(DataCode::Ascii), asc1eRead, asc1eOther,
         Bytes(ascWrite1eText.begin(), ascWrite1eText.end())},
    };

    // The eight serial combinations; the frames are written from the tables of spec 5.4-5.6.
    for (bool threeC : {true, false}) {
        for (SerialFormat format : {SerialFormat::Format1, SerialFormat::Format2,
                                    SerialFormat::Format3, SerialFormat::Format4}) {
            static std::deque<std::string> names; // element addresses stay valid
            names.push_back(std::string(threeC ? "3C F" : "1C F") +
                            std::to_string(static_cast<int>(format)));
            Wire w;
            w.name = names.back().c_str();
            w.cfg = threeC ? FrameConfig::frame3C(format) : FrameConfig::frame1C(format);
            w.read = sf::request(w.cfg, sf::readD(w.cfg, 100, 3));
            w.readOtherRoute = sf::request(w.cfg, sf::readD(w.cfg, 100, 3), otherSerialRoute());
            w.write = sf::request(w.cfg, sf::writeD(w.cfg, 100, "1234"));
            list.push_back(w);
        }
    }
    return list;
}

MockPlc seeded(const Wire& w) {
    MockPlc plc(configOf(w));
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    return plc;
}

void feed(MockPlc& plc, const Bytes& bytes) { plc.bytesIn(ByteView{bytes.data(), bytes.size()}); }

// The responses pending, one element each.
std::vector<Bytes> drainAll(MockPlc& plc) {
    std::vector<Bytes> all;
    ByteView view;
    while (plc.nextResponse(view)) {
        all.emplace_back(view.data, view.data + view.size);
    }
    return all;
}

Bytes cleanResponse(const Wire& w, const Bytes& request) {
    MockPlc plc = seeded(w);
    feed(plc, request);
    std::vector<Bytes> all = drainAll(plc);
    REQUIRE(all.size() == 1);
    return all[0];
}

// The documented damage of `mode` on a clean 3E or 1E response, written out by hand.
Bytes expectedSerialDamage(Corruption mode, const Wire& w, const Bytes& clean) {
    const FrameConfig& cfg = w.cfg;
    const bool threeC = cfg.frame == FrameType::F3C;
    const bool format2 = cfg.format == SerialFormat::Format2;
    const bool format4 = cfg.format == SerialFormat::Format4;
    Bytes out = clean;

    // A SUM is there when the response has an ETX and two characters follow it (before CR LF).
    const auto etxAt = std::find(clean.begin(), clean.end(), sf::kEtx);
    const size_t etx = static_cast<size_t>(etxAt - clean.begin());
    const bool hasSum = etxAt != clean.end() && clean.size() == etx + 1 + 2 + (format4 ? 2 : 0);
    auto setSum = [&](unsigned value) {
        const std::string text = sf::hex2(value);
        out[etx + 1] = static_cast<uint8_t>(text[0]);
        out[etx + 2] = static_cast<uint8_t>(text[1]);
    };
    auto addOne = [&](size_t at) {
        const std::string field(clean.begin() + static_cast<std::ptrdiff_t>(at),
                                clean.begin() + static_cast<std::ptrdiff_t>(at) + 2);
        const unsigned v = static_cast<unsigned>(std::stoul(field, nullptr, 16));
        const std::string text = sf::hex2(v + 1);
        out[at] = static_cast<uint8_t>(text[0]); // wraps at FFH
        out[at + 1] = static_cast<uint8_t>(text[1]);
    };

    switch (mode) {
    case Corruption::WrongSubheader:
        return out; // there is no subheader in a serial frame
    case Corruption::WrongSumCheck:
        if (hasSum) {
            setSum(sf::byteSum(clean, 1, etx + 1) + 1);
        }
        return out;
    case Corruption::WrongRoute: {
        const size_t p = format2 ? 3 : 1; // start of the access route
        if (threeC) {
            addOne(p + 2); // station (after "F9")
            addOne(p + 4); // network
            addOne(p + 6); // PC; the self-station at p + 8 stays
        } else {
            addOne(p);     // station
            addOne(p + 2); // PC
        }
        if (hasSum) {
            setSum(sf::byteSum(out, 1, etx + 1)); // the route is the only damage
        }
        return out;
    }
    case Corruption::WrongBlockNo:
        if (format2) {
            addOne(1);
            if (hasSum) {
                setSum(sf::byteSum(out, 1, etx + 1)); // the block number is the only damage
            }
        }
        return out;
    case Corruption::Truncate:
        out.pop_back();
        return out;
    case Corruption::JunkPrefix:
        out.insert(out.begin(), kJunk.begin(), kJunk.end());
        return out;
    case Corruption::ExtraByte:
        out.push_back(0x00);
        return out;
    }
    return out;
}

Bytes expectedDamage(Corruption mode, const Wire& w, const Bytes& clean) {
    if (w.serial()) {
        return expectedSerialDamage(mode, w, clean);
    }
    const bool ascii = w.ascii();
    Bytes out = clean;
    switch (mode) {
    case Corruption::WrongSubheader:
        out[0] = ascii ? uint8_t{'E'} : static_cast<uint8_t>(clean[0] ^ 0x01);
        return out;
    case Corruption::WrongRoute: {
        if (w.is1e()) {
            return out; // a 1E response has no route fields
        }
        // network, PC and station + 1 (wrapping); the I/O number is not touched.
        const size_t net = ascii ? 4 : 2;
        const size_t pc = ascii ? 6 : 3;
        const size_t station = ascii ? 12 : 6;
        for (size_t at : {net, pc, station}) {
            if (ascii) {
                const std::string field(clean.begin() + at, clean.begin() + at + 2);
                const unsigned v = static_cast<unsigned>(std::stoul(field, nullptr, 16));
                char text[8];
                std::snprintf(text, sizeof(text), "%02X", (v + 1) & 0xFFu);
                out[at] = static_cast<uint8_t>(text[0]);
                out[at + 1] = static_cast<uint8_t>(text[1]);
            } else {
                out[at] = static_cast<uint8_t>(clean[at] + 1);
            }
        }
        return out;
    }
    case Corruption::Truncate:
        out.pop_back();
        return out;
    case Corruption::JunkPrefix:
        out.insert(out.begin(), kJunk.begin(), kJunk.end());
        return out;
    case Corruption::ExtraByte:
        out.push_back(0x00);
        return out;
    case Corruption::WrongSumCheck:
    case Corruption::WrongBlockNo:
        return out; // serial only: an Ethernet response is left alone
    }
    return out;
}

const Corruption kAllModes[] = {Corruption::WrongSubheader, Corruption::WrongSumCheck,
                                Corruption::WrongRoute,     Corruption::WrongBlockNo,
                                Corruption::Truncate,       Corruption::JunkPrefix,
                                Corruption::ExtraByte};

// A 3E Binary request built by hand: 50 00, default route, length, timer, command, subcommand,
// body.
Bytes binaryRequest(uint16_t cmd, uint16_t sub, const Bytes& body) {
    Bytes data = {0x10, 0x00, static_cast<uint8_t>(cmd & 0xFF), static_cast<uint8_t>(cmd >> 8),
                  static_cast<uint8_t>(sub & 0xFF), static_cast<uint8_t>(sub >> 8)};
    data.insert(data.end(), body.begin(), body.end());
    Bytes frame = {0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
                   static_cast<uint8_t>(data.size()), 0x00};
    frame.insert(frame.end(), data.begin(), data.end());
    return frame;
}

// A 3E ASCII request with the default route; `fields` is the text after the monitoring timer.
Bytes asciiRequest(const std::string& fields) {
    char len[8];
    std::snprintf(len, sizeof(len), "%04X", static_cast<unsigned>(fields.size() + 4));
    const std::string text = std::string("500000FF03FF00") + len + "0010" + fields;
    return Bytes(text.begin(), text.end());
}

std::string asText(const Bytes& b) { return std::string(b.begin(), b.end()); }

} // namespace

TEST_CASE("MCK-09 3E: 0403 and 1402 are answered with unsupportedQna and the request's error "
          "information") {
    // V-3E-B-13 / V-3E-A-13: the 0403 request of the manual (tagged v1.1, so the client skips it).
    const Bytes random403Binary = byId(loadFile("3e_binary.vec"), "V-3E-B-13").bytes;
    const Bytes random403Ascii = byId(loadFile("3e_ascii.vec"), "V-3E-A-13").bytes;
    const Bytes error0403Binary = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0B, 0x00, 0x59,
                                   0xC0, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x03, 0x04, 0x00, 0x00};
    const Bytes error1402Binary = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0B, 0x00, 0x59,
                                   0xC0, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x14, 0x00, 0x00};
    const std::string error0403Ascii = "D00000FF03FF000016C05900FF03FF0004030000";
    const std::string error1402Ascii = "D00000FF03FF000016C05900FF03FF0014020000";

    SUBCASE("3E Binary") {
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
        plc.setWord(Device{DeviceType::D, 100}, 7);
        feed(plc, random403Binary);
        feed(plc, binaryRequest(0x1402, 0x0000, {0x01, 0x00, 0x64, 0x00, 0x00, 0xA8, 0x34, 0x12}));
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 2);
        CHECK(got[0] == error0403Binary);
        CHECK(got[1] == error1402Binary);
        REQUIRE(plc.requests().size() == 2);
        CHECK(plc.requests()[0].answered);
        CHECK(plc.requests()[0].answeredWith.plcCode == 0xC059);
        CHECK(plc.requests()[0].answeredWith.info.command == 0x0403);
        CHECK(plc.requests()[1].answeredWith.info.command == 0x1402);
        CHECK(plc.requests()[1].answeredWith.info.subcommand == 0x0000);
        CHECK(plc.word(Device{DeviceType::D, 100}) == 7); // nothing executed
    }
    SUBCASE("3E ASCII") {
        MockPlc plc(FrameConfig::frame3E(DataCode::Ascii));
        feed(plc, random403Ascii);
        feed(plc, asciiRequest("14020000" "0100" "D*000100" "1234"));
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 2);
        CHECK(asText(got[0]) == error0403Ascii);
        CHECK(asText(got[1]) == error1402Ascii);
        CHECK(plc.requests()[0].answeredWith.plcCode == 0xC059);
        CHECK(plc.requests()[0].answeredWith.info.command == 0x0403);
        CHECK(plc.requests()[1].answeredWith.info.command == 0x1402);
    }
    SUBCASE("other subcommands and other commands") {
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
        feed(plc, binaryRequest(0x0403, 0x0002, {0x00})); // iQ-R random read
        feed(plc, binaryRequest(0x1402, 0x0001, {0x00})); // bit-unit random write
        feed(plc, binaryRequest(0x0406, 0x0000, {0x00})); // block read (v1.1)
        feed(plc, binaryRequest(0x0619, 0x0000, {}));     // loopback test
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 4);
        for (const Bytes& r : got) {
            REQUIRE(r.size() == 20);
            CHECK(r[9] == 0x59);
            CHECK(r[10] == 0xC0);
        }
        CHECK(plc.requests()[0].answeredWith.info.subcommand == 0x0002);
        CHECK(plc.requests()[2].answeredWith.info.command == 0x0406);
        CHECK(plc.requests()[3].answeredWith.info.command == 0x0619);
    }
}

TEST_CASE("MCK-09 3E: the error codes follow MockOptions, and the defaults are pinned") {
    MockOptions opt;
    opt.unsupportedQna = 0xC061;
    opt.outOfRangeQna = 0xC062;
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
    plc.setDeviceLimit(DeviceType::D, 10);
    feed(plc, binaryRequest(0x0403, 0x0000, {0x00}));
    feed(plc, binaryRequest(0x0401, 0x0000, {0x64, 0x00, 0x00, 0xA8, 0x01, 0x00}));
    std::vector<Bytes> got = drainAll(plc);
    REQUIRE(got.size() == 2);
    CHECK(got[0][9] == 0x61);
    CHECK(got[0][10] == 0xC0);
    CHECK(got[1][9] == 0x62);
    CHECK(got[1][10] == 0xC0);

    // Tests elsewhere depend on these values (spec "Ask first": changing a default).
    const MockOptions defaults;
    CHECK(defaults.unsupportedQna == 0xC059);
    CHECK(defaults.unsupported1e == 0x50);
    CHECK(defaults.unsupported1c == 0x06);
    CHECK(defaults.sumErrorQna == 0x7151);
    CHECK(defaults.sumError1c == 0x06);
    CHECK(defaults.outOfRangeQna == 0xC051);
    CHECK(defaults.outOfRange1e == 0x5B);
    CHECK(defaults.outOfRange1eAbnormal == 0x10);
    CHECK(defaults.outOfRange1c == 0x06);
}

TEST_CASE("MCK-11 3E and 1E: mute swallows every request until it is turned off") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        const Bytes clean = cleanResponse(w, w.read);
        MockPlc plc = seeded(w);

        plc.mute(true);
        feed(plc, w.read);
        feed(plc, w.read);
        CHECK(drainAll(plc).empty());
        REQUIRE(plc.requests().size() == 2);
        for (const auto& rec : plc.requests()) {
            CHECK_FALSE(rec.answered);
            CHECK(rec.answeredWith.ok());
            CHECK(rec.op == mc::Op::ReadWords); // still decoded
            CHECK(rec.count == 3);
        }

        plc.mute(false);
        feed(plc, w.read);
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == clean);
        CHECK(plc.requests().back().answered);
    }
}

TEST_CASE("MCK-11 3E and 1E: a swallowed request is not executed") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        MockPlc plc(configOf(w));
        const Bytes& write = w.write;
        plc.mute(true);
        feed(plc, write);
        CHECK(drainAll(plc).empty());
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests()[0].op == mc::Op::WriteWords);
        CHECK_FALSE(plc.requests()[0].answered);

        plc.mute(false);
        feed(plc, write);
        CHECK(drainAll(plc).size() == 1);
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1234);
    }
}

TEST_CASE("MCK-11 3E and 1E: muteNext(n) swallows exactly n requests, then answers cleanly") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        const Bytes clean = cleanResponse(w, w.read);
        MockPlc plc = seeded(w);
        plc.muteNext(2);
        for (int i = 0; i < 4; ++i) {
            feed(plc, w.read);
        }
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 2);
        CHECK(got[0] == clean);
        CHECK(got[1] == clean);
        REQUIRE(plc.requests().size() == 4);
        CHECK_FALSE(plc.requests()[0].answered);
        CHECK_FALSE(plc.requests()[1].answered);
        CHECK(plc.requests()[2].answered);
        CHECK(plc.requests()[3].answered);
    }
}

TEST_CASE("MCK-11 3E and 1E: each Corruption mode damages exactly n responses as documented") {
    for (const Wire& w : wires()) {
        for (Corruption mode : kAllModes) {
            INFO(w.name, " mode ", static_cast<int>(mode));
            for (const Bytes* request : {&w.read, &w.readOtherRoute}) {
                const Bytes clean = cleanResponse(w, *request);
                const Bytes damaged = expectedDamage(mode, w, clean);

                MockPlc plc = seeded(w);
                plc.corruptNext(mode, 2);
                for (int i = 0; i < 3; ++i) {
                    feed(plc, *request);
                }
                std::vector<Bytes> got = drainAll(plc);
                REQUIRE(got.size() == 3);
                CHECK(got[0] == damaged);
                CHECK(got[1] == damaged);
                CHECK(got[2] == clean); // n counted down
                for (const auto& rec : plc.requests()) {
                    CHECK(rec.answered);
                    CHECK(rec.answeredWith.ok());
                }
            }
        }
    }
}

TEST_CASE("MCK-11 3E Binary: the documented bytes of each mode, spelled out for the default "
          "route") {
    // Clean: D0 00 | 00 FF | FF 03 | 00 | 08 00 | 00 00 | 95 19 02 12 30 11
    MockPlc plc(FrameConfig::frame3E(DataCode::Binary));
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    const Bytes request = byId(loadFile("3e_binary.vec"), "V-3E-B-01").bytes;

    plc.corruptNext(Corruption::WrongSubheader);
    feed(plc, request);
    CHECK(drainAll(plc)[0] ==
          Bytes{0xD1, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x95, 0x19, 0x02,
                0x12, 0x30, 0x11});
    plc.corruptNext(Corruption::WrongRoute);
    feed(plc, request);
    CHECK(drainAll(plc)[0] ==
          Bytes{0xD0, 0x00, 0x01, 0x00, 0xFF, 0x03, 0x01, 0x08, 0x00, 0x00, 0x00, 0x95, 0x19, 0x02,
                0x12, 0x30, 0x11});
    plc.corruptNext(Corruption::Truncate);
    feed(plc, request);
    CHECK(drainAll(plc)[0] ==
          Bytes{0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x95, 0x19, 0x02,
                0x12, 0x30});
    plc.corruptNext(Corruption::JunkPrefix);
    feed(plc, request);
    CHECK(drainAll(plc)[0] ==
          Bytes{0x55, 0x55, 0x55, 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00,
                0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
    plc.corruptNext(Corruption::ExtraByte);
    feed(plc, request);
    CHECK(drainAll(plc)[0] ==
          Bytes{0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x95, 0x19, 0x02,
                0x12, 0x30, 0x11, 0x00});
}

TEST_CASE("MCK-11 3E ASCII: the documented text of the subheader and route modes") {
    MockPlc plc(FrameConfig::frame3E(DataCode::Ascii));
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    const Bytes request = byId(loadFile("3e_ascii.vec"), "V-3E-A-01").bytes;

    plc.corruptNext(Corruption::WrongSubheader);
    feed(plc, request);
    CHECK(asText(drainAll(plc)[0]) == "E00000FF03FF0000100000199512021130");
    plc.corruptNext(Corruption::WrongRoute);
    feed(plc, request);
    CHECK(asText(drainAll(plc)[0]) == "D000010003FF0100100000199512021130");
}

TEST_CASE("MCK-11 3E and 1E: error responses can be corrupted too") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        MockPlc reference = seeded(w);
        reference.failRange(DeviceType::D, 100, 100, 0xC051);
        feed(reference, w.read);
        const Bytes clean = drainAll(reference)[0];

        MockPlc plc = seeded(w);
        plc.failRange(DeviceType::D, 100, 100, 0xC051);
        plc.corruptNext(Corruption::Truncate);
        feed(plc, w.read);
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == expectedDamage(Corruption::Truncate, w, clean));
    }
}

TEST_CASE("MCK-11 3E and 1E: corruptNext calls queue in order, n = 0 does nothing, and a swallowed "
          "request does not use a corruption up") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        const Bytes clean = cleanResponse(w, w.read);
        MockPlc plc = seeded(w);

        plc.corruptNext(Corruption::Truncate, 1);
        plc.corruptNext(Corruption::ExtraByte, 1);
        plc.corruptNext(Corruption::JunkPrefix, 0);
        for (int i = 0; i < 3; ++i) {
            feed(plc, w.read);
        }
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 3);
        CHECK(got[0] == expectedDamage(Corruption::Truncate, w, clean));
        CHECK(got[1] == expectedDamage(Corruption::ExtraByte, w, clean));
        CHECK(got[2] == clean);

        plc.corruptNext(Corruption::Truncate, 1);
        plc.muteNext(1);
        feed(plc, w.read); // swallowed: the corruption waits for the next response
        feed(plc, w.read);
        got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == expectedDamage(Corruption::Truncate, w, clean));
    }
}

TEST_CASE("MCK-11 3E and 1E: clearFaults restores clean answers") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        const Bytes clean = cleanResponse(w, w.read);
        MockPlc plc = seeded(w);
        plc.failRange(DeviceType::D, 0, 0xFFFF, 0xC051);
        plc.mute(true);
        plc.muteNext(3);
        plc.corruptNext(Corruption::Truncate, 5);

        feed(plc, w.read);
        CHECK(drainAll(plc).empty()); // muted

        plc.clearFaults();
        feed(plc, w.read);
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == clean);
        CHECK(plc.requests().back().answered);
        CHECK(plc.requests().back().answeredWith.ok());
    }
}

TEST_CASE("MCK-09 1E: 04H and 05H are answered with unsupported1e, and the next request is not "
          "confused") {
    // 1E-12a..d: the 04H / 05H requests of the manual (tagged v1.1, so the client skips them).
    const std::vector<Vector> binVectors = loadFile("1e_binary.vec");
    const std::vector<Vector> ascVectors = loadFile("1e_ascii.vec");
    const Bytes test04Binary = byId(binVectors, "1E-12a").bytes;
    const Bytes test05Binary = byId(binVectors, "1E-12b").bytes;
    const Bytes test04Ascii = byId(ascVectors, "1E-12c").bytes;
    const Bytes test05Ascii = byId(ascVectors, "1E-12d").bytes;
    const Bytes readBinary = byId(binVectors, "V-1E-B-01").bytes;
    const Bytes readAscii = byId(ascVectors, "V-1E-A-01").bytes;

    SUBCASE("1E Binary") {
        MockPlc plc(FrameConfig::frame1E(DataCode::Binary));
        plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
        // Both frames and a good read in one buffer: 04H and 05H end where their n x item layout
        // says, so the read after them is decoded from its own first byte.
        Bytes stream = test04Binary;
        stream.insert(stream.end(), test05Binary.begin(), test05Binary.end());
        stream.insert(stream.end(), readBinary.begin(), readBinary.end());
        feed(plc, stream);
        std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 3);
        CHECK(got[0] == Bytes{0x84, 0x50});
        CHECK(got[1] == Bytes{0x85, 0x50});
        CHECK(got[2] == Bytes{0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
        REQUIRE(plc.requests().size() == 3);
        CHECK(plc.requests()[0].answered);
        CHECK(plc.requests()[0].answeredWith.plcCode == 0x50);
        CHECK(plc.requests()[1].answeredWith.plcCode == 0x50);
        CHECK(plc.requests()[2].answeredWith.ok());
        CHECK_FALSE(plc.bit(Device{DeviceType::Y, 0x94}));  // nothing of 04H was executed
        CHECK(plc.word(Device{DeviceType::W, 0x26}) == 0); // nor of 05H
    }
    SUBCASE("1E ASCII") {
        Bytes stream = test04Ascii;
        stream.insert(stream.end(), test05Ascii.begin(), test05Ascii.end());
        stream.insert(stream.end(), readAscii.begin(), readAscii.end());
        // Byte by byte as well: the frame end is found from the item count only.
        for (size_t chunk : {stream.size(), size_t{1}}) {
            MockPlc each(FrameConfig::frame1E(DataCode::Ascii));
            each.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
            for (size_t pos = 0; pos < stream.size(); pos += chunk) {
                each.bytesIn(ByteView{stream.data() + pos, std::min(chunk, stream.size() - pos)});
            }
            std::vector<Bytes> got = drainAll(each);
            REQUIRE(got.size() == 3);
            CHECK(asText(got[0]) == "8450");
            CHECK(asText(got[1]) == "8550");
            CHECK(asText(got[2]) == "8100199512021130");
        }
    }
}

TEST_CASE("MCK-09 1E: the error codes follow MockOptions") {
    MockOptions opt;
    opt.unsupported1e = 0x51;
    opt.outOfRange1e = 0x5B;
    opt.outOfRange1eAbnormal = 0x11;
    MockPlc plc(FrameConfig::frame1E(DataCode::Binary), opt);
    plc.setDeviceLimit(DeviceType::D, 10);
    feed(plc, Bytes{0x04, 0xFF, 0x0A, 0x00, 0x00, 0x00}); // 04H with n = 0: a complete frame
    feed(plc, byId(loadFile("1e_binary.vec"), "V-1E-B-01").bytes);
    std::vector<Bytes> got = drainAll(plc);
    REQUIRE(got.size() == 2);
    CHECK(got[0] == Bytes{0x84, 0x51});
    CHECK(got[1] == Bytes{0x81, 0x5B, 0x11});

    // An end code other than 5BH is sent without an abnormal code, whatever the option says.
    MockOptions other;
    other.outOfRange1e = 0x60;
    MockPlc plc2(FrameConfig::frame1E(DataCode::Ascii), other);
    plc2.setDeviceLimit(DeviceType::D, 10);
    feed(plc2, byId(loadFile("1e_ascii.vec"), "V-1E-A-01").bytes);
    got = drainAll(plc2);
    REQUIRE(got.size() == 1);
    CHECK(asText(got[0]) == "8160");
}

TEST_CASE("MCK-11 1E Binary: the documented bytes of each mode, spelled out") {
    // Clean: 81 00 | 95 19 02 12 30 11
    MockPlc plc(FrameConfig::frame1E(DataCode::Binary));
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    const Bytes request = byId(loadFile("1e_binary.vec"), "V-1E-B-01").bytes;

    plc.corruptNext(Corruption::WrongSubheader);
    feed(plc, request);
    CHECK(drainAll(plc)[0] == Bytes{0x80, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
    plc.corruptNext(Corruption::WrongRoute); // no route fields in a 1E response: unchanged
    feed(plc, request);
    CHECK(drainAll(plc)[0] == Bytes{0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
    plc.corruptNext(Corruption::Truncate);
    feed(plc, request);
    CHECK(drainAll(plc)[0] == Bytes{0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30});
    plc.corruptNext(Corruption::JunkPrefix);
    feed(plc, request);
    CHECK(drainAll(plc)[0] ==
          Bytes{0x55, 0x55, 0x55, 0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
    plc.corruptNext(Corruption::ExtraByte);
    feed(plc, request);
    CHECK(drainAll(plc)[0] == Bytes{0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11, 0x00});
    plc.corruptNext(Corruption::WrongSumCheck);
    plc.corruptNext(Corruption::WrongBlockNo);
    feed(plc, request);
    feed(plc, request);
    std::vector<Bytes> got = drainAll(plc);
    REQUIRE(got.size() == 2);
    CHECK(got[0] == Bytes{0x81, 0x00, 0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
    CHECK(got[1] == got[0]);
}

TEST_CASE("MCK-11 1E ASCII: the documented text of the subheader and route modes, and the error "
          "responses") {
    MockPlc plc(FrameConfig::frame1E(DataCode::Ascii));
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    const Bytes request = byId(loadFile("1e_ascii.vec"), "V-1E-A-01").bytes;

    plc.corruptNext(Corruption::WrongSubheader);
    feed(plc, request);
    CHECK(asText(drainAll(plc)[0]) == "E100199512021130");
    plc.corruptNext(Corruption::WrongRoute);
    feed(plc, request);
    CHECK(asText(drainAll(plc)[0]) == "8100199512021130");

    // An error response is damaged like any other: "81" "5B" "10" -> "E1" "5B" "10".
    plc.failRange(DeviceType::D, 100, 100, 0x5B, 0x10);
    plc.corruptNext(Corruption::WrongSubheader);
    feed(plc, request);
    CHECK(asText(drainAll(plc)[0]) == "E15B10");
}

// ---- 3C and 1C ---------------------------------------------------------------------------------

namespace {

std::vector<Wire> serialWires() {
    std::vector<Wire> out;
    for (const Wire& w : wires()) {
        if (w.serial()) {
            out.push_back(w);
        }
    }
    return out;
}

// Request data the mock frames by its layout and answers with the unsupported error of the family.
std::vector<std::pair<const char*, std::string>> unsupportedData(bool threeC) {
    if (threeC) {
        return {
            {"0403 random read, Q/L", "04030000" "0100" "D*000100"},
            {"0403 random read, iQ-R, one word and one dword device",
             "04030002" "0101" "D***00000100" "D***00000200"},
            {"1402 random write, word units", "14020000" "0100" "D*000100" "1234"},
            {"1402 random write, one word and one dword",
             "14020002" "0101" "D***00000100" "1234" "D***00000200" "12345678"},
            {"1402 random write, bit units, Q/L", "14020001" "01" "M*000100" "01"},
            {"1402 random write, bit units, iQ-R", "14020003" "01" "M***00000100" "0001"},
        };
    }
    return {
        {"BT", "BT0" "01" "M0100" "1"},
        {"WT", "WT0" "01" "D0100" "1234"},
        {"JT", "JT0" "01" "M000100" "1"},
        {"QT", "QT0" "02" "D000100" "1234" "D000101" "5678"},
    };
}

} // namespace

TEST_CASE("MCK-09 3C and 1C: commands the mock does not execute are answered with the unsupported "
          "error of the family") {
    for (const Wire& w : serialWires()) {
        INFO(w.name);
        const bool threeC = w.cfg.frame == FrameType::F3C;
        const std::string code = threeC ? "C059" : "06";
        const auto commands = unsupportedData(threeC);
        for (size_t chunk : {size_t{1024}, size_t{1}}) {
            INFO("chunk ", chunk);
            // Every such request and a good read after them, in one stream: each ends where its
            // layout says, so the read is found.
            Bytes stream;
            for (const auto& c : commands) {
                const Bytes frame = sf::request(w.cfg, c.second);
                stream.insert(stream.end(), frame.begin(), frame.end());
            }
            stream.insert(stream.end(), w.read.begin(), w.read.end());
            MockPlc plc = seeded(w);
            for (size_t pos = 0; pos < stream.size(); pos += chunk) {
                plc.bytesIn(ByteView{stream.data() + pos, std::min(chunk, stream.size() - pos)});
            }
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == commands.size() + 1);
            for (size_t i = 0; i < commands.size(); ++i) {
                INFO(commands[i].first);
                CHECK(got[i] == sf::response(w.cfg, sf::Kind::Nak, code));
                CHECK(plc.requests()[i].answered);
                CHECK(plc.requests()[i].answeredWith.plcCode == (threeC ? 0xC059 : 0x06));
            }
            CHECK(got.back() == sf::response(w.cfg, sf::Kind::Data, "199512021130"));
            CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1995); // nothing was executed
        }

        // The codes are MockOptions values.
        MockOptions opt;
        opt.unsupportedQna = 0xC061;
        opt.unsupported1c = 0x07;
        MockPlc custom(w.cfg, opt);
        feed(custom, sf::request(w.cfg, commands.front().second));
        const std::vector<Bytes> got = drainAll(custom);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == sf::response(w.cfg, sf::Kind::Nak, threeC ? "C061" : "07"));
    }
}

TEST_CASE("MCK-09 3C and 1C: request data that does not fit the command is answered, not "
          "executed") {
    struct Case {
        const char* name;
        std::string data3c;
        std::string data1c; // empty: no 1C twin
    };
    const std::vector<Case> cases = {
        {"bit read of a word device", sf::data3c(0x0401, 0x0001, "D*000100", 1),
         sf::data1c("BR", '0', "D0100", 1)},
        {"device without a code in the family", sf::data3c(0x0401, 0x0000, "RD000100", 1),
         sf::data1c("WR", '0', "V0100", 1)},
        {"unknown device code", sf::data3c(0x0401, 0x0000, "Q*000100", 1),
         sf::data1c("WR", '0', "Q0100", 1)},
        {"digit outside the radix", sf::data3c(0x0401, 0x0000, "D*0001G0", 1),
         sf::data1c("WR", '0', "D01G0", 1)},
        {"zero points (3C)", sf::data3c(0x0401, 0x0000, "D*000100", 0), ""},
        {"word data with a bad digit", sf::data3c(0x1401, 0x0000, "D*000100", 1, "12G4"),
         sf::data1c("WW", '0', "D0100", 1, "12G4")},
        {"bit data that is not 0 or 1", sf::data3c(0x1401, 0x0001, "M*000100", 2, "12"),
         sf::data1c("BW", '0', "M0100", 2, "12")},
        {"bit write to a word device", sf::data3c(0x1401, 0x0001, "D*000100", 1, "1"),
         sf::data1c("BW", '0', "D0100", 1, "1")},
    };
    for (const Wire& w : serialWires()) {
        INFO(w.name);
        const bool threeC = w.cfg.frame == FrameType::F3C;
        const std::string code = threeC ? "C059" : "06";
        for (const Case& c : cases) {
            const std::string& data = threeC ? c.data3c : c.data1c;
            if (data.empty()) {
                continue;
            }
            INFO(c.name);
            MockPlc plc = seeded(w);
            feed(plc, sf::request(w.cfg, data));
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == sf::response(w.cfg, sf::Kind::Nak, code));
            CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1995);
            CHECK_FALSE(plc.bit(Device{DeviceType::M, 100}));
        }
    }
}

TEST_CASE("MCK-09 3C and 1C format 3: a frame is framed by its ETX, so an unknown command or a "
          "short write is answered") {
    for (const Wire& w : serialWires()) {
        if (w.cfg.format != SerialFormat::Format3) {
            continue;
        }
        INFO(w.name);
        const bool threeC = w.cfg.frame == FrameType::F3C;
        const std::vector<std::string> data =
            threeC ? std::vector<std::string>{"0406" "0000" "00", "04010004" "D*000100" "0001", "",
                                              "0401"}
                   : std::vector<std::string>{"ZZ0" "D0100" "01", "WW0" "D0100" "02" "1234", "",
                                              "WR"};
        for (const std::string& d : data) {
            INFO("request data \"", d, "\"");
            MockPlc plc = seeded(w);
            feed(plc, sf::request(w.cfg, d));
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == sf::response(w.cfg, sf::Kind::Nak, threeC ? "C059" : "06"));
        }
    }
}

TEST_CASE("MCK-11 3C and 1C: the documented bytes of each mode, spelled out") {
    const std::string stx = "\x02";
    const std::string etx = "\x03";
    const std::string tail = "199512021130" + etx;
    // 3C format 1: clean <STX>F90000FF00199512021130<ETX>90
    {
        MockPlc plc(FrameConfig::frame3C(SerialFormat::Format1));
        plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
        const Bytes request = byId(loadFile("3c_f1.vec"), "V-3C1-01").bytes;
        auto answer = [&](Corruption mode) {
            plc.corruptNext(mode);
            feed(plc, request);
            return asText(drainAll(plc)[0]);
        };
        CHECK(answer(Corruption::WrongSubheader) == stx + "F90000FF00" + tail + "90");
        CHECK(answer(Corruption::WrongSumCheck) == stx + "F90000FF00" + tail + "91");
        // Station, network and PC + 1 (PC FF wraps to 00), self-station untouched, SUM recomputed.
        CHECK(answer(Corruption::WrongRoute) == stx + "F901010000" + tail + "66");
        CHECK(answer(Corruption::WrongBlockNo) == stx + "F90000FF00" + tail + "90"); // not format 2
        CHECK(answer(Corruption::Truncate) == stx + "F90000FF00" + tail + "9");
        CHECK(answer(Corruption::JunkPrefix) == "UUU" + stx + "F90000FF00" + tail + "90");
        CHECK(answer(Corruption::ExtraByte) ==
              stx + "F90000FF00" + tail + "90" + std::string(1, '\0'));
    }
    // 1C format 2: clean <STX>0000FF199512021130<ETX>B1 (block 00, station 00, PC FF)
    {
        MockPlc plc(FrameConfig::frame1C(SerialFormat::Format2));
        plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
        const Bytes request = byId(loadFile("1c_f2.vec"), "V-1C2-01").bytes;
        auto answer = [&](Corruption mode) {
            plc.corruptNext(mode);
            feed(plc, request);
            return asText(drainAll(plc)[0]);
        };
        CHECK(answer(Corruption::WrongSumCheck) == stx + "0000FF" + tail + "B2");
        CHECK(answer(Corruption::WrongRoute) == stx + "00" "0100" + tail + "86");
        CHECK(answer(Corruption::WrongBlockNo) == stx + "0100FF" + tail + "B2");
        CHECK(answer(Corruption::WrongSubheader) == stx + "0000FF" + tail + "B1");
    }
    // Format 4: the LF is what Truncate drops, ExtraByte goes after the CR LF, and a short ACK has
    // no SUM for WrongSumCheck to change.
    {
        const FrameConfig cfg = FrameConfig::frame1C(SerialFormat::Format4);
        MockPlc plc(cfg);
        plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
        const std::vector<Vector> vectors = loadFile("1c_f4.vec");
        const Bytes request = byId(vectors, "V-1C4-01").bytes;
        const Bytes clean = byId(vectors, "V-1C4-02").bytes;
        plc.corruptNext(Corruption::Truncate);
        feed(plc, request);
        CHECK(drainAll(plc)[0] == Bytes(clean.begin(), clean.end() - 1));
        plc.corruptNext(Corruption::ExtraByte);
        feed(plc, request);
        Bytes extra = clean;
        extra.push_back(0x00);
        CHECK(drainAll(plc)[0] == extra);
        plc.corruptNext(Corruption::WrongSumCheck);
        feed(plc, sf::request(cfg, sf::writeD(cfg, 100, "0001")));
        CHECK(drainAll(plc)[0] == byId(vectors, "V-1C4-07").bytes);
    }
}

TEST_CASE("MCK-11 3C and 1C: a failRange fault is answered with NAK, QNAK or NN, and 1C sends the "
          "low byte of the code") {
    for (const Wire& w : serialWires()) {
        INFO(w.name);
        const bool threeC = w.cfg.frame == FrameType::F3C;
        MockPlc plc = seeded(w);
        plc.failRange(DeviceType::D, 100, 100, 0x7151);
        feed(plc, w.read);
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == sf::response(w.cfg, sf::Kind::Nak, threeC ? "7151" : "51"));
        CHECK(plc.requests()[0].answeredWith.plcCode == (threeC ? 0x7151 : 0x51));
    }
}

// ---- MCK-16: the error codes of a MockTarget (SPEC-mock-plc "Targets") -------------------------
//
// Every request and every expected response in kCells is a record of a hardware capture
// (tests/vectors/captured/<profile>/steps.vec, ids CAP-<profile>-<step> and ...-R): the bytes are
// what the PLC sent, never what the mock sends. A cell is set up the way a replay of the profile
// sets the mock up: setDeviceLimit for an out-of-range step, setTypeSupported(type, false) for a
// step whose device type is outside the profile's `supports`, nothing for a wrong-sum step.

namespace {

enum class Prof {
    Q3eBin, // Q03UDECPU built-in Ethernet, 3E Binary
    Q3eAsc, // 3E ASCII
    F5eBin, // FX5U built-in Ethernet, 3E Binary
    F5eAsc, // 3E ASCII
    Q3cF1,  // Q03UDECPU + QJ71C24N, 3C format 1 with SUM
    Q3cF1n, // format 1 without SUM
    Q3cF2,  // format 2 (block number 5AH)
    Q3cF3,  // format 3
    Q3cF4,  // format 4
    F5c,    // FX5U RS-485, 3C format 1 with SUM
    F3eBin, // FX3U + FX3U-ENET-ADP, 1E Binary
    F3eAsc, // 1E ASCII
    F3cF1,  // FX3U computer link, 1C format 1 with SUM
    F3cF1n, // 1C format 1 without SUM
    F3cF4   // 1C format 4
};

enum class CellKind {
    Range, // a device beyond the profile's device_end
    Type,  // a device type outside the profile's `supports`
    Sum    // a request whose SUM characters were altered (G7-03)
};

struct Cell {
    const char* id;
    Prof prof;
    MockTarget target;
    CellKind kind;
    DeviceType type; // the request's head device type
    uint32_t limit;  // Range: the first device number that does not exist
    const char* request;
    const char* response;
};

Bytes hexBytes(const char* text) {
    Bytes out;
    unsigned value = 0;
    int digits = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        const char ch = *p;
        if (ch == ' ') {
            continue;
        }
        const unsigned nibble =
            ch >= 'A' ? static_cast<unsigned>(ch - 'A' + 10) : static_cast<unsigned>(ch - '0');
        value = (value << 4) | nibble;
        if (++digits == 2) {
            out.push_back(static_cast<uint8_t>(value));
            value = 0;
            digits = 0;
        }
    }
    return out;
}

std::string hexText(const Bytes& b) {
    std::string t;
    char two[4];
    for (uint8_t v : b) {
        std::snprintf(two, sizeof(two), "%02X ", v);
        t += two;
    }
    return t;
}

FrameConfig profileConfig(Prof p) {
    FrameConfig cfg;
    switch (p) {
    case Prof::Q3eBin:
        return FrameConfig::frame3E(DataCode::Binary);
    case Prof::Q3eAsc:
        return FrameConfig::frame3E(DataCode::Ascii);
    case Prof::F5eBin:
    case Prof::F5eAsc:
        cfg = FrameConfig::frame3E(p == Prof::F5eBin ? DataCode::Binary : DataCode::Ascii);
        cfg.xyNotation = XyNumbering::Octal;
        return cfg;
    case Prof::Q3cF1:
    case Prof::Q3cF1n:
    case Prof::F5c:
        cfg = FrameConfig::frame3C(SerialFormat::Format1);
        cfg.sumCheck = p != Prof::Q3cF1n;
        if (p == Prof::F5c) {
            cfg.xyNotation = XyNumbering::Octal;
        }
        return cfg;
    case Prof::Q3cF2:
        cfg = FrameConfig::frame3C(SerialFormat::Format2);
        cfg.blockNo = 90;
        return cfg;
    case Prof::Q3cF3:
        return FrameConfig::frame3C(SerialFormat::Format3);
    case Prof::Q3cF4:
        return FrameConfig::frame3C(SerialFormat::Format4);
    case Prof::F3eBin:
    case Prof::F3eAsc:
        cfg = FrameConfig::frame1E(p == Prof::F3eBin ? DataCode::Binary : DataCode::Ascii);
        cfg.targetFamily = TargetFamily::A;
        cfg.xyNotation = XyNumbering::Octal;
        return cfg;
    case Prof::F3cF1:
    case Prof::F3cF1n:
    case Prof::F3cF4:
        cfg =
            FrameConfig::frame1C(p == Prof::F3cF4 ? SerialFormat::Format4 : SerialFormat::Format1);
        cfg.sumCheck = p != Prof::F3cF1n;
        cfg.targetFamily = TargetFamily::A;
        cfg.xyNotation = XyNumbering::Octal;
        cfg.xyAsciiDigits = XyNumbering::Octal;
        return cfg;
    }
    return cfg;
}

bool isEthernetProf(Prof p) { return p <= Prof::F5eAsc; }
bool isThreeCProf(Prof p) { return p >= Prof::Q3cF1 && p <= Prof::F5c; }
bool isOneEProf(Prof p) { return p == Prof::F3eBin || p == Prof::F3eAsc; }
bool isOneCProf(Prof p) { return p >= Prof::F3cF1; }

// The mock set up for a cell and fed its request; returns the one response.
Bytes answerOfCell(const Cell& c, MockTarget target) {
    MockOptions opt;
    opt.target = target;
    MockPlc plc(profileConfig(c.prof), opt);
    if (c.kind == CellKind::Range) {
        plc.setDeviceLimit(c.type, c.limit);
    } else if (c.kind == CellKind::Type) {
        plc.setTypeSupported(c.type, false);
    }
    feed(plc, hexBytes(c.request));
    std::vector<Bytes> all = drainAll(plc);
    INFO(c.id);
    REQUIRE(all.size() == 1);
    return all[0];
}

const Cell kCells[] = {
    {"q03ude-eth-3e-bin-G5-02", Prof::Q3eBin, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288, "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 30 00 A8 01 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"},
    {"q03ude-eth-3e-bin-G5-03", Prof::Q3eBin, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288, "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 FF 2F 00 A8 02 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"},
    {"q03ude-eth-3e-bin-G5-04", Prof::Q3eBin, MockTarget::QCpu, CellKind::Range, DeviceType::M,
     8192, "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 20 00 90 01 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 01 00"},
    {"q03ude-eth-3e-bin-G4-02-STS", Prof::Q3eBin, MockTarget::QCpu, CellKind::Type, DeviceType::STS,
     0, "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 C7 01 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 01 00"},
    {"q03ude-eth-3e-bin-G4-02-STN", Prof::Q3eBin, MockTarget::QCpu, CellKind::Type, DeviceType::STN,
     0, "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 C8 01 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"},
    {"q03ude-eth-3e-bin-G4-02-R", Prof::Q3eBin, MockTarget::QCpu, CellKind::Type, DeviceType::R, 0,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 AF 01 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"},
    {"q03ude-eth-3e-bin-G4-02-ZR", Prof::Q3eBin, MockTarget::QCpu, CellKind::Type, DeviceType::ZR,
     0, "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 B0 01 00",
     "D0 00 00 FF FF 03 00 0B 00 31 40 00 FF FF 03 00 01 04 00 00"},
    {"q03ude-eth-3e-ascii-G5-02", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 "
     "2A 30 31 32 32 38 38 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"q03ude-eth-3e-ascii-G5-03", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 "
     "2A 30 31 32 32 38 37 30 30 30 32",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"q03ude-eth-3e-ascii-G5-04", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Range, DeviceType::M,
     8192,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 4D "
     "2A 30 30 38 31 39 32 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 31"},
    {"q03ude-eth-3e-ascii-G4-02-STS", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Type,
     DeviceType::STS, 0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 53 "
     "53 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 31"},
    {"q03ude-eth-3e-ascii-G4-02-STN", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Type,
     DeviceType::STN, 0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 53 "
     "4E 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"q03ude-eth-3e-ascii-G4-02-R", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Type, DeviceType::R,
     0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 52 "
     "2A 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"q03ude-eth-3e-ascii-G4-02-ZR", Prof::Q3eAsc, MockTarget::QCpu, CellKind::Type, DeviceType::ZR,
     0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 5A "
     "52 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 34 30 33 31 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"fx5u-eth-3e-bin-G5-02", Prof::F5eBin, MockTarget::Fx5, CellKind::Range, DeviceType::D, 8000,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 40 1F 00 A8 01 00",
     "D0 00 00 FF FF 03 00 0B 00 56 C0 00 FF FF 03 00 01 04 00 00"},
    {"fx5u-eth-3e-bin-G5-03", Prof::F5eBin, MockTarget::Fx5, CellKind::Range, DeviceType::D, 8000,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 3F 1F 00 A8 02 00",
     "D0 00 00 FF FF 03 00 0B 00 56 C0 00 FF FF 03 00 01 04 00 00"},
    {"fx5u-eth-3e-bin-G5-04", Prof::F5eBin, MockTarget::Fx5, CellKind::Range, DeviceType::M, 7680,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 1E 00 90 01 00",
     "D0 00 00 FF FF 03 00 0B 00 56 C0 00 FF FF 03 00 01 04 01 00"},
    {"fx5u-eth-3e-bin-G4-02-V", Prof::F5eBin, MockTarget::Fx5, CellKind::Type, DeviceType::V, 0,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 94 01 00",
     "D0 00 00 FF FF 03 00 0B 00 5C C0 00 FF FF 03 00 01 04 01 00"},
    {"fx5u-eth-3e-bin-G4-02-DX", Prof::F5eBin, MockTarget::Fx5, CellKind::Type, DeviceType::DX, 0,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 A2 01 00",
     "D0 00 00 FF FF 03 00 0B 00 5C C0 00 FF FF 03 00 01 04 01 00"},
    {"fx5u-eth-3e-bin-G4-02-DY", Prof::F5eBin, MockTarget::Fx5, CellKind::Type, DeviceType::DY, 0,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 00 00 00 A3 01 00",
     "D0 00 00 FF FF 03 00 0B 00 5C C0 00 FF FF 03 00 01 04 01 00"},
    {"fx5u-eth-3e-bin-G4-02-ZR", Prof::F5eBin, MockTarget::Fx5, CellKind::Type, DeviceType::ZR, 0,
     "50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 00 00 00 B0 01 00",
     "D0 00 00 FF FF 03 00 0B 00 5C C0 00 FF FF 03 00 01 04 00 00"},
    {"fx5u-eth-3e-ascii-G5-02", Prof::F5eAsc, MockTarget::Fx5, CellKind::Range, DeviceType::D, 8000,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 "
     "2A 30 30 38 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 36 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"fx5u-eth-3e-ascii-G5-03", Prof::F5eAsc, MockTarget::Fx5, CellKind::Range, DeviceType::D, 8000,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 "
     "2A 30 30 37 39 39 39 30 30 30 32",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 36 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"fx5u-eth-3e-ascii-G5-04", Prof::F5eAsc, MockTarget::Fx5, CellKind::Range, DeviceType::M, 7680,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 4D "
     "2A 30 30 37 36 38 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 36 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 31"},
    {"fx5u-eth-3e-ascii-G4-02-V", Prof::F5eAsc, MockTarget::Fx5, CellKind::Type, DeviceType::V, 0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 56 "
     "2A 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 43 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 31"},
    {"fx5u-eth-3e-ascii-G4-02-DX", Prof::F5eAsc, MockTarget::Fx5, CellKind::Type, DeviceType::DX, 0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 44 "
     "58 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 30 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 31"},
    {"fx5u-eth-3e-ascii-G4-02-DY", Prof::F5eAsc, MockTarget::Fx5, CellKind::Type, DeviceType::DY, 0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 44 "
     "59 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 30 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 31"},
    {"fx5u-eth-3e-ascii-G4-02-ZR", Prof::F5eAsc, MockTarget::Fx5, CellKind::Type, DeviceType::ZR, 0,
     "35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 5A "
     "52 30 30 30 30 30 30 30 30 30 31",
     "44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 43 30 30 46 46 30 33 46 46 30 "
     "30 30 34 30 31 30 30 30 30"},
    {"q03ude-c24-3c-f1-sum-G5-02", Prof::Q3cF1, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 38 30 30 30 31 "
     "31 34",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-sum-G5-03", Prof::Q3cF1, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 37 30 30 30 32 "
     "31 34",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-sum-G5-04", Prof::Q3cF1, MockTarget::QCpu, CellKind::Range, DeviceType::M,
     8192,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 38 31 39 32 30 30 30 31 "
     "31 44",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-sum-G4-02-STS", Prof::Q3cF1, MockTarget::QCpu, CellKind::Type,
     DeviceType::STS, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 53 53 30 30 30 30 30 30 30 30 30 31 "
     "33 38",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-sum-G4-02-R", Prof::Q3cF1, MockTarget::QCpu, CellKind::Type, DeviceType::R,
     0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 52 2A 30 30 30 30 30 30 30 30 30 31 "
     "30 44",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-sum-G4-02-ZR", Prof::Q3cF1, MockTarget::QCpu, CellKind::Type, DeviceType::ZR,
     0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 5A 52 30 30 30 30 30 30 30 30 30 31 "
     "33 44",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-sum-G7-03", Prof::Q3cF1, MockTarget::QCpu, CellKind::Sum, DeviceType::D, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 31 "
     "30 31",
     "15 46 39 30 30 30 30 46 46 30 30 37 46 32 34"},
    {"q03ude-c24-3c-f1-nosum-G5-02", Prof::Q3cF1n, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 38 30 30 30 31",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-nosum-G5-03", Prof::Q3cF1n, MockTarget::QCpu, CellKind::Range, DeviceType::D,
     12288,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 37 30 30 30 32",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-nosum-G5-04", Prof::Q3cF1n, MockTarget::QCpu, CellKind::Range, DeviceType::M,
     8192,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 38 31 39 32 30 30 30 31",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-nosum-G4-02-STS", Prof::Q3cF1n, MockTarget::QCpu, CellKind::Type,
     DeviceType::STS, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 53 53 30 30 30 30 30 30 30 30 30 31",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-nosum-G4-02-R", Prof::Q3cF1n, MockTarget::QCpu, CellKind::Type,
     DeviceType::R, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 52 2A 30 30 30 30 30 30 30 30 30 31",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f1-nosum-G4-02-ZR", Prof::Q3cF1n, MockTarget::QCpu, CellKind::Type,
     DeviceType::ZR, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 5A 52 30 30 30 30 30 30 30 30 30 31",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G5-02", Prof::Q3cF2, MockTarget::QCpu, CellKind::Range, DeviceType::D, 12288,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 38 30 30 "
     "30 31 38 41",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G5-03", Prof::Q3cF2, MockTarget::QCpu, CellKind::Range, DeviceType::D, 12288,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 37 30 30 "
     "30 32 38 41",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G5-04", Prof::Q3cF2, MockTarget::QCpu, CellKind::Range, DeviceType::M, 8192,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 38 31 39 32 30 30 "
     "30 31 39 33",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G4-02-STS", Prof::Q3cF2, MockTarget::QCpu, CellKind::Type, DeviceType::STS,
     0,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 53 53 30 30 30 30 30 30 30 30 "
     "30 31 41 45",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G4-02-R", Prof::Q3cF2, MockTarget::QCpu, CellKind::Type, DeviceType::R, 0,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 52 2A 30 30 30 30 30 30 30 30 "
     "30 31 38 33",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G4-02-ZR", Prof::Q3cF2, MockTarget::QCpu, CellKind::Type, DeviceType::ZR, 0,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 5A 52 30 30 30 30 30 30 30 30 "
     "30 31 42 33",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 34 30 33 31"},
    {"q03ude-c24-3c-f2-G7-03", Prof::Q3cF2, MockTarget::QCpu, CellKind::Sum, DeviceType::D, 0,
     "05 35 41 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 "
     "30 31 37 37",
     "15 35 41 46 39 30 30 30 30 46 46 30 30 37 46 32 34"},
    {"q03ude-c24-3c-f3-G5-02", Prof::Q3cF3, MockTarget::QCpu, CellKind::Range, DeviceType::D, 12288,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 38 30 30 30 31 "
     "03 31 37",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03"},
    {"q03ude-c24-3c-f3-G5-03", Prof::Q3cF3, MockTarget::QCpu, CellKind::Range, DeviceType::D, 12288,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 37 30 30 30 32 "
     "03 31 37",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03"},
    {"q03ude-c24-3c-f3-G5-04", Prof::Q3cF3, MockTarget::QCpu, CellKind::Range, DeviceType::M, 8192,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 38 31 39 32 30 30 30 31 "
     "03 32 30",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03"},
    {"q03ude-c24-3c-f3-G4-02-STS", Prof::Q3cF3, MockTarget::QCpu, CellKind::Type, DeviceType::STS,
     0,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 53 53 30 30 30 30 30 30 30 30 30 31 "
     "03 33 42",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03"},
    {"q03ude-c24-3c-f3-G4-02-R", Prof::Q3cF3, MockTarget::QCpu, CellKind::Type, DeviceType::R, 0,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 52 2A 30 30 30 30 30 30 30 30 30 31 "
     "03 31 30",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03"},
    {"q03ude-c24-3c-f3-G4-02-ZR", Prof::Q3cF3, MockTarget::QCpu, CellKind::Type, DeviceType::ZR, 0,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 5A 52 30 30 30 30 30 30 30 30 30 31 "
     "03 34 30",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03"},
    {"q03ude-c24-3c-f3-G7-03", Prof::Q3cF3, MockTarget::QCpu, CellKind::Sum, DeviceType::D, 0,
     "02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 31 "
     "03 30 34",
     "02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 37 46 32 34 03"},
    {"q03ude-c24-3c-f4-G5-02", Prof::Q3cF4, MockTarget::QCpu, CellKind::Range, DeviceType::D, 12288,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 38 30 30 30 31 "
     "31 34 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31 0D 0A"},
    {"q03ude-c24-3c-f4-G5-03", Prof::Q3cF4, MockTarget::QCpu, CellKind::Range, DeviceType::D, 12288,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 31 32 32 38 37 30 30 30 32 "
     "31 34 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31 0D 0A"},
    {"q03ude-c24-3c-f4-G5-04", Prof::Q3cF4, MockTarget::QCpu, CellKind::Range, DeviceType::M, 8192,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 38 31 39 32 30 30 30 31 "
     "31 44 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31 0D 0A"},
    {"q03ude-c24-3c-f4-G4-02-STS", Prof::Q3cF4, MockTarget::QCpu, CellKind::Type, DeviceType::STS,
     0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 53 53 30 30 30 30 30 30 30 30 30 31 "
     "33 38 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31 0D 0A"},
    {"q03ude-c24-3c-f4-G4-02-R", Prof::Q3cF4, MockTarget::QCpu, CellKind::Type, DeviceType::R, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 52 2A 30 30 30 30 30 30 30 30 30 31 "
     "30 44 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31 0D 0A"},
    {"q03ude-c24-3c-f4-G4-02-ZR", Prof::Q3cF4, MockTarget::QCpu, CellKind::Type, DeviceType::ZR, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 5A 52 30 30 30 30 30 30 30 30 30 31 "
     "33 44 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 34 30 33 31 0D 0A"},
    {"q03ude-c24-3c-f4-G7-03", Prof::Q3cF4, MockTarget::QCpu, CellKind::Sum, DeviceType::D, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 31 "
     "30 31 0D 0A",
     "15 46 39 30 30 30 30 46 46 30 30 37 46 32 34 0D 0A"},
    {"fx5u-rs485-3c-f1-sum-G5-02", Prof::F5c, MockTarget::Fx5, CellKind::Range, DeviceType::D, 8000,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 38 30 30 30 30 30 30 31 "
     "30 37",
     "15 46 39 30 30 30 30 46 46 30 30 37 31 34 33"},
    {"fx5u-rs485-3c-f1-sum-G5-03", Prof::F5c, MockTarget::Fx5, CellKind::Range, DeviceType::D, 8000,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 37 39 39 39 30 30 30 32 "
     "32 32",
     "15 46 39 30 30 30 30 46 46 30 30 37 31 34 33"},
    {"fx5u-rs485-3c-f1-sum-G5-04", Prof::F5c, MockTarget::Fx5, CellKind::Range, DeviceType::M, 7680,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 4D 2A 30 30 37 36 38 30 30 30 30 31 "
     "31 45",
     "15 46 39 30 30 30 30 46 46 30 30 37 31 34 33"},
    {"fx5u-rs485-3c-f1-sum-G4-02-V", Prof::F5c, MockTarget::Fx5, CellKind::Type, DeviceType::V, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 56 2A 30 30 30 30 30 30 30 30 30 31 "
     "31 32",
     "15 46 39 30 30 30 30 46 46 30 30 37 45 34 33"},
    {"fx5u-rs485-3c-f1-sum-G4-02-DX", Prof::F5c, MockTarget::Fx5, CellKind::Type, DeviceType::DX, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 44 58 30 30 30 30 30 30 30 30 30 31 "
     "32 45",
     "15 46 39 30 30 30 30 46 46 30 30 37 46 32 31"},
    {"fx5u-rs485-3c-f1-sum-G4-02-DY", Prof::F5c, MockTarget::Fx5, CellKind::Type, DeviceType::DY, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 31 44 59 30 30 30 30 30 30 30 30 30 31 "
     "32 46",
     "15 46 39 30 30 30 30 46 46 30 30 37 46 32 31"},
    {"fx5u-rs485-3c-f1-sum-G4-02-ZR", Prof::F5c, MockTarget::Fx5, CellKind::Type, DeviceType::ZR, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 5A 52 30 30 30 30 30 30 30 30 30 31 "
     "33 44",
     "15 46 39 30 30 30 30 46 46 30 30 37 45 34 33"},
    {"fx5u-rs485-3c-f1-sum-G7-03", Prof::F5c, MockTarget::Fx5, CellKind::Sum, DeviceType::D, 0,
     "05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 31 "
     "30 31",
     "15 46 39 30 30 30 30 46 46 30 30 37 46 32 34"},
    {"fx3-eth-1e-bin-G5-04", Prof::F3eBin, MockTarget::Fx3, CellKind::Range, DeviceType::M, 7680,
     "00 FF 0A 00 00 1E 00 00 20 4D 01 00", "80 58"},
    {"fx3-eth-1e-bin-G4-02-F", Prof::F3eBin, MockTarget::Fx3, CellKind::Type, DeviceType::F, 0,
     "00 FF 0A 00 00 00 00 00 20 46 01 00", "80 56"},
    {"fx3-eth-1e-bin-G4-02-B", Prof::F3eBin, MockTarget::Fx3, CellKind::Type, DeviceType::B, 0,
     "00 FF 0A 00 00 00 00 00 20 42 01 00", "80 56"},
    {"fx3-eth-1e-bin-G4-02-W", Prof::F3eBin, MockTarget::Fx3, CellKind::Type, DeviceType::W, 0,
     "01 FF 0A 00 00 00 00 00 20 57 01 00", "81 56"},
    {"fx3-eth-1e-bin-G4-02-TC", Prof::F3eBin, MockTarget::Fx3, CellKind::Type, DeviceType::TC, 0,
     "00 FF 0A 00 00 00 00 00 43 54 01 00", "80 56"},
    {"fx3-eth-1e-bin-G4-02-CC", Prof::F3eBin, MockTarget::Fx3, CellKind::Type, DeviceType::CC, 0,
     "00 FF 0A 00 00 00 00 00 43 43 01 00", "80 56"},
    {"fx3-eth-1e-ascii-G5-04", Prof::F3eAsc, MockTarget::Fx3, CellKind::Range, DeviceType::M, 7680,
     "30 30 46 46 30 30 30 41 34 44 32 30 30 30 30 30 31 45 30 30 30 31 30 30", "38 30 35 38"},
    {"fx3-eth-1e-ascii-G4-02-F", Prof::F3eAsc, MockTarget::Fx3, CellKind::Type, DeviceType::F, 0,
     "30 30 46 46 30 30 30 41 34 36 32 30 30 30 30 30 30 30 30 30 30 31 30 30", "38 30 35 36"},
    {"fx3-eth-1e-ascii-G4-02-B", Prof::F3eAsc, MockTarget::Fx3, CellKind::Type, DeviceType::B, 0,
     "30 30 46 46 30 30 30 41 34 32 32 30 30 30 30 30 30 30 30 30 30 31 30 30", "38 30 35 36"},
    {"fx3-eth-1e-ascii-G4-02-W", Prof::F3eAsc, MockTarget::Fx3, CellKind::Type, DeviceType::W, 0,
     "30 31 46 46 30 30 30 41 35 37 32 30 30 30 30 30 30 30 30 30 30 31 30 30", "38 31 35 36"},
    {"fx3-eth-1e-ascii-G4-02-TC", Prof::F3eAsc, MockTarget::Fx3, CellKind::Type, DeviceType::TC, 0,
     "30 30 46 46 30 30 30 41 35 34 34 33 30 30 30 30 30 30 30 30 30 31 30 30", "38 30 35 36"},
    {"fx3-eth-1e-ascii-G4-02-CC", Prof::F3eAsc, MockTarget::Fx3, CellKind::Type, DeviceType::CC, 0,
     "30 30 46 46 30 30 30 41 34 33 34 33 30 30 30 30 30 30 30 30 30 31 30 30", "38 30 35 36"},
    {"fx3-serial-1c-f1-sum-G5-04", Prof::F3cF1, MockTarget::Fx3, CellKind::Range, DeviceType::M,
     7680, "05 30 30 46 46 42 52 30 4D 37 36 38 30 30 31 33 33", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-sum-G4-02-L", Prof::F3cF1, MockTarget::Fx3, CellKind::Type, DeviceType::L, 0,
     "05 30 30 46 46 42 52 30 4C 30 30 30 30 30 31 31 44", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-sum-G4-02-W", Prof::F3cF1, MockTarget::Fx3, CellKind::Type, DeviceType::W, 0,
     "05 30 30 46 46 57 52 30 57 30 30 30 30 30 31 33 44", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-sum-G4-02-TC", Prof::F3cF1, MockTarget::Fx3, CellKind::Type, DeviceType::TC,
     0, "05 30 30 46 46 42 52 30 54 43 30 30 30 30 31 33 38", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-sum-G7-03", Prof::F3cF1, MockTarget::Fx3, CellKind::Sum, DeviceType::D, 0,
     "05 30 30 46 46 57 52 30 44 30 31 30 30 30 31 32 43", "15 30 30 46 46 30 32"},
    {"fx3-serial-1c-f1-nosum-G5-04", Prof::F3cF1n, MockTarget::Fx3, CellKind::Range, DeviceType::M,
     7680, "05 30 30 46 46 42 52 30 4D 37 36 38 30 30 31", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-nosum-G4-02-L", Prof::F3cF1n, MockTarget::Fx3, CellKind::Type, DeviceType::L,
     0, "05 30 30 46 46 42 52 30 4C 30 30 30 30 30 31", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-nosum-G4-02-W", Prof::F3cF1n, MockTarget::Fx3, CellKind::Type, DeviceType::W,
     0, "05 30 30 46 46 57 52 30 57 30 30 30 30 30 31", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f1-nosum-G4-02-TC", Prof::F3cF1n, MockTarget::Fx3, CellKind::Type,
     DeviceType::TC, 0, "05 30 30 46 46 42 52 30 54 43 30 30 30 30 31", "15 30 30 46 46 30 36"},
    {"fx3-serial-1c-f4-G5-04", Prof::F3cF4, MockTarget::Fx3, CellKind::Range, DeviceType::M, 7680,
     "05 30 30 46 46 42 52 30 4D 37 36 38 30 30 31 33 33 0D 0A", "15 30 30 46 46 30 36 0D 0A"},
    {"fx3-serial-1c-f4-G4-02-L", Prof::F3cF4, MockTarget::Fx3, CellKind::Type, DeviceType::L, 0,
     "05 30 30 46 46 42 52 30 4C 30 30 30 30 30 31 31 44 0D 0A", "15 30 30 46 46 30 36 0D 0A"},
    {"fx3-serial-1c-f4-G4-02-W", Prof::F3cF4, MockTarget::Fx3, CellKind::Type, DeviceType::W, 0,
     "05 30 30 46 46 57 52 30 57 30 30 30 30 30 31 33 44 0D 0A", "15 30 30 46 46 30 36 0D 0A"},
    {"fx3-serial-1c-f4-G4-02-TC", Prof::F3cF4, MockTarget::Fx3, CellKind::Type, DeviceType::TC, 0,
     "05 30 30 46 46 42 52 30 54 43 30 30 30 30 31 33 38 0D 0A", "15 30 30 46 46 30 36 0D 0A"},
    {"fx3-serial-1c-f4-G7-03", Prof::F3cF4, MockTarget::Fx3, CellKind::Sum, DeviceType::D, 0,
     "05 30 30 46 46 57 52 30 44 30 31 30 30 30 31 32 43 0D 0A", "15 30 30 46 46 30 32 0D 0A"},
};

// Runs the cells of the profiles for which `pick` is true, each with its own target.
template <typename Pick> void checkCells(Pick pick) {
    size_t seen = 0;
    for (const Cell& c : kCells) {
        if (!pick(c.prof)) {
            continue;
        }
        ++seen;
        const Bytes want = hexBytes(c.response);
        const Bytes got = answerOfCell(c, c.target);
        INFO(c.id);
        INFO("captured " << hexText(want) << "| mock " << hexText(got));
        CHECK(got == want);
    }
    CHECK(seen > 0);
}

// The altered-SUM variant of a request of `cfg` (the last SUM character changed).
Bytes withWrongSum(const FrameConfig& cfg, Bytes request) {
    const size_t at = cfg.format == SerialFormat::Format4 ? request.size() - 3 : request.size() - 1;
    request[at] = request[at] == '0' ? uint8_t{'1'} : uint8_t{'0'};
    return request;
}

} // namespace

TEST_CASE("MCK-16 3E Binary and ASCII: QCpu and Fx5 answer the captured out-of-range and "
          "type-not-supported codes") {
    // QCpu: 4031 for both. Fx5: C056 out of range; C05C for V and ZR; DX and DY C05C in Binary and
    // C050 in ASCII (CAP-q03ude-eth-3e-*, CAP-fx5u-eth-3e-*: G5-02, G5-03, G5-04, G4-02-x).
    checkCells([](Prof p) { return isEthernetProf(p); });
}

TEST_CASE("MCK-16 3C formats 1 to 4: QCpu answers 4031 and 7F24, Fx5 answers 7143, 7E43, 7F21 and "
          "7F24, as captured") {
    checkCells([](Prof p) { return isThreeCProf(p); });
}

TEST_CASE("MCK-16 1E Binary and ASCII: Fx3 answers 58H and 56H without an abnormal code") {
    checkCells([](Prof p) { return isOneEProf(p); });
}

TEST_CASE("MCK-16 1C formats 1 and 4: Fx3 answers 06 for range and type, and 02 for a wrong SUM") {
    checkCells([](Prof p) { return isOneCProf(p); });
}

TEST_CASE("MCK-16 3C format 3: a target code keeps the short-response SUM rule of the config") {
    // The captured QNAK 4031 has no SUM (f3ShortResponseHasSum false). With the flag on, the same
    // answer carries a SUM, spelled out by the serial builder of this directory.
    const Cell* cell = nullptr;
    for (const Cell& c : kCells) {
        if (c.prof == Prof::Q3cF3 && c.kind == CellKind::Range) {
            cell = &c;
            break;
        }
    }
    REQUIRE(cell != nullptr);
    CHECK(hexBytes(cell->response) ==
          sf::response(profileConfig(Prof::Q3cF3), sf::Kind::Nak, "4031"));

    FrameConfig cfg = profileConfig(Prof::Q3cF3);
    cfg.f3ShortResponseHasSum = true;
    MockOptions opt;
    opt.target = MockTarget::QCpu;
    MockPlc plc(cfg, opt);
    plc.setDeviceLimit(cell->type, cell->limit);
    feed(plc, hexBytes(cell->request));
    const std::vector<Bytes> all = drainAll(plc);
    REQUIRE(all.size() == 1);
    CHECK(all[0] == sf::response(cfg, sf::Kind::Nak, "4031"));
}

TEST_CASE("MCK-16 Generic is the default target and answers the MockOptions codes as before") {
    const MockOptions defaults;
    CHECK(defaults.target == MockTarget::Generic);

    const std::vector<Vector> bin3e = loadFile("3e_binary.vec");
    const std::vector<Vector> asc3e = loadFile("3e_ascii.vec");
    const std::vector<Vector> bin1e = loadFile("1e_binary.vec");
    const std::vector<Vector> asc1e = loadFile("1e_ascii.vec");

    for (bool explicitGeneric : {false, true}) {
        INFO("explicit Generic: " << explicitGeneric);
        MockOptions opt;
        if (explicitGeneric) {
            opt.target = MockTarget::Generic;
        }
        // V-3E-B-10 / V-3E-A-10 / V-1E-B-11 / V-1E-A-11: D100 x3 beyond a limit of 100.
        {
            MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
            plc.setDeviceLimit(DeviceType::D, 100);
            feed(plc, byId(bin3e, "V-3E-B-01").bytes);
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == byId(bin3e, "V-3E-B-10").bytes);
        }
        {
            MockPlc plc(FrameConfig::frame3E(DataCode::Ascii), opt);
            plc.setDeviceLimit(DeviceType::D, 100);
            feed(plc, byId(asc3e, "V-3E-A-01").bytes);
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == byId(asc3e, "V-3E-A-10").bytes);
        }
        {
            MockPlc plc(FrameConfig::frame1E(DataCode::Binary), opt);
            plc.setDeviceLimit(DeviceType::D, 100);
            feed(plc, byId(bin1e, "V-1E-B-01").bytes);
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == byId(bin1e, "V-1E-B-11").bytes);
        }
        {
            MockPlc plc(FrameConfig::frame1E(DataCode::Ascii), opt);
            plc.setDeviceLimit(DeviceType::D, 100);
            feed(plc, byId(asc1e, "V-1E-A-01").bytes);
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == byId(asc1e, "V-1E-A-11").bytes);
        }
        // 3C and 1C format 1 with SUM: a wrong SUM is NAK 7151 (V-3C1-05) and NAK 06; a device
        // beyond the limit is NAK C051 and NAK 06.
        {
            const FrameConfig cfg = FrameConfig::frame3C(SerialFormat::Format1);
            MockPlc plc(cfg, opt);
            plc.setDeviceLimit(DeviceType::D, 100);
            feed(plc, withWrongSum(cfg, sf::request(cfg, sf::readD(cfg, 0, 3))));
            feed(plc, sf::request(cfg, sf::readD(cfg, 100, 3)));
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 2);
            CHECK(got[0] == sf::response(cfg, sf::Kind::Nak, "7151"));
            CHECK(got[1] == sf::response(cfg, sf::Kind::Nak, "C051"));
        }
        {
            const FrameConfig cfg = FrameConfig::frame1C(SerialFormat::Format1);
            MockPlc plc(cfg, opt);
            plc.setDeviceLimit(DeviceType::D, 100);
            feed(plc, withWrongSum(cfg, sf::request(cfg, sf::readD(cfg, 0, 3))));
            feed(plc, sf::request(cfg, sf::readD(cfg, 100, 3)));
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 2);
            CHECK(got[0] == sf::response(cfg, sf::Kind::Nak, "06"));
            CHECK(got[1] == sf::response(cfg, sf::Kind::Nak, "06"));
        }
    }
}

TEST_CASE("MCK-16 a target replaces only the codes of its table: the other cells keep the "
          "Generic code") {
    const std::vector<Vector> bin3e = loadFile("3e_binary.vec");
    const std::vector<Vector> bin1e = loadFile("1e_binary.vec");

    // 3E on Fx3: "as Generic" in both rows (C051 for a range, C059 for a type).
    {
        MockOptions opt;
        opt.target = MockTarget::Fx3;
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
        plc.setDeviceLimit(DeviceType::D, 100);
        plc.setTypeSupported(DeviceType::M, false);
        feed(plc, byId(bin3e, "V-3E-B-01").bytes);
        feed(plc, hexBytes("50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 64 00 00 90 01 00"));
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 2);
        CHECK(got[0] == byId(bin3e, "V-3E-B-10").bytes);
        CHECK(got[1] == hexBytes("D0 00 00 FF FF 03 00 0B 00 59 C0 00 FF FF 03 00 01 04 01 00"));
    }
    // 1E on QCpu and Fx5: "as Generic": 5BH with the abnormal code 10H, and 50H for a type.
    for (MockTarget target : {MockTarget::QCpu, MockTarget::Fx5}) {
        MockOptions opt;
        opt.target = target;
        MockPlc plc(FrameConfig::frame1E(DataCode::Binary), opt);
        plc.setDeviceLimit(DeviceType::D, 100);
        plc.setTypeSupported(DeviceType::M, false);
        feed(plc, byId(bin1e, "V-1E-B-01").bytes);
        feed(plc, hexBytes("00 FF 0A 00 64 00 00 00 20 4D 01 00")); // read M100, one bit
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 2);
        CHECK(got[0] == byId(bin1e, "V-1E-B-11").bytes);
        CHECK(got[1] == hexBytes("80 50"));
    }
    // 1C on QCpu and Fx5: NAK 06 for a range, a type and a wrong SUM.
    for (MockTarget target : {MockTarget::QCpu, MockTarget::Fx5}) {
        MockOptions opt;
        opt.target = target;
        const FrameConfig cfg = FrameConfig::frame1C(SerialFormat::Format1);
        MockPlc plc(cfg, opt);
        plc.setDeviceLimit(DeviceType::D, 100);
        plc.setTypeSupported(DeviceType::M, false);
        feed(plc, sf::request(cfg, sf::readD(cfg, 100, 3)));
        feed(plc, sf::request(cfg, sf::data1c("WR", '0', "M0000", 1)));
        feed(plc, withWrongSum(cfg, sf::request(cfg, sf::readD(cfg, 0, 3))));
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 3);
        for (const Bytes& r : got) {
            CHECK(r == sf::response(cfg, sf::Kind::Nak, "06"));
        }
    }
}

TEST_CASE("MCK-16 under a target: unsupported commands, failRange and the 1E abnormal option keep "
          "their MockOptions meaning") {
    const std::vector<Vector> bin3e = loadFile("3e_binary.vec");
    // An unsupported command is in no target's table: unsupportedQna still answers it.
    for (MockTarget target : {MockTarget::QCpu, MockTarget::Fx5}) {
        MockOptions opt;
        opt.target = target;
        opt.unsupportedQna = 0xC0AA;
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
        feed(plc, byId(bin3e, "V-3E-B-13").bytes); // 0403
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        REQUIRE(got[0].size() == 20);
        CHECK(got[0][9] == 0xAA);
        CHECK(got[0][10] == 0xC0);
    }
    // failRange carries its own code, whatever the target says.
    {
        MockOptions opt;
        opt.target = MockTarget::Fx5;
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
        plc.failRange(DeviceType::D, 100, 102, 0x1234);
        feed(plc, byId(bin3e, "V-3E-B-01").bytes);
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == hexBytes("D0 00 00 FF FF 03 00 0B 00 34 12 00 FF FF 03 00 01 04 00 00"));
    }
    // Fx3 on 1E: end code 58H is not 5BH, so no abnormal byte is sent, whatever the option says.
    {
        MockOptions opt;
        opt.target = MockTarget::Fx3;
        opt.outOfRange1eAbnormal = 0x11;
        MockPlc plc(FrameConfig::frame1E(DataCode::Binary), opt);
        plc.setDeviceLimit(DeviceType::D, 100);
        feed(plc, byId(loadFile("1e_binary.vec"), "V-1E-B-01").bytes);
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == hexBytes("81 58"));
    }
    // The codes of the table win over the Generic fields for the cells it lists.
    {
        MockOptions opt;
        opt.target = MockTarget::Fx5;
        opt.outOfRangeQna = 0xC0AA;
        MockPlc plc(FrameConfig::frame3E(DataCode::Binary), opt);
        plc.setDeviceLimit(DeviceType::D, 100);
        feed(plc, byId(bin3e, "V-3E-B-01").bytes);
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == hexBytes("D0 00 00 FF FF 03 00 0B 00 56 C0 00 FF FF 03 00 01 04 00 00"));
    }
}

TEST_CASE(
    "MCK-16 Fx3 on 1C: the range and type codes stay the MockOptions fields, not a fixed 06") {
    MockOptions opt;
    opt.target = MockTarget::Fx3;
    opt.outOfRange1c = 0x07;
    opt.unsupported1c = 0x08;
    const FrameConfig cfg = FrameConfig::frame1C(SerialFormat::Format1);
    MockPlc plc(cfg, opt);
    plc.setDeviceLimit(DeviceType::D, 100);
    plc.setTypeSupported(DeviceType::M, false);
    feed(plc, sf::request(cfg, sf::readD(cfg, 100, 3)));
    feed(plc, sf::request(cfg, sf::data1c("WR", '0', "M0000", 1)));
    const std::vector<Bytes> got = drainAll(plc);
    REQUIRE(got.size() == 2);
    CHECK(got[0] == sf::response(cfg, sf::Kind::Nak, "07"));
    CHECK(got[1] == sf::response(cfg, sf::Kind::Nak, "08"));
}
