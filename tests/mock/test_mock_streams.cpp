// MCK-13: input streams of MockPlc (spec SPEC-mock-plc.md "Streams", amended 2026-10-04). Every
// frame family is covered: the Ethernet families through the golden request and response vectors
// (V-3E-*-01..04, V-1E-*-01..04), the serial families through the hand-built frames of
// serial_frames.h (pinned to the golden vectors by MCK-01 in test_mock_stream.cpp).
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
using mc::MockPlc;
using mc::MockStreamId;
using mc::Op;
using mc::SerialFormat;
using mc::test::loadVectors;
using mc::test::Vector;

namespace sf = mc::test::serial;

namespace {

using Bytes = sf::Bytes;

// What one client of a cell sends, and what the mock must answer it.
struct Exchange {
    Bytes request;
    Bytes response;
    Op op;
};

struct Cell {
    std::string name;
    FrameConfig cfg;
    Exchange first;  // client A
    Exchange second; // client B: a different request, so a mix-up cannot go unnoticed
};

const Vector& byId(const std::vector<Vector>& vectors, const std::string& id) {
    for (const Vector& v : vectors) {
        if (v.id == id) {
            return v;
        }
    }
    FAIL("no vector with id ", id);
    return vectors.front();
}

// The memory the golden responses read: D100..D102 and M103, M106, M107 (V-*-02 and V-*-04).
void seed(MockPlc& plc) {
    plc.setWords(Device{DeviceType::D, 100}, {0x1995, 0x1202, 0x1130});
    for (uint32_t n : {103u, 106u, 107u}) {
        plc.setBit(Device{DeviceType::M, n}, true);
    }
}

void addEthernetCell(std::vector<Cell>& cells, const char* file, const char* tag, bool oneE,
                     mc::DataCode code) {
    const std::vector<Vector> v =
        loadVectors(std::filesystem::path(MC_TESTS_SOURCE_DIR) / "vectors" / file);
    REQUIRE_FALSE(v.empty());
    const std::string t = tag;
    Cell cell;
    cell.name = std::string(oneE ? "1E " : "3E ") + (code == mc::DataCode::Ascii ? "ASCII" : "Binary");
    cell.cfg = oneE ? FrameConfig::frame1E(code) : FrameConfig::frame3E(code);
    cell.first = {byId(v, t + "01").bytes, byId(v, t + "02").bytes, Op::ReadWords};
    cell.second = {byId(v, t + "03").bytes, byId(v, t + "04").bytes, Op::ReadBits};
    cells.push_back(cell);
}

const std::vector<Cell>& cells() {
    static const std::vector<Cell> list = [] {
        std::vector<Cell> all;
        addEthernetCell(all, "3e_binary.vec", "V-3E-B-", false, mc::DataCode::Binary);
        addEthernetCell(all, "3e_ascii.vec", "V-3E-A-", false, mc::DataCode::Ascii);
        addEthernetCell(all, "1e_binary.vec", "V-1E-B-", true, mc::DataCode::Binary);
        addEthernetCell(all, "1e_ascii.vec", "V-1E-A-", true, mc::DataCode::Ascii);
        struct Serial {
            const char* name;
            FrameConfig cfg;
        };
        const Serial serial[] = {
            {"3C F1", FrameConfig::frame3C(SerialFormat::Format1)},
            {"3C F2", FrameConfig::frame3C(SerialFormat::Format2)},
            {"3C F3", FrameConfig::frame3C(SerialFormat::Format3)},
            {"3C F4", FrameConfig::frame3C(SerialFormat::Format4)},
            {"1C F1", FrameConfig::frame1C(SerialFormat::Format1)},
            {"1C F2", FrameConfig::frame1C(SerialFormat::Format2)},
            {"1C F3", FrameConfig::frame1C(SerialFormat::Format3)},
            {"1C F4", FrameConfig::frame1C(SerialFormat::Format4)},
        };
        for (const Serial& s : serial) {
            Cell cell;
            cell.name = s.name;
            cell.cfg = s.cfg;
            cell.first = {sf::request(s.cfg, sf::readD(s.cfg, 100, 3)),
                          sf::response(s.cfg, sf::Kind::Data, "199512021130"), Op::ReadWords};
            cell.second = {sf::request(s.cfg, sf::writeD(s.cfg, 200, "1234")),
                           sf::response(s.cfg, sf::Kind::Ack, ""), Op::WriteWords};
            all.push_back(cell);
        }
        return all;
    }();
    return list;
}

void feed(MockPlc& plc, MockStreamId id, const Bytes& bytes, size_t from = 0,
          size_t to = static_cast<size_t>(-1)) {
    to = std::min(to, bytes.size());
    plc.bytesIn(id, ByteView{bytes.data() + from, to - from});
}

std::vector<Bytes> drain(MockPlc& plc, MockStreamId id) {
    std::vector<Bytes> all;
    ByteView view;
    while (plc.nextResponse(id, view)) {
        all.emplace_back(view.data, view.data + view.size);
    }
    return all;
}

} // namespace

