# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- `MockPlc` input streams, one per client of a server: `MockStreamId`, `openStream()`,
  `closeStream()`, `bytesIn(id, bytes)`, `nextResponse(id, out)` and `MockRequestRecord::stream`.
  Each stream has its own partial request, serial scan state and response queue; memory, faults,
  the request log and the counters stay shared, and the stream-less calls are stream 0. The
  `mc_workbench` mock tab opens one stream per TCP client, so interleaved partial requests and a
  request abandoned by a closed client no longer corrupt another client.
- X/Y numbering for FX CPUs: `XyNumbering { Hex, Octal }`, `FrameConfig::xyNotation` (how X/Y
  numbers are written as text) and `FrameConfig::xyAsciiDigits` (the digits of X/Y numbers inside
  ASCII frames: 3E, 1E, 3C, 1C), both defaulting to `Hex`, and the overloads
  `parseDevice(text, XyNumbering)` and `formatDevice(d, out, capacity, XyNumbering)`. A `Device`
  keeps holding the point index and Binary frames always carry it. `validate()` counts the digits
  actually written, `MockPlc` reads them the same way, `McDeviceConfig` gets the JSON keys
  `frame.xyNotation` and `frame.xyAsciiDigits` (`"Hex"` or `"Octal"`; a missing key is `Hex`) and
  reads its text devices (subscriptions, heartbeat, `McDevice` text arguments) in `xyNotation`.
  `virtual_plc` and `qt_console_poller` take `--xy` and `--xy-ascii` (`octal` or `hex`).
- `MockPlc::skippedBytes()`: serial bytes skipped before a start byte or as unframable, reset by
  `clearLog()`. `SessionConfig::validate()` (so `Session::create()` and
  `McDeviceConfig::validate()`) rejects `maxConsecutiveLinkErrors == 0` with `InvalidConfig`; the
  default stays 3.
  `MockOptions::log` writes one `Trace` line per skipped serial byte; the `CR LF` of a format 4
  `EOT CR LF` is not counted as skipped.
- CMake build skeleton: `mc::core` static library, the `MC_BUILD_*` option set, warning and
  version helpers, vendored doctest, and the first test binary (BLD-01, BLD-08, BLD-09).
- qmake mirror: `mc_core.pri`, `mc_device.pri`, `mc_mock.pri`, `mc_protocol.pri`, and the
  dev-only `mc_protocol.pro` (BLD-03).
- Build guard tests: `pri_sync` (BLD-04) and `include_hygiene` (BLD-05), plus the CMake and
  qmake consumer smoke projects (BLD-06, BLD-07).
- `scripts/check.ps1` and `scripts/check.sh`: one command that builds and tests both build
  systems, both compilers, and the consumer smoke projects (BLD-01 through BLD-09).
- `.clang-format`, this changelog, `README.md` and `LICENSE`.
- `mc/core/types.h` (`ByteView`, `MutableByteView`, `ByteBuf`, `kNoCode`) and `mc/core/result.h`
  (`ErrorCategory`, `ErrorCode`, `ErrorInfo`, `Error`, `Expected<T>`, `Expected<void>`), the first
  headers of `core-model`; test binary `mc_core_model_tests` (RES-01 through RES-04).
- `mc/core/device.h`: the device model (`DeviceKind`, `Radix`, `DeviceType`, `DeviceInfo`,
  `deviceInfo()`, `Device`, `parseDevice()`, `formatDevice()`) with the full spec §3.2 device
  table (DEV-01 through DEV-07-data, DEV-14).
- `mc/core/frame_config.h` (`FrameType`, `DataCode`, `SerialFormat`, `PlcSeries`, `TargetFamily`,
  `C1CommandSet`, `FrameConfig` with its named constructors, `validate()`, `effectiveTimeoutMs()`,
  `isSerial()`) and `mc/core/request.h` (`Op`, `BitLayout`, `Request` with its builders,
  `validate(Request, FrameConfig)`) (CFG-01 through CFG-05, DEV-08 through DEV-13).
- `mc/core/limits.h`: the spec §4.4 point-limit table transcription (`maxPoints()`) and request
  splitting into commands that fit it (`Chunk`, `chunkCount()`, `chunk()`) per spec §8.5
  (LIM-01, LIM-02, CHK-01 through CHK-06). The random-access rows of spec §4.4 ("0403", "1402")
  are transcribed too, as internal (non-public) data in `src/core/model/limits_table.h`/`.cpp`,
  for v1.1 (LIM-03).
