// RPL-04: the poll transcripts of session.vec replayed through a Session on a fake clock.
//
// What drives the Session. The tool records, besides the wire chunks and the Session's events, the
// inputs it gave the Session (records of kind input: the heartbeat, the subscriptions, the ad-hoc
// writes) and the link state changes of the device (the linkState events). The replay feeds all of
// them back in the order of their sequence numbers, each at its own time: the heartbeat and the
// initial subscriptions before the first linkUp(), then the received chunks, the later
// subscriptions, unsubscriptions and ad-hoc writes, and, after a link fault, the linkDown() and the
// linkUp() of the reconnect (a linkState event Disconnected is a linkDown(), a later Connected a
// linkUp(); round numbering then starts again at 1, as it did live).
//
// The fallback. A capture without input records (an older one) is replayed with inputs rebuilt from
// the transcript instead: the heartbeat is a one-bit write that opens the transcript, the
// subscriptions are the segments of the snapshot events (a change first visible in round N was made
// while round N-1 was running), an ad-hoc write is a written frame that is not the heartbeat. That
// path knows nothing of a link fault.
//
// The fake clock is the captured nanosecond stamp in milliseconds from the first frame. A timer
// tick is made at the time of the next captured output (a frame or an event that only a tick can
// produce). The timing contract is checked in one direction: when a tick produces a captured
// output, the Session's own deadline (cycle interval, response timeout, serial inter-character
// time) may not lie more than two milliseconds after the captured time; if it does, the Session
// would act later than the live run did, and the replay fails with both times. The other direction
// (a deadline earlier than the output) cannot be judged: a live tick runs late whenever the machine
// is loaded, so it is replayed at the captured time and not compared. For the same reason a
// deadline that has passed while the capture holds nothing owed is left alone: a response that the
// live run handled before its late timer (a stall of the event loop) is not turned into a fault,
// which also means a Session that no longer faults on such a late response is not detected. The tx
// frames and the events are compared as two lists (the interleaving of a frame and an event of the
// same call is not part of the contract). A cycle's duration may differ by two milliseconds from
// the recorded one (the stamps and the Session clock have different origins).
#include "replay.h"
#include "replay_util.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace mc::replay {

using namespace util;

