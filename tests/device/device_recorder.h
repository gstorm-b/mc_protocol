// device_recorder.h -- records every signal of an mc::McDevice in emission order, so a test can
// assert the order and not only the counts. Header-only test helper; connections are direct
// (the recorder lives on the device's thread unless the test says otherwise).
#pragma once

#include "mc/device/mc_device.h"
#include "mc/device/meta_types.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <cstring>

namespace testing {

inline const char* name(mc::LinkState s) {
    switch (s) {
    case mc::LinkState::Disconnected: return "Disconnected";
    case mc::LinkState::Connecting: return "Connecting";
    case mc::LinkState::Connected: return "Connected";
    case mc::LinkState::Faulted: return "Faulted";
    }
    return "?";
}

inline const char* name(mc::LinkReason r) {
    switch (r) {
    case mc::LinkReason::Requested: return "Requested";
    case mc::LinkReason::OpenFailed: return "OpenFailed";
    case mc::LinkReason::PeerClosed: return "PeerClosed";
    case mc::LinkReason::TransportError: return "TransportError";
    case mc::LinkReason::Fault: return "Fault";
    }
    return "?";
}

inline const char* name(mc::ErrorCode c) {
    switch (c) {
    case mc::ErrorCode::Ok: return "Ok";
    case mc::ErrorCode::Timeout: return "Timeout";
    case mc::ErrorCode::LinkDown: return "LinkDown";
    case mc::ErrorCode::PlcError: return "PlcError";
    case mc::ErrorCode::QueueFull: return "QueueFull";
    default: return "Error";
    }
}

struct Event {
    enum class Kind { Link, Fault, Changed, Snapshot, Cycle, Finished };
    Kind kind{Kind::Link};
    mc::LinkState state{mc::LinkState::Disconnected};
    mc::LinkReason reason{mc::LinkReason::Requested};
    QString detail;
    mc::LinkFaultInfo fault;
    mc::DeviceType type{mc::DeviceType::D};
    quint32 round{0};
    QVector<mc::Change> changes;
    mc::DeviceSnapshot snapshot;
    mc::CycleInfo cycle{};
    mc::RequestId id{0};
    mc::Error error;
    QByteArray payload;
    QString text; // the compact form used by DeviceRecorder::trace()
    QThread* thread{nullptr}; // the thread the recorder ran on when it got the signal
};

// Connects to every signal of a device and appends one Event per emission.
class DeviceRecorder : public QObject {
  public:
    // Lives on the device's thread as its child.
    explicit DeviceRecorder(mc::McDevice& device) : QObject(&device) { connectAll(device); }

    // Lives where `parent` lives; when that is another thread than the device's, the signals
    // arrive through queued connections and `Event::thread` is the receiving thread.
    DeviceRecorder(mc::McDevice& device, QObject* parent) : QObject(parent) { connectAll(device); }

    void connectAll(mc::McDevice& device) {
        connect(&device, &mc::McDevice::linkStateChanged, this,
                [this](mc::LinkState s, mc::LinkReason r, const QString& d) {
                    Event e;
                    e.kind = Event::Kind::Link;
                    e.state = s;
                    e.reason = r;
                    e.detail = d;
                    e.text = QStringLiteral("link(%1,%2)")
                                 .arg(QLatin1String(name(s)), QLatin1String(name(r)));
                    add(e);
                });
        connect(&device, &mc::McDevice::linkFault, this, [this](const mc::LinkFaultInfo& f) {
            Event e;
            e.kind = Event::Kind::Fault;
            e.fault = f;
            e.text = QStringLiteral("fault(%1,reopen=%2)")
                         .arg(QLatin1String(f.kind == mc::LinkFaultKind::Timeout ? "Timeout" : "ProtocolError"))
                         .arg(f.reopenTransport ? 1 : 0);
            add(e);
        });
        connect(&device, &mc::McDevice::valuesChanged, this,
                [this](mc::DeviceType t, quint32 round, const QVector<mc::Change>& c) {
                    Event e;
                    e.kind = Event::Kind::Changed;
                    e.type = t;
                    e.round = round;
                    e.changes = c;
                    e.text = QStringLiteral("changed(%1,%2)")
                                 .arg(QLatin1String(mc::deviceInfo(t).symbol))
                                 .arg(round);
                    add(e);
                });
        connect(&device, &mc::McDevice::snapshotReady, this, [this](const mc::DeviceSnapshot& s) {
            Event e;
            e.kind = Event::Kind::Snapshot;
            e.type = s.type;
            e.round = s.round;
            e.snapshot = s;
            e.text = QStringLiteral("snapshot(%1,%2)")
                         .arg(QLatin1String(mc::deviceInfo(s.type).symbol))
                         .arg(s.round);
            add(e);
        });
        connect(&device, &mc::McDevice::cycleDone, this, [this](const mc::CycleInfo& c) {
            Event e;
            e.kind = Event::Kind::Cycle;
            e.cycle = c;
            e.round = c.round;
            e.text = QStringLiteral("cycle(%1)").arg(c.round);
            add(e);
        });
        connect(&device, &mc::McDevice::requestFinished, this,
                [this](mc::RequestId id, const mc::Error& err, const QByteArray& payload) {
                    Event e;
                    e.kind = Event::Kind::Finished;
                    e.id = id;
                    e.error = err;
                    e.payload = payload;
                    e.text = QStringLiteral("finished(%1,%2)")
                                 .arg(static_cast<qulonglong>(id))
                                 .arg(QLatin1String(name(err.code)));
                    add(e);
                });
    }

    QVector<Event> events;

    // The emission order as one short text per signal, e.g. "link(Connected,Requested)".
    QStringList trace() const {
        QStringList out;
        for (const Event& e : events) {
            out << e.text;
        }
        return out;
    }

    int count(Event::Kind kind) const {
        int n = 0;
        for (const Event& e : events) {
            n += e.kind == kind ? 1 : 0;
        }
        return n;
    }

    // Events of one kind, in order.
    QVector<Event> of(Event::Kind kind) const {
        QVector<Event> out;
        for (const Event& e : events) {
            if (e.kind == kind) {
                out.append(e);
            }
        }
        return out;
    }

    bool hasCycle(quint32 round) const {
        for (const Event& e : events) {
            if (e.kind == Event::Kind::Cycle && e.round == round) {
                return true;
            }
        }
        return false;
    }

    // Trace entries from the first "link(Connected" on: the part of the story after the connect.
    QStringList traceAfterConnect() const {
        const QStringList all = trace();
        for (int i = 0; i < all.size(); ++i) {
            if (all[i].startsWith(QLatin1String("link(Connected"))) {
                return all.mid(i + 1);
            }
        }
        return QStringList();
    }

  private:
    void add(Event e) {
        e.thread = QThread::currentThread();
        events.append(e);
    }
};

} // namespace testing