- `mc/core/convert.h` (namespace `mc::convert`): word/dword/float32/float64/string accessors and
  writers, `packBits`/`unpackBits`, `wordsToBits`/`bitsToWords`, and the owning `from*` builders,
  all bounds-checked (spec §2.3, §2.4, §8.7; CNV-01 through CNV-08).
- `mc/core/log.h`: `LogLevel`, `LogSink`, `NullLogSink`, `hexDump()` (spec §8.6; LOG-01, LOG-02).
  `tests/common/alloc_counter.h`: a shared counting override of global `operator new`/`delete`
  for every zero-allocation test; ALC-01 proves zero allocations across `parseDevice`,
  `validate`, `chunk`, `convert::float64At`, `hexDump` and `formatDevice`.
- Checkpoint A: `MC_COVERAGE` implemented (`cmake/mc_coverage.cmake`, GCC/Clang only, no effect
  on MSVC) and `mc_coverage_report` (`cmake/mc_coverage_report.cmake`), a `gcov`-based report of
  `src/core/model`'s line coverage per file and in total (no Python on this machine, so no
  `gcovr`). `core-model` verified on MinGW GCC 13.1 for the first time this phase, with coverage
  ≥ 95 %.
- `mc/core/protocol.h`: `McProtocol` and `Parser` (`ParseStatus`), the codec facade that turns a
  validated `Request` into one complete frame and a response frame back into a normalized payload
  or an `Error` — the public contract `core-session` (Phase 3) builds on. 3E is fully implemented,
  Binary and ASCII (request envelope, response parsing, error mapping per spec §5.1/§7.2);
  `Parser` is a fixed-size, trivially copyable value type (`sizeof(Parser) == 96`); every other
  frame family/wire code not implemented yet reports `ErrorCode::UnsupportedCommand` rather than
  undefined behaviour. Golden-vector round trips: every Appendix A.1/A.2 vector (3E Binary/ASCII);
  A.3/A.4 (4E) transcribed and tagged `v2`, skipped. Streaming: byte-at-a-time and coalesced-frame
  parsing, `reset()` recovery (STR-01, STR-02, STR-04); zero allocations across encode, `feed()`
  and `payload()` (ALC-01).