namespace {

/// How much later than the captured output the Session's own deadline may lie (the stamps and the
/// Session clock are rounded to whole milliseconds from different origins).
constexpr TimeMs kTickToleranceMs = 2;

struct Chunk {
    bool tx{true};
    int64_t t{0};
    int seq{0};
    std::vector<uint8_t> bytes;
};

struct Canon {
    std::string name;
    std::string type;
    uint32_t round{0};
    std::vector<uint8_t> payload; ///< Compared byte for byte.
    std::string outcome;          ///< requestFinished: first word of the outcome.
    int64_t duration{-1};         ///< cycleDone: duration in ms (tolerance), else -1.
    int64_t t{0};
    int seq{0};
};

struct Seg {
    DeviceType type{DeviceType::D};
    uint32_t head{0};
    uint32_t count{0};
    bool operator==(const Seg& o) const {
        return type == o.type && head == o.head && count == o.count;
    }
};

int64_t toI64(const std::string& s) {
    return static_cast<int64_t>(std::strtoll(s.c_str(), nullptr, 10));
}

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

std::string outcomeWord(const Error& e) {
    if (e.code == ErrorCode::Ok) {
        return "ok";
    }
    if (e.code == ErrorCode::Timeout) {
        return "timeout";
    }
    switch (e.category) {
    case ErrorCategory::Plc:
        return "plcError";
    case ErrorCategory::Protocol:
        return "protocolError";
    case ErrorCategory::Transport:
        return "transportError";
    default:
        return "notSent";
    }
}

/// The comparable form of a captured event; false for the events that are not Session outputs.
bool canonOfCaptured(const mc::test::Vector& v, Canon* out) {
    const std::string name = v.field("event");
    if (name != "snapshot" && name != "valuesChanged" && name != "cycleDone" &&
        name != "requestFinished" && name != "linkFault") {
        return false;
    }
    out->name = name;
    out->type = v.field("type");
    out->round = static_cast<uint32_t>(toI64(v.field("round")));
    out->t = toI64(v.field("t_ns"));
    out->seq = static_cast<int>(toI64(v.field("seq")));
    if (v.bytes.empty()) {
        return true;
    }
    const std::vector<uint8_t>& b = v.bytes;
    if (name == "cycleDone") {
        // tag, round u32, startedAt u64, durationMs u32, requests u16, failedChunks u16,
        // heartbeatOk u8
        if (b.size() >= 22) {
            out->round = le32(b, 1);
            put32(out->payload, le32(b, 1));
            out->duration = le32(b, 13);
            out->payload.insert(out->payload.end(), b.begin() + 17, b.begin() + 22);
        }
        return true;
    }
    out->payload.assign(b.begin() + 1, b.end());
    if (name == "requestFinished") {
        const std::vector<std::string> w = words(v.field("outcome"));
        out->outcome = w.empty() ? "" : w[0];
    }
    return true;
}

std::string describeEvent(const Canon& e) {
    std::string t = e.name;
    if (!e.type.empty()) {
        t += " " + e.type;
    }
    if (e.name == "snapshot" || e.name == "valuesChanged" || e.name == "cycleDone") {
        t += " round " + std::to_string(e.round);
    }
    if (!e.outcome.empty()) {
        t += " " + e.outcome;
    }
    return t;
}

/// Empty when equal, else what differs.
std::string compareEvents(const Canon& want, const Canon& got) {
    if (want.name != got.name) {
        return "captured a " + describeEvent(want) + " event, the replay produced " +
               describeEvent(got);
    }
    if (want.name == "snapshot" || want.name == "valuesChanged") {
        if (!want.type.empty() && want.type != got.type) {
            return describeEvent(want) + ": the replay produced device type " + got.type;
        }
        if (want.round != got.round) {
            return describeEvent(want) + ": the replay produced round " + std::to_string(got.round);
        }
    }
    if (want.name == "requestFinished" && want.outcome != got.outcome) {
        return describeEvent(want) + ": the replay produced outcome " + got.outcome;
    }
    if (want.payload != got.payload) {
        return describeEvent(want) + ": payload differs, " +
               describeDifference(want.payload, got.payload, "replay");
    }
    if (want.name == "cycleDone" && want.duration >= 0) {
        const int64_t diff = want.duration > got.duration ? want.duration - got.duration
                                                          : got.duration - want.duration;
        if (diff > 2) {
            return describeEvent(want) + ": duration " + std::to_string(want.duration) +
                   " ms captured, " + std::to_string(got.duration) + " ms replayed";
        }
    }
    return std::string();
}

class PollReplay {
  public:
    PollReplay(const Capture& c, std::string step, std::vector<Chunk> chunks,
               std::vector<mc::test::Vector> events, std::vector<mc::test::Vector> inputs,
               Report& r)
        : m_c(c), m_step(std::move(step)), m_chunks(std::move(chunks)), m_events(std::move(events)),
          m_inputVecs(std::move(inputs)), m_r(r) {}

    void run();

  private:
    struct TxItem {
        size_t chunkIndex{0};
        std::vector<Decoded> decoded;
        bool adhoc{false};
    };

    void failure(const std::string& message) {
        m_r.failures.push_back(Failure{"RPL-04", m_c.profile, m_step, message});
    }

    TimeMs ms(int64_t t) const {
        return t <= m_base ? 0 : static_cast<TimeMs>((t - m_base) / 1000000);
    }
    void drain();
    void afterDrain();
    void applySubDiff(uint32_t round);
    void subscribe(const Seg& s);
    void advanceTo(int beforeSeq);
    bool pendingBefore(int seq) const;
    int firstOwnedByLinkDown(int linkSeq) const;
    void runWithInputs(int64_t endT);
    void subscribeNamed(const std::string& name, Device head, uint32_t count);
    void submit(const TxItem& tx, TimeMs at);
    bool queuedBehind(const TxItem& tx) const;
    void injectIdle(TimeMs upTo);
    std::optional<TimeMs> nextCapturedOutput(int beforeSeq) const;
    void compare();

    const Capture& m_c;
    std::string m_step;
    std::vector<Chunk> m_chunks;
    std::vector<mc::test::Vector> m_events;
    std::vector<mc::test::Vector> m_inputVecs;
    std::map<std::string, SubscriptionId> m_named;
    bool m_haveInputs{false};
    Report& m_r;

