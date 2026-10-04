# Spec: mock-plc

- **Module id:** `mock-plc` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** `core-protocol` (private primitives only, see "Independence rule"), `core-model`
- **Depended on by:** `qt-device` (tests, `examples/virtual_plc`), `core-session` (`examples/session_loop` only)
- **Inputs:** owner interview 2026-09-25 (hybrid construction); ideas doc §2 variant 6; reference spec `docs/mc_reference/mc-protocol-frame-spec.md` §4, §5.1, §5.3, §5.4–5.6, §6.3, §7.2, §9.1, §9.11, Appendix A; `SPEC-core-protocol.md`, `SPEC-core-session.md`.

## Objective

A **PLC responder in standard C++**, sans-I/O like the engine: request bytes in, response bytes out, with a device memory image and fault injection. It speaks the four v1 frames (3E and 1E in both codes, 3C and 1C in formats 1–4) and exists for three jobs:

1. **Integration tests without hardware.** `Session` ↔ `MockPlc` in one process, over an in-memory byte pipe and a fake clock, for every frame × code × format (the L6 matrix of spec §9.11, v1 subset). No socket, no thread, no Qt.
2. **Qt loopback tests.** `qt-device` wraps it in a `QTcpServer` test helper.
3. **A virtual PLC for demos** (`examples/virtual_plc`), like the old app's `VirtualPlcDevice`.

**Construction (owner decision, 2026-09-25): hybrid.** The mock reuses the core's *primitives* (device table, hex-ASCII, sum check, field codecs), which have their own PRIM and DEV vectors. It writes the *server direction* (decode a request, build a response) itself, from the tables of the reference spec. The client direction of `core-protocol` only encodes requests and parses responses, so the server direction is new code either way. Both directions are pinned to the **same golden vectors**, read in opposite directions. A bug shared by client and mock could only hide in a primitive, and the primitives are tested on their own. This is how the spec §9.1 advice ("do not import the client's encoder") is kept without writing the primitives twice.

**User stories**

- As `core-session`'s integration test, I connect a `Session` to a `MockPlc` for 3C format 2 and watch a full polling round, a write, a PLC error, a timeout with EOT and a sum-check retry, all in microseconds of real time.
- As a `qt-device` test, I serve a `MockPlc` over loopback TCP and change its memory between rounds to trigger change events.
- As a developer demoing an app, I run `virtual_plc --frame 3E --port 5000` and point the app at it.
- As a maintainer, the mock reproduces every Appendix A response vector byte-for-byte, so a green integration test means the client is right, not that client and mock agree on a mistake.

## Tech Stack

C++17, standard library only, doctest. The mock is test and demo infrastructure: it may allocate freely and is not bound by the complexity table of ideas §9.3. No exceptions across the public boundary, no Qt, no I/O, no clock.

## Commands

```powershell
cmake --build build/cmake-debug --target mc_mock_tests mc_integration_tests
ctest --test-dir build/cmake-debug -L "mock|integration" --output-on-failure
```

## Project Structure

```text
include/mc/mock/
└── mock_plc.h           MockOptions, Corruption, MockRequestRecord, MockPlc
src/mock/
├── memory_image.h/.cpp  sparse per-type pages; bit and word views of the same memory
├── request_decode_ethernet.cpp   3E, 1E request decoding (server direction)
├── request_decode_serial.cpp     3C, 1C formats 1–4, incremental, EOT, junk skipping
├── command_exec.cpp     batch read/write on the memory image; fault lookup
├── response_build.cpp   3E, 1E, 3C, 1C responses (spec §5 tables)
├── corruption.cpp       the Corruption modes
└── mock_plc.cpp         facade, request log
tests/mock/
├── test_mock_vectors.cpp    MCK-01…03, MCK-10
├── test_mock_memory.cpp     MCK-04, MCK-12
├── test_mock_stream.cpp     MCK-05…08
├── test_mock_faults.cpp     MCK-09, MCK-11
├── check_mock_includes.cmake  MCK-HYG (ctest script)
└── integration/
    ├── pipe.h               in-memory byte pipe with seeded random fragmentation
    ├── rig.h/.cpp           Session + MockPlc + FakeClock + pipe, one call per scenario step
    └── test_integration.cpp INT-xx over the 12-combination matrix
```

**Independence rule** (enforced by MCK-HYG). Files under `src/mock/` may include `mc/core/*.h` and exactly these private core headers: `core/protocol/hexascii.h`, `core/protocol/sumcheck.h`, `core/protocol/field_codec.h`, `core/protocol/device_encode.h`. They must not include `frame_*.h`, `command_*.h`, `serial_parser.h`, nor use `McProtocol` or `Parser`.

