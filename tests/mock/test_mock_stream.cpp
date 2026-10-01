// MCK-05, MCK-06, MCK-07 and MCK-08 for the serial frames: how MockPlc receives a byte stream of
// 3C and 1C requests in formats 1 to 4 (spec section 6.3 seen from the PLC; the Ethernet parts of
// MCK-05 and MCK-07 are in test_mock_vectors.cpp). The frames are built by hand from the tables of
// spec sections 5.4-5.6 and 2.5 (serial_frames.h), never by the client encoder; the first case
// pins those builders to the golden vectors, so a mistake in a builder cannot hide a mistake in the
// mock.
#include "doctest/doctest.h"

#include "common/vectors.h"
#include "serial_frames.h"

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/mock/mock_plc.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using mc::ByteView;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::FrameType;
using mc::MockOptions;
using mc::MockPlc;
using mc::Op;
using mc::SerialFormat;
using mc::test::loadVectors;
using mc::test::Vector;

namespace sf = mc::test::serial;

namespace {

using Bytes = sf::Bytes;

struct Cell {
    const char* name;
    FrameConfig cfg;
};

// The eight serial combinations of the v1 matrix.
const std::vector<Cell>& cells() {
    static const std::vector<Cell> list = {
        {"3C F1", FrameConfig::frame3C(SerialFormat::Format1)},
        {"3C F2", FrameConfig::frame3C(SerialFormat::Format2)},
        {"3C F3", FrameConfig::frame3C(SerialFormat::Format3)},
        {"3C F4", FrameConfig::frame3C(SerialFormat::Format4)},
        {"1C F1", FrameConfig::frame1C(SerialFormat::Format1)},
        {"1C F2", FrameConfig::frame1C(SerialFormat::Format2)},
        {"1C F3", FrameConfig::frame1C(SerialFormat::Format3)},
        {"1C F4", FrameConfig::frame1C(SerialFormat::Format4)},
    };
    return list;
}

bool format3(const FrameConfig& cfg) { return cfg.format == SerialFormat::Format3; }

std::vector<Vector> loadFile(const std::string& name) {
    std::vector<Vector> vectors =
        loadVectors(std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors" / name);
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

void feed(MockPlc& plc, const Bytes& bytes) { plc.bytesIn(ByteView{bytes.data(), bytes.size()}); }

void feedInChunks(MockPlc& plc, const Bytes& bytes, size_t chunk) {
    for (size_t pos = 0; pos < bytes.size(); pos += chunk) {
        plc.bytesIn(ByteView{bytes.data() + pos, std::min(chunk, bytes.size() - pos)});
    }
}

std::vector<Bytes> drainAll(MockPlc& plc) {
    std::vector<Bytes> all;
    ByteView view;
    while (plc.nextResponse(view)) {
        all.emplace_back(view.data, view.data + view.size);
    }
    return all;
}

const char* const kWords = "199512021130"; // D100..D102 of the golden vectors (G1)

MockPlc seeded(const FrameConfig& cfg, const MockOptions& opt = {}) {
    MockPlc plc(cfg, opt);
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    return plc;
}

Bytes readRequest(const FrameConfig& cfg, const sf::Route& r = {}) {
    return sf::request(cfg, sf::readD(cfg, 100, 3), r);
}

Bytes writeRequest(const FrameConfig& cfg, const sf::Route& r = {}) {
    return sf::request(cfg, sf::writeD(cfg, 100, "1234"), r);
}

Bytes cleanRead(const FrameConfig& cfg, const sf::Route& r = {}) {
    return sf::response(cfg, sf::Kind::Data, kWords, r);
}

Bytes concat(Bytes a, const Bytes& b) {
    sf::append(a, b);
    return a;
}

// Where the two SUM characters of a request frame are: the end, or before CR LF in format 4.
size_t sumOffset(const FrameConfig& cfg, const Bytes& frame) {
    return frame.size() - (cfg.format == SerialFormat::Format4 ? 4 : 2);
}

// What a client sends to reset the receiver: EOT, and CR LF after it in format 4.
Bytes eotFrame(const FrameConfig& cfg) {
    Bytes eot = {sf::kEot};
    if (cfg.format == SerialFormat::Format4) {
        eot.push_back(sf::kCr);
        eot.push_back(sf::kLf);
    }
    return eot;
}

} // namespace

TEST_CASE("MCK-01 the hand-built serial frames of these tests equal the golden vectors") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        const bool threeC = sf::isThreeC(cell.cfg);
        const std::string n = std::to_string(static_cast<int>(cell.cfg.format));
        const std::string prefix = std::string(threeC ? "V-3C" : "V-1C") + n + "-";
        const std::vector<Vector> vectors =
            loadFile(std::string(threeC ? "3c_f" : "1c_f") + n + ".vec");
        CHECK(readRequest(cell.cfg) == byId(vectors, prefix + "01").bytes);
        CHECK(cleanRead(cell.cfg) == byId(vectors, prefix + "02").bytes);
        CHECK(sf::response(cell.cfg, sf::Kind::Ack, "") ==
              byId(vectors, prefix + (threeC ? "04" : "07")).bytes);
        CHECK(sf::response(cell.cfg, sf::Kind::Nak, threeC ? "7151" : "06") ==
              byId(vectors, prefix + (threeC ? "05" : "08")).bytes);

        // The same frames with the sum check off (3C1-NOSUM-REQ ...).
        FrameConfig noSum = cell.cfg;
        noSum.sumCheck = false;
        const std::string noSumPrefix = std::string(threeC ? "3C" : "1C") + n + "-NOSUM-";
        CHECK(readRequest(noSum) == byId(vectors, noSumPrefix + "REQ").bytes);
        CHECK(cleanRead(noSum) == byId(vectors, noSumPrefix + "RSP").bytes);
    }
}

