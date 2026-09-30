#include "mc/mock/mock_plc.h"

#include "mock/memory_image.h"
#include "mock/mock_internal.h"

#include <deque>
#include <utility>

namespace mc {

using detail::mock::DecodeResult;
using detail::mock::Fault;
using detail::mock::FrameStatus;
using detail::mock::Outcome;
using detail::mock::QnaRequest;

struct MockPlc::Impl {
    struct PendingCorruption {
        Corruption mode;
        uint32_t remaining;
    };

    Impl(const FrameConfig& c, const MockOptions& o) : cfg(c), opt(o) {}

    void receive(ByteView bytes);
    void handle3e(const QnaRequest& request);
    bool swallowsRequest();
    void queueResponse(ByteBuf response);
    void logJunk();

    FrameConfig cfg;
    MockOptions opt;
    detail::mock::MemoryImage memory;
    std::vector<MockRequestRecord> log;
    uint32_t eot{0};
    std::vector<Fault> faults;
    bool muted{false};
    uint32_t muteCount{0};
    std::deque<PendingCorruption> corruptions;
    ByteBuf rx; // request bytes received but not yet framed
    bool inJunk{false}; // the last dropped bytes belong to a junk run already logged
    std::deque<ByteBuf> responses; // responses waiting for nextResponse()
    ByteBuf current; // backing store of the view handed out by nextResponse()
};

void MockPlc::Impl::receive(ByteView bytes) {
    // Only the 3E server direction exists: for any other frame family no request is recognised,
    // so nothing is logged and nothing is answered.
    if (cfg.frame != FrameType::F3E) {
        return;
    }

    rx.insert(rx.end(), bytes.data, bytes.data + bytes.size);
    size_t pos = 0;
    while (pos < rx.size()) {
        DecodeResult r = detail::mock::decode3eRequest(cfg.code, ByteView{rx.data() + pos,
                                                                          rx.size() - pos});
        if (r.status == FrameStatus::NeedMore) {
            break;
        }
        pos += r.consumed;
        if (r.status == FrameStatus::Junk) {
            logJunk();
            continue;
        }
        inJunk = false;
        handle3e(r.request);
    }
    rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(pos));
}

// Bytes that cannot start a request are dropped one at a time; a run of them is one log record,
// so the log does not depend on how the stream was fragmented.
void MockPlc::Impl::logJunk() {
    if (inJunk) {
        return;
    }
    inJunk = true;
    MockRequestRecord rec;
    rec.frame = cfg.frame;
    log.push_back(rec);
}

// A muted request is decoded and logged like any other but neither executed nor answered.
bool MockPlc::Impl::swallowsRequest() {
    if (muteCount > 0) {
        --muteCount;
        return true;
    }
    return muted;
}

// The next pending corruption damages this response; `remaining` counts responses, so a request
// that was swallowed does not use one up.
void MockPlc::Impl::queueResponse(ByteBuf response) {
    if (!corruptions.empty()) {
        PendingCorruption& next = corruptions.front();
        detail::mock::corrupt3eResponse(next.mode, cfg.code, response);
        if (--next.remaining == 0) {
            corruptions.pop_front();
        }
    }
    responses.push_back(std::move(response));
}

void MockPlc::Impl::handle3e(const QnaRequest& request) {
    MockRequestRecord rec;
    rec.frame = cfg.frame;
    rec.op = request.op;
    rec.head = request.head;
    rec.count = request.count;
    rec.series = request.series;
    if (swallowsRequest()) {
        log.push_back(rec);
        return;
    }

    Outcome outcome = detail::mock::executeQna(request, cfg.code, memory, faults, opt);
    rec.answered = true;
    if (!outcome.ok) {
        Error e;
        e.category = ErrorCategory::Plc;
        e.code = ErrorCode::PlcError;
        e.plcCode = outcome.plcCode;
        e.info.network = request.route.network;
        e.info.pc = request.route.pc;
        e.info.io = request.route.io;
        e.info.station = request.route.station;
        e.info.command = request.command;
        e.info.subcommand = request.subcommand;
        e.message = "mock PLC error response";
        rec.answeredWith = e;
    }
    log.push_back(rec);

    queueResponse(detail::mock::build3eResponse(cfg.code, request, outcome));
}

MockPlc::MockPlc(const FrameConfig& cfg, const MockOptions& opt)
    : m_impl(std::make_unique<Impl>(cfg, opt)) {}

MockPlc::~MockPlc() = default;
MockPlc::MockPlc(MockPlc&& other) noexcept = default;
MockPlc& MockPlc::operator=(MockPlc&& other) noexcept = default;

void MockPlc::bytesIn(ByteView bytes) { m_impl->receive(bytes); }

bool MockPlc::nextResponse(ByteView& out) {
    if (m_impl->responses.empty()) {
        return false;
    }
    m_impl->current = std::move(m_impl->responses.front());
    m_impl->responses.pop_front();
    out = ByteView{m_impl->current.data(), m_impl->current.size()};
    return true;
}

void MockPlc::setWord(Device d, uint16_t v) { m_impl->memory.setWordAt(d, 0, v); }

void MockPlc::setWords(Device head, std::initializer_list<uint16_t> values) {
    uint64_t k = 0;
    for (uint16_t v : values) {
        m_impl->memory.setWordAt(head, k++, v);
    }
}

void MockPlc::setBit(Device d, bool v) { m_impl->memory.setBitAt(d, 0, v); }

void MockPlc::setBits(Device head, std::initializer_list<bool> values) {
    uint64_t i = 0;
    for (bool v : values) {
        m_impl->memory.setBitAt(head, i++, v);
    }
}

uint16_t MockPlc::word(Device d) const { return m_impl->memory.wordAt(d, 0); }

bool MockPlc::bit(Device d) const { return m_impl->memory.bitAt(d, 0); }

void MockPlc::setDeviceLimit(DeviceType t, uint32_t limit) { m_impl->memory.setLimit(t, limit); }

void MockPlc::failRange(DeviceType t, uint32_t first, uint32_t last, uint16_t code,
                        uint8_t abnormal) {
    m_impl->faults.push_back(Fault{t, first, last, code, abnormal});
}

void MockPlc::clearFaults() {
    m_impl->faults.clear();
    m_impl->muted = false;
    m_impl->muteCount = 0;
    m_impl->corruptions.clear();
}

void MockPlc::mute(bool on) { m_impl->muted = on; }

void MockPlc::muteNext(uint32_t n) { m_impl->muteCount = n; }

void MockPlc::corruptNext(Corruption c, uint32_t n) {
    if (n > 0) {
        m_impl->corruptions.push_back(Impl::PendingCorruption{c, n});
    }
}

const std::vector<MockRequestRecord>& MockPlc::requests() const { return m_impl->log; }

uint32_t MockPlc::eotCount() const { return m_impl->eot; }

void MockPlc::clearLog() { m_impl->log.clear(); }

} // namespace mc
