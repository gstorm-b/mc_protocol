# Spec: qt-device

- **Module id:** `qt-device` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** `core-session` (and through it `core-protocol`, `core-model`); `mock-plc` for tests and examples only
- **Depended on by:** applications; v2 `mc_widgets`
- **Inputs:** owner interview 2026-09-25 (value-publishing contract, dynamic subscriptions); intent (McDevice: event-driven, non-blocking, correlation ids, log sink); decisions 1, 6, 7; ideas doc §4.2, §4.5; capability-map assumptions 1, 6; the old device in `reference_source/device/plc/mc_protocol_device.h/.cpp`, `mc_msg_interface.h`, `mc_msg_tcp_client.h`, `mc_msg_serial_port.h`, `mc_context.h`; `SPEC-core-session.md`, `SPEC-mock-plc.md`.

## Objective

`mc::McDevice` is the class the intent names: a Qt 6 `QObject` that talks to a Mitsubishi PLC over **TCP or a serial COM port**, polls the subscribed devices, and reports through signals. It is a thin adapter: all protocol behaviour (rounds, timeouts, retries, change detection, correlation) lives in `mc::Session`. `McDevice` only does four things:

1. owns a `Transport` (`TcpTransport` or `SerialTransport`, or one the app injects) and moves bytes between it and the `Session`;
2. turns time into `Session` input with one single-shot `QTimer` armed at `Session::nextDeadline()`;
3. turns `Session` outputs into Qt signals carrying Qt value types;
4. keeps the link state the application sees (`Disconnected`, `Connecting`, `Connected`, `Faulted`).

It never blocks, never starts a thread, never reconnects by itself (decision 7), and the whole class is testable over loopback TCP against `mc::MockPlc`.

**User stories**

- As a Qt app, I build an `McDevice` from a JSON config, call `connectToPlc()`, and receive one `snapshotReady` per device type after the first round, then `valuesChanged` for each response that changes something.
- As a Qt app, I call `writeWords("D100", {…})`, keep the returned id, and get exactly one `requestFinished` with that id: success, the PLC's error, a timeout, or "link down".
- As a Qt app, when the link faults I get `linkFault` and decide myself whether to call `connectToPlc()` again.
- As a Qt app, I move the device to a worker thread and receive its signals in the GUI thread.
- As a maintainer, a slot that calls back into the device from inside a signal cannot corrupt the engine.

## Tech Stack

| Item | Choice |
|---|---|
| Qt | **5.15**, or 6.2 LTS and later (5.15 added 2026-10-07, owner decision): `Core`, `Network`, `SerialPort`; `Test` for tests (capability-map assumption 1, 6) |
| Language | C++17, `AUTOMOC ON` |
| Threads | none inside the library; the app may `moveToThread` |
| Test framework | QtTest; the loopback server wraps `mc::MockPlc` |

The complexity rules of ideas §9.3 bind the core only. This layer allocates per signal (Qt containers); the allocation-free engine underneath is unaffected.

## Commands

```powershell
cmake --build build/cmake-debug --target mc_device_tests qt_console_poller virtual_plc
ctest --test-dir build/cmake-debug -L device --output-on-failure
$env:MC_TEST_SERIAL_PAIR = "COM10,COM11"   # optional: enables QDV-14 with a virtual COM pair
build/cmake-debug/examples/virtual_plc --frame 3E --code Binary --port 5000 --set D100=1234 --wiggle D105
build/cmake-debug/examples/qt_console_poller --host 127.0.0.1 --port 5000 --frame 3E --sub D100:64 --sub M0:32
```

## Project Structure