TEST_CASE("MCK-05 serial: a request split anywhere is not executed before it is complete") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        const Bytes frame = writeRequest(cell.cfg);
        for (size_t cut = 1; cut < frame.size(); ++cut) {
            INFO("cut after byte ", cut);
            MockPlc plc(cell.cfg);
            plc.bytesIn(ByteView{frame.data(), cut});
            CHECK(plc.requests().empty());
            CHECK(plc.word(Device{DeviceType::D, 100}) == 0);
            CHECK(drainAll(plc).empty());
            plc.bytesIn(ByteView{frame.data() + cut, frame.size() - cut});
            CHECK(plc.requests().size() == 1);
            CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1234);
            CHECK(drainAll(plc).size() == 1);
        }
    }
}

TEST_CASE("MCK-05 serial: responses come out in request order, one per request") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        plc.setWords(Device{DeviceType::D, 100}, {0x1111, 0x2222});
        Bytes stream = sf::request(cell.cfg, sf::readD(cell.cfg, 100, 1));
        sf::append(stream, sf::request(cell.cfg, sf::readD(cell.cfg, 101, 1)));
        sf::append(stream, sf::request(cell.cfg, sf::readD(cell.cfg, 100, 2)));
        feedInChunks(plc, stream, 7);
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 3);
        CHECK(got[0] == sf::response(cell.cfg, sf::Kind::Data, "1111"));
        CHECK(got[1] == sf::response(cell.cfg, sf::Kind::Data, "2222"));
        CHECK(got[2] == sf::response(cell.cfg, sf::Kind::Data, "11112222"));
        REQUIRE(plc.requests().size() == 3);
        CHECK(plc.requests()[2].count == 2);
    }
}

TEST_CASE("MCK-06 serial: bytes before ENQ (STX in format 3) are skipped without a trace") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        // Not a start byte for this format: STX before an ENQ, ENQ before an STX; and no EOT.
        const Bytes junk = {0x55,    'x',     0x00,
                            0xFF,    0x20,    0x7E,
                            sf::kCr, sf::kLf, format3(cell.cfg) ? sf::kEnq : sf::kStx,
                            'A'};
        for (size_t chunk : {size_t{256}, size_t{1}}) {
            INFO("chunk ", chunk);
            MockPlc plc = seeded(cell.cfg);
            Bytes stream = junk;
            sf::append(stream, readRequest(cell.cfg));
            sf::append(stream, junk);
            sf::append(stream, readRequest(cell.cfg));
            feedInChunks(plc, stream, chunk);
            REQUIRE(plc.requests().size() == 2); // the skipped bytes leave no record
            CHECK(plc.requests()[0].answered);
            CHECK(plc.requests()[1].answered);
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 2);
            CHECK(got[0] == cleanRead(cell.cfg));
            CHECK(got[1] == cleanRead(cell.cfg));
            CHECK(plc.eotCount() == 0);
        }

        MockPlc alone = seeded(cell.cfg);
        feed(alone, junk);
        CHECK(alone.requests().empty());
        CHECK(drainAll(alone).empty());
    }
}

