#include "mc/mock/mock_plc.h"

#include "mock/memory_image.h"
#include "mock/mock_internal.h"

#include <cstdio>
#include <deque>
#include <utility>

namespace mc {

namespace {
constexpr uint8_t kCr = 0x0D;
constexpr uint8_t kLf = 0x0A;
} // namespace

using detail::mock::DecodeResult;
using detail::mock::DecodeResult1e;
using detail::mock::E1Request;
using detail::mock::Fault;
using detail::mock::FrameStatus;
using detail::mock::Outcome;
using detail::mock::QnaRequest;
using detail::mock::SerialDecodeResult;
using detail::mock::SerialRequest;

struct MockPlc::Impl {
    struct PendingCorruption {
        Corruption mode;
        uint32_t remaining;
    };

    Impl(const FrameConfig& c, const MockOptions& o) : cfg(c), opt(o) {}

    void receive(ByteView bytes);
    void receiveSerial(ByteView bytes);
    void handle3e(const QnaRequest& request);
    void handle1e(const E1Request& request);
    void handleSerial(const SerialRequest& request);
    bool swallowsRequest();
    void queueResponse(ByteBuf response);
    void logJunk();
    void logSkipped();

    FrameConfig cfg;
    MockOptions opt;
    detail::mock::MemoryImage memory;
    std::vector<MockRequestRecord> log;
    uint32_t eot{0};
    uint64_t skipped{0};           // serial bytes dropped before a start byte or as unframable
    uint8_t eotTail{0};            // format 4: CR (2) and LF (1) still to come after an EOT
    std::vector<Fault> faults;
    bool muted{false};
    uint32_t muteCount{0};
    std::deque<PendingCorruption> corruptions;
    ByteBuf rx;                    // request bytes received but not yet framed
    bool inJunk{false};            // the last dropped bytes belong to a junk run already logged
    std::deque<ByteBuf> responses; // responses waiting for nextResponse()
    ByteBuf current;               // backing store of the view handed out by nextResponse()
};

void MockPlc::Impl::receive(ByteView bytes) {
    if (cfg.frame == FrameType::F3C || cfg.frame == FrameType::F1C) {
        receiveSerial(bytes);
        return;
    }
    // The other frame families (4E, 4C) have no server direction: no request is recognised, so
    // nothing is logged and nothing is answered.
    if (cfg.frame != FrameType::F3E && cfg.frame != FrameType::F1E) {
        return;
    }

    rx.insert(rx.end(), bytes.data, bytes.data + bytes.size);
    size_t pos = 0;
    while (pos < rx.size()) {
        const ByteView pending{rx.data() + pos, rx.size() - pos};
        FrameStatus status = FrameStatus::NeedMore;
        size_t consumed = 0;
        if (cfg.frame == FrameType::F3E) {
            DecodeResult r = detail::mock::decode3eRequest(cfg.code, pending);
            status = r.status;
            consumed = r.consumed;
            if (status == FrameStatus::Complete) {
                inJunk = false;
                handle3e(r.request);
            }
        } else {
            DecodeResult1e r = detail::mock::decode1eRequest(cfg.code, pending);
            status = r.status;
            consumed = r.consumed;
            if (status == FrameStatus::Complete) {
                inJunk = false;
                handle1e(r.request);
            }
        }
        if (status == FrameStatus::NeedMore) {
            break;
        }
        pos += consumed;
        if (status == FrameStatus::Junk) {
            logJunk();
        }
    }
    rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(pos));
}