```text
include/mc/device/
├── transport.h            Transport (abstract QObject)
├── tcp_transport.h        TcpSettings, TcpTransport
├── serial_transport.h     SerialSettings, SerialTransport
├── mc_device_config.h     TransportKind, SubscriptionSpec, McDeviceConfig (+ JSON)
├── mc_device.h            LinkState, LinkReason, LinkFaultInfo, SnapshotSegment, ChunkStatus,
│                          DeviceSnapshot, McDevice
└── meta_types.h           Q_DECLARE_METATYPE for every signal type; registerMetaTypes()
src/device/
├── tcp_transport.cpp
├── serial_transport.cpp
├── mc_device_config.cpp   JSON mapping, validate()
├── mc_device.cpp          state, pump, signal queue
└── meta_types.cpp
tests/device/
├── mock_plc_server.h/.cpp   QTcpServer hosting one MockPlc per connection; mute/close/count controls
├── serial_bridge.h/.cpp     QSerialPort end of a virtual COM pair hosting a MockPlc (QDV-14)
├── tst_mc_device.cpp        QDV-01…08, QDV-11…13, QDV-15…17
├── tst_mc_device_thread.cpp QDV-09
├── tst_config_json.cpp      QDV-10
└── tst_serial.cpp           QDV-14 (QSKIP without MC_TEST_SERIAL_PAIR)
examples/
├── qt_console_poller/       connect, subscribe from arguments, print snapshots and changes
└── virtual_plc/             Qt console PLC: QTcpServer or QSerialPort in front of mc::MockPlc
```

`mc_device` links `mc::core` and Qt only. Only `tests/device` and `examples/*` link `mc::mock`.

## Public API

### `transport.h`

```cpp
namespace mc {
/// Moves bytes; knows nothing about MC. The frame (FrameConfig) decides protocol behaviour, so
/// a 3C frame over a TCP serial-device server is allowed and behaves as serial.
class Transport : public QObject {
    Q_OBJECT
public:
    enum class State : uint8_t { Closed, Opening, Open };
    using QObject::QObject;
    ~Transport() override = default;

    /// Asynchronous. Emits exactly one of opened() / openFailed() unless close() comes first.
    virtual void open() = 0;
    /// Closes at once; emits nothing (the caller asked for it).
    virtual void close() = 0;
    /// Queues bytes for sending. Returns false on an immediate failure (lost() follows).
    virtual bool write(ByteView bytes) = 0;
    /// Non-blocking read of what has arrived. Returns bytes copied (0 = nothing more now).
    virtual size_t read(MutableByteView out) = 0;
    virtual State state() const = 0;
    /// For logs and signals: "192.168.0.10:5000", "COM3 9600 7E1".
    virtual QString describe() const = 0;
    /// Why the last lost() happened: true = the peer closed or reset the link (PeerClosed),
    /// false = a local I/O error (TransportError). Default false. (Amended 2026-09-30, owner
    /// decision: lost() carries no cause, and the link table needs one.)
    virtual bool lastLossWasPeerClose() const { return false; }
signals:
    void opened();
    void openFailed(const QString& reason);
    void readyRead();
    /// The peer closed or an I/O error happened while Open. Not emitted after close().
    void lost(const QString& reason);
};
}
```

### `tcp_transport.h`, `serial_transport.h`

```cpp
namespace mc {
struct TcpSettings {
    QString host{QStringLiteral("192.168.0.1")};   ///< Old defaults (mc_msg_tcp_client.h).
    quint16 port{5000};
    int connectTimeoutMs{2000};
};
/// QTcpSocket with LowDelayOption (no Nagle delay on small request frames) and KeepAliveOption.
/// The connect timeout is a QTimer: QAbstractSocket has none of its own.
class TcpTransport final : public Transport { /* … */ public: explicit TcpTransport(TcpSettings s, QObject* parent = nullptr); };

struct SerialSettings {
    QString portName;                              ///< "COM3", "/dev/ttyUSB0"; empty = invalid.
    qint32 baudRate{9600};                         ///< Old defaults (mc_msg_serial_port.h): 9600 7E1, no flow control.
    QSerialPort::DataBits dataBits{QSerialPort::Data7};
    QSerialPort::Parity parity{QSerialPort::EvenParity};
    QSerialPort::StopBits stopBits{QSerialPort::OneStop};
    QSerialPort::FlowControl flowControl{QSerialPort::NoFlowControl};
};
/// QSerialPort; open() completes on the next event-loop turn so both transports behave alike.
class SerialTransport final : public Transport { /* … */ public: explicit SerialTransport(SerialSettings s, QObject* parent = nullptr); };
}
```