- Supporting internal layers under `src/core/protocol/` (`mc::detail`, not part of the public
  surface): hex-ASCII and 8-bit sum-check primitives (`hexascii.h`, `sumcheck.h`); the
  `AsciiCodec`/`BinaryCodec` field codec (`field_codec.h`) sharing one code path per field kind,
  with no ASCII/Binary branch outside device encoding; on-wire device address encoding for all
  eight device families (`device_encode.h`, DEV-07 wire half); the QnA `0401`/`1401` batch
  read/write command layer (`command_qna.h`), shared unchanged by 3E and (later) 3C. Golden
  vectors: `prim.vec`, `cmd.vec`, `cmdd.vec`, `3e_binary.vec`, `3e_ascii.vec`, `4e_binary.vec`,
  `4e_ascii.vec`, each self-checked against the reference spec's own bytes before being typed in.
  The shared `.vec` loader (`tests/common/vectors.h`/`.cpp`) and its own transcription guard
  (VEC-02: every vector's hex byte count matches its own `bytes:` heading) are reused by every
  test file above and will be by `tests/mock` and `tests/replay` later.
- `mc/core/session.h` and the sans-I/O `core-session` engine (Phase 3, T-020 through T-028;
  `SPEC-core-session.md`): `RangeSet`/`ReadPlan` (subscription bookkeeping, alignment, gap
  merging and chunking, `poll_plan.h`; PLN-01 through PLN-10), `ValueStore` (per-device-type
  segments, `apply()`/`markFailed()` with decision S4's silent baseline, `rebuild()` carry-over on
  a re-plan, `value_store.h`; STO-01 through STO-04), and `Session` itself — round scheduling
  (`FixedRate`/`FixedDelay`), the receive path and value publishing (decisions S1–S5, reproducing
  the spec's own worked timeline), dynamic `subscribe()`/`unsubscribe()` deferred to the next
  round boundary, ad-hoc requests (a FIFO ring arena and job queue, chunked reads and split
  writes, `maxAdHocBurst` dispatch priority, exactly-once `RequestDone` including on `linkDown()`
  — SES-09 through SES-15), the Ethernet column of the fault table (timeout, protocol error,
  unsolicited bytes, receive-buffer overflow → `LinkFault`, no resend, no auto-reconnect —
  SES-16, SES-19, SES-20, SES-24), the drain contract (a replaceable, debug-only violation
  handler behind a private header, `src/core/session/drain_violation.h`, so the spec's own
  debug-asserts/release-discards split is both real and testable — SES-25), and the optional
  heartbeat write (SES-21). Zero allocation in the steady state (ALC-01: rounds 3–10, changes
  every round, heartbeat on) and across 1000 ad-hoc submit/complete cycles including a
  `linkDown()` with a full queue (ALC-02). `mc_bench_session` (`tests/bench`, `-DMC_BUILD_BENCH`
  only): per-round cost vs. subscribed points, regression tracking only, no threshold.
  Checkpoint C: verified on both MSVC and MinGW GCC 13.1 (`scripts/check.ps1`, both kits); line
  coverage of `src/core/session` 97.77% total (a latent `mc_coverage_report.cmake` bug —
  `file(STRINGS)` silently merging lines whose own text contained an unescaped `;`, occasionally
  under-counting a file to as little as 0 lines — found and fixed along the way); `core-model` and
  `core-protocol` unaffected, still ≥ 95%.
- `mc/mock/mock_plc.h` and the sans-I/O `mock-plc` module (Phase 4, T-029 through T-034;
  `SPEC-mock-plc.md`; the `mc::mock` static library, std-only, no Qt): `MockPlc`, a PLC responder
  with request bytes in and response bytes out, a sparse per-device-type memory image (bit and
  word views of one memory, unwritten memory reads 0, no aliasing, per-type `setDeviceLimit()`;
  MCK-04, MCK-12), the request log (`requests()`, `eotCount()`, `clearLog()`), and the 3E server
  direction in Binary and ASCII, the only frame answered in this version, written from the
  reference spec's tables rather than by calling the client (MCK-01 through MCK-03, MCK-05,
  MCK-07, MCK-10: the golden vectors are reproduced byte for byte in the reverse direction,
  fragmented one byte at a time and coalesced). Fault injection: `failRange()`,
  `mute()`/`muteNext()`, `corruptNext()` with the Ethernet `Corruption` modes, unsupported
  commands answered with the configured error code (MCK-09, MCK-11). The independence rule
  (`src/mock` includes only the four allowed private primitive headers and never the client
  codec) is enforced by the `MCK-HYG` ctest script, with negative controls that prove it bites.
  The integration matrix `mc_integration_tests` (`tests/mock/integration`; label `integration`)
  runs `Session` against `MockPlc` in one process, over an in-memory pipe with seeded random
  fragmentation and a fake clock (no sleeps, no threads, no sockets), on 3E Binary and 3E ASCII:
  ad-hoc writes and reads, odd bit counts, `chunkCount()` splitting, PLC errors, a polling round
  with change events, timeout and protocol-error link faults, `bitsAsWords` on and off, the
  heartbeat, 20 fragmentation seeds and `bitsAsWords` past a device limit (INT-01, 02, 03, 07,
  08, 10 through 16); the 1E, 3C and 1C cells join the matrix with Phase 6.
  `examples/session_loop` (`MC_BUILD_EXAMPLES`, built when `MC_BUILD_MOCK`; no Qt, no header from
  `tests/`) is the non-Qt usage of `Session` against `MockPlc` through an in-memory pipe and
  `std::chrono::steady_clock`, printing round-1 snapshots and then the changes made in the mock,
  registered as the `examples.session_loop` smoke test and built through qmake too
  (`examples/qmake`, `mc_protocol.pro`). `mc_mock.pri` mirrors the CMake target. BLD-04 also
  holds with a layer switched off (`-DMC_BUILD_MOCK=OFF`): the source list of a disabled layer is
  exported from a variable, so its `.pri` is still compared. Checkpoint C4: verified on MSVC and
  MinGW GCC 13.1 (`scripts/check.ps1`, both kits; the qmake binaries list the same test cases as
  their CMake twins); line coverage of `src/mock` 96.74% total, `core-model`, `core-protocol`
  and `core-session` still ≥ 95%.
- `mc::device`, the Qt device layer (Phase 5, T-035 to T-040; `SPEC-qt-device.md`; the `mc_device`
  static library, `AUTOMOC`, Qt 6.2 API only, links `mc::core` and Qt only, never `mc::mock`):
  `Transport` (the abstract byte transport with its `opened`, `openFailed`, `readyRead`, `lost`
  signals), `TcpTransport` (`QTcpSocket` with `LowDelayOption` and `KeepAliveOption`, connect
  timeout as a single-shot `QTimer`, no signal after `close()`), `registerMetaTypes()`,
  `SerialSettings`, and `McDeviceConfig` (`TransportKind`, `SubscriptionSpec`, `validate()` naming
  the offending JSON path, `toJson()` / `fromJson()` for schema 1: every `FrameConfig` and
  `SessionConfig` field has a key). `mc_device.pri` mirrors the CMake target; QtTest binaries
  `mc_tcp_transport_tests` and `mc_config_json_tests` (label `device`; QDV-06 and QDV-12 at
  transport level, QDV-10, QDV-16 at validate level) have qmake twins, and `QDV-HYG` (with
  negative controls) keeps blocking waits, nested event loops, threads and mutexes out of
  `src/device` and `include/mc/device`. With `-DMC_BUILD_DEVICE=OFF` CMake still never searches for Qt and BLD-04 still
  compares `mc_device.pri`. `McDevice` (`mc/device/mc_device.h`): a thin Qt adapter that drives a
  `Session` over a `Transport` (link state machine `LinkState` / `LinkReason`, a FIFO signal queue
  flushed only by the outermost call so slots may call back into the device, one single-shot
  deadline timer, `linkStateChanged` / `linkFault` / `valuesChanged` / `snapshotReady` /
  `cycleDone` / `requestFinished` carrying Qt value types, `setConfig()` / `subscribe()` /
  `submit()` and the `writeWords` / `readWords` family). It never blocks, starts no thread,
  takes no mutex and never reconnects by itself; `Transport::lastLossWasPeerClose()` tells a peer
  close from an I/O error. Test binaries `mc_device_tests` (QDV-01 to 08, 11, 13 (3E ASCII), 15 to 17 on
  loopback TCP against a `MockPlc` server, plus fault, config and log tests on a fake transport)
  and `mc_device_thread_tests` (QDV-09) have qmake twins. Examples `virtual_plc` (a TCP server in
  front of `MockPlc`, `--set` / `--wiggle`) and `qt_console_poller` (an `McDevice` printing
  round-1 snapshots and each change) run against each other on one machine.
  Checkpoint D: `mc_coverage_report_device` prints line coverage of `src/device` per file (report only,
  no bar; 91.68% total on MinGW), the CMake and qmake consumer projects (BLD-06, BLD-07) now build an
  `McDevice` and link `mc::device` / `mc_protocol.pri`, and `scripts/check.ps1` puts the kit's Qt
  `bin` on PATH when it runs them; the device tests, both examples, the qmake twins and
  `scripts/check.ps1` pass with MSVC and MinGW GCC 13.1.
- The 1E frame (Binary and ASCII) through every layer (batch 6a). Core protocol: 1E commands 00H to
  03H and the 1E envelope in `McProtocol` / `Parser` (the command code travels in the subheader,
  the response length follows from the request, end code 5BH is followed by an abnormal code, any
  other end code ends the frame after two bytes or four characters), pinned to the Appendix A.5 and
  A.6 vectors (`1e_binary.vec`, `1e_ascii.vec`) and the 1E rows of the CMD and CMDD vectors; the
  04H and 05H encoders exist but are not reachable through `Op`. `MockPlc` answers 1E (00H to 03H
  executed, 04H and 05H answered with `unsupported1e`; the abnormal code only after 5BH; the
  trailing dummy character or zero nibble of an odd bit read) and every `Corruption` mode except
  `WrongRoute`, which leaves a 1E response unchanged because it has no route field.
  `virtual_plc --frame 1E` and `qt_console_poller --frame 1E` run against each other; the
  integration matrix (INT-01 to INT-16) runs on 3E and 1E, Binary and ASCII, and QDV-13 covers 1E
  Binary and 1E ASCII over TCP. Two defects that only a 1E or 1C frame reached are fixed:
  `chunkCount()` and `chunk()` no longer refuse a request above the 256-unit field maximum (they
  split it by `maxPoints()`; the encode path still rejects 257 points), and
  `Session::subscribe()` checks the word-aligned range the plan will read, so a bit subscription
  such as `M8180 x 10` is accepted on 1E and 1C. `autoGap()` gives 7 for 1E words. MSVC and MinGW
  GCC 13.1, both qmake kits and `scripts/check.ps1` / `check.sh` pass.
- The serial frames 3C and 1C, formats 1 to 4 (batch 6b). Core protocol: a serial receive state
  machine (start on STX, ACK or NAK, junk before the start counted in `Parser::skipped()`, an ETX
  scan over new bytes only, the sum check, CR LF, block number and the Format 3 `QACK` / `QNAK`
  and `GG` / `NN` forms with and without a SUM on the short responses), the 3C and 1C envelopes in
  `McProtocol` / `Parser` (frame ID and access route, sum check ranges, route and block number checks,
  2- and 4-character error codes mapped to `Plc` errors) and the 1C command layer (BR, WR, BW, WW
  for ACPU and JR, QR, JW, QW for AnA/AnU, the message wait character, 256 points as `00`). The
  BT / WT test encoders exist but are not reachable through `Op`. Pinned to the Appendix A.12 to
  A.19 vectors (`3c_f1.vec` to `3c_f4.vec`, `1c_f1.vec` to `1c_f4.vec`) and the 1C rows of the CMD
  and CMDD vectors; the 4C vectors of A.7 to A.11 are transcribed and tagged `v2`. ALC-01 now covers
  every v1 family (3E and 1E in both data codes, 3C and 1C in all four formats): every request
  vector encodes and every response vector parses byte by byte with zero allocations.
  A `messageWait` above 15 is refused by `McProtocol::encode()` with `InvalidConfig`. VEC-RT proves
  that all 309 enabled vector records of the twelve v1 families (the Appendix A rows plus the
  derived rows) round-trip and that the 45 tagged `v1.1` / `v2` ones are present and skipped. MSVC
  and MinGW GCC 13.1 and both qmake kits pass; line coverage of `src/core/protocol` is 97.9 %.
- The serial transport and the serial behaviour of the Session through every layer (batch 6c,
  Checkpoint E). `Session`: on a serial frame a timeout or protocol error sends EOT (`EOT CR LF` in
  Format 4), flushes until `serialFlushMs` of silence (capped at `effectiveTimeoutMs()`, the cap
  counting as one more link error), resends a read up to `readRetries` times with identical bytes
  and fails a write with `Timeout` without ever resending it, faults the link after
  `maxConsecutiveLinkErrors` errors in a row (`reopenTransport = false`; a PLC error resets the
  count), applies the first-byte and inter-character deadlines and discards unsolicited bytes
  (SES-17 to SES-20, SES-24, SES-27; ALC-01 covers a serial retry and an EOT/flush cycle).
  `MockPlc` answers 3C and 1C in formats 1 to 4 pinned to the Appendix A.12 to A.19 vectors read in
  the reverse direction (junk skipped until the start byte, EOT counted and discarding a partial
  request, station mismatch unanswered, wrong SUM answered with NAK, every `Corruption` mode on
  serial frames; MCK-01 to MCK-12). The integration matrix runs on the twelve frame cells (3E, 1E,
  3C and 1C in every data code or format). `SerialTransport` (`QSerialPort`, the port opens one
  event-loop turn after `open()`, a port error while open ends the link with `lost()`) is built by
  `McDevice` for `TransportKind::Serial`; QDV-13 adds 3C Format 1 over TCP and QDV-14 runs 3C
  Format 4 and 1C Format 1 over a virtual COM pair (`MC_TEST_SERIAL_PAIR`, skipped without it; the
  ctest entry holds the `mc_serial_pair` resource lock). A serial line that keeps sending during
  `Flushing` still ends the flush at its cap through `McDevice`. `virtual_plc --serial COMx
  [--baud N]` serves a `MockPlc` on a COM port and `qt_console_poller` takes `--serial`, `--baud`
  and `--format`. Line coverage on MinGW: `src/core/session` 98.1 %, `src/core/protocol` 97.9 %,
  `src/mock` 97.3 %, `src/device` 91.8 % (report only). MSVC and MinGW GCC 13.1, both qmake kits,
  `scripts/check.ps1` (both kits) and `check.sh` pass.
- `scripts/check.ps1` and `scripts/check.sh` build in parallel: `-Jobs` / `MC_CHECK_JOBS` (default: the logical processor count) for `cmake --build --parallel`, `jom -j` (MSVC qmake stages, `nmake` fallback if no jom) and `mingw32-make -j`; `ctest -j 8`.
- Phase 7, HIL capture tooling (`SPEC-hil-capture.md`), no hardware involved: the developer tool `tools/hil_capture`
  (`MC_BUILD_TOOLS`) loads a profile and a plan, runs a safety gate that refuses every write outside the
  declared scratch area before anything connects (`--dry-run` prints the frames), drives `McDevice` through a
  `RecordingTransport` (mutate and raw frames included), recovers link faults, and writes `steps.vec`,
  `session.vec`, `run.meta` and `bench.csv`; `--report` builds the timing tables. The four plans of
  `docs/hil/COMMAND-CATALOGUE.md` live in `tests/hil/plans`, example profiles in `tests/hil/profiles`. Test
  binaries `mc_hil_tool_tests` (HIL-01 to HIL-06, label `hil_tool`) and the std-only `mc_replay_tests` (RPL-01
  to RPL-06, label `replay`, with committed `virtual_plc` fixtures). Captures made against `virtual_plc` never
  go to `tests/vectors/captured/`. A frame the gate cannot fully decode (`readOnly`) needs a `recover` step and
  an explicit confirmation even with `--yes`, and a mutated write can never be declared `readOnly`; `run.meta`
  is scrubbed of the profile's host, port and COM port name.
- Phase 8, the GUI tool `mc_workbench` (`SPEC-gui-tool.md`; `tools/mc_workbench`, `MC_BUILD_GUI`, a developer tool like
  `hil_capture`, not part of the library): a Qt 6.5+ Widgets application on the public API. Every `McDevice`, transport,
  mock server and HIL run lives on its own runner thread; the GUI thread only renders and forwards queued commands and
  value copies, a throwing runner or a failed port is contained in its tab, and closing a tab or the window joins its
  threads. Device tabs: the whole `McDeviceConfig` as a property grid (every field editable, a refused value shows the
  library's own `validate()` message), connect and disconnect, run-time subscriptions, a live points table with change
  highlight, a trend chart and an ad-hoc read/write console. Mock tabs: a `MockPlc` served over TCP (several clients) or a
  COM port, memory editor (word and bit ranges, X/Y octal for FX), fault injection (mute, `corruptNext`, `failRange`,
  device limits) and the request log with `eotCount()` and `skippedBytes()`. A frame trace (ring buffer, frame decode,
  pause, save), a debug log dock, and capture to `steps.vec` / `session.vec` / `run.meta` with export as replay data
  (captures of mocks or `virtual_plc` are tagged `not hardware` and never go to `tests/vectors/captured/`); one batch
  in flight per runner keeps memory and GUI latency bounded under a flood. A HIL runner view on `mc_hil_tool`: the same
  gate decisions and dry runs as `hil_capture` on every committed plan, the typed profile id for `readOnly` frames, live
  step outcomes, replay and bench report. Workspace save and load (devices, mocks with memory presets, HIL inputs, dock
  layout; a damaged file or an unknown key is refused with its JSON path and changes nothing). The docking library (Qt
  Advanced Docking System, LGPL) is a local dependency found through `MC_ADS_DIR` (CMake, env) or `mc_local.pri` (qmake),
  never in the repository (`scripts/build-ads.ps1` builds it for MinGW into `build/`); the property browser `qpb` (MIT)
  is vendored in `components/qpb`; without the docking library the GUI is skipped and the rest of the build is unchanged.
  Additive changes in `tools/hil_capture`: `RecordingTransport::takeChunks()` and `StepRecord::source` (the `# source:`
  tag, default `plc`); the gate and its tests are untouched. Test binary `mc_workbench_tests` (label `gui`, offscreen,
  GUI-01 to GUI-09 plus the tab, trace, capture and workspace cases; qmake twin); T-074 adds the cases that run the GUI
  against the `examples/virtual_plc` process (a device tab, and the whole `tests/hil/e2e/plan_3e.json` including E-10
  through the HIL view with a capture that replays green) and the report-only targets `mc_coverage_report_gui_core` and
  `mc_coverage_report_gui_ui`. MSVC and MinGW GCC 13.1 (CMake and qmake), `scripts/check.ps1` (both kits) and
  `check.sh` pass; line coverage on MinGW: `src/core/model` 98.1 %, `src/core/protocol` 97.9 %, `src/core/session`
  97.9 %, `src/mock` 97.3 %, `src/device` 92.3 % (report only), `tools/mc_workbench` core 90.8 % and ui 85.4 % (report
  only).
