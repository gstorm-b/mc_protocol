#include "replay.h"
#include "replay_util.h"

#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace mc::replay {

using namespace util;

std::string Failure::text() const {
    std::string t = check + " " + profile;
    if (!step.empty()) {
        t += " step " + step;
    }
    return t + ": " + message;
}

std::vector<Failure> Report::of(const std::string& check) const {
    std::vector<Failure> out;
    for (const Failure& f : failures) {
        if (f.check == check) {
            out.push_back(f);
        }
    }
    return out;
}

namespace {

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error(file.string() + ": cannot open");
    }
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

[[noreturn]] void bad(const fs::path& file, const std::string& why) {
    throw std::runtime_error(file.string() + ": " + why);
}

std::string need(const std::map<std::string, std::string>& meta, const std::string& key,
                 const fs::path& file) {
    const auto it = meta.find(key);
    if (it == meta.end()) {
        bad(file, "missing key '" + key + "'");
    }
    return it->second;
}

uint32_t needNumber(const std::map<std::string, std::string>& meta, const std::string& key,
                    const fs::path& file) {
    const auto n = number(need(meta, key, file), 10);
    if (!n) {
        bad(file, "key '" + key + "' is not a number: '" + need(meta, key, file) + "'");
    }
    return *n;
}

bool needBool(const std::map<std::string, std::string>& meta, const std::string& key,
              const fs::path& file) {
    const std::string& v = need(meta, key, file);
    if (v == "true") {
        return true;
    }
    if (v == "false") {
        return false;
    }
    bad(file, "key '" + key + "' is not true or false: '" + v + "'");
}

template <typename E>
E needEnum(const std::map<std::string, std::string>& meta, const std::string& key,
           const fs::path& file, const std::vector<std::pair<const char*, E>>& names) {
    const std::string& v = need(meta, key, file);
    for (const auto& n : names) {
        if (v == n.first) {
            return n.second;
        }
    }
    bad(file, "key '" + key + "' has an unknown value '" + v + "'");
}

FrameConfig frameFromMeta(const std::map<std::string, std::string>& m, const fs::path& file) {
    FrameConfig f;
    f.frame = needEnum<FrameType>(m, "frame.frame", file,
                                  {{"3E", FrameType::F3E},
                                   {"1E", FrameType::F1E},
                                   {"3C", FrameType::F3C},
                                   {"1C", FrameType::F1C}});
    f.code = needEnum<DataCode>(m, "frame.code", file,
                                {{"Binary", DataCode::Binary}, {"Ascii", DataCode::Ascii}});
    f.network = static_cast<uint8_t>(needNumber(m, "frame.network", file));
    f.pc = static_cast<uint8_t>(needNumber(m, "frame.pc", file));
    f.io = static_cast<uint16_t>(needNumber(m, "frame.io", file));
    f.station = static_cast<uint8_t>(needNumber(m, "frame.station", file));
    f.monitoringTimer = static_cast<uint16_t>(needNumber(m, "frame.monitoringTimer", file));
    f.series = needEnum<PlcSeries>(m, "frame.series", file,
                                   {{"QL", PlcSeries::QL}, {"IqR", PlcSeries::IqR}});
    f.checkRoute = needBool(m, "frame.checkRoute", file);
    f.serialStart = static_cast<uint16_t>(needNumber(m, "frame.serialStart", file));
    f.format = needEnum<SerialFormat>(m, "frame.format", file,
                                      {{"Format1", SerialFormat::Format1},
                                       {"Format2", SerialFormat::Format2},
                                       {"Format3", SerialFormat::Format3},
                                       {"Format4", SerialFormat::Format4}});
    f.stationNo = static_cast<uint8_t>(needNumber(m, "frame.stationNo", file));
    f.selfStation = static_cast<uint8_t>(needNumber(m, "frame.selfStation", file));
    f.sumCheck = needBool(m, "frame.sumCheck", file);
    f.blockNo = static_cast<uint8_t>(needNumber(m, "frame.blockNo", file));
    f.checkBlockNo = needBool(m, "frame.checkBlockNo", file);
    f.sendEotOnError = needBool(m, "frame.sendEotOnError", file);
    f.f3ShortResponseHasSum = needBool(m, "frame.f3ShortResponseHasSum", file);
    f.messageWait = static_cast<uint8_t>(needNumber(m, "frame.messageWait", file));
    f.commandSet = needEnum<C1CommandSet>(
        m, "frame.commandSet", file, {{"ACPU", C1CommandSet::ACPU}, {"AnA", C1CommandSet::AnA}});
    f.e1AliasLS = needBool(m, "frame.e1AliasLS", file);
    f.targetFamily = needEnum<TargetFamily>(
        m, "frame.targetFamily", file,
        {{"IqR_Q_L", TargetFamily::IqR_Q_L}, {"QnA", TargetFamily::QnA}, {"A", TargetFamily::A}});
    f.highPerformanceQcpu = needBool(m, "frame.highPerformanceQcpu", file);
    f.aSeriesTarget = needBool(m, "frame.aSeriesTarget", file);
    f.splitWrites = needBool(m, "frame.splitWrites", file);
    f.timeoutMs = needNumber(m, "frame.timeoutMs", file);
    f.readRetries = static_cast<uint8_t>(needNumber(m, "frame.readRetries", file));
    return f;
}