TEST_CASE("MCK-13 two streams fed one byte each in turn both decode and answer on their own") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();
        CHECK(a != 0);
        CHECK(b != 0);
        CHECK(a != b);

        const Bytes& ra = cell.first.request;
        const Bytes& rb = cell.second.request;
        const size_t longest = std::max(ra.size(), rb.size());
        for (size_t i = 0; i < longest; ++i) {
            if (i < ra.size()) {
                feed(plc, a, ra, i, i + 1);
            }
            if (i < rb.size()) {
                feed(plc, b, rb, i, i + 1);
            }
        }

        const std::vector<Bytes> gotA = drain(plc, a);
        const std::vector<Bytes> gotB = drain(plc, b);
        REQUIRE(gotA.size() == 1);
        REQUIRE(gotB.size() == 1);
        CHECK(gotA[0] == cell.first.response);
        CHECK(gotB[0] == cell.second.response);
        CHECK(drain(plc, 0).empty());

        REQUIRE(plc.requests().size() == 2);
        for (const auto& rec : plc.requests()) {
            CHECK(rec.answered);
            CHECK(rec.answeredWith.ok());
        }
        // Each request is logged with the stream it came from, in the order they completed.
        const bool aFirst = ra.size() <= rb.size();
        CHECK(plc.requests()[0].stream == (aFirst ? a : b));
        CHECK(plc.requests()[1].stream == (aFirst ? b : a));
        CHECK(plc.requests()[aFirst ? 0 : 1].op == cell.first.op);
        CHECK(plc.requests()[aFirst ? 1 : 0].op == cell.second.op);
    }
}

TEST_CASE("MCK-13 a stream closed mid-request leaves no trace for the next stream") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId gone = plc.openStream();
        const Bytes& half = cell.first.request;
        feed(plc, gone, half, 0, half.size() / 2);
        plc.closeStream(gone);

        const MockStreamId next = plc.openStream();
        CHECK(next != gone);
        feed(plc, next, cell.second.request);
        const std::vector<Bytes> got = drain(plc, next);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == cell.second.response);
        REQUIRE(plc.requests().size() == 1);
        CHECK(plc.requests()[0].op == cell.second.op);
        CHECK(plc.requests()[0].stream == next);
        CHECK(plc.skippedBytes() == 0);
        CHECK(plc.eotCount() == 0);

        // Not even the default stream saw the abandoned half.
        feed(plc, 0, cell.first.request);
        const std::vector<Bytes> zero = drain(plc, 0);
        REQUIRE(zero.size() == 1);
        CHECK(zero[0] == cell.first.response);
        CHECK(plc.requests().size() == 2);
    }
}

TEST_CASE("MCK-13 closing a stream drops its unsent responses, and the others keep theirs") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();
        feed(plc, a, cell.first.request);
        feed(plc, b, cell.second.request);
        plc.closeStream(a);
        ByteView view;
        CHECK_FALSE(plc.nextResponse(a, view));
        const std::vector<Bytes> got = drain(plc, b);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == cell.second.response);
        // The request itself was executed and stays in the log.
        CHECK(plc.requests().size() == 2);
    }
}