    int64_t m_base{0};
    std::optional<Session> m_session;
    TimeMs m_now{0};
    bool m_hb{false};
    Device m_hbDevice{};
    std::vector<TxItem> m_txList;
    std::vector<Canon> m_expectEv;
    std::map<uint32_t, std::map<int, std::vector<Seg>>> m_snapByRound;
    std::map<int, std::vector<Seg>> m_current;
    std::map<int, std::vector<SubscriptionId>> m_ids;
    std::vector<std::vector<uint8_t>> m_gotTx;
    std::vector<Canon> m_gotEv;
    size_t m_cycles{0};
    size_t m_sendsThisRound{0};
    std::set<uint32_t> m_applied;
    bool m_stop{false};
};

std::vector<Seg> segmentsOf(const mc::test::Vector& v) {
    std::vector<Seg> out;
    const auto type = typeBySymbol(v.field("type"));
    if (!type || v.bytes.size() < 1) {
        return out;
    }
    const bool bit = isBitDevice(*type);
    size_t pos = 1;
    while (pos + 8 <= v.bytes.size()) {
        const uint32_t head = le32(v.bytes, pos);
        const uint32_t count = le32(v.bytes, pos + 4);
        const size_t len = 8 + (bit ? count : 2u * count) + count;
        if (pos + len > v.bytes.size()) {
            break;
        }
        out.push_back(Seg{*type, head, count});
        pos += len;
    }
    return out;
}

void PollReplay::subscribe(const Seg& s) {
    const Expected<SubscriptionId> id = m_session->subscribe(Device{s.type, s.head}, s.count);
    if (!id) {
        failure("cannot subscribe " + deviceText(Device{s.type, s.head}) + " x" +
                std::to_string(s.count) + " (rebuilt from the snapshots)");
        m_stop = true;
        return;
    }
    m_ids[static_cast<int>(s.type)].push_back(id.value());
    m_current[static_cast<int>(s.type)].push_back(s);
}

void PollReplay::drain() {
    Output o;
    while (m_session->nextOutput(o)) {
        switch (o.kind) {
        case OutputKind::Send:
            m_gotTx.emplace_back(o.bytes.data, o.bytes.data + o.bytes.size);
            ++m_sendsThisRound;
            break;
        case OutputKind::Snapshot: {
            Canon e;
            e.name = "snapshot";
            e.type = deviceInfo(o.deviceType).symbol;
            e.round = o.round;
            const ValueStore& store = m_session->values();
            for (size_t i = 0; i < store.segmentCount(o.deviceType); ++i) {
                const SegmentView s = store.segment(o.deviceType, i);
                put32(e.payload, s.head.number);
                put32(e.payload, s.count);
                if (s.words != nullptr) {
                    for (uint32_t k = 0; k < s.count; ++k) {
                        put16(e.payload, s.words[k]);
                    }
                } else if (s.bits != nullptr) {
                    e.payload.insert(e.payload.end(), s.bits, s.bits + s.count);
                }
                e.payload.insert(e.payload.end(), s.states, s.states + s.count);
            }
            m_gotEv.push_back(std::move(e));
            break;
        }
        case OutputKind::ValuesChanged: {
            Canon e;
            e.name = "valuesChanged";
            e.type = deviceInfo(o.deviceType).symbol;
            e.round = o.round;
            for (size_t i = 0; i < o.changeCount; ++i) {
                put32(e.payload, o.changes[i].device.number);
                put16(e.payload, o.changes[i].oldValue);
                put16(e.payload, o.changes[i].newValue);
            }
            m_gotEv.push_back(std::move(e));
            break;
        }
        case OutputKind::CycleDone: {
            Canon e;
            e.name = "cycleDone";
            e.round = o.cycle.round;
            put32(e.payload, o.cycle.round);
            put16(e.payload, o.cycle.requests);
            put16(e.payload, o.cycle.failedChunks);
            e.payload.push_back(o.cycle.heartbeatOk ? 1 : 0);
            e.duration = o.cycle.durationMs;
            m_gotEv.push_back(std::move(e));
            ++m_cycles;
            m_sendsThisRound = 0;
            break;
        }
        case OutputKind::RequestDone: {
            Canon e;
            e.name = "requestFinished";
            e.outcome = outcomeWord(o.error);
            e.payload.assign(o.payload.data, o.payload.data + o.payload.size);
            m_gotEv.push_back(std::move(e));
            break;
        }
        case OutputKind::LinkFault: {
            Canon e;
            e.name = "linkFault";
            e.payload.push_back(static_cast<uint8_t>(o.fault));
            put16(e.payload, static_cast<uint16_t>(o.error.code));
            put16(e.payload, o.error.plcCode);
            e.payload.push_back(o.reopenTransport ? 1 : 0);
            m_gotEv.push_back(std::move(e));
            break;
        }
        }
    }
}

// A subscription change visible from round N was made while round N-1 was running (it takes effect
// at the next round boundary, whether it was made mid-round or between two rounds): apply it once
// round N-1 has sent its first frame. Applying it earlier would make it visible one round too soon.
void PollReplay::afterDrain() {
    if (m_haveInputs || m_stop || m_sendsThisRound == 0) {
        return;
    }
    const uint32_t visibleFrom = static_cast<uint32_t>(m_cycles) + 2;
    if (m_applied.insert(visibleFrom).second) {
        applySubDiff(visibleFrom - 1);
    }
}

void PollReplay::applySubDiff(uint32_t round) {
    const auto next = m_snapByRound.find(round + 1);
    if (next == m_snapByRound.end()) {
        return;
    }
    std::vector<int> types;
    for (const auto& kv : m_current) {
        types.push_back(kv.first);
    }
    for (const auto& kv : next->second) {
        if (std::find(types.begin(), types.end(), kv.first) == types.end()) {
            types.push_back(kv.first);
        }
    }
    for (const int t : types) {
        const auto it = next->second.find(t);
        const std::vector<Seg> wanted = it == next->second.end() ? std::vector<Seg>{} : it->second;
        if (m_current[t] == wanted) {
            continue;
        }
        for (const SubscriptionId id : m_ids[t]) {
            (void)m_session->unsubscribe(id);
        }
        m_ids[t].clear();
        m_current[t].clear();
        for (const Seg& s : wanted) {
            subscribe(s);
        }
    }
}

// The captured time of the first frame or event, before sequence number @p beforeSeq, that the
// replay has not produced yet.
std::optional<TimeMs> PollReplay::nextCapturedOutput(int beforeSeq) const {
    std::optional<TimeMs> best;
    if (m_gotTx.size() < m_txList.size()) {
        const Chunk& ch = m_chunks[m_txList[m_gotTx.size()].chunkIndex];
        if (ch.seq < beforeSeq) {
            best = ms(ch.t);
        }
    }
    if (m_gotEv.size() < m_expectEv.size() && m_expectEv[m_gotEv.size()].seq < beforeSeq) {
        const TimeMs t = ms(m_expectEv[m_gotEv.size()].t);
        if (!best || t < *best) {
            best = t;
        }
    }
    return best;
}

// True while the capture holds a frame or an event, before sequence number @p seq, that the replay
// has not produced yet: only a timer tick can produce it. A deadline that has passed is otherwise
// left alone, because the live run handled the response that was already waiting before its timer
// (a stall of the event loop, e.g. a loaded machine, makes a response late by more than the timeout
// without a fault).
bool PollReplay::pendingBefore(int seq) const {
    if (m_gotTx.size() < m_txList.size() &&
        m_chunks[m_txList[m_gotTx.size()].chunkIndex].seq < seq) {
        return true;
    }
    return m_gotEv.size() < m_expectEv.size() && m_expectEv[m_gotEv.size()].seq < seq;
}

// A linkDown() finishes every outstanding request: those requestFinished events are recorded just
// before the linkState event and are the call's own output, not a tick's, so ticks stop short of
// them. Returns the sequence number of the first of them, or INT_MAX when there is none.
int PollReplay::firstOwnedByLinkDown(int linkSeq) const {
    int first = INT_MAX;
    for (size_t i = m_expectEv.size(); i > m_gotEv.size(); --i) {
        const Canon& e = m_expectEv[i - 1];
        if (e.seq >= linkSeq) {
            continue;
        }
        if (e.name != "requestFinished") {
            break;
        }
        first = e.seq;
    }
    return first;
}

void PollReplay::advanceTo(int beforeSeq) {
    for (int guard = 0; guard < 100000 && !m_stop; ++guard) {
        const TimeMs dl = m_session->nextDeadline();
        if (dl == kNoDeadline || !pendingBefore(beforeSeq)) {
            return;
        }
        // The tick is due: the capture holds an output before the next input that only a tick can
        // produce. It is made at the time of that output, not at the stamp of the next input: the
        // stamps and the Session clock are rounded to milliseconds differently, so a tick that the
        // live run made just before a response can look one millisecond early. It is never made
        // before the Session's own deadline, and the deadline may not be later than the captured
        // time by more than the tolerance (the Session would be slower than the live run was: a
        // changed cycle interval or timeout).
        TimeMs at = dl;
        const auto out = nextCapturedOutput(beforeSeq);
        if (out) {
            if (dl > *out + kTickToleranceMs) {
                failure("the Session's next timer is due at " + std::to_string(dl) +
                        " ms, but the capture shows the output it produces at " +
                        std::to_string(*out) + " ms (more than " +
                        std::to_string(kTickToleranceMs) +
                        " ms earlier): the cycle interval, a timeout or an inter-character time "
                        "differs from the one the capture was made with");
                m_stop = true;
                return;
            }
            at = std::max(at, *out);
        }
        m_now = std::max(m_now, at);
        m_session->tick(m_now);
        drain();
        afterDrain();
    }
}

void PollReplay::submit(const TxItem& tx, TimeMs at) {
    m_now = std::max(m_now, at);
    const Request req = toRequest(tx.decoded.front());
    const Expected<RequestId> id = m_session->submit(req, m_now);
    if (!id) {
        failure("the ad-hoc request at frame " + std::to_string(tx.chunkIndex) +
                " is refused by the Session");
        m_stop = true;
        return;
    }
    drain();
    afterDrain();
}

bool PollReplay::queuedBehind(const TxItem& tx) const {
    if (tx.chunkIndex == 0) {
        return false;
    }
    const Chunk& prev = m_chunks[tx.chunkIndex - 1];
    return !prev.tx && (m_chunks[tx.chunkIndex].t - prev.t) < 100000;
}

void PollReplay::injectIdle(TimeMs upTo) {
    for (int guard = 0; guard < 10000 && !m_stop; ++guard) {
        if (m_gotTx.size() >= m_txList.size()) {
            return;
        }
        const TxItem& tx = m_txList[m_gotTx.size()];
        if (!tx.adhoc || queuedBehind(tx)) {
            return;
        }
        const TimeMs at = ms(m_chunks[tx.chunkIndex].t);
        if (at > upTo) {
            return;
        }
        advanceTo(m_chunks[tx.chunkIndex].seq);
        const size_t before = m_gotTx.size();
        submit(tx, at);
        if (m_gotTx.size() == before) {
            return; // the Session did not send it: let the comparison report it
        }
    }
}

void PollReplay::compare() {
    int reported = 0;
    for (size_t i = 0; i < std::max(m_txList.size(), m_gotTx.size()) && reported < 5; ++i) {
        const bool haveWant = i < m_txList.size();
        const bool haveGot = i < m_gotTx.size();
        if (!haveWant) {
            failure("tx frame #" + std::to_string(i + 1) +
                    ": the replay sent a frame the capture does not have: " + hex(m_gotTx[i]));
            ++reported;
        } else if (!haveGot) {
            failure("tx frame #" + std::to_string(i + 1) +
                    ": the capture has a frame the replay never sent: " +
                    hex(m_chunks[m_txList[i].chunkIndex].bytes));
            ++reported;
        } else if (m_chunks[m_txList[i].chunkIndex].bytes != m_gotTx[i]) {
            failure(
                "tx frame #" + std::to_string(i + 1) + " differs from the replay, " +
                describeDifference(m_chunks[m_txList[i].chunkIndex].bytes, m_gotTx[i], "replay"));
            ++reported;
        }
        ++m_r.checks;
    }
    reported = 0;
    for (size_t i = 0; i < std::max(m_expectEv.size(), m_gotEv.size()) && reported < 5; ++i) {
        ++m_r.checks;
        if (i >= m_expectEv.size()) {
            failure("event #" + std::to_string(i + 1) + ": the replay produced an extra " +
                    describeEvent(m_gotEv[i]) + " event");
            ++reported;
        } else if (i >= m_gotEv.size()) {
            failure("event #" + std::to_string(i + 1) + ": the capture has a " +
                    describeEvent(m_expectEv[i]) + " event the replay never produced");
            ++reported;
        } else {
            const std::string diff = compareEvents(m_expectEv[i], m_gotEv[i]);
            if (!diff.empty()) {
                failure("event #" + std::to_string(i + 1) + ": " + diff);
                ++reported;
            }
        }
    }
}

void PollReplay::subscribeNamed(const std::string& name, Device head, uint32_t count) {
    const Expected<SubscriptionId> id = m_session->subscribe(head, count);
    if (!id) {
        failure("cannot subscribe " + deviceText(head) + " x" + std::to_string(count) +
                " (recorded input '" + name + "')");
        m_stop = true;
        return;
    }
    m_named[name] = id.value();
}

// The recorded inputs (session.vec records of kind input) drive the Session as the tool did:
// heartbeat and initial subscriptions before the link comes up, then the received chunks, later
// subscriptions, unsubscriptions and ad-hoc writes in the order of their sequence numbers, each at
// its own time.
void PollReplay::runWithInputs(int64_t endT) {
    struct In {
        int seq{0};
        int64_t t{0};
        int tag{0};
        std::vector<uint8_t> b;
        std::string name;
    };
    std::vector<In> inputs;
    for (const mc::test::Vector& v : m_inputVecs) {
        In in;
        in.seq = static_cast<int>(toI64(v.field("seq")));
        in.t = toI64(v.field("t_ns"));
        in.b = v.bytes;
        in.tag = v.bytes.empty() ? 0 : v.bytes[0];
        in.name = v.field("name");
        inputs.push_back(std::move(in));
        endT = std::max(endT, inputs.back().t);
    }
    std::sort(inputs.begin(), inputs.end(), [](const In& a, const In& b) { return a.seq < b.seq; });

    int firstTxSeq = INT_MAX;
    for (const Chunk& ch : m_chunks) {
        if (ch.tx) {
            firstTxSeq = std::min(firstTxSeq, ch.seq);
        }
    }

    SessionConfig cfg = m_c.session;
    for (const In& in : inputs) {
        if (in.tag == 0x07 && in.b.size() >= 7) {
            cfg.heartbeat.enabled = in.b[1] != 0;
            cfg.heartbeat.device = Device{static_cast<DeviceType>(in.b[2]), le32(in.b, 3)};
        }
    }
    Expected<Session> created = Session::create(m_c.frame, cfg);
    if (!created) {
        failure("the Session cannot be created from run.meta: " +
                std::string(created.error().message));
        return;
    }
    m_session.emplace(std::move(created.value()));
    for (const In& in : inputs) {
        if (in.tag == 0x08 && in.b.size() >= 10 && in.seq < firstTxSeq) {
            subscribeNamed(in.name, Device{static_cast<DeviceType>(in.b[1]), le32(in.b, 2)},
                           le32(in.b, 6));
        }
    }
    m_session->linkUp(0);
    drain();

    // The link state changes after the first Connected: a Disconnected is a linkDown(), a Connected
    // a linkUp().
    struct Link {
        int seq{0};
        int64_t t{0};
        bool up{false};
    };
    std::vector<Link> links;
    bool connectedOnce = false;
    for (const mc::test::Vector& v : m_events) {
        if (v.field("event") != "linkState" || v.bytes.size() < 3) {
            continue;
        }
        const uint8_t state = v.bytes[1];
        if (state == 2 && !connectedOnce) {
            connectedOnce = true; // the first Connected is the linkUp() made above
            continue;
        }
        if (connectedOnce && (state == 0 || state == 2)) {
            links.push_back(
                Link{static_cast<int>(toI64(v.field("seq"))), toI64(v.field("t_ns")), state == 2});
            endT = std::max(endT, links.back().t);
        }
    }

    struct Driver {
        int seq;
        const Chunk* rx;
        const In* in;
        const Link* link;
    };
    std::vector<Driver> drivers;
    for (const Chunk& ch : m_chunks) {
        if (!ch.tx) {
            drivers.push_back(Driver{ch.seq, &ch, nullptr, nullptr});
        }
    }
    for (const Link& l : links) {
        drivers.push_back(Driver{l.seq, nullptr, nullptr, &l});
    }
    for (const In& in : inputs) {
        const bool initial = (in.tag == 0x07) || (in.tag == 0x08 && in.seq < firstTxSeq);
        if (!initial) {
            drivers.push_back(Driver{in.seq, nullptr, &in, nullptr});
        }
    }
    std::sort(drivers.begin(), drivers.end(),
              [](const Driver& a, const Driver& b) { return a.seq < b.seq; });

    for (const Driver& d : drivers) {
        if (m_stop) {
            break;
        }
        const int64_t t = d.rx != nullptr ? d.rx->t : d.link != nullptr ? d.link->t : d.in->t;
        // An ad-hoc write that went out at once is recorded before its input record (the frame is
        // stamped inside submit): that frame is the input's own output, not a tick's, so ticks stop
        // short of it.
        int limit = d.seq;
        if (d.in != nullptr && d.in->tag == 0x0A && d.in->b.size() >= 9 &&
            m_gotTx.size() < m_txList.size()) {
            const Chunk& next = m_chunks[m_txList[m_gotTx.size()].chunkIndex];
            const std::vector<Decoded> dec = decode(m_c.frame, next.bytes);
            const std::vector<uint8_t>& b = d.in->b;
            if (!dec.empty() && static_cast<uint8_t>(dec.front().op) == b[1] &&
                static_cast<uint8_t>(dec.front().head.type) == b[2] &&
                dec.front().head.number == le32(b, 3) &&
                dec.front().count == static_cast<uint16_t>(b[7] | (b[8] << 8))) {
                limit = std::min(limit, next.seq);
            }
        }
        if (d.link != nullptr && !d.link->up) {
            limit = std::min(limit, firstOwnedByLinkDown(d.seq));
        }
        advanceTo(limit);
        m_now = std::max(m_now, ms(t));
        if (d.rx != nullptr) {
            m_session->bytesIn(view(d.rx->bytes), m_now);
        } else if (d.link != nullptr) {
            if (d.link->up) {
                m_session->linkUp(m_now);
            } else {
                m_session->linkDown(m_now);
            }
        } else if (d.in->tag == 0x08 && d.in->b.size() >= 10) {
            subscribeNamed(d.in->name,
                           Device{static_cast<DeviceType>(d.in->b[1]), le32(d.in->b, 2)},
                           le32(d.in->b, 6));
        } else if (d.in->tag == 0x09) {
            const auto it = m_named.find(d.in->name);
            if (it != m_named.end()) {
                (void)m_session->unsubscribe(it->second);
                m_named.erase(it);
            }
        } else if (d.in->tag == 0x0A && d.in->b.size() >= 9) {
            const std::vector<uint8_t>& b = d.in->b;
            const Device head{static_cast<DeviceType>(b[2]), le32(b, 3)};
            const std::vector<uint8_t> data(b.begin() + 9, b.end());
            Request req;
            switch (static_cast<Op>(b[1])) {
            case Op::WriteBits:
                req = Request::writeBits(head, view(data));
                break;
            case Op::WriteWords:
                req = Request::writeWords(head, view(data));
                break;
            case Op::ReadBits:
                req = Request::readBits(head, static_cast<uint16_t>(b[7] | (b[8] << 8)));
                break;
            case Op::ReadWords:
                req = Request::readWords(head, static_cast<uint16_t>(b[7] | (b[8] << 8)));
                break;
            }
            const Expected<RequestId> id = m_session->submit(req, m_now);
            if (!id) {
                failure("the recorded ad-hoc request at input #" + std::to_string(d.seq) +
                        " is refused by the Session");
                m_stop = true;
            }
        }
        drain();
    }
    if (!m_stop) {
        advanceTo(INT_MAX);
        compare();
    }
}

void PollReplay::run() {
    if (m_chunks.empty()) {
        return;
    }
    std::sort(m_chunks.begin(), m_chunks.end(),
              [](const Chunk& a, const Chunk& b) { return a.seq < b.seq; });
    std::stable_sort(m_events.begin(), m_events.end(),
                     [](const mc::test::Vector& a, const mc::test::Vector& b) {
                         return toI64(a.field("seq")) < toI64(b.field("seq"));
                     });
    m_base = m_chunks.front().t;
    int64_t endT = 0;
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        if (m_chunks[i].tx) {
            m_base = m_chunks[i].t;
            break;
        }
    }
    for (const Chunk& ch : m_chunks) {
        endT = std::max(endT, ch.t);
    }
    for (const mc::test::Vector& v : m_events) {
        Canon e;
        if (canonOfCaptured(v, &e)) {
            endT = std::max(endT, e.t);
            m_expectEv.push_back(std::move(e));
        }
        if (v.field("event") == "snapshot") {
            const uint32_t round = static_cast<uint32_t>(toI64(v.field("round")));
            const auto type = typeBySymbol(v.field("type"));
            if (type) {
                m_snapByRound[round][static_cast<int>(*type)] = segmentsOf(v);
            }
        }
    }

