// MCK-09 (3E: unsupported commands) and the Ethernet cases of MCK-11 (mute, muteNext and every
// Corruption mode) on 3E Binary and 3E ASCII, asserted byte for byte against a clean response;
// plus clearFaults(). The expected bytes are derived here from the documented rule of each mode
// (mc/mock/mock_plc.h) applied to a clean response, never by calling the mock a second time for
// the damage itself.
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
using mc::Corruption;
using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::MockOptions;
using mc::MockPlc;
using mc::test::loadVectors;
using mc::test::Vector;

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

// One wire representation under test: its code, a request for D100 x 3 words (the vector of the
// spec's example G1), and the same request with a route that carries hex digits (0F, FE, 1F).
struct Wire {
    const char* name;
    DataCode code;
    Bytes read;
    Bytes readOtherRoute;
};

std::vector<Wire> wires() {
    const Bytes binRead = byId(loadFile("3e_binary.vec"), "V-3E-B-01").bytes;
    const Bytes ascRead = byId(loadFile("3e_ascii.vec"), "V-3E-A-01").bytes;

    Bytes binOther = binRead;
    const Bytes binRoute = {0x0F, 0xFE, 0xFF, 0x03, 0x1F};
    std::copy(binRoute.begin(), binRoute.end(), binOther.begin() + 2);

    Bytes ascOther = ascRead;
    const std::string ascRoute = "0FFE03FF1F";
    std::copy(ascRoute.begin(), ascRoute.end(), ascOther.begin() + 4);

    return {
        {"3E Binary", DataCode::Binary, binRead, binOther},
        {"3E ASCII", DataCode::Ascii, ascRead, ascOther},
    };
}

MockPlc seeded(const Wire& w) {
    MockPlc plc(FrameConfig::frame3E(w.code));
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

// The documented damage of `mode` on a clean 3E response, written out by hand.
Bytes expectedDamage(Corruption mode, const Wire& w, const Bytes& clean) {
    const bool ascii = w.code == DataCode::Ascii;
    Bytes out = clean;
    switch (mode) {
    case Corruption::WrongSubheader:
        out[0] = ascii ? uint8_t{'E'} : static_cast<uint8_t>(clean[0] ^ 0x01);
        return out;
    case Corruption::WrongRoute: {
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

TEST_CASE("MCK-11 3E: mute swallows every request until it is turned off") {
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

TEST_CASE("MCK-11 3E: a swallowed request is not executed") {
    for (const Wire& w : wires()) {
        INFO(w.name);
        MockPlc plc(FrameConfig::frame3E(w.code));
        const Bytes write =
            w.code == DataCode::Ascii
                ? asciiRequest("14010000" "D*000100" "0001" "1234")
                : binaryRequest(0x1401, 0x0000,
                                {0x64, 0x00, 0x00, 0xA8, 0x01, 0x00, 0x34, 0x12});
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

TEST_CASE("MCK-11 3E: muteNext(n) swallows exactly n requests, then answers cleanly") {
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

TEST_CASE("MCK-11 3E: each Corruption mode damages exactly n responses as documented") {
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

TEST_CASE("MCK-11 3E: error responses can be corrupted too") {
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

TEST_CASE("MCK-11 3E: corruptNext calls queue in order, n = 0 does nothing, and a swallowed "
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

TEST_CASE("MCK-11 3E: clearFaults restores clean answers") {
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