SessionConfig sessionFromMeta(const std::map<std::string, std::string>& m, const fs::path& file) {
    SessionConfig s;
    s.cycleIntervalMs = needNumber(m, "session.cycleIntervalMs", file);
    s.cycleMode = needEnum<CycleMode>(
        m, "session.cycleMode", file,
        {{"FixedRate", CycleMode::FixedRate}, {"FixedDelay", CycleMode::FixedDelay}});
    s.plan.bitsAsWords = needBool(m, "session.bitsAsWords", file);
    const std::string& gap = need(m, "session.maxGap", file);
    if (gap == "auto") {
        s.plan.maxGap = kAutoGap;
    } else {
        s.plan.maxGap = needNumber(m, "session.maxGap", file);
    }
    s.adHocCapacity = static_cast<uint16_t>(needNumber(m, "session.adHocCapacity", file));
    s.adHocArenaBytes = needNumber(m, "session.adHocArenaBytes", file);
    s.maxAdHocBurst = static_cast<uint8_t>(needNumber(m, "session.maxAdHocBurst", file));
    s.maxConsecutiveLinkErrors =
        static_cast<uint8_t>(needNumber(m, "session.maxConsecutiveLinkErrors", file));
    s.serialInterCharMs = static_cast<uint16_t>(needNumber(m, "session.serialInterCharMs", file));
    s.serialFlushMs = static_cast<uint16_t>(needNumber(m, "session.serialFlushMs", file));
    s.heartbeat.enabled = needBool(m, "session.heartbeat.enabled", file);
    const auto dev = parseDevice(need(m, "session.heartbeat.device", file));
    if (!dev) {
        bad(file, "key 'session.heartbeat.device' is not a device: '" +
                      need(m, "session.heartbeat.device", file) + "'");
    }
    s.heartbeat.device = dev.value();
    return s;
}

/// "E-02.2" and "E-06+2" -> "E-02" and "E-06".
std::string baseStep(std::string id) {
    size_t plus = id.rfind('+');
    if (plus != std::string::npos && plus + 1 < id.size() && number(id.substr(plus + 1), 10)) {
        id.erase(plus);
    }
    size_t dot = id.rfind('.');
    if (dot != std::string::npos && dot + 1 < id.size() && number(id.substr(dot + 1), 10)) {
        id.erase(dot);
    }
    return id;
}

} // namespace

Capture loadCapture(const fs::path& dir) {
    Capture c;
    c.dir = dir;

    // run.meta
    const fs::path metaFile = dir / "run.meta";
    {
        std::istringstream in(readText(metaFile));
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                bad(metaFile, "line without a colon: '" + line + "'");
            }
            c.meta[line.substr(0, colon)] = trim(line.substr(colon + 1));
        }
    }
    c.profile = need(c.meta, "profile", metaFile);
    c.transport = need(c.meta, "transport", metaFile);
    c.frame = frameFromMeta(c.meta, metaFile);
    c.session = sessionFromMeta(c.meta, metaFile);
    for (const std::string& tok : words(need(c.meta, "device_end", metaFile))) {
        const size_t eq = tok.find('=');
        const auto type = eq == std::string::npos ? std::nullopt : typeBySymbol(tok.substr(0, eq));
        if (!type) {
            bad(metaFile, "device_end entry '" + tok + "' is not <symbol>=<number>");
        }
        const auto n = number(tok.substr(eq + 1), deviceInfo(*type).radix == Radix::Hex ? 16 : 10);
        if (!n) {
            bad(metaFile, "device_end entry '" + tok + "' has a bad number");
        }
        c.deviceEnd.emplace_back(*type, *n);
    }
    for (const auto& kv : c.meta) {
        if (startsWith(kv.first, "recovery.")) {
            const std::string text = kv.second;
            const size_t after = text.find("after ");
            const size_t colon = text.find(':', after == std::string::npos ? 0 : after);
            if (after != std::string::npos && colon != std::string::npos) {
                c.recoveries.emplace_back(text.substr(after + 6, colon - (after + 6)), text);
            }
        }
    }

    // steps.vec
    const fs::path stepsFile = dir / "steps.vec";
    const std::vector<mc::test::Vector> vecs = mc::test::loadVectors(stepsFile);
    std::map<std::string, size_t> byVecId;
    for (size_t i = 0; i < vecs.size(); ++i) {
        const mc::test::Vector& v = vecs[i];
        const std::string kind = v.field("kind");
        if (kind == "request") {
            Record r;
            r.id = v.field("step");
            r.step = baseStep(r.id);
            r.via = v.field("via");
            r.op = v.field("op");
            r.device = v.field("device");
            r.count = static_cast<int>(
                number(v.field("count").empty() ? "0" : v.field("count"), 10).value_or(0));
            r.mirrors = v.field("mirrors");
            r.frame = v.field("frame");
            r.code = v.field("code");
            r.format = static_cast<int>(
                number(v.field("format").empty() ? "0" : v.field("format"), 10).value_or(0));
            r.overrideText = v.field("override");
            r.request = v.bytes;
            r.outcome = v.field("outcome");
            r.expect = v.field("expect");
            r.verdict = v.field("verdict");
            byVecId[v.id] = c.records.size();
            c.records.push_back(std::move(r));
        } else if (kind == "response" || kind == "response-partial") {
            const std::string of = v.field("of");
            const auto it = byVecId.find(of);
            if (it == byVecId.end()) {
                bad(stepsFile, "line " + std::to_string(v.line) + ": response '" + v.id +
                                   "' belongs to no request ('of: " + of + "')");
            }
            Record* owner = &c.records[it->second];
            owner->response = v.bytes;
            owner->hasResponse = true;
            owner->partial = kind == "response-partial";
            owner->outcome = v.field("outcome");
            owner->expect = v.field("expect");
            owner->verdict = v.field("verdict");
        } else {
            bad(stepsFile, "line " + std::to_string(v.line) + ": unknown kind '" + kind + "'");
        }
    }

    // session.vec: the tool always writes it (empty when no poll step ran); RPL-04 reports a
    // missing one
    const fs::path sessionFile = dir / "session.vec";
    c.hasSessionFile = fs::exists(sessionFile);
    if (c.hasSessionFile) {
        c.sessionVecs = mc::test::loadVectors(sessionFile);
    }

    // divergences.txt: "<step id> <FINDINGS id>" per line, '#' comments
    const fs::path divFile = dir / "divergences.txt";
    if (fs::exists(divFile)) {
        std::istringstream in(readText(divFile));
        std::string line;
        while (std::getline(in, line)) {
            const size_t hash = line.find('#');
            if (hash != std::string::npos) {
                line.erase(hash);
            }
            const std::vector<std::string> w = words(line);
            if (w.empty()) {
                continue;
            }
            c.divergences.emplace_back(w[0], w.size() > 1 ? w[1] : std::string());
        }
    }
    return c;
}