TEST_CASE("MCK-13 stream 0 is the stream-less calls") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        // Stream-less in, stream 0 out and the other way round.
        plc.bytesIn(ByteView{cell.first.request.data(), cell.first.request.size()});
        ByteView view;
        REQUIRE(plc.nextResponse(MockStreamId{0}, view));
        CHECK(Bytes(view.data, view.data + view.size) == cell.first.response);
        CHECK_FALSE(plc.nextResponse(view));

        feed(plc, 0, cell.second.request);
        REQUIRE(plc.nextResponse(view));
        CHECK(Bytes(view.data, view.data + view.size) == cell.second.response);
        CHECK_FALSE(plc.nextResponse(MockStreamId{0}, view));

        // A partial request started stream-less is finished on stream 0.
        const Bytes& r = cell.first.request;
        plc.bytesIn(ByteView{r.data(), r.size() / 2});
        feed(plc, 0, r, r.size() / 2);
        REQUIRE(plc.nextResponse(view));
        CHECK(Bytes(view.data, view.data + view.size) == cell.first.response);
        REQUIRE(plc.requests().size() == 3);
        for (const auto& rec : plc.requests()) {
            CHECK(rec.stream == 0);
        }

        plc.closeStream(0); // does nothing: 0 always exists
        feed(plc, 0, cell.first.request);
        CHECK(drain(plc, 0).size() == 1);
    }
}

TEST_CASE("MCK-13 memory, faults and the request log are shared by every stream") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();

        // A write on one stream is read back on the other.
        const bool writeCell = cell.second.op == Op::WriteWords;
        if (writeCell) {
            feed(plc, b, cell.second.request); // writes D200
            CHECK(drain(plc, b).size() == 1);
            CHECK(plc.word(Device{DeviceType::D, 200}) == 0x1234);
        }

        // failRange applies to the requests of every stream.
        plc.failRange(DeviceType::D, 100, 102, 0xC051);
        feed(plc, a, cell.first.request);
        feed(plc, b, cell.first.request);
        for (MockStreamId id : {a, b}) {
            const std::vector<Bytes> got = drain(plc, id);
            REQUIRE(got.size() == 1);
            CHECK(got[0] != cell.first.response);
        }
        const size_t n = plc.requests().size();
        REQUIRE(n >= 2);
        CHECK_FALSE(plc.requests()[n - 2].answeredWith.ok());
        CHECK_FALSE(plc.requests()[n - 1].answeredWith.ok());
        CHECK(plc.requests()[n - 2].stream == a);
        CHECK(plc.requests()[n - 1].stream == b);
        plc.clearFaults();

        // muteNext counts requests across the streams, in the order they complete.
        plc.clearLog();
        plc.muteNext(1);
        feed(plc, b, cell.first.request);
        feed(plc, a, cell.first.request);
        CHECK(drain(plc, b).empty());
        const std::vector<Bytes> got = drain(plc, a);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == cell.first.response);
        REQUIRE(plc.requests().size() == 2);
        CHECK_FALSE(plc.requests()[0].answered);
        CHECK(plc.requests()[0].stream == b);
        CHECK(plc.requests()[1].answered);
        CHECK(plc.requests()[1].stream == a);
    }
}

TEST_CASE("MCK-13 corruptNext damages the next response whatever stream it is on") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();
        plc.corruptNext(mc::Corruption::Truncate, 1);
        feed(plc, a, cell.first.request);
        feed(plc, b, cell.first.request);
        const std::vector<Bytes> gotA = drain(plc, a);
        const std::vector<Bytes> gotB = drain(plc, b);
        REQUIRE(gotA.size() == 1);
        REQUIRE(gotB.size() == 1);
        CHECK(gotA[0].size() + 1 == cell.first.response.size());
        CHECK(gotB[0] == cell.first.response);
    }
}