TEST_CASE("MCK-06 serial: EOT mid-request drops the partial request and is counted") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        const Bytes frame = readRequest(cell.cfg);
        const Bytes eot = eotFrame(cell.cfg);
        for (size_t cut : {size_t{1}, size_t{2}, frame.size() / 2, frame.size() - 1}) {
            INFO("cut after byte ", cut);
            const Bytes partial(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(cut));
            for (size_t chunk : {size_t{256}, size_t{1}}) {
                INFO("chunk ", chunk);
                MockPlc plc = seeded(cell.cfg);
                feedInChunks(plc, partial, chunk);
                feedInChunks(plc, eot, chunk);
                CHECK(plc.eotCount() == 1);
                CHECK(plc.requests().empty());
                CHECK(drainAll(plc).empty()); // C24 sends no response to EOT

                // The receiver is back in the command wait state: a whole request is answered.
                feedInChunks(plc, frame, chunk);
                CHECK(plc.eotCount() == 1);
                REQUIRE(plc.requests().size() == 1);
                const std::vector<Bytes> got = drainAll(plc);
                REQUIRE(got.size() == 1);
                CHECK(got[0] == cleanRead(cell.cfg));
            }
        }

        // The EOT and the next request in one buffer; EOT with nothing pending; two EOTs.
        MockPlc plc = seeded(cell.cfg);
        feed(plc, eot);
        CHECK(plc.eotCount() == 1);
        feed(plc, concat(concat(eot, eot), frame));
        CHECK(plc.eotCount() == 3);
        CHECK(plc.requests().size() == 1);
        CHECK(drainAll(plc).size() == 1);
    }
}

TEST_CASE("MCK-06 serial: an EOT is counted while muted and does not use up a muteNext") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc = seeded(cell.cfg);
        plc.mute(true);
        feed(plc, readRequest(cell.cfg));
        feed(plc, eotFrame(cell.cfg));
        CHECK(plc.eotCount() == 1);
        CHECK(drainAll(plc).empty());

        plc.mute(false);
        plc.muteNext(1);
        feed(plc, eotFrame(cell.cfg));
        feed(plc, readRequest(cell.cfg)); // this is the request that muteNext swallows
        CHECK(plc.eotCount() == 2);
        CHECK(drainAll(plc).empty());
        feed(plc, readRequest(cell.cfg));
        CHECK(drainAll(plc).size() == 1);
        REQUIRE(plc.requests().size() == 3);
        CHECK_FALSE(plc.requests()[0].answered);
        CHECK_FALSE(plc.requests()[1].answered);
        CHECK(plc.requests()[2].answered);
    }
}

TEST_CASE("MCK-06 serial format 4: a request without its LF stays incomplete") {
    for (const Cell& cell : cells()) {
        if (cell.cfg.format != SerialFormat::Format4) {
            continue;
        }
        INFO(cell.name);
        const Bytes frame = readRequest(cell.cfg);
        REQUIRE(frame.size() > 4);
        MockPlc plc = seeded(cell.cfg);
        plc.bytesIn(ByteView{frame.data(), frame.size() - 2});     // up to the SUM
        plc.bytesIn(ByteView{frame.data() + frame.size() - 2, 1}); // CR
        CHECK(plc.requests().empty());
        CHECK(drainAll(plc).empty());
        plc.bytesIn(ByteView{frame.data() + frame.size() - 1, 1}); // LF
        CHECK(plc.requests().size() == 1);
        CHECK(drainAll(plc).size() == 1);

        // A CR that is not followed by LF is not the end of the request: it is dropped like junk,
        // and the next request is answered.
        Bytes broken(frame.begin(), frame.end() - 1);
        broken.push_back('X');
        feed(plc, broken);
        feed(plc, frame);
        CHECK(plc.requests().size() == 2);
        CHECK(drainAll(plc).size() == 1);
    }
}