/// A folder is a capture when it holds any file the tool writes. A folder with steps.vec but no
/// run.meta is a damaged capture, and loading it reports the missing run.meta instead of skipping
/// it silently.
static bool looksLikeCapture(const fs::path& dir) {
    for (const char* name :
         {"run.meta", "steps.vec", "session.vec", "bench.csv", "divergences.txt"}) {
        if (fs::exists(dir / name)) {
            return true;
        }
    }
    return false;
}

std::vector<fs::path> captureFolders(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return out;
    }
    for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
        if (e.is_directory() && looksLikeCapture(e.path())) {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

namespace {

void fail(Report& r, const char* check, const Capture& c, const std::string& step,
          const std::string& message) {
    r.failures.push_back(Failure{check, c.profile, step, message});
}

std::string errorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok:
        return "Ok";
    case ErrorCode::InvalidConfig:
        return "InvalidConfig";
    case ErrorCode::NotSubscribed:
        return "NotSubscribed";
    case ErrorCode::InvalidDevice:
        return "InvalidDevice";
    case ErrorCode::PointCount:
        return "PointCount";
    case ErrorCode::UnsupportedCommand:
        return "UnsupportedCommand";
    case ErrorCode::DataSizeMismatch:
        return "DataSizeMismatch";
    case ErrorCode::BufferTooSmall:
        return "BufferTooSmall";
    case ErrorCode::Timeout:
        return "Timeout";
    case ErrorCode::LinkDown:
        return "LinkDown";
    case ErrorCode::QueueFull:
        return "QueueFull";
    case ErrorCode::FrameMismatch:
        return "FrameMismatch";
    case ErrorCode::LengthMismatch:
        return "LengthMismatch";
    case ErrorCode::SumCheck:
        return "SumCheck";
    case ErrorCode::InvalidCharacter:
        return "InvalidCharacter";
    case ErrorCode::PlcError:
        return "PlcError";
    }
    return "?";
}

/// The head device of a record's metadata; nullopt when it does not parse.
std::optional<Device> recordHead(const Record& rec) {
    const auto d = parseDevice(rec.device);
    if (!d) {
        return std::nullopt;
    }
    return d.value();
}

/// The Request a record's metadata describes (op, head, count), data left empty.
std::optional<Request> metaRequest(const Record& rec) {
    const auto op = opByName(rec.op);
    const auto head = recordHead(rec);
    if (!op || !head || rec.count <= 0) {
        return std::nullopt;
    }
    Request r;
    r.op = *op;
    r.head = *head;
    r.count = static_cast<uint16_t>(rec.count);
    return r;
}

