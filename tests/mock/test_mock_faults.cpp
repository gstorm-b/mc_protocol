// MCK-09 (unsupported commands) and MCK-11 (mute, muteNext and every Corruption mode) on 3E and
// 1E, Binary and ASCII, and on 3C and 1C in formats 1 to 4, asserted byte for byte against a clean
// response; plus clearFaults() and the serial parts of MCK-12. The expected bytes are derived here
// from the documented rule of each mode (mc/mock/mock_plc.h) applied to a clean response, never by
// calling the mock a second time for the damage itself.
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
using mc::SerialFormat;
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