## Public API

### `mock_plc.h`

```cpp
namespace mc {

/// Error codes the mock answers with when a test does not choose one. These are TEST VALUES: where
/// a golden vector carries an error code it is reused (C051H, 50H, 5BH + 10H, 7151H, 06H); C059H
/// is an arbitrary choice. The library asserts nothing about their meaning (spec §7.2, §10 Q6).
struct MockOptions {
    uint16_t unsupportedQna{0xC059};   ///< 3E / 3C: command or subcommand not supported by the mock.
    uint8_t  unsupported1e{0x50};      ///< 1E end code (as in V-1E-B-12).
    uint8_t  unsupported1c{0x06};      ///< 1C NAK code (as in V-1C1-08).
    uint16_t sumErrorQna{0x7151};      ///< 3C request with a wrong SUM (code as in V-3C1-05).
    uint8_t  sumError1c{0x06};
    uint16_t outOfRangeQna{0xC051};    ///< Device beyond setDeviceLimit() (code as in V-3E-B-10).
    uint8_t  outOfRange1e{0x5B};  uint8_t outOfRange1eAbnormal{0x10};   ///< as V-1E-B-11
    uint8_t  outOfRange1c{0x06};
    LogSink* log{nullptr};             ///< Optional log sink (category "mc.mock"); nullptr = no logging. Owner decision 2026-10-03.
};

/// Ways to damage the next response(s), for protocol-error tests.
enum class Corruption : uint8_t {
    WrongSubheader,  ///< 3E/1E: first byte XOR 01H (ASCII: first character changed to 'E').
    WrongSumCheck,   ///< 3C/1C: SUM value + 1.
    WrongRoute,      ///< Station / network / PC field + 1.
    WrongBlockNo,    ///< Format 2: block number + 1.
    Truncate,        ///< Last byte dropped (the client must time out).
    JunkPrefix,      ///< Three 55H bytes before the frame (serial: must be skipped).
    ExtraByte        ///< One 00H byte after the frame (Ethernet: stream desync).
};

/// One decoded request, for assertions.
struct MockRequestRecord {
    FrameType frame;
    Op op;
    Device head;
    uint16_t count;
    PlcSeries series;       ///< From the subcommand actually received (QnA), else QL.
    bool answered;          ///< false when muted, station mismatch, or dropped.
    Error answeredWith;     ///< Ok, or the PLC error the mock returned.
    uint32_t stream{0};     ///< Input stream the request came from (0 = the default stream).
};

/// Identifies one input stream of a MockPlc (one client connection). 0 is the default stream.
using MockStreamId = uint32_t;

class MockPlc {
public:
    /// Speaks exactly the frame, code, format, sum-check and station settings of `cfg`
    /// (the same FrameConfig the client uses). Precondition: cfg.validate() is Ok.
    explicit MockPlc(const FrameConfig& cfg, const MockOptions& opt = {});
    /// Pimpl: movable, not copyable (amended 2026-09-30, owner decision).
    ~MockPlc();
    MockPlc(MockPlc&&) noexcept;
    MockPlc& operator=(MockPlc&&) noexcept;
    MockPlc(const MockPlc&) = delete;
    MockPlc& operator=(const MockPlc&) = delete;

    // ---- bytes (sans-I/O) --------------------------------------------------------------
    /// Request bytes from the client, any fragmentation. Complete requests are executed at once.
    void bytesIn(ByteView bytes);
    /// Next response to send, if any. The view is valid until the next call on this object.
    bool nextResponse(ByteView& out);

    // ---- streams (amended 2026-10-04, owner decision a1) --------------------------------
    // One server, several clients: each client gets its own stream, so its partial request
    // and its responses never mix with another client's. Memory, faults, the request log and
    // the counters stay shared. bytesIn(bytes) / nextResponse(out) above are stream 0, which
    // always exists.
    /// Opens a new stream and returns its id (never 0, never reused by this object).
    MockStreamId openStream();
    /// Closes a stream: its partial request and its unsent responses are discarded (not counted
    /// as EOT or skipped bytes). Closing 0 or an unknown id does nothing.
    void closeStream(MockStreamId id);
    /// As bytesIn(bytes) for one stream. Bytes for a closed or unknown stream are ignored.
    void bytesIn(MockStreamId id, ByteView bytes);
    /// As nextResponse(out) for one stream; false for a closed or unknown stream.
    bool nextResponse(MockStreamId id, ByteView& out);

    // ---- memory image ------------------------------------------------------------------
    /// Bit devices are single bits; word access to a bit device sees bit i of word k at
    /// head + 16k + i (spec §2.4). Unwritten memory reads as 0. Numbers are not aliased
    /// (M9000 on 1E is M9000, not SM1000).
    void setWord(Device d, uint16_t v);
    void setWords(Device head, std::initializer_list<uint16_t> values);
    void setBit(Device d, bool v);
    void setBits(Device head, std::initializer_list<bool> values);
    uint16_t word(Device d) const;
    bool bit(Device d) const;
    /// Points with number >= limit do not exist: any request touching one gets the
    /// out-of-range error of MockOptions. Default: no limit.
    void setDeviceLimit(DeviceType t, uint32_t limit);

    // ---- faults ------------------------------------------------------------------------
    /// Any request touching [first, last] of type t is answered with this PLC error
    /// (3E: end code + error information; 1E: end code, abnormal code when 5BH; 3C/1C: NAK /
    /// QNAK / NN with the code).
    void failRange(DeviceType t, uint32_t first, uint32_t last, uint16_t code, uint8_t abnormal = 0);
    void clearFaults();
    void mute(bool on);                          ///< Swallow every request (client times out).
    void muteNext(uint32_t n);                   ///< Swallow the next n requests.
    void corruptNext(Corruption c, uint32_t n = 1);

    // ---- observation -------------------------------------------------------------------
    const std::vector<MockRequestRecord>& requests() const;
    uint32_t eotCount() const;                   ///< EOT (or EOT CR LF) received.
    uint64_t skippedBytes() const;               ///< Serial: bytes skipped before a start byte or as unframable (junk); for debug traces (owner 2026-10-03).
    void clearLog();
};
}
```