/// The normalized payload of a response to @p req, or nullopt (with @p why) when it is not an ok
/// frame.
std::optional<std::vector<uint8_t>> payloadOf(const FrameConfig& cfg, const Request& req,
                                              const std::vector<uint8_t>& bytes, std::string* why) {
    const McProtocol proto(cfg);
    Parser p = proto.parser(req);
    const ParseStatus st = p.feed(view(bytes));
    if (st != ParseStatus::Done) {
        if (why != nullptr) {
            *why = st == ParseStatus::NeedMore
                       ? "the response is not a complete frame"
                       : "the response is an error: " + errorCodeName(p.error().code);
        }
        return std::nullopt;
    }
    std::vector<uint8_t> out(proto.payloadSize(req));
    const Expected<size_t> n = p.payload(view(bytes), MutableByteView{out.data(), out.size()});
    if (!n) {
        if (why != nullptr) {
            *why = "the payload does not decode: " + errorCodeName(n.error().code);
        }
        return std::nullopt;
    }
    out.resize(n.value());
    return out;
}

// ---------------------------------------------------------------------------------------------

struct OutcomeText {
    std::string kind; ///< ok, plcError, protocolError, timeout, notSent, transportError.
    std::optional<uint32_t> plcCode;
    std::optional<uint32_t> abnormal;
    std::vector<uint32_t> info; ///< net, pc, io, station, command, subcommand
    std::string name;           ///< protocolError ErrorCode name.
};

OutcomeText parseOutcome(const std::string& text) {
    OutcomeText o;
    const std::vector<std::string> w = words(text);
    if (w.empty()) {
        return o;
    }
    o.kind = w[0];
    for (size_t i = 1; i < w.size(); ++i) {
        if (o.kind == "plcError" && i == 1) {
            o.plcCode = number(w[i], 16);
        } else if (w[i] == "abnormal" && i + 1 < w.size()) {
            o.abnormal = number(w[++i], 16);
        } else if (w[i] == "info" && i + 1 < w.size()) {
            std::string part;
            std::istringstream in(w[++i]);
            while (std::getline(in, part, '/')) {
                o.info.push_back(number(part, 16).value_or(0xFFFFFFFFu));
            }
        } else if (o.kind == "protocolError" && i == 1) {
            o.name = w[i];
        }
    }
    return o;
}

/// "words 1 2 3" / "bits 1 0" -> the numbers; empty when the expectation names no values.
std::optional<std::vector<uint32_t>> expectedValues(const std::string& expect, bool* bits) {
    const std::vector<std::string> w = words(expect);
    if (w.empty() || (w[0] != "words" && w[0] != "bits")) {
        return std::nullopt;
    }
    *bits = w[0] == "bits";
    std::vector<uint32_t> v;
    for (size_t i = 1; i < w.size(); ++i) {
        const auto n = number(w[i], *bits ? 10 : 16);
        if (!n) {
            break; // valuesFrom, frames, ...
        }
        v.push_back(*n);
    }
    return v;
}

} // namespace

// ---- RPL-01 ---------------------------------------------------------------------------------

void checkReencode(const Capture& c, Report& r) {
    const McProtocol proto(c.frame);
    for (const Record& rec : c.records) {
        if (rec.via != "api") {
            continue;
        }
        if (!rec.overrideText.empty()) {
            r.notes.push_back("RPL-01 " + c.profile + " step " + rec.id +
                              ": skipped, the step ran with another FrameConfig (" +
                              rec.overrideText + ")");
            continue;
        }
        ++r.checks;
        const std::string frameName = c.meta.at("frame.frame");
        if (rec.frame != frameName || rec.code != c.meta.at("frame.code")) {
            fail(r, "RPL-01", c, rec.id,
                 "the record says frame " + rec.frame + " " + rec.code + " but run.meta says " +
                     frameName + " " + c.meta.at("frame.code"));
            continue;
        }
        const auto meta = metaRequest(rec);
        if (!meta) {
            fail(r, "RPL-01", c, rec.id,
                 "the metadata (op '" + rec.op + "', device '" + rec.device + "', count " +
                     std::to_string(rec.count) + ") does not describe a request");
            continue;
        }
        const std::vector<Decoded> dec = decode(c.frame, rec.request);
        if (dec.empty()) {
            fail(r, "RPL-01", c, rec.id,
                 "the captured request is not a frame the mock can decode with the run.meta "
                 "FrameConfig: " +
                     hex(rec.request));
            continue;
        }
        const Decoded& d = dec.front();
        if (d.op != meta->op || d.head != meta->head || d.count != meta->count) {
            fail(r, "RPL-01", c, rec.id,
                 "the captured bytes ask for " + std::to_string(static_cast<int>(d.op)) + " " +
                     deviceText(d.head) + " x" + std::to_string(d.count) + ", the metadata says " +
                     rec.op + " " + rec.device + " x" + std::to_string(rec.count));
            continue;
        }
        Request req = *meta;
        if (isWriteOp(req.op)) {
            req = toRequest(d);
        }
        const Expected<ByteBuf> encoded = proto.encode(req);
        if (!encoded) {
            fail(r, "RPL-01", c, rec.id,
                 "the encoder refuses the request: " + errorCodeName(encoded.error().code));
            continue;
        }
        if (encoded.value() != rec.request) {
            fail(r, "RPL-01", c, rec.id,
                 "the encoder output differs from the captured request, " +
                     describeDifference(rec.request, encoded.value(), "encoder"));
        }
    }
}

// ---- RPL-02 ---------------------------------------------------------------------------------