// A request that cannot be framed in formats 1, 2 and 4 (an unknown command, an access route that
// is not hexadecimal, a control character inside) is skipped like junk and does not hide the next
// one.
TEST_CASE(
    "MCK-06 serial: a request that cannot be framed is skipped and the next one is answered") {
    for (const Cell& cell : cells()) {
        if (format3(cell.cfg)) {
            continue;
        }
        INFO(cell.name);
        const bool threeC = sf::isThreeC(cell.cfg);
        const Bytes good = readRequest(cell.cfg);
        const size_t routeAt = cell.cfg.format == SerialFormat::Format2 ? 3 : 1;

        std::vector<std::pair<const char*, Bytes>> bad;
        bad.emplace_back("unknown command",
                         sf::request(cell.cfg, threeC ? "0406000000000000" : "ZZ0D010003"));
        bad.emplace_back("non-hex station", [&] {
            Bytes b = good;
            b[routeAt + (threeC ? 2 : 0)] = 'G';
            return b;
        }());
        bad.emplace_back("control code inside", [&] {
            Bytes b = good;
            b[b.size() / 2] = sf::kCr;
            return b;
        }());
        bad.emplace_back(
            "another request started inside",
            Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 2)));
        if (threeC) {
            bad.emplace_back("frame ID of a 4C frame", [&] {
                Bytes b = good;
                b[routeAt + 1] = '8'; // "F9" -> "F8"
                return b;
            }());
            bad.emplace_back("unknown subcommand",
                             sf::request(cell.cfg, sf::data3c(0x0401, 0x0004, "D*000100", 3)));
        } else {
            bad.emplace_back("non-hex message wait",
                             sf::request(cell.cfg, sf::data1c("WR", 'Z', "D0100", 3)));
        }

        for (const auto& [name, frame] : bad) {
            INFO(name);
            for (size_t chunk : {size_t{256}, size_t{1}}) {
                INFO("chunk ", chunk);
                MockPlc plc = seeded(cell.cfg);
                feedInChunks(plc, concat(frame, good), chunk);
                REQUIRE(plc.requests().size() == 1);
                CHECK(plc.requests()[0].answered);
                const std::vector<Bytes> got = drainAll(plc);
                REQUIRE(got.size() == 1);
                CHECK(got[0] == cleanRead(cell.cfg));
                CHECK(plc.eotCount() == 0);
            }
        }
    }
}

TEST_CASE("MCK-06 serial format 3: a second STX or a control code inside a request restarts it") {
    for (const Cell& cell : cells()) {
        if (!format3(cell.cfg)) {
            continue;
        }
        INFO(cell.name);
        const Bytes good = readRequest(cell.cfg);
        // The start of a request, then the whole request: the STX of the second one is a new start.
        // Cut after the ETX too: the SUM characters of the first one are then the next STX.
        for (size_t cut : {size_t{3}, good.size() - 3, good.size() - 2, good.size() - 1}) {
            INFO("cut after byte ", cut);
            MockPlc plc = seeded(cell.cfg);
            Bytes stream(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
            sf::append(stream, good);
            feed(plc, stream);
            REQUIRE(plc.requests().size() == 1);
            CHECK(plc.requests()[0].answered);
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 1);
            CHECK(got[0] == cleanRead(cell.cfg));
        }

        Bytes inside = good;
        inside[5] = sf::kCr;
        MockPlc plc = seeded(cell.cfg);
        feed(plc, concat(inside, good));
        CHECK(plc.requests().size() == 1);
        CHECK(drainAll(plc).size() == 1);

        // A frame ID that is not "F9" or a route that is not hexadecimal is not for this PLC.
        Bytes foreign = good;
        foreign[sf::isThreeC(cell.cfg) ? 2 : 1] = 'G';
        MockPlc other = seeded(cell.cfg);
        feed(other, concat(foreign, good));
        CHECK(other.requests().size() == 1);
        CHECK(drainAll(other).size() == 1);
    }
}

TEST_CASE("MCK-07 serial: a request for another station gets no response") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        FrameConfig cfg = cell.cfg;
        cfg.stationNo = 0x05;
        sf::Route mine;
        mine.station = 0x05;
        sf::Route other; // station 00, as in the golden vectors
        const Bytes writeOther = writeRequest(cfg, other);

        MockPlc plc = seeded(cfg);
        feed(plc, readRequest(cfg, other));
        feed(plc, writeOther);
        CHECK(drainAll(plc).empty());
        REQUIRE(plc.requests().size() == 2);
        for (const auto& rec : plc.requests()) {
            CHECK_FALSE(rec.answered);
            CHECK(rec.answeredWith.ok());
        }
        CHECK(plc.requests()[0].op == Op::ReadWords); // still decoded
        CHECK(plc.requests()[0].head == Device{DeviceType::D, 100});
        CHECK(plc.requests()[0].count == 3);
        CHECK(plc.requests()[1].op == Op::WriteWords);
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1995); // and the write was not executed

        // Its own station is answered, and the response carries it.
        feed(plc, readRequest(cfg, mine));
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == cleanRead(cfg, mine));
        CHECK(plc.requests().back().answered);

        // The wrong station has a wrong SUM too: still no response.
        Bytes badSum = readRequest(cfg, other);
        const size_t sumAt = sumOffset(cfg, badSum);
        badSum[sumAt] = badSum[sumAt] == '0' ? '1' : '0';
        feed(plc, badSum);
        CHECK(drainAll(plc).empty());
    }
}

