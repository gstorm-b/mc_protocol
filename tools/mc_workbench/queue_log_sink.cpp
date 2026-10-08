#include "mc_workbench/queue_log_sink.h"

#include <new>

namespace mc::workbench {

void QueueLogSink::write(mc::LogLevel level, std::string_view category,
                         std::string_view message) noexcept {
    if (m_lines.size() >= kMaxPending) {
        ++m_dropped;
        return;
    }
    try {
        LogLine line;
        line.tNs = m_clock->nowNs();
        line.level = level;
        line.category =
            QString::fromUtf8(category.data(), static_cast<QString::size_type>(category.size()));
        line.message =
            QString::fromUtf8(message.data(), static_cast<QString::size_type>(message.size()));
        m_lines.push_back(std::move(line));
    } catch (const std::bad_alloc&) {
        ++m_dropped; // a sink must not throw into the library
    }
}

QVector<LogLine> QueueLogSink::take() {
    QVector<LogLine> out;
    out.swap(m_lines);
    if (m_dropped != 0) {
        LogLine note;
        note.tNs = m_clock->nowNs();
        note.level = mc::LogLevel::Warn;
        note.category = QStringLiteral("workbench");
        note.message = QStringLiteral("%1 log lines dropped (queue full)").arg(m_dropped);
        out.push_back(std::move(note));
        m_dropped = 0;
    }
    return out;
}

} // namespace mc::workbench