void checkParse(const Capture& c, Report& r) {
    const McProtocol proto(c.frame);
    for (const Record& rec : c.records) {
        const bool met = rec.verdict.empty() || rec.verdict == "passed";
        if (!met) {
            ++r.checks;
            fail(r, "RPL-02", c, rec.id,
                 "the capture records verdict " + rec.verdict +
                     ": the step did not meet its expectation (a finding: tag it in "
                     "divergences.txt once FINDINGS.md has its entry)");
        }
        if (!rec.hasResponse || rec.partial || rec.op == "Raw") {
            continue;
        }
        const auto req = metaRequest(rec);
        if (!req) {
            fail(r, "RPL-02", c, rec.id,
                 "the metadata (op '" + rec.op + "', device '" + rec.device +
                     "') does not describe a request to parse the response with");
            continue;
        }
        ++r.checks;
        Parser p = proto.parser(*req);
        const ParseStatus st = p.feed(view(rec.response));
        const OutcomeText want = parseOutcome(rec.outcome);
        if (want.kind == "ok") {
            if (st != ParseStatus::Done) {
                fail(r, "RPL-02", c, rec.id,
                     std::string("recorded outcome ok, the parser says ") +
                         (st == ParseStatus::NeedMore
                              ? "need more bytes"
                              : "failed with " + errorCodeName(p.error().code)));
                continue;
            }
            if (rec.via == "api" && p.frameLength() != rec.response.size()) {
                fail(r, "RPL-02", c, rec.id,
                     "the response holds " + std::to_string(rec.response.size()) +
                         " bytes but its frame is " + std::to_string(p.frameLength()));
                continue;
            }
            bool bitValues = false;
            const auto values = expectedValues(rec.expect, &bitValues);
            if (values && met) { // a step that missed its expectation carries values that differ by
                                 // definition
                std::string why;
                const auto payload = payloadOf(c.frame, *req, rec.response, &why);
                if (!payload) {
                    fail(r, "RPL-02", c, rec.id, why);
                    continue;
                }
                std::vector<uint32_t> got;
                if (isBitOp(req->op)) {
                    for (const uint8_t b : *payload) {
                        got.push_back(b);
                    }
                } else {
                    for (size_t i = 0; i + 1 < payload->size(); i += 2) {
                        got.push_back(static_cast<uint32_t>((*payload)[i]) |
                                      (static_cast<uint32_t>((*payload)[i + 1]) << 8));
                    }
                }
                if (got != *values) {
                    fail(r, "RPL-02", c, rec.id,
                         "the parsed values differ from the recorded ones (" +
                             std::to_string(got.size()) + " parsed, " +
                             std::to_string(values->size()) + " recorded)");
                }
            }
        } else if (want.kind == "plcError") {
            if (st != ParseStatus::Failed || p.error().category != ErrorCategory::Plc) {
                fail(r, "RPL-02", c, rec.id,
                     std::string("recorded outcome plcError, the parser says ") +
                         (st == ParseStatus::Done ? "ok"
                          : st == ParseStatus::NeedMore
                              ? "need more bytes"
                              : "a " + errorCodeName(p.error().code) + " error"));
                continue;
            }
            const Error& e = p.error();
            if (want.plcCode && *want.plcCode != e.plcCode) {
                fail(r, "RPL-02", c, rec.id,
                     "the PLC error code differs: recorded " + std::to_string(*want.plcCode) +
                         ", parsed " + std::to_string(e.plcCode) + " (decimal)");
            } else if (want.abnormal && *want.abnormal != e.abnormalCode) {
                fail(r, "RPL-02", c, rec.id, "the abnormal code differs from the recorded one");
            } else if (want.info.size() == 6 &&
                       (want.info[0] != e.info.network || want.info[1] != e.info.pc ||
                        want.info[2] != e.info.io || want.info[3] != e.info.station ||
                        want.info[4] != e.info.command || want.info[5] != e.info.subcommand)) {
                fail(r, "RPL-02", c, rec.id,
                     "the error information block differs from the recorded one");
            }
        } else if (want.kind == "protocolError") {
            if (st != ParseStatus::Failed || p.error().category != ErrorCategory::Protocol) {
                fail(r, "RPL-02", c, rec.id,
                     std::string("recorded outcome protocolError, the parser says ") +
                         (st == ParseStatus::Done ? "ok"
                          : st == ParseStatus::NeedMore
                              ? "need more bytes"
                              : "a " + errorCodeName(p.error().code) + " error"));
            } else if (!want.name.empty() && want.name != errorCodeName(p.error().code)) {
                fail(r, "RPL-02", c, rec.id,
                     "the protocol error differs: recorded " + want.name + ", parsed " +
                         errorCodeName(p.error().code));
            }
        }
        // other outcomes (timeout, transportError ...) carry no complete response to parse
    }
}

// ---- RPL-03 ---------------------------------------------------------------------------------