TEST_CASE("MCK-07 serial: a request for another station does not use up a muteNext") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        FrameConfig cfg = cell.cfg;
        cfg.stationNo = 0x05;
        sf::Route mine;
        mine.station = 0x05;
        MockPlc plc = seeded(cfg);
        plc.muteNext(1);
        feed(plc, readRequest(cfg));       // station 00: ignored, not counted
        feed(plc, readRequest(cfg, mine)); // swallowed
        feed(plc, readRequest(cfg, mine)); // answered
        CHECK(drainAll(plc).size() == 1);
        REQUIRE(plc.requests().size() == 3);
        CHECK_FALSE(plc.requests()[0].answered);
        CHECK_FALSE(plc.requests()[1].answered);
        CHECK(plc.requests()[2].answered);
    }
}

TEST_CASE("MCK-07 serial: the access route and the block number of a request are echoed") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        // Network 07, PC 03, self-station 0E, block 3A: only the station has to match.
        sf::Route route;
        route.network = 0x07;
        route.pc = 0x03;
        route.self = 0x0E;
        route.block = 0x3A;
        MockPlc plc = seeded(cell.cfg);
        feed(plc, readRequest(cell.cfg, route));
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == cleanRead(cell.cfg, route));
        CHECK(got[0] != cleanRead(cell.cfg)); // the default route is not what comes back
    }
}

TEST_CASE("MCK-07 serial: hexadecimal digits of the route may be lower case, the response is upper "
          "case") {
    for (bool threeC : {true, false}) {
        FrameConfig cfg = threeC ? FrameConfig::frame3C() : FrameConfig::frame1C();
        cfg.stationNo = 0x0A;
        MockPlc plc = seeded(cfg);
        // ENQ P RD SUM with the station written "0a" and the PC "fe", the SUM computed over it.
        Bytes frame = {sf::kEnq};
        sf::append(frame, threeC ? "F90a00fe00" : "0afe");
        sf::append(frame, sf::readD(cfg, 100, 3));
        sf::append(frame, sf::hex2(sf::byteSum(frame, 1, frame.size())));
        feed(plc, frame);
        sf::Route route;
        route.station = 0x0A;
        route.pc = 0xFE;
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == cleanRead(cfg, route));
    }
}

TEST_CASE("MCK-08 serial: a request with a wrong SUM is answered with NAK and the sumError code, "
          "and not executed") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        const bool threeC = sf::isThreeC(cell.cfg);
        Bytes bad = writeRequest(cell.cfg);
        const size_t sumAt = sumOffset(cell.cfg, bad);
        bad[sumAt] = bad[sumAt] == '0' ? '1' : '0';

        MockPlc plc = seeded(cell.cfg);
        feed(plc, bad);
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == sf::response(cell.cfg, sf::Kind::Nak, threeC ? "7151" : "06"));
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1995); // not executed
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests()[0].answered);
        CHECK(plc.requests()[0].answeredWith.category == mc::ErrorCategory::Plc);
        CHECK(plc.requests()[0].answeredWith.plcCode == (threeC ? 0x7151 : 0x06));
        CHECK(plc.requests()[0].op == Op::WriteWords); // the request itself was understood

        // The error code is a MockOptions value.
        MockOptions opt;
        opt.sumErrorQna = 0x7152;
        opt.sumError1c = 0x07;
        MockPlc custom = seeded(cell.cfg, opt);
        feed(custom, bad);
        const std::vector<Bytes> gotCustom = drainAll(custom);
        REQUIRE(gotCustom.size() == 1);
        CHECK(gotCustom[0] == sf::response(cell.cfg, sf::Kind::Nak, threeC ? "7152" : "07"));

        // The next, correct request is answered normally.
        feed(plc, readRequest(cell.cfg));
        CHECK(drainAll(plc) == std::vector<Bytes>{cleanRead(cell.cfg)});
    }
}

