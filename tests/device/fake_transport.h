// fake_transport.h -- a Transport double for the McDevice tests that need control the loopback
// server cannot give: a write that fails, a loss with a chosen cause, bytes the peer never sent
// through a socket. Everything it does asynchronously runs from the event loop, like the real
// transports.
#pragma once

#include "mc/device/transport.h"

#include <QByteArray>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <cstring>

class FakeTransport : public mc::Transport {
    Q_OBJECT
  public:
    explicit FakeTransport(QObject* parent = nullptr) : mc::Transport(parent) {}

    bool failOpen{false};   // open() ends in openFailed()
    bool failWrites{false}; // write() returns false, then lost() follows
    bool lostByPeer{false}; // answer of lastLossWasPeerClose()
    QByteArray written;     // every byte accepted by write()
    int openCalls{0};
    int closeCalls{0};

    void open() override {
        if (m_state != State::Closed) {
            return;
        }
        ++openCalls;
        m_state = State::Opening;
        QTimer::singleShot(0, this, [this]() {
            if (m_state != State::Opening) {
                return;
            }
            if (failOpen) {
                m_state = State::Closed;
                emit openFailed(QStringLiteral("fake: open refused"));
            } else {
                m_state = State::Open;
                emit opened();
            }
        });
    }

    void close() override {
        ++closeCalls;
        m_state = State::Closed;
    }

    bool write(mc::ByteView bytes) override {
        if (m_state != State::Open) {
            return false;
        }
        if (failWrites) {
            m_state = State::Closed;
            QTimer::singleShot(0, this, [this]() { emit lost(QStringLiteral("fake: write failed")); });
            return false;
        }
        written.append(reinterpret_cast<const char*>(bytes.data),
                       static_cast<QByteArray::size_type>(bytes.size));
        return true;
    }

    size_t read(mc::MutableByteView out) override {
        const size_t n = std::min(out.size, static_cast<size_t>(m_incoming.size()));
        if (n > 0) {
            std::memcpy(out.data, m_incoming.constData(), n);
            m_incoming.remove(0, static_cast<QByteArray::size_type>(n));
        }
        return n;
    }

    State state() const override { return m_state; }
    QString describe() const override { return QStringLiteral("fake"); }
    bool lastLossWasPeerClose() const override { return lostByPeer; }

    // Bytes "received from the peer"; readyRead() follows from the event loop.
    void deliver(const QByteArray& bytes) {
        m_incoming.append(bytes);
        QTimer::singleShot(0, this, [this]() { emit readyRead(); });
    }

    // The link goes away while Open; lost() follows from the event loop.
    void simulateLost(const QString& reason, bool byPeer) {
        lostByPeer = byPeer;
        m_state = State::Closed;
        QTimer::singleShot(0, this, [this, reason]() { emit lost(reason); });
    }

  private:
    State m_state{State::Closed};
    QByteArray m_incoming;
};