The line settings must match the C24 module's parameters; a mismatch shows up as timeouts or NAKs, not as a clear error (spec §8.3, §10.3).

### `mc_device_config.h`

```cpp
namespace mc {
enum class TransportKind : uint8_t { Tcp, Serial };

struct SubscriptionSpec { QString device; quint32 count{1}; };   ///< "D2000", 64

/// Everything needed to build an McDevice, as plain data (no Q_GADGET; ideas §7).
struct McDeviceConfig {
    FrameConfig frame{FrameConfig::frame3E()};
    SessionConfig session{};                  ///< session.log is ignored; use McDevice::setLogSink().
    TransportKind transport{TransportKind::Tcp};
    TcpSettings tcp{};
    SerialSettings serial{};
    QVector<SubscriptionSpec> subscriptions{};

    /// frame.validate(), session.validate(frame), every subscription parses and fits the frame,
    /// the selected transport's settings are usable (host non-empty, port non-zero, portName
    /// non-empty, baud > 0). The first failure is returned; `where` (if given) receives the
    /// offending JSON-style path, e.g. "subscriptions[2].device".
    Expected<void> validate(QString* where = nullptr) const;

    QJsonObject toJson() const;
    /// Missing keys take the defaults above; unknown keys are ignored; a wrong type or invalid
    /// value fails with {Config, InvalidConfig} and its path in `where`. "schema" must be 1.
    static Expected<McDeviceConfig> fromJson(const QJsonObject& obj, QString* where = nullptr);
};
}
```

JSON shape (every `FrameConfig` and `SessionConfig` field has a key of the same name; enums are strings):

```json
{
  "schema": 1,
  "frame":   { "frame": "3E", "code": "Binary", "network": 0, "pc": 255, "io": 1023, "station": 0,
               "monitoringTimer": 16, "series": "QL", "timeoutMs": 0, "readRetries": 0, "...": "…" },
  "session": { "cycleIntervalMs": 100, "cycleMode": "FixedRate", "bitsAsWords": true, "maxGap": "auto",
               "adHocCapacity": 64, "adHocArenaBytes": 65536, "maxAdHocBurst": 4,
               "maxConsecutiveLinkErrors": 3, "serialInterCharMs": 100, "serialFlushMs": 50,
               "heartbeat": { "enabled": false, "device": "M2000" } },
  "transport": { "kind": "Tcp",
                 "tcp":    { "host": "192.168.0.10", "port": 5000, "connectTimeoutMs": 2000 },
                 "serial": { "portName": "COM3", "baudRate": 9600, "dataBits": 7, "parity": "Even",
                             "stopBits": "1", "flowControl": "None" } },
  "subscriptions": [ { "device": "D2000", "count": 64 }, { "device": "M2000", "count": 64 } ]
}
```

**Value spellings** (amended 2026-09-30, owner decision; matched case-sensitively):