namespace {

using Point = std::pair<int, uint32_t>;

void forEachPoint(const Decoded& d, const std::function<void(const Point&)>& fn) {
    const uint32_t n = pointsTouched(d);
    for (uint32_t i = 0; i < n; ++i) {
        fn(Point{static_cast<int>(d.head.type), d.head.number + i});
    }
}

std::unique_ptr<MockPlc> makeMock(const Capture& c) {
    auto m = std::make_unique<MockPlc>(c.frame);
    for (const auto& end : c.deviceEnd) {
        m->setDeviceLimit(end.first, end.second + 1);
    }
    return m;
}

/// Seeds the mock's memory from the payload of a captured read, for the points the run did not
/// write.
void seed(MockPlc& mock, const Decoded& d, const std::vector<uint8_t>& payload,
          const std::set<Point>& owned, const std::set<Point>& polled) {
    auto free = [&](uint32_t number) {
        const Point p{static_cast<int>(d.head.type), number};
        return owned.count(p) == 0 || polled.count(p) != 0;
    };
    if (d.op == Op::ReadBits) {
        for (uint32_t i = 0; i < d.count && i < payload.size(); ++i) {
            if (free(d.head.number + i)) {
                mock.setBit(Device{d.head.type, d.head.number + i}, payload[i] != 0);
            }
        }
    } else if (d.op == Op::ReadWords) {
        const bool bitDevice = isBitDevice(d.head.type);
        for (uint32_t k = 0; k < d.count && 2 * k + 1 < payload.size(); ++k) {
            const uint16_t w = static_cast<uint16_t>(payload[2 * k] | (payload[2 * k + 1] << 8));
            if (!bitDevice) {
                if (free(d.head.number + k)) {
                    mock.setWord(Device{d.head.type, d.head.number + k}, w);
                }
            } else {
                for (uint32_t i = 0; i < 16; ++i) {
                    const uint32_t n = d.head.number + 16 * k + i;
                    if (free(n)) {
                        mock.setBit(Device{d.head.type, n}, ((w >> i) & 1u) != 0);
                    }
                }
            }
        }
    }
}

} // namespace

void checkMock(const Capture& c, Report& r) {
    // Points the poll steps wrote (heartbeat, ad-hoc writes): their timing against the steps is not
    // recorded, so reads of them are seeded from the capture every time.
    std::set<Point> polled;
    for (const mc::test::Vector& v : c.sessionVecs) {
        if (v.field("kind") == "tx") {
            for (const Decoded& d : decode(c.frame, v.bytes)) {
                if (isWriteOp(d.op)) {
                    forEachPoint(d, [&](const Point& p) { polled.insert(p); });
                }
            }
        }
    }

    const McProtocol proto(c.frame);
    std::unique_ptr<MockPlc> mock = makeMock(c);
    std::set<Point> owned;
    for (const Record& rec : c.records) {
        const std::vector<Decoded> dec = decode(c.frame, rec.request);
        if (rec.hasResponse && !rec.partial) {
            size_t pos = 0;
            for (const Decoded& d : dec) {
                const Request req = toRequest(d);
                Parser p = proto.parser(req);
                const ByteView rest{rec.response.data() + std::min(pos, rec.response.size()),
                                    rec.response.size() - std::min(pos, rec.response.size())};
                if (p.feed(rest) == ParseStatus::Done) {
                    if (!isWriteOp(d.op) && d.answeredOk) {
                        std::vector<uint8_t> payload(proto.payloadSize(req));
                        const Expected<size_t> n =
                            p.payload(rest, MutableByteView{payload.data(), payload.size()});
                        if (n) {
                            seed(*mock, d, payload, owned, polled);
                        }
                    }
                    pos += p.frameLength();
                } else {
                    break;
                }
            }
        }

        mock->bytesIn(view(rec.request));
        std::vector<uint8_t> got;
        ByteView out;
        while (mock->nextResponse(out)) {
            got.insert(got.end(), out.data, out.data + out.size);
        }
        if (!rec.partial) {
            ++r.checks;
            if (got != rec.response) {
                fail(r, "RPL-03", c, rec.id,
                     rec.hasResponse
                         ? "the mock's response differs from the captured one, " +
                               describeDifference(rec.response, got, "mock")
                         : "the capture has no response but the mock answered " + hex(got));
            }
        }

        for (const Decoded& d : dec) {
            if (isWriteOp(d.op) && d.answeredOk) {
                forEachPoint(d, [&](const Point& p) { owned.insert(p); });
            }
        }
        for (const auto& rcv : c.recoveries) {
            if (rcv.first == rec.id && c.transport == "tcp") {
                // A new connection: the PLC's stream state starts clean, and the run cannot know
                // whether the PLC (virtual_plc gives each connection a fresh MockPlc) kept its
                // memory.
                mock = makeMock(c);
                owned.clear();
                r.notes.push_back("RPL-03 " + c.profile +
                                  ": memory not assumed after the link recovery following " +
                                  rec.id);
            }
        }
    }
}

// ---- RPL-05 ---------------------------------------------------------------------------------