## Behaviour

### Requests understood (v1)

| Frame | Commands | Notes |
|---|---|---|
| 3E, 3C | 0401 / 1401 with subcommands 0000, 0001 (Q/L) and 0002, 0003 (iQ-R) | Device layout chosen from the **received** subcommand, so a client sending the wrong one is caught |
| 1E | 00H, 01H, 02H, 03H | Points `00` = 256 (spec E8) |
| 1C | BR, WR, BW, WW (ACPU) and JR, QR, JW, QW (AnA) | Message wait character accepted and ignored |
| any | anything else (0403, 1402, 04H, 05H, BT, WT, …) | Answered with the family's `unsupported*` error |

### Serial reception (server side of spec §6.3)

- **Start:** F1, F2, F4 skip bytes until `ENQ`; F3 skips until `STX`. Skipped bytes are counted in `skippedBytes()` (cleared by `clearLog()`; not in the request log) and logged at `Trace` through `MockOptions::log` (category `"mc.mock"`) with the running count, to help debug traces (owner decision 2026-10-03). In format 4 the `CR LF` of an `EOT CR LF` belongs to the EOT and is not counted as skipped.
- **Length:** F1, F2 and F4 requests have no terminator before SUM (F4 ends in `CR LF` after it), so the decoder computes the request-data length from the command and the point count, exactly as the tables of spec §4.1–4.3 define it. F3 reads up to `ETX`.
- **Sum check:** verified when `cfg.sumCheck`; wrong → NAK (F3: `QNAK` / `NN`) with `sumError*`.
- **Station:** a request whose station number differs from `cfg.stationNo` gets **no response** (multidrop behaviour).
- **EOT:** `EOT` (F4: `EOT CR LF`) at any point discards the partial request and increments `eotCount()`.

### Streams

Every stream has its own reception state (partial request, serial scan position, F2 block state)
and its own response queue. Requests execute in the order they complete, whatever their stream,
against the one memory image; `mute`, `muteNext`, `corruptNext` and `failRange` count and apply
across all streams in that order. `requests()` records the stream of each request; `eotCount()`
and `skippedBytes()` are totals over all streams. Closing a stream mid-request drops that partial
request only, so the next client never sees it (the `virtual_plc` behaviour of one `MockPlc` per
connection, with shared memory).

### Ethernet reception

3E requests are framed by the request-data-length field; 1E requests by the command's fixed layout plus the point count. Bytes that cannot start a request (wrong subheader) are answered with nothing and logged in the request record as unanswered, so a client desync shows up as a timeout, like a real module. The 3E response **echoes the request's route**; `Corruption::WrongRoute` is how route checking is tested.

### Responses

