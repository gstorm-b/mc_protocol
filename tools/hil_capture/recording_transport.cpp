#include "hil_capture/recording_transport.h"

#include <algorithm>
#include <utility>

namespace mc::hil {

RecordingTransport::RecordingTransport(std::unique_ptr<Transport> inner,
                                       std::shared_ptr<RecordingClock> clock, QObject* parent)
    : Transport(parent), m_inner(inner.release()), m_clock(std::move(clock)) {
    m_inner->setParent(this);
    // Every signal of the wrapped transport goes out unchanged.
    connect(m_inner, &Transport::opened, this, &Transport::opened);
    connect(m_inner, &Transport::openFailed, this, &Transport::openFailed);
    connect(m_inner, &Transport::lost, this, &Transport::lost);
    connect(m_inner, &Transport::readyRead, this, &RecordingTransport::onInnerReadyRead);
}

RecordingTransport::~RecordingTransport() = default;

void RecordingTransport::open() {
    m_unread.clear();
    m_inner->open();
}

void RecordingTransport::close() {
    m_unread.clear();
    m_inner->close();
}

bool RecordingTransport::write(ByteView bytes) {
    const qint64 t = m_clock->nowNs();
    const bool accepted = m_inner->write(bytes);
    if (accepted && bytes.size != 0) {
        m_chunks.push_back(WireChunk{t, WireDirection::Tx,
                                     QByteArray(reinterpret_cast<const char*>(bytes.data),
                                                static_cast<QByteArray::size_type>(bytes.size))});
    }
    return accepted;
}

size_t RecordingTransport::read(MutableByteView out) {
    const size_t n = std::min(out.size, static_cast<size_t>(m_unread.size()));
    if (n != 0) {
        std::copy(m_unread.constData(), m_unread.constData() + n, out.data);
        m_unread.remove(0, static_cast<QByteArray::size_type>(n));
    }
    return n;
}

Transport::State RecordingTransport::state() const { return m_inner->state(); }

QString RecordingTransport::describe() const { return m_inner->describe(); }

bool RecordingTransport::lastLossWasPeerClose() const { return m_inner->lastLossWasPeerClose(); }

void RecordingTransport::setRawMode(bool on) {
    m_raw = on;
    if (!on) {
        m_unread.clear();
    }
}

QByteArray RecordingTransport::takeRaw() {
    QByteArray bytes;
    bytes.swap(m_unread);
    return bytes;
}

void RecordingTransport::onInnerReadyRead() {
    const qint64 t = m_clock->nowNs();
    QByteArray arrived;
    QByteArray buffer(65536, '\0');
    for (;;) {
        const size_t n = m_inner->read(MutableByteView{reinterpret_cast<uint8_t*>(buffer.data()),
                                                       static_cast<size_t>(buffer.size())});
        if (n == 0) {
            break;
        }
        arrived.append(buffer.constData(), static_cast<QByteArray::size_type>(n));
    }
    if (arrived.isEmpty()) {
        return;
    }
    m_chunks.push_back(WireChunk{t, WireDirection::Rx, arrived});
    m_unread.append(arrived);
    if (m_raw) {
        emit rawReadyRead();
    } else {
        emit readyRead();
    }
}

} // namespace mc::hil