    if (!m_inputVecs.empty()) {
        m_haveInputs = true;
        // the tx list is still needed for the comparison and the pending test
        for (size_t i = 0; i < m_chunks.size(); ++i) {
            if (m_chunks[i].tx) {
                TxItem item;
                item.chunkIndex = i;
                m_txList.push_back(std::move(item));
            }
        }
        runWithInputs(endT);
        return;
    }

    // inputs rebuilt from the transcript (a capture without recorded inputs)
    SessionConfig cfg = m_c.session;
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        if (!m_chunks[i].tx) {
            continue;
        }
        TxItem item;
        item.chunkIndex = i;
        item.decoded = decode(m_c.frame, m_chunks[i].bytes);
        if (m_txList.empty() && !item.decoded.empty() && item.decoded.front().op == Op::WriteBits &&
            item.decoded.front().count == 1) {
            m_hb = true;
            m_hbDevice = item.decoded.front().head;
            cfg.heartbeat.enabled = true;
            cfg.heartbeat.device = m_hbDevice;
        }
        m_txList.push_back(std::move(item));
    }
    for (TxItem& tx : m_txList) {
        const bool heartbeat =
            m_hb && !tx.decoded.empty() && tx.decoded.front().op == Op::WriteBits &&
            tx.decoded.front().count == 1 && tx.decoded.front().head == m_hbDevice;
        tx.adhoc = !tx.decoded.empty() && isWriteOp(tx.decoded.front().op) && !heartbeat;
    }
    if (m_snapByRound.empty()) {
        m_r.notes.push_back(
            "RPL-04 " + m_c.profile + " step " + m_step +
            ": skipped, the transcript holds no snapshot to rebuild the subscriptions from");
        return;
    }

    Expected<Session> created = Session::create(m_c.frame, cfg);
    if (!created) {
        failure("the Session cannot be created from run.meta: " +
                std::string(created.error().message));
        return;
    }
    m_session.emplace(std::move(created.value()));
    for (const auto& kv : m_snapByRound.begin()->second) {
        for (const Seg& s : kv.second) {
            subscribe(s);
        }
    }
    m_session->linkUp(0);
    drain();
    afterDrain();

    for (size_t i = 0; i < m_chunks.size() && !m_stop; ++i) {
        const Chunk& ch = m_chunks[i];
        if (ch.tx) {
            continue;
        }
        const TimeMs t = ms(ch.t);
        injectIdle(t);
        advanceTo(ch.seq);
        if (m_gotTx.size() < m_txList.size()) {
            const TxItem& next = m_txList[m_gotTx.size()];
            if (next.adhoc && next.chunkIndex == i + 1 && queuedBehind(next)) {
                submit(next, t);
            }
        }
        m_now = std::max(m_now, t);
        m_session->bytesIn(view(ch.bytes), m_now);
        drain();
        afterDrain();
    }
    const TimeMs tEnd = ms(endT);
    injectIdle(tEnd);
    advanceTo(INT_MAX);
    if (!m_stop) {
        compare();
    }
}

} // namespace