Built from the spec §5 tables for the configured frame: 3E header + response data length + end code + data or error information; 1E subheader `cmd | 80H` + end code (+ abnormal code) + data, with the trailing dummy character on odd ASCII bit reads and a zero low nibble on odd binary bit reads; 3C/1C per format with `ACK`/`NAK`, `QACK`/`QNAK`, `GG`/`NN`, SUM when enabled, `CR LF` for F4, block number for F2. `f3ShortResponseHasSum` is honoured (spec Q1).

## Testing Strategy

Two doctest binaries. `mc_mock_tests` (label `mock`) checks the mock against the golden vectors. `mc_integration_tests` (label `integration`) runs `Session` against the mock.

### Mock against the vectors

| ID | Covers |
|---|---|
| MCK-01 | Every request vector of A.1, A.2, A.5, A.6, A.12–A.19 decodes to the op, head, count, series and write data stated in its metadata |
| MCK-02 | With memory seeded from the vector metadata, every success response vector of those sections is reproduced byte-for-byte |
| MCK-03 | Error vectors reproduced with `failRange`: V-3E-B-10 / A-10 (C051H + error information), V-1E-B-11 / A-11 (5BH + 10H), V-1E-B-12 / A-12 (50H), V-3Cn-05 (7151H), V-1Cn-08 (06H) |
| MCK-04 | Writes change memory: word write, bit write with odd count, word write to a bit device packs 16 points per word |
| MCK-05 | Every request vector fed one byte at a time, and two requests back-to-back in one buffer, decode identically |
| MCK-06 | Serial: junk before `ENQ`/`STX` skipped; `EOT` mid-request resets and counts; F4 without `LF` stays incomplete |
| MCK-07 | Serial station mismatch → no response; 3E response echoes a non-default request route |
| MCK-08 | Wrong SUM in a request → NAK with `sumError*`; `sumCheck=false` → no SUM expected or produced |
| MCK-09 | 0403, 1402, 1E 04H, 1C WT → the family's `unsupported*` error |
| MCK-10 | Odd bit reads: 1E ASCII trailing dummy (V-1E-A-10), binary zero nibble (V-1E-B-10, V-3E-B-09) |
| MCK-11 | `mute`, `muteNext`, and each `Corruption` mode produce exactly the documented bytes (or none) |
| MCK-12 | `setDeviceLimit`: a request reaching the limit gets the out-of-range error; one below it succeeds |
| MCK-13 | Streams: two streams fed interleaved fragments (one byte each, alternating) decode both requests and answer each on its own stream, on every frame family; a stream closed mid-request leaves no trace for the next stream; stream 0 calls equal the stream-less calls; faults and the request log are shared (`stream` recorded); a closed or unknown id is ignored |
| MCK-HYG | `src/mock/**` includes only the headers allowed by the independence rule |

The vector loader is shared with `core-protocol` (see "Changes required in other specs").

### Integration: `Session` ↔ `MockPlc` (spec §9.11, v1 subset)

The rig connects `Session::nextOutput(Send)` → pipe → `MockPlc::bytesIn` and `MockPlc::nextResponse` → pipe → `Session::bytesIn`. The pipe splits every transfer into random fragments from a fixed seed. The clock is fake: a muted mock is a jump of the clock past `nextDeadline()` followed by `tick()`. Unless a scenario says otherwise the rig uses `readRetries = 2` (the `FrameConfig` default is 0, which would hide retries) and `maxConsecutiveLinkErrors = 3`.

**Matrix, 12 combinations:** 3E Binary, 3E ASCII, 1E Binary, 1E ASCII, 3C F1–F4, 1C F1–F4. Every scenario below runs on every combination unless the column says otherwise.