| Key | Values |
|---|---|
| `frame.frame` | `"3E"`, `"1E"`, `"3C"`, `"1C"`, `"4E"`, `"4C"` |
| `frame.code` | `"Binary"`, `"Ascii"` |
| `frame.series` | `"QL"`, `"IqR"` |
| `frame.format` | `"Format1"` … `"Format5"` |
| `frame.targetFamily` | `"IqR_Q_L"`, `"QnA"`, `"A"` |
| `frame.commandSet` | `"ACPU"`, `"AnA"` |
| `frame.xyNotation`, `frame.xyAsciiDigits` | `"Hex"`, `"Octal"`; missing = `"Hex"` (amended 2026-10-03, T-066; see `SPEC-core-model.md` "X/Y numbering for FX CPUs") |
| `session.cycleMode` | `"FixedRate"`, `"FixedDelay"` |
| `session.maxGap` | `"auto"` or a number (the sentinel value itself is refused; `"auto"` is its only spelling) |
| `session.heartbeat.device` | device text in canonical `formatDevice()` spelling, X/Y written in `frame.xyNotation` |
| `transport.kind` | `"Tcp"`, `"Serial"` |
| `transport.serial.dataBits` | a number, 5 … 8 |
| `transport.serial.parity` | `"None"`, `"Even"`, `"Odd"`, `"Space"`, `"Mark"` |
| `transport.serial.stopBits` | `"1"`, `"1.5"`, `"2"` |
| `transport.serial.flowControl` | `"None"`, `"Hardware"`, `"Software"` |

**`"schema"`:** a missing key counts as 1 (so `{}` is the default config); any other value
(`2`, `0`, `"1"`, `1.5`) fails with path `schema` (amended 2026-09-30, owner decision).
`fromJson` checks JSON shape, types and value ranges only; whether a subscription's device text
parses and fits the frame is `validate()`'s job (path `subscriptions[i].device`).

### `mc_device.h`

```cpp
namespace mc {
enum class LinkState : uint8_t { Disconnected, Connecting, Connected, Faulted };
enum class LinkReason : uint8_t {
    Requested,       ///< connectToPlc() / disconnectFromPlc()
    OpenFailed,      ///< transport could not open, or the config is invalid
    PeerClosed,      ///< the other side closed the connection
    TransportError,  ///< I/O error while open (cable, port removed)
    Fault            ///< Session reported a LinkFault (see linkFault())
};
struct LinkFaultInfo { LinkFaultKind kind; Error error; bool reopenTransport; };

/// One device type's subscribed points after a round. `values` uses the normalized payload
/// layout (words: 2 bytes LE; bits: 1 byte per point, 0/1), so mc::convert works on it
/// directly; `states` holds one PointState per point.
struct SnapshotSegment { Device head; quint32 count; QByteArray values; QByteArray states; };
struct ChunkStatus { Request request; ChunkState state; Error error; };
struct DeviceSnapshot {
    DeviceType type{DeviceType::D};
    quint32 round{0};
    QVector<SnapshotSegment> segments;
    QVector<ChunkStatus> chunks;    ///< The read chunks of this type and how the last round went.
};

class McDevice : public QObject {
    Q_OBJECT
public:
    explicit McDevice(QObject* parent = nullptr);                        ///< Default config.
    explicit McDevice(const McDeviceConfig& cfg, QObject* parent = nullptr);
    /// Custom transport (tests, other media); cfg.transport/tcp/serial are ignored.
    McDevice(const McDeviceConfig& cfg, std::unique_ptr<Transport> transport, QObject* parent = nullptr);
    /// Closes the transport. Emits nothing: call disconnectFromPlc() first to receive the
    /// completions of outstanding requests. Requests still outstanding are listed (count and
    /// ids) in one Warn log line, so a forgotten disconnect leaves a trace.
    ~McDevice() override;

    /// Only while Disconnected; otherwise {Config, InvalidConfig}. Replaces the Session (values
    /// and subscriptions made at run time are dropped; cfg.subscriptions are applied).
    Expected<void> setConfig(const McDeviceConfig& cfg, QString* where = nullptr);
    const McDeviceConfig& config() const noexcept;
    /// Error of the current config, if any; connectToPlc() reports it through linkStateChanged.
    Expected<void> configStatus() const;
    void setLogSink(LogSink* sink);                 ///< Not owned; also used by the Session.

    // Subscriptions (dynamic; take effect at the next round boundary). Not written back to config().
    Expected<SubscriptionId> subscribe(Device head, quint32 count);
    Expected<SubscriptionId> subscribe(QStringView device, quint32 count);
    Expected<void> unsubscribe(SubscriptionId id);

    // Ad-hoc requests: exactly one requestFinished() per returned id.
    Expected<RequestId> submit(const Request& r);
    Expected<RequestId> writeWords(QStringView head, const QVector<quint16>& values);
    Expected<RequestId> writeBits(QStringView head, const QVector<bool>& values);
    Expected<RequestId> readWords(QStringView head, quint16 count);
    Expected<RequestId> readBits(QStringView head, quint16 count);

    LinkState linkState() const noexcept;
    const ValueStore& values() const noexcept;      ///< Read in a slot, or any time on the device's thread.
    const SessionStats& stats() const noexcept;

public slots:
    /// Disconnected → open (Connecting). Connected → re-publish Connected, no second socket.
    /// Connecting → no-op. Faulted → close, then open again. Always publishes a state.
    void connectToPlc();
    /// Completes outstanding requests with LinkDown, closes, publishes Disconnected(Requested).
    void disconnectFromPlc();

signals:
    void linkStateChanged(mc::LinkState state, mc::LinkReason reason, const QString& detail);
    void linkFault(const mc::LinkFaultInfo& fault);
    /// One response's changes, round >= 2 only (decision S3).
    void valuesChanged(mc::DeviceType type, quint32 round, const QVector<mc::Change>& changes);
    /// One device type after a round; after round 1 all types arrive together (decision S2).
    void snapshotReady(const mc::DeviceSnapshot& snapshot);
    void cycleDone(const mc::CycleInfo& cycle);
    void requestFinished(mc::RequestId id, const mc::Error& error, const QByteArray& payload);
};
}
```