TEST_CASE("MCK-08 serial: SUM digits may be lower case, anything else that is not hexadecimal is a "
          "wrong SUM") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        // Find a request whose SUM holds a letter, and write it in lower case.
        bool found = false;
        for (unsigned number = 100; number < 200 && !found; ++number) {
            Bytes frame = sf::request(cell.cfg, sf::readD(cell.cfg, number, 1));
            const size_t at = sumOffset(cell.cfg, frame);
            for (size_t i = at; i < at + 2; ++i) {
                if (frame[i] >= 'A' && frame[i] <= 'F') {
                    frame[i] = static_cast<uint8_t>(frame[i] + ('a' - 'A'));
                    found = true;
                }
            }
            if (found) {
                MockPlc plc = seeded(cell.cfg);
                feed(plc, frame);
                REQUIRE(plc.requests().size() == 1);
                CHECK(plc.requests()[0].answeredWith.ok());
            }
        }
        CHECK(found);

        Bytes junk = readRequest(cell.cfg);
        const size_t sumAt = sumOffset(cell.cfg, junk);
        junk[sumAt] = 'Z';
        junk[sumAt + 1] = '!';
        MockPlc plc = seeded(cell.cfg);
        feed(plc, junk);
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests()[0].answeredWith.plcCode == (sf::isThreeC(cell.cfg) ? 0x7151 : 0x06));
    }
}

TEST_CASE("MCK-08 serial: with sumCheck off no SUM is expected or produced") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        FrameConfig cfg = cell.cfg;
        cfg.sumCheck = false;
        MockPlc plc = seeded(cfg);
        feed(plc, readRequest(cfg));
        feed(plc, writeRequest(cfg));
        const std::vector<Bytes> got = drainAll(plc);
        REQUIRE(got.size() == 2);
        CHECK(got[0] == cleanRead(cfg));
        CHECK(got[1] == sf::response(cfg, sf::Kind::Ack, ""));
        CHECK(plc.word(Device{DeviceType::D, 100}) == 0x1234);
        // The data response ends in ETX (and CR LF in format 4): nothing between.
        const size_t etx =
            static_cast<size_t>(std::find(got[0].begin(), got[0].end(), sf::kEtx) - got[0].begin());
        CHECK(got[0].size() == etx + 1 + (cfg.format == SerialFormat::Format4 ? 2 : 0));

        // Even the short responses of format 3 carry none, whatever f3ShortResponseHasSum says.
        cfg.f3ShortResponseHasSum = true;
        MockPlc setting = seeded(cfg);
        setting.failRange(DeviceType::D, 100, 100, 0x7151);
        feed(setting, writeRequest(cfg));
        const std::vector<Bytes> gotShort = drainAll(setting);
        REQUIRE(gotShort.size() == 1);
        CHECK(gotShort[0] == sf::response(cfg, sf::Kind::Nak, sf::isThreeC(cfg) ? "7151" : "51"));
    }
}

TEST_CASE("MCK-08 serial format 3: the short responses carry a SUM only with "
          "f3ShortResponseHasSum") {
    for (bool threeC : {true, false}) {
        for (bool withSum : {false, true}) {
            INFO((threeC ? "3C" : "1C"), withSum ? " with the setting" : " as printed");
            FrameConfig cfg = threeC ? FrameConfig::frame3C(SerialFormat::Format3)
                                     : FrameConfig::frame1C(SerialFormat::Format3);
            cfg.f3ShortResponseHasSum = withSum;
            MockPlc plc = seeded(cfg);
            plc.failRange(DeviceType::D, 200, 200, 0x7151);
            feed(plc, writeRequest(cfg));                              // ACK
            feed(plc, sf::request(cfg, sf::writeD(cfg, 200, "0001"))); // NAK
            feed(plc, readRequest(cfg));                               // data: always a SUM
            const std::vector<Bytes> got = drainAll(plc);
            REQUIRE(got.size() == 3);
            CHECK(got[0] == sf::response(cfg, sf::Kind::Ack, ""));
            CHECK(got[1] == sf::response(cfg, sf::Kind::Nak, threeC ? "7151" : "51"));
            CHECK(got[2] == sf::response(cfg, sf::Kind::Data, "123412021130")); // D100 was written
            // The SUM is the last two characters of the short responses, after the ETX.
            CHECK((got[0].back() == sf::kEtx) == !withSum);
            CHECK((got[1].back() == sf::kEtx) == !withSum);
            CHECK(got[2].back() != sf::kEtx);
        }
    }
}