void checkSession(const Capture& c, Report& r) {
    if (!c.hasSessionFile) {
        r.failures.push_back(Failure{"RPL-04", c.profile, "",
                                     "session.vec is missing from the capture folder (the tool "
                                     "always writes it, empty when no poll step ran)"});
        return;
    }
    std::map<std::string, std::vector<Chunk>> chunks;
    std::map<std::string, std::vector<mc::test::Vector>> events;
    std::map<std::string, std::vector<mc::test::Vector>> inputs;
    std::vector<std::string> order;
    for (const mc::test::Vector& v : c.sessionVecs) {
        const std::string step = v.field("step");
        const std::string kind = v.field("kind");
        if (chunks.count(step) == 0 && events.count(step) == 0 && inputs.count(step) == 0) {
            order.push_back(step);
        }
        if (kind == "tx" || kind == "rx") {
            Chunk ch;
            ch.tx = kind == "tx";
            ch.t = toI64(v.field("t_ns"));
            ch.seq = static_cast<int>(toI64(v.field("seq")));
            ch.bytes = v.bytes;
            chunks[step].push_back(std::move(ch));
        } else if (kind == "event") {
            events[step].push_back(v);
        } else if (kind == "input") {
            inputs[step].push_back(v);
        }
    }
    // run.meta lists the poll steps the run made (key `polls`); each needs a transcript.
    const auto listed = c.meta.find("polls");
    if (listed != c.meta.end()) {
        for (const std::string& step : words(listed->second)) {
            if (std::find(order.begin(), order.end(), step) == order.end()) {
                r.failures.push_back(Failure{
                    "RPL-04", c.profile, step,
                    "run.meta lists this poll step but session.vec holds no transcript of it"});
            }
        }
    } else if (order.empty()) {
        r.notes.push_back("RPL-04 " + c.profile +
                          ": session.vec holds no poll transcript, nothing to replay (run.meta has "
                          "no `polls` key to say whether one was expected)");
    }
    for (const std::string& step : order) {
        PollReplay replay(c, step, chunks[step], events[step], inputs[step], r);
        replay.run();
    }
}

} // namespace mc::replay