TEST_CASE("MCK-13 serial: junk and EOT are counted over all streams, scanned per stream") {
    for (const Cell& cell : cells()) {
        if (cell.cfg.frame != mc::FrameType::F3C && cell.cfg.frame != mc::FrameType::F1C) {
            continue;
        }
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();

        // Junk on A, then half a request on A while B sends a whole one and an EOT: the EOT of B
        // does not cut the partial request of A.
        const Bytes junk = {0x55, 0x55};
        feed(plc, a, junk);
        const Bytes& ra = cell.first.request;
        feed(plc, a, ra, 0, ra.size() / 2);
        feed(plc, b, cell.second.request);
        Bytes eot = {sf::kEot};
        if (cell.cfg.format == SerialFormat::Format4) {
            eot.push_back(sf::kCr);
            eot.push_back(sf::kLf);
        }
        feed(plc, b, eot);
        feed(plc, a, ra, ra.size() / 2);

        CHECK(plc.skippedBytes() == 2);
        CHECK(plc.eotCount() == 1);
        const std::vector<Bytes> gotA = drain(plc, a);
        const std::vector<Bytes> gotB = drain(plc, b);
        REQUIRE(gotA.size() == 1);
        REQUIRE(gotB.size() == 1);
        CHECK(gotA[0] == cell.first.response);
        CHECK(gotB[0] == cell.second.response);

        // The CR LF that follows an EOT in format 4 belongs to the EOT of its own stream only.
        if (cell.cfg.format == SerialFormat::Format4) {
            feed(plc, a, eot, 0, 1); // EOT on A, its CR LF still to come
            feed(plc, b, cell.first.request);
            CHECK(drain(plc, b).size() == 1);
            CHECK(plc.skippedBytes() == 2);
        }
    }
}

TEST_CASE("MCK-13 a closed or unknown stream id is ignored") {
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        ByteView view;

        // Never opened.
        feed(plc, 99, cell.first.request);
        CHECK_FALSE(plc.nextResponse(MockStreamId{99}, view));
        plc.closeStream(99); // no effect

        // Closed.
        const MockStreamId id = plc.openStream();
        plc.closeStream(id);
        feed(plc, id, cell.first.request);
        CHECK_FALSE(plc.nextResponse(id, view));
        plc.closeStream(id); // twice: no effect

        CHECK(plc.requests().empty());
        CHECK(plc.skippedBytes() == 0);
        CHECK(plc.eotCount() == 0);
        CHECK_FALSE(plc.nextResponse(view));

        // The mock still works, and a new stream gets a fresh id.
        const MockStreamId later = plc.openStream();
        CHECK(later != id);
        CHECK(later != 0);
        feed(plc, later, cell.first.request);
        CHECK(drain(plc, later).size() == 1);
    }
}

// ---- Added by the batch tester (tester9, T-075): randomised interleaving ---------------------------
// Four streams, each sending a script of requests cut into random chunks, the chunks of the streams
// shuffled together by a fixed-seed generator; some streams abandon a half request and are closed and
// reopened. Every stream must get exactly its own answers, in order, and nothing else.
TEST_CASE("MCK-13 randomised chunk interleaving over four streams answers each stream only its own") {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state](uint32_t bound) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>((state >> 33) % bound);
    };
    for (const Cell& cell : cells()) {
        INFO(cell.name);
        for (int trial = 0; trial < 25; ++trial) {
            MockPlc plc(cell.cfg);
            seed(plc);
            constexpr int kStreams = 4;
            struct Link {
                MockStreamId id{0};
                Bytes pending;               // bytes still to send, in order
                std::vector<Bytes> expected; // answers owed, in order
            };
            Link links[kStreams];
            for (Link& l : links) {
                l.id = plc.openStream();
                const int requests = 1 + static_cast<int>(next(4));
                for (int i = 0; i < requests; ++i) {
                    const Exchange& e = next(2) == 0 ? cell.first : cell.second;
                    l.pending.insert(l.pending.end(), e.request.begin(), e.request.end());
                    l.expected.push_back(e.response);
                }
            }
            std::vector<std::vector<Bytes>> got(kStreams);
            size_t sentTotal = 0;
            size_t total = 0;
            for (const Link& l : links) {
                total += l.pending.size();
            }
            std::vector<size_t> pos(kStreams, 0);
            while (sentTotal < total) {
                const int s = static_cast<int>(next(kStreams));
                Link& l = links[s];
                if (pos[s] >= l.pending.size()) {
                    continue;
                }
                const size_t chunk = std::min<size_t>(1 + next(7), l.pending.size() - pos[s]);
                plc.bytesIn(l.id, ByteView{l.pending.data() + pos[s], chunk});
                pos[s] += chunk;
                sentTotal += chunk;
                // a stream that is closed and reopened mid-script in the middle of nowhere would lose
                // its own bytes; instead a throw-away stream abandons half a request now and then
                if (next(5) == 0) {
                    const MockStreamId junkLink = plc.openStream();
                    const Bytes& half = (next(2) == 0 ? cell.first : cell.second).request;
                    plc.bytesIn(junkLink, ByteView{half.data(), half.size() / 2});
                    plc.closeStream(junkLink);
                }
                ByteView v;
                for (int k = 0; k < kStreams; ++k) {
                    while (plc.nextResponse(links[k].id, v)) {
                        got[k].emplace_back(v.data, v.data + v.size);
                    }
                }
            }
            for (int k = 0; k < kStreams; ++k) {
                INFO("trial ", trial, " stream ", k);
                REQUIRE(got[k].size() == links[k].expected.size());
                for (size_t i = 0; i < got[k].size(); ++i) {
                    CHECK(got[k][i] == links[k].expected[i]);
                }
            }
            CHECK(drain(plc, 0).empty());
            CHECK(plc.skippedBytes() == 0);
            CHECK(plc.eotCount() == 0);
        }
    }
}

