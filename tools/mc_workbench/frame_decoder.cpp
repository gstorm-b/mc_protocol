#include "mc_workbench/frame_decoder.h"

#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"

#include <vector>

namespace mc::workbench {

namespace {

const char* opName(mc::Op op) {
    switch (op) {
    case mc::Op::ReadBits:
        return "ReadBits";
    case mc::Op::ReadWords:
        return "ReadWords";
    case mc::Op::WriteBits:
        return "WriteBits";
    case mc::Op::WriteWords:
        return "WriteWords";
    }
    return "?";
}

QString deviceText(const mc::Device& device, mc::XyNumbering xy) {
    char buffer[24];
    const size_t size = mc::formatDevice(device, buffer, sizeof(buffer), xy);
    return QString::fromLatin1(buffer, static_cast<QString::size_type>(size));
}

mc::ByteView viewOf(const QByteArray& bytes) {
    return mc::ByteView{reinterpret_cast<const uint8_t*>(bytes.constData()),
                        static_cast<size_t>(bytes.size())};
}

} // namespace

FrameDecoder::FrameDecoder(const mc::FrameConfig& frame)
    : m_frame(frame), m_mock(std::make_unique<mc::MockPlc>(frame)) {}

FrameDecoder::~FrameDecoder() = default;

void FrameDecoder::annotate(FrameRecord& record) {
    if (record.tx) {
        annotateRequest(record);
    } else {
        annotateAnswer(record);
    }
}

void FrameDecoder::annotateRequest(FrameRecord& record) {
    m_answer.clear();
    m_op.reset();
    m_mock->bytesIn(viewOf(record.bytes));
    mc::ByteView response;
    while (m_mock->nextResponse(response)) {
        // The mock's answers are not wanted here; draining keeps its queue empty.
    }
    const std::vector<mc::MockRequestRecord>& requests = m_mock->requests();
    const uint32_t eot = m_mock->eotCount();
    if (!requests.empty()) {
        const mc::MockRequestRecord& last = requests.back();
        m_op = last.op;
        m_head = last.head;
        m_count = last.count;
        record.edge = FrameEdge::Complete;
        record.note = QStringLiteral("%1 %2 x%3")
                          .arg(QLatin1String(opName(last.op)),
                               deviceText(last.head, m_frame.xyNotation))
                          .arg(last.count);
        if (requests.size() > 1) {
            record.note += QStringLiteral(" (+%1 more)").arg(requests.size() - 1);
        }
    } else if (eot != m_eotSeen) {
        record.edge = FrameEdge::Complete;
        record.note = QStringLiteral("EOT");
    } else {
        record.edge = FrameEdge::Partial;
        record.note = QStringLiteral("request fragment (%1 bytes)").arg(record.bytes.size());
    }
    m_eotSeen = eot;
    m_mock->clearLog();
}

void FrameDecoder::annotateAnswer(FrameRecord& record) {
    if (!m_op) {
        record.edge = FrameEdge::Unknown;
        record.note = QStringLiteral("no request to match");
        return;
    }
    m_answer.append(record.bytes);

    // The parser needs the request the answer belongs to; a write needs its data (any bytes of the
    // right size: the answer to a write carries none).
    const bool bitOp = *m_op == mc::Op::ReadBits || *m_op == mc::Op::WriteBits;
    std::vector<uint8_t> filler;
    mc::Request request;
    switch (*m_op) {
    case mc::Op::ReadBits:
        request = mc::Request::readBits(m_head, m_count);
        break;
    case mc::Op::ReadWords:
        request = mc::Request::readWords(m_head, m_count);
        break;
    case mc::Op::WriteBits:
        filler.assign(m_count, 0);
        request = mc::Request::writeBits(m_head, mc::ByteView{filler.data(), filler.size()});
        break;
    case mc::Op::WriteWords:
        filler.assign(static_cast<size_t>(m_count) * 2, 0);
        request = mc::Request::writeWords(m_head, mc::ByteView{filler.data(), filler.size()});
        break;
    }

    const mc::McProtocol protocol(m_frame);
    mc::Parser parser = protocol.parser(request);
    const mc::ParseStatus status = parser.feed(viewOf(m_answer));
    switch (status) {
    case mc::ParseStatus::NeedMore:
        record.edge = FrameEdge::Partial;
        record.note = QStringLiteral("partial answer (%1 bytes so far)").arg(m_answer.size());
        return;
    case mc::ParseStatus::Done: {
        record.edge = FrameEdge::Complete;
        const qsizetype extra = m_answer.size() - static_cast<qsizetype>(parser.frameLength());
        record.note = *m_op == mc::Op::ReadBits || *m_op == mc::Op::ReadWords
                          ? QStringLiteral("ok, %1 %2").arg(m_count).arg(bitOp ? QStringLiteral("bits")
                                                                              : QStringLiteral("words"))
                          : QStringLiteral("ok");
        if (extra > 0) {
            record.note += QStringLiteral(" (+%1 stray bytes)").arg(extra);
        }
        break;
    }
    case mc::ParseStatus::Failed: {
        record.edge = FrameEdge::Complete;
        const mc::Error& error = parser.error();
        record.note = error.category == mc::ErrorCategory::Plc
                          ? QStringLiteral("PLC error 0x") +
                                QString::number(error.plcCode, 16).rightJustified(4, QLatin1Char('0')).toUpper()
                          : QStringLiteral("protocol error: %1").arg(QString::fromUtf8(error.message));
        break;
    }
    }
    m_answer.clear();
    m_op.reset(); // answered: further bytes belong to no request
}

} // namespace mc::workbench