// Serial: bytes before the start byte are skipped and counted (spec §6.3), and an EOT cancels the
// partial request and is counted. In format 4 the CR LF of an EOT CR LF belongs to the EOT, also when
// it arrives in a later call.
void MockPlc::Impl::receiveSerial(ByteView bytes) {
    rx.insert(rx.end(), bytes.data, bytes.data + bytes.size);
    size_t pos = 0;
    while (pos < rx.size()) {
        if (eotTail > 0) {
            const uint8_t expected = eotTail == 2 ? kCr : kLf;
            if (rx[pos] == expected) {
                --eotTail;
                ++pos;
                continue;
            }
            eotTail = 0;
        }
        SerialDecodeResult r =
            detail::mock::decodeSerialRequest(cfg, ByteView{rx.data() + pos, rx.size() - pos});
        if (r.status == FrameStatus::NeedMore) {
            break;
        }
        pos += r.consumed;
        if (r.status == FrameStatus::Eot) {
            ++eot;
            eotTail = cfg.format == SerialFormat::Format4 ? 2 : 0;
        } else if (r.status == FrameStatus::Complete) {
            handleSerial(r.request);
        } else if (r.status == FrameStatus::Junk) {
            skipped += r.consumed;
            logSkipped();
        }
    }
    rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(pos));
}

// One Trace line per skipped serial byte, with the running count.
void MockPlc::Impl::logSkipped() {
    if (opt.log == nullptr || !opt.log->enabled(LogLevel::Trace)) {
        return;
    }
    char text[64];
    std::snprintf(text, sizeof text, "serial byte skipped; %llu in all",
                  static_cast<unsigned long long>(skipped));
    opt.log->write(LogLevel::Trace, "mc.mock", text);
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
        if (cfg.frame == FrameType::F1E) {
            detail::mock::corrupt1eResponse(next.mode, cfg.code, response);
        } else if (cfg.frame == FrameType::F3C || cfg.frame == FrameType::F1C) {
            detail::mock::corruptSerialResponse(cfg, next.mode, response);
        } else {
            detail::mock::corrupt3eResponse(next.mode, cfg.code, response);
        }
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

void MockPlc::Impl::handle1e(const E1Request& request) {
    MockRequestRecord rec;
    rec.frame = cfg.frame;
    rec.op = request.op;
    rec.head = request.head;
    rec.count = request.count;
    if (swallowsRequest()) {
        log.push_back(rec);
        return;
    }

    Outcome outcome = detail::mock::executeE1(request, memory, faults, opt);
    rec.answered = true;
    if (!outcome.ok) {
        Error e;
        e.category = ErrorCategory::Plc;
        e.code = ErrorCode::PlcError;
        e.plcCode = outcome.plcCode;
        if (outcome.plcCode == detail::mock::kE1EndCodeWithAbnormal) {
            e.abnormalCode = outcome.abnormal; // only this end code carries one on the wire
        }
        e.message = "mock PLC error response";
        rec.answeredWith = e;
    }
    log.push_back(rec);

    queueResponse(detail::mock::build1eResponse(cfg.code, request, outcome));
}

// Multidrop (spec "Serial reception"): a request for another station gets no answer, and does not
// use up a mute. Then the SUM is judged, then the command is executed.
void MockPlc::Impl::handleSerial(const SerialRequest& request) {
    MockRequestRecord rec;
    rec.frame = cfg.frame;
    rec.op = request.op;
    rec.head = request.head;
    rec.count = request.count;
    rec.series = request.series;
    if (request.station != cfg.stationNo || swallowsRequest()) {
        log.push_back(rec);
        return;
    }

    const bool threeC = cfg.frame == FrameType::F3C;
    Outcome outcome;
    if (!request.sumValid) {
        outcome.ok = false;
        outcome.plcCode = threeC ? opt.sumErrorQna : opt.sumError1c;
    } else {
        outcome = detail::mock::executeSerial(request, cfg.frame, memory, faults, opt);
    }
    rec.answered = true;
    if (!outcome.ok) {
        Error e;
        e.category = ErrorCategory::Plc;
        e.code = ErrorCode::PlcError;
        e.plcCode = outcome.plcCode;
        e.message = "mock PLC error response";
        rec.answeredWith = e;
    }
    log.push_back(rec);

    queueResponse(detail::mock::buildSerialResponse(cfg, request, outcome));
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

uint64_t MockPlc::skippedBytes() const { return m_impl->skipped; }

void MockPlc::clearLog() {
    m_impl->log.clear();
    m_impl->skipped = 0;
}

} // namespace mc