`meta_types.h` declares every type used in a signal with `Q_DECLARE_METATYPE`; the `McDevice` constructors call `registerMetaTypes()` once, so queued connections across threads work without app code.

## Behaviour

### Link state machine

| From | Event | Action | To, signal |
|---|---|---|---|
| Disconnected | `connectToPlc()`, config invalid | none | Disconnected, `(Disconnected, OpenFailed, reason)` |
| Disconnected | `connectToPlc()` | build transport from config (unless injected), `open()` | Connecting, `(Connecting, Requested)` |
| Connecting | `opened()` | `session.linkUp(now)` | Connected, `(Connected, Requested)` |
| Connecting | `openFailed(r)` | none | Disconnected, `(Disconnected, OpenFailed, r)` |
| Connecting | `connectToPlc()` | none (no second socket) | Connecting, `(Connecting, Requested)` re-published (amended 2026-09-30) |
| Connected | `connectToPlc()` | none | Connected, `(Connected, Requested)` re-published |
| Connected | Session `LinkFault` | none: transport stays open, Session discards bytes | Faulted, `linkFault(info)` then `(Faulted, Fault, text)` |
| Connected, Faulted | `lost(r)` | `session.linkDown(now)` | Disconnected, `(Disconnected, PeerClosed or TransportError, r)` — chosen by `Transport::lastLossWasPeerClose()` |
| Faulted | `connectToPlc()` | `session.linkDown`, `close()`, `open()` | Connecting |
| any but Disconnected | `disconnectFromPlc()` | `session.linkDown`, `close()` | Disconnected, `(Disconnected, Requested)` |

`requestFinished` signals produced by `linkDown` are emitted **before** the state signal of the same call. The device never opens a transport on its own (decision 7). Every call to `connectToPlc()` ends in at least one `linkStateChanged`: the old app's runner once hung because a failed connect returned silently.

### Pump and signal order

Every public method, transport slot and timer slot ends the same way:

1. **Drain.** Pop every `Session` output. `Send` is written to the transport immediately. Every other output is converted to its Qt value (`QVector<Change>`, `DeviceSnapshot` copied from `values()`, `QByteArray` payload) and appended to the device's **signal queue**. A failed `write` is recorded; after the drain the device calls `session.linkDown(now)` and drains again.
2. **Arm.** Start the single-shot `QTimer` (`Qt::PreciseTimer`) at `nextDeadline() - now`, or stop it when there is no deadline. Time is a `QElapsedTimer` started in the constructor.
3. **Flush.** Only at the outermost call: pop the signal queue in FIFO order and emit each. A slot connected directly may call any `McDevice` method; that nested call drains into the same queue and returns; its signals are emitted after the ones already queued. The drain contract of the `Session` is therefore never violated, and signal order is causal.

Received bytes: on `readyRead`, read into a fixed 4 KiB buffer until `read` returns 0, calling `session.bytesIn(view, now)` and draining after each read.

### Threads

`McDevice` and its transport and timer are one object tree on one thread. The app may `moveToThread()` the device while it is `Disconnected`; all later calls must come from that thread or through queued connections / `QMetaObject::invokeMethod`. Nothing in the class locks a mutex.

## Testing Strategy

QtTest binaries under label `device`. The loopback server `MockPlcServer` hosts a fresh `MockPlc` per accepted connection and exposes the mock, a connection counter, `closeClient()` and `mute()`. Every test runs a real `QTcpSocket` on `127.0.0.1` with an OS-chosen port.

| ID | Covers |
|---|---|
| QDV-01 | Connect: `Connecting` → `Connected`; after round 1, one `snapshotReady` per subscribed type in `DeviceType` order, then `cycleDone(round=1)`; no `valuesChanged` in round 1 |
| QDV-02 | Mock memory changed between rounds → `valuesChanged` with exactly those points, then that type's `snapshotReady` |
| QDV-03 | `writeWords` → `requestFinished(ok)` and the mock's memory changed; `readWords` payload normalized; `readBits` honours byte-per-point |
| QDV-04 | Server closes the connection with requests in flight and queued → one `requestFinished(LinkDown)` per id, then `(Disconnected, PeerClosed)` |
| QDV-05 | Mock muted → `linkFault(Timeout, reopen=true)`, `(Faulted, Fault)`; no new connection within 3 × timeout (server counts 1); `connectToPlc()` reconnects and round 1 repeats (snapshots, no `valuesChanged`) |
| QDV-06 | Connect to a port nobody listens on → `(Disconnected, OpenFailed)` within `connectTimeoutMs + 500 ms` |
| QDV-07 | `connectToPlc()` while connected re-publishes `Connected`; the server still counts 1 connection |
| QDV-08 | A slot on `valuesChanged` calls `submit()` and `subscribe()`; the queued signals stay FIFO; the request completes |
| QDV-09 | Device moved to a `QThread` before connecting; signals received in the main thread through queued connections; values correct |
| QDV-10 | `McDeviceConfig` JSON: round trip is equal; missing keys → defaults; wrong type → error with the path; `schema: 2` → error |
| QDV-11 | Timeout precision: with the mock muted, `linkFault` arrives within `effectiveTimeoutMs() + 100 ms` of the send |
| QDV-12 | The connected socket has `LowDelayOption` and `KeepAliveOption` set |
| QDV-13 | Smoke over TCP for 3E ASCII, 1E Binary, 1E ASCII and 3C F1 (serial frame over TCP): one round and one write each |
| QDV-14 | Serial over a virtual COM pair (`MC_TEST_SERIAL_PAIR`): 3C F4 and 1C F1, a round and a write; `QSKIP` when the variable is unset |
| QDV-15 | `disconnectFromPlc()` with a request in flight → `requestFinished(LinkDown)` then `(Disconnected, Requested)` |
| QDV-16 | Invalid config (subscription `"Q10"`) → `connectToPlc()` publishes `(Disconnected, OpenFailed)` naming `subscriptions[0].device`; no socket opened |
| QDV-17 | Destroying a connected device with 2 queued requests: no crash, no signal after destruction starts, one `Warn` log line naming both ids, the server sees the socket close |

## Boundaries

**Always**