| ID | Scenario | Expected |
|---|---|---|
| INT-01 | Ad-hoc `writeWords(D100, 1995H, 1202H, 1130H)` then ad-hoc read of D100×3 | same values; one `RequestDone` each |
| INT-02 | `writeBits(M100, 1,1,0,0,1,1,0,0)` then read M100×8 | same values |
| INT-03 | Odd count: write 5 bits, read 5 bits | same values (nibble padding, 1E dummy) |
| INT-07 | Ad-hoc read D0×2000 | the mock logs `chunkCount()` requests for the frame (3E Binary: 3; 1E Binary: 8); payload concatenated in order |
| INT-08 | `failRange(D, 100, 100, code)` then read D100×1 | `RequestDone` with `{Plc, PlcError, plcCode = code}` (u8 for 1E/1C) |
| INT-10 | Subscribe D100×64, M0×64, X0×32; round 1; change D105 and M5 in the mock; round 2 | round 1: no `ValuesChanged`, then snapshots X, M, D (`DeviceType` enum order) and `CycleDone`; round 2: `ValuesChanged` for D105 and M5 only, a snapshot per type |
| INT-11 | `mute(true)` during polling | 3E/1E: `LinkFault{Timeout, reopen}`; 3C/1C: `eotCount()` grows, the read is repeated `readRetries` times (visible in the request log), then `LinkFault{Timeout, reopen=false}` after `maxConsecutiveLinkErrors` |
| INT-12 | `corruptNext(WrongSumCheck)` (serial) / `corruptNext(WrongSubheader)` (Ethernet) | serial: EOT, retry succeeds, values correct; Ethernet: `LinkFault{ProtocolError, reopen}` |
| INT-13 | Same memory evolution with `bitsAsWords` on and off | identical `ValueStore` contents and identical change events |
| INT-14 | Heartbeat on (M2000) for 5 rounds | the mock's M2000 reads 1, 0, 1, 0, 1 after rounds 1–5 |
| INT-15 | INT-10 repeated with 20 fragmentation seeds | identical output sequences |
| INT-16 | `setDeviceLimit(M, 8190)`, subscribe M8180×10 with `bitsAsWords` on (end aligned up to 8191) | chunk fails with the out-of-range error every round; with `bitsAsWords` off it succeeds (pins `SPEC-core-session.md` open question 4) |

INT-04, INT-05, INT-06 (random access) and INT-09 (4C F5) stay tagged `v1.1` / `v2` and are skipped.

Coverage: with `MC_COVERAGE=ON`, line coverage of `src/mock` ≥ 90 %.

## Boundaries

**Always**

- Derive the server direction from the reference spec tables; verify it with the golden vectors in the reverse direction.
- Keep the mock deterministic: same inputs, same bytes out. Randomness lives only in the test pipe, behind a fixed seed.
- Keep the independence rule (MCK-HYG).

**Ask first**

- Letting `src/mock` include any other private core header, or use `McProtocol` / `Parser`.
- Changing a default in `MockOptions` (tests depend on them).
- Adding timing behaviour (delays) to the mock itself rather than to the test rig.

**Never**

- Generate responses by calling the client encoder, or verify requests by calling the client parser.
- Perform I/O, read a clock, or start a thread.
- Include a Qt header.

## Changes required in other specs (applied on 2026-09-26)

1. **`core-protocol`:** move the vector loader from `tests/core/protocol/vectors.h/.cpp` to `tests/common/vectors.h/.cpp` so `tests/mock` uses the same parser of `.vec` files; the loader must expose each vector's metadata (`op`, `device`, `count`, `expect`, write data) as data, not only the hex line.
2. **`build-packaging`:** add `tests/common/` (owned by `core-protocol`) to the structure; register `mc_integration_tests` under label `integration`, built when `MC_BUILD_MOCK` and `MC_BUILD_TESTS` are both on; let `mc_mock` see `src/` privately (already the rule for every target).
3. **`CAPABILITY-MAP.md`:** the `mock-plc` row states the hybrid construction and that the std-only integration matrix lives here.

## Success Criteria

1. MCK-01…03 reproduce every enabled Appendix A vector in the reverse direction, naming the vector ID on failure.
2. All 12 matrix combinations pass INT-01…03, 07, 08, 10…16.
3. MCK-HYG passes; adding `#include "core/protocol/frame_3e.h"` to a mock source makes it fail.
4. `mc_integration_tests` completes in under 5 seconds of wall time (fake clock, no sleeps).
5. `examples/session_loop` and `examples/virtual_plc` build against `mc::mock` without any test header.

## Open Questions

1. **Default error codes** in `MockOptions` reuse the vectors' codes (C059H is arbitrary); their meanings are not asserted. Keep them (proposed) or pick codes from a module manual? *Decision:* keep the vector codes; the header states they are test values with no asserted meaning (2026-09-26).
2. **3E route mismatch.** Echo the request's route (proposed, like a relaying module) or stay silent? *Decision:* answer from the mock's memory and echo the request's route (2026-09-26).
3. **Semver status of `mc::mock`.** Proposed: its header is public and versioned like the rest, because consumers will use it for demos. Alternative: mark it "test utility, no compatibility promise". *Decision:* public and versioned under the same semver rule as the rest of `include/mc/` (2026-09-26).
4. **Special-relay aliasing.** Proposed: none (M9000 on 1E is M9000 in the mock). Needed only if a test wants the same memory seen through 1E and 3E. *Decision:* no aliasing in v1; may be added later as a `MockOptions` flag (2026-09-26).