// ---- Added by the batch tester (tester9, T-075): per-stream junk flag and EOT tail ------------------
// The developer's MCK-13 cases pass when the "junk run already logged" flag or the format 4 "CR LF
// still to come" counter is shared by all streams (mutation M3 / M4 of the tester report); these two
// cases observe the difference.
TEST_CASE("MCK-13 tester: every stream logs its own junk run (the junk flag is per stream)") {
    for (const Cell& cell : cells()) {
        if (cell.cfg.frame != mc::FrameType::F3E && cell.cfg.frame != mc::FrameType::F1E) {
            continue;
        }
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();
        const Bytes junk = {0xFF, 0xFF, 0xFF};
        feed(plc, a, junk);
        feed(plc, b, junk);
        feed(plc, a, junk); // the same run on A: no new record
        feed(plc, b, junk);
        REQUIRE(plc.requests().size() == 2);
        CHECK(plc.requests()[0].stream == a);
        CHECK(plc.requests()[1].stream == b);
        CHECK_FALSE(plc.requests()[0].answered);
        CHECK_FALSE(plc.requests()[1].answered);
        // A real request on A ends A's run only: new junk on A is a new record, B's run goes on.
        feed(plc, a, cell.first.request);
        CHECK(drain(plc, a).size() == 1);
        feed(plc, a, junk);
        feed(plc, b, junk);
        REQUIRE(plc.requests().size() == 4);
        CHECK(plc.requests()[3].stream == a);
    }
}

TEST_CASE("MCK-13 tester: the CR LF that follows an EOT in format 4 is owed by its own stream only") {
    for (const Cell& cell : cells()) {
        if (cell.cfg.format != SerialFormat::Format4 ||
            (cell.cfg.frame != mc::FrameType::F3C && cell.cfg.frame != mc::FrameType::F1C)) {
            continue;
        }
        INFO(cell.name);
        MockPlc plc(cell.cfg);
        seed(plc);
        const MockStreamId a = plc.openStream();
        const MockStreamId b = plc.openStream();
        const Bytes eot = {sf::kEot};
        const Bytes crlf = {sf::kCr, sf::kLf};
        feed(plc, a, eot); // A owes CR LF
        CHECK(plc.eotCount() == 1);
        feed(plc, b, crlf); // not B's tail: two skipped bytes
        CHECK(plc.skippedBytes() == 2);
        feed(plc, a, crlf); // A's tail: swallowed, not skipped
        CHECK(plc.skippedBytes() == 2);
        CHECK(plc.eotCount() == 1);
        // and the other way round
        feed(plc, b, eot);
        feed(plc, a, crlf); // A owes nothing now
        CHECK(plc.skippedBytes() == 4);
        feed(plc, b, crlf);
        CHECK(plc.skippedBytes() == 4);
        CHECK(plc.eotCount() == 2);
    }
}