namespace {

/// Splits "V-3E-B-05/06" into the vector ids V-3E-B-05 and V-3E-B-06.
std::vector<std::string> mirrorIds(const std::string& mirrors) {
    std::vector<std::string> ids;
    for (const std::string& token : words(mirrors)) {
        const size_t dash = token.rfind('-');
        if (dash == std::string::npos) {
            ids.push_back(token);
            continue;
        }
        const std::string prefix = token.substr(0, dash + 1);
        std::istringstream in(token.substr(dash + 1));
        std::string part;
        while (std::getline(in, part, '/')) {
            ids.push_back(prefix + part);
        }
    }
    return ids;
}

} // namespace

void checkSanity(const Capture& c, const Env& env, Report& r) {
    // (a) every write is followed by a read-back of the same points that matched
    for (size_t i = 0; i < c.records.size(); ++i) {
        const Record& w = c.records[i];
        if (w.via != "api" || w.op.rfind("Write", 0) != 0 || !startsWith(w.outcome, "ok")) {
            continue;
        }
        ++r.checks;
        const std::vector<Decoded> dec = decode(c.frame, w.request);
        if (dec.empty() || !isWriteOp(dec.front().op)) {
            fail(r, "RPL-05", c, w.id, "the captured write is not a frame the mock can decode");
            continue;
        }
        const Decoded& wd = dec.front();
        // point -> written value (words: the word; bit devices: one entry per bit)
        std::map<Point, uint16_t> written;
        const bool bitDev = isBitDevice(wd.head.type);
        if (isBitOp(wd.op)) {
            for (uint32_t k = 0; k < wd.count; ++k) {
                written[Point{static_cast<int>(wd.head.type), wd.head.number + k}] = wd.data[k];
            }
        } else {
            for (uint32_t k = 0; k < wd.count; ++k) {
                const uint16_t word =
                    static_cast<uint16_t>(wd.data[2 * k] | (wd.data[2 * k + 1] << 8));
                if (!bitDev) {
                    written[Point{static_cast<int>(wd.head.type), wd.head.number + k}] = word;
                } else {
                    for (uint32_t b = 0; b < 16; ++b) {
                        written[Point{static_cast<int>(wd.head.type),
                                      wd.head.number + 16 * k + b}] = (word >> b) & 1u;
                    }
                }
            }
        }
        std::map<Point, uint16_t> seen;
        for (size_t j = i + 1; j < c.records.size(); ++j) {
            const Record& rd = c.records[j];
            if (rd.step != w.step || rd.via != "api" || rd.op.rfind("Read", 0) != 0 ||
                !rd.hasResponse || !startsWith(rd.outcome, "ok")) {
                continue;
            }
            const std::vector<Decoded> rdec = decode(c.frame, rd.request);
            if (rdec.empty()) {
                continue;
            }
            const auto payload = payloadOf(c.frame, toRequest(rdec.front()), rd.response, nullptr);
            if (!payload) {
                continue;
            }
            const Decoded& d = rdec.front();
            if (d.op == Op::ReadBits) {
                for (uint32_t k = 0; k < d.count && k < payload->size(); ++k) {
                    seen[Point{static_cast<int>(d.head.type), d.head.number + k}] = (*payload)[k];
                }
            } else {
                for (uint32_t k = 0; k < d.count && 2 * k + 1 < payload->size(); ++k) {
                    const uint16_t word =
                        static_cast<uint16_t>((*payload)[2 * k] | ((*payload)[2 * k + 1] << 8));
                    if (!isBitDevice(d.head.type)) {
                        seen[Point{static_cast<int>(d.head.type), d.head.number + k}] = word;
                    } else {
                        for (uint32_t b = 0; b < 16; ++b) {
                            seen[Point{static_cast<int>(d.head.type), d.head.number + 16 * k + b}] =
                                (word >> b) & 1u;
                        }
                    }
                }
            }
        }
        for (const auto& pv : written) {
            const auto it = seen.find(pv.first);
            if (it == seen.end()) {
                fail(r, "RPL-05", c, w.id,
                     "the write of " + w.device + " x" + std::to_string(w.count) +
                         " has no read-back of " +
                         deviceText(
                             Device{static_cast<DeviceType>(pv.first.first), pv.first.second}) +
                         " (the plan needs readBack)");
                break;
            }
            if (it->second != pv.second) {
                fail(r, "RPL-05", c, w.id,
                     "the read-back of " +
                         deviceText(
                             Device{static_cast<DeviceType>(pv.first.first), pv.first.second}) +
                         " is " + std::to_string(it->second) + " but " + std::to_string(pv.second) +
                         " was written");
                break;
            }
        }
    }

    // (b) GV steps equal the Appendix A vector they mirror
    std::map<std::string, bool> gvMatched; // step -> some record matched a mirrored vector
    std::vector<mc::test::Vector> golden;
    bool loaded = false;
    std::map<std::string, const mc::test::Vector*> byId;
    for (const Record& rec : c.records) {
        if (rec.mirrors.empty() || !startsWith(rec.step, "GV")) {
            continue;
        }
        gvMatched.emplace(rec.step, false);
        if (!loaded) {
            loaded = true;
            try {
                golden = mc::test::loadAllVectors(env.vectorsDir);
            } catch (const std::exception& e) {
                fail(r, "RPL-05", c, rec.id,
                     std::string("the golden vectors cannot be loaded: ") + e.what());
                return;
            }
            for (const mc::test::Vector& v : golden) {
                byId[v.id] = &v;
            }
        }
        if (rec.via != "api") {
            continue;
        }
        const std::vector<std::string> ids = mirrorIds(rec.mirrors);
        const mc::test::Vector* reqVec = nullptr;
        for (const std::string& id : ids) {
            const auto it = byId.find(id);
            if (it == byId.end()) {
                continue;
            }
            const mc::test::Vector& v = *it->second;
            if (v.field("kind") == "request" && v.field("op") == rec.op &&
                v.field("device") == rec.device && v.field("count") == std::to_string(rec.count)) {
                reqVec = &v;
                break;
            }
        }
        if (reqVec == nullptr) {
            continue;
        }
        gvMatched[rec.step] = true;
        ++r.checks;
        if (reqVec->bytes != rec.request) {
            fail(r, "RPL-05", c, rec.id,
                 "the request differs from Appendix A vector " + reqVec->id + ", " +
                     describeDifference(reqVec->bytes, rec.request, "capture"));
            continue;
        }
        if (!rec.hasResponse) {
            continue;
        }
        for (const mc::test::Vector& v : golden) {
            const std::string kind = v.field("kind");
            if (startsWith(kind, "response") && v.field("of") == reqVec->id && kind == "response") {
                // the response is compared when the data are the vector's own (the default values)
                const auto req = metaRequest(rec);
                if (!req) {
                    break;
                }
                const auto a = payloadOf(c.frame, *req, v.bytes, nullptr);
                const auto b = payloadOf(c.frame, *req, rec.response, nullptr);
                if (a && b && *a == *b) {
                    ++r.checks;
                    if (v.bytes != rec.response) {
                        fail(r, "RPL-05", c, rec.id + "-R",
                             "the response carries the vector's data but its bytes differ from "
                             "Appendix A vector " +
                                 v.id + ", " +
                                 describeDifference(v.bytes, rec.response, "capture"));
                    }
                }
                break;
            }
        }
    }
    for (const auto& kv : gvMatched) {
        if (!kv.second) {
            fail(r, "RPL-05", c, kv.first,
                 "the step names Appendix A vectors but none of its records matches one of them");
        }
    }
}