- Keep protocol behaviour in `Session`; this layer only adapts.
- Publish a link state for every `connectToPlc()` and every transport event.
- Emit `requestFinished` for every id returned while the object lives.

**Ask first**

- Adding a signal, a `LinkState`/`LinkReason` value or a config key (public API, JSON schema).
- Installing virtual COM pair software (com0com, socat) on a machine.
- Any automatic reconnect, even optional.

**Never**

- Block: no `waitFor*()`, no nested event loop.
- Start a thread or take a mutex inside the library.
- Emit a signal while the `Session` still holds undrained outputs.
- Link `mc::mock` into `mc_device`.

## Divergences from the old device (recorded, intentional)

| Old `McProtocolDevice` / `McMsgInterface` | New `McDevice` / `Transport` |
|---|---|
| Created its own transport; no injection point (so it was never executed in a test) | `Transport` can be injected; loopback tests drive the real class |
| Blocking `waitForConnected` / `waitForBytesWritten` / `waitForReadyRead` | Fully signal-driven |
| A mutex around the queue and the codec | One thread, no lock; the app moves the object |
| Periodic polling `QTimer` drives everything | One single-shot timer at the engine's next deadline |
| Base classes `IDevice`, `PlcDevice`, `Q_GADGET` configs | Plain structs with JSON |
| `deviceMChanged` / `deviceDChanged` per point, M and D only | `valuesChanged` per response for any type; `snapshotReady` per type |
| `Disconnected` vs `LostConnected` statuses | `LinkState` + `LinkReason` |
| Tag names `M0100` (four-digit padding) | Canonical `formatDevice()` spelling; padding is the app's business |

## Changes required in other specs (applied on 2026-09-26)

1. **`build-packaging`:** `examples/qt_console_poller` and `examples/virtual_plc` link `mc::device` and `mc::mock`; `tests/device` links `mc::mock`; `mc_device` itself must not (add to BLD-05's checks).
2. **`CAPABILITY-MAP.md`:** the `qt-device` row gains `examples/virtual_plc`.

## Success Criteria

1. QDV-01…13 and 15…17 pass on Windows (MSVC) and Linux (GCC); QDV-14 passes where a virtual COM pair exists and is skipped elsewhere.
2. No `waitFor`, `QEventLoop`, `QThread` or `QMutex` token appears in `src/device`.
3. The class compiles with only `mc/core/*.h` and Qt headers; `mc_device` does not link `mc_mock`.
4. `virtual_plc` and `qt_console_poller` talk to each other on one machine and print round-1 snapshots, then changes caused by `--wiggle`.

## Open Questions

1. **Serial setting types.** Use `QSerialPort` enums in the public header (proposed; `mc::device` already links SerialPort) or library-owned enums? *Decision:* `QSerialPort` enums; JSON keeps string values mapped by the library (2026-09-26).
2. **Frame and transport pairing.** Allow any pairing (proposed: a 3C frame over a TCP serial-device server is real) or enforce 3E/1E ↔ TCP and 3C/1C ↔ serial in `validate()`? *Decision:* allow any pairing; behaviour follows the frame; `connectToPlc()` logs an `Info` line when the pairing is unusual (3C/1C over TCP, 3E/1E over a COM port) (2026-09-26).
3. **Legacy JSON import.** Read the old `McProtocolConfig` keys (`refreshInterval`, `activeMDevice`, `startMAddress`, …)? Proposed: not in v1; a small helper in the consuming app. *Decision:* no; the library reads only its own schema, migration is the consuming app's job (2026-09-26).
4. **Runtime subscriptions and `config()`.** Proposed: not written back; the app persists what it wants. *Decision:* not written back; `config()` is always what the app supplied (2026-09-26).
5. **Destructor.** Proposed: no signals from the destructor; completions require `disconnectFromPlc()` first. *Decision:* no signals from the destructor; if requests are still outstanding it logs one `Warn` line with their count and ids (2026-09-26).