// ---- RPL-06 ---------------------------------------------------------------------------------

namespace {

/// FINDINGS.md holds its entry template inside an HTML comment: headings there are not entries.
std::string withoutHtmlComments(std::string text) {
    for (size_t open = text.find("<!--"); open != std::string::npos;
         open = text.find("<!--", open)) {
        const size_t close = text.find("-->", open + 4);
        text.erase(open, close == std::string::npos ? std::string::npos : close + 3 - open);
    }
    return text;
}

} // namespace

void checkDivergences(const Capture& c, const Env& env, Report& r) {
    if (c.divergences.empty()) {
        return;
    }
    std::string findings;
    try {
        findings = withoutHtmlComments(readText(env.findingsFile));
    } catch (const std::exception& e) {
        fail(r, "RPL-06", c, "", e.what());
        return;
    }
    std::set<std::string> steps;
    for (const Record& rec : c.records) {
        steps.insert(rec.step);
        steps.insert(rec.id);
    }
    for (const mc::test::Vector& v : c.sessionVecs) {
        steps.insert(v.field("step"));
    }
    for (const auto& d : c.divergences) {
        ++r.checks;
        if (d.second.empty()) {
            fail(r, "RPL-06", c, d.first, "divergences.txt gives no FINDINGS id for this step");
            continue;
        }
        if (steps.count(d.first) == 0) {
            fail(r, "RPL-06", c, d.first,
                 "divergences.txt names a step that is not in the capture");
        }
        bool found = false;
        size_t at = 0;
        const std::string heading = "### " + d.second;
        while ((at = findings.find(heading, at)) != std::string::npos) {
            const size_t end = at + heading.size();
            if (end >= findings.size() ||
                !std::isalnum(static_cast<unsigned char>(findings[end]))) {
                found = true;
                break;
            }
            at = end;
        }
        if (!found) {
            fail(r, "RPL-06", c, d.first,
                 "FINDINGS id " + d.second + " has no entry ('### " + d.second + "') in " +
                     env.findingsFile.string());
        }
    }
}

// ---- all ------------------------------------------------------------------------------------

Report checkAll(const Capture& c, const Env& env) {
    Report all;
    checkReencode(c, all);
    checkParse(c, all);
    checkMock(c, all);
    checkSession(c, all);
    checkSanity(c, env, all);
    checkDivergences(c, env, all);

    Report out;
    out.checks = all.checks;
    out.notes = all.notes;
    std::set<std::string> tagged;
    for (const auto& d : c.divergences) {
        tagged.insert(d.first);
    }
    for (const Failure& f : all.failures) {
        const bool known = f.check != "RPL-06" &&
                           (tagged.count(f.step) != 0 || tagged.count(baseStep(f.step)) != 0);
        (known ? out.knownDivergences : out.failures).push_back(f);
    }
    return out;
}

} // namespace mc::replay
