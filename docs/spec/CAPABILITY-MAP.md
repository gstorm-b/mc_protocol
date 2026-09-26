# Capability Map: MC Protocol Library (`mc`)

- **Status:** approved by the owner on 2026-09-25 (assumptions 1–10 answered inline below; #3 amended: git is used locally, the owner decides the ignore list before anything is added)
- **Inputs:** `docs/intent/mc_protocol_library.md`, `docs/ideas/mc_protocol_library.md` (direction C confirmed, decisions 0–7, performance section 9)
- **Purpose:** fix module boundaries, dependency direction and build order before any module spec is written. Each module gets its own `docs/spec/SPEC-<module-id>.md`. Module ids are stable and never renamed.

## Modules

| Module id | Responsibility | Depends on |
|---|---|---|
| `build-packaging` | Repository skeleton; the `include/mc/` public-surface rule; CMake targets `mc::core`, `mc::device`, `mc::mock`; qmake `mc_core.pri`, `mc_device.pri`, `mc_protocol.pri`; `mc/version.h`; a check that the qmake and CMake source lists agree; `CHANGELOG.md`; the one-command build-and-test script | — |
| `core-model` | Pure data and small pure functions with no protocol logic: device table (spec §3.2, all QnA/1E/1C symbols) and `parseDevice()`; `Request`, `Result`, `Error`, `Expected<T>`; `FrameConfig` for 3E/1E/3C/1C with reserved fields for 4E serial and format 5; point-limit table (spec §4.4) and `chunk()`; `convert` helpers (words ↔ int16/int32/uint32/float32/float64/string, bit packing); `LogSink` | `build-packaging` |
| `core-protocol` | Wire encoding and decoding: `AsciiCodec`, `BinaryCodec`; QnA (0401/1401), A1E (00H–03H), A1C (BR/WR/BW/WW) commands; frames 3E, 1E, 3C, 1C (format 1 and 4, sum check on/off); `McProtocol` facade; incremental `Parser` with `remainder()`; normalized payload contract; golden-vector test data (spec Appendix A, CMD-xx) | `core-model` |
| `core-session` | The sans-I/O engine: `RangeSet` (dynamic subscriptions), `ReadPlan` (coalesce, gap merge, bits-as-words, chunking), `ValueStore` (contiguous per-type segments with per-point state, change detection), `Session` (single in-flight request, rounds over pre-encoded frames, snapshot per device type with round 1 held back to its end, change events per response from round 2, ad-hoc queue with exactly-once completion, serial retry + EOT, Ethernet fault on timeout, optional heartbeat, `LinkFault` reporting only), `SessionConfig`, `Output`; fake-clock test harness; zero-allocation steady-state test; the non-Qt usage example (links `mc::mock`) | `core-protocol` |
| `mock-plc` | A std-only, sans-I/O PLC responder, **hybrid**: reuses the core's primitives (device table, hex-ASCII, sum check, field codecs) but writes the server direction (request decoding, response building) itself from the reference spec, verified against the golden vectors in reverse. Memory image, fault injection, corruption modes, request log, for all four frames. Hosts the std-only `Session` ↔ mock integration matrix (spec §9.11, v1 subset). Used by `qt-device` tests and the examples | `core-protocol` |
| `qt-device` | The Qt 6 layer: `Transport` interface, `TcpTransport`, `SerialTransport`, `McDevice` (QObject adapter over `Session`, single-shot deadline timer, FIFO signal queue safe for re-entrant slots, signals for changes, per-type snapshots, request completion, link state, link faults; no auto reconnect), `McDeviceConfig` with JSON round-trip; loopback tests against `mock-plc`; the Qt console poller and `virtual_plc` examples | `core-session`; `mock-plc` for tests and examples only |
| `hil-capture` | Hardware-in-the-loop capture on the owner's bench (Q, FX5, FX3): per-PLC profiles with a declared scratch area and a safety gate; the command catalogue (`docs/hil/COMMAND-CATALOGUE.md`) as plans; the `hil_capture` Qt tool (runs plans through `McDevice` and raw/mutated frames, records every byte to `.vec`, measures ttfb / rx / rtt); the std-only replay tests that re-check encoder, parser, mock and `Session` against the captures on every build; `docs/hil/FINDINGS.md` and `BENCH.md` | `qt-device`, `mock-plc` |

## Build order

```text
build-packaging → core-model → core-protocol → { core-session, mock-plc } → qt-device → hil-capture
```

`hil-capture` is last on purpose: the first capture run happens only after `McProtocol` and `McDevice` pass their own suites against the mock, and before the 1.0 tag.

`core-session` and `mock-plc` are independent of each other and can be built in parallel once `core-protocol` exists. Two artefacts need both and are built last among the std-only pieces: the integration tests (`tests/mock/integration`) and `examples/session_loop`.

## Module specs

| Module | Spec | Status |
|---|---|---|
| `build-packaging` | `SPEC-build-packaging.md` | approved 2026-09-26 |
| `core-model` | `SPEC-core-model.md` | approved 2026-09-26 |
| `core-protocol` | `SPEC-core-protocol.md` | approved 2026-09-26 |
| `core-session` | `SPEC-core-session.md` | approved 2026-09-26 |
| `mock-plc` | `SPEC-mock-plc.md` | approved 2026-09-26 |
| `qt-device` | `SPEC-qt-device.md` | approved 2026-09-26 |
| `hil-capture` | `SPEC-hil-capture.md` (+ `docs/hil/COMMAND-CATALOGUE.md`, `docs/hil/FINDINGS.md`) | approved 2026-09-26 |

Each spec's "Changes required in other specs" section lists edits for other specs. All of them were applied on 2026-09-26.

## Boundary rules that hold across every module

- Public headers live only under `include/mc/`. Nothing under `include/mc/` includes anything from `src/`.
- `core-*` and `mock-plc` compile with no Qt on the include path. Only `qt-device` may include Qt.
- Public API reports errors through `Error` / `Expected<T>`; no exception crosses a public boundary (decision 1).
- Every hot-path operation in `core-*` respects the complexity table in `docs/ideas/mc_protocol_library.md` §9.3.
- Committed documents and source comments are in English (decision 3).

## Not in this map (v1 exclusions, see ideas §7)

Qt widgets, frames 4E and 4C, UDP, random-access commands 0403/1402, installable/prebuilt packaging and ABI promises, threads inside the library, PLC error-code meaning tables.

---

## Assumptions to confirm before module specs are written

Answer inline; a blank line means "as assumed".

1. **Toolchains.** Primary: MSVC 2019 or newer and MinGW on Windows; GCC and Clang must also compile the core. Minimum Qt: **6.2 LTS**. CMake **3.16** or newer; qmake from the same Qt.
   *Decision:* as assumed.
2. **Test framework for the std-only modules.** Vendor **doctest** as a single header under `tests/third_party/` (MIT licence) so the core tests add no external dependency. `qt-device` tests use **QtTest**. This is an "ask first" item because it adds a vendored dependency.
   *Decision:* as assumed.
3. **No CI in v1.** The repository is not under git yet. A `scripts/check.ps1` (and `check.sh`) runs both build systems and all tests locally; CI wiring is deferred until the repo has a remote.
   *Decision:* Using git locally, owner decide ignore list before git add.
4. **Namespaces.** Public types are flat in `mc::`; internals in `mc::detail`. No per-layer sub-namespaces in the public API.
   *Decision:* as assumed.
5. **Payload contract.** Bit results: one byte per point holding 0 or 1, in device order; optional packed form eight points per byte, LSB first. Word results: two bytes per word, little-endian, in device order. This contract is versioned with the public API and never changes within a major version.
   *Decision:* as assumed.
6. **Serial transport is required, not optional, in `qt-device`** because 1C and 3C are in v1. `mc::device` therefore links QtSerialPort. A CMake option to build TCP-only can be added later if a consumer asks.
   *Decision:* as assumed.
7. **Log levels.** `Trace`, `Debug`, `Info`, `Warn`, `Error`. TX/RX hex dumps go out at `Trace`; the sink is consulted for its level before any string is built.
   *Decision:* as assumed.
8. **Host endianness.** Code is written byte-wise and endian-independent; no `reinterpret_cast` of multi-byte integers on the wire buffer.
   *Decision:* as assumed.
9. **Spec file locations.** This map and every module spec live in `docs/spec/`. Plans and task lists live in `tasks/` per the `planning-and-task-breakdown` convention.
   *Decision:* as assumed.
10. **Golden vectors.** Taken from `docs/mc_reference/mc-protocol-frame-spec.md` Appendix A and the CMD-xx tables, stored as JSON under `tests/vectors/`, and treated as the source of truth for `core-protocol`. Where the reference implementation in `reference_source/` and the spec disagree, the spec wins and the disagreement is logged in the module spec.
    *Decision:* as assumed. **Amended 2026-09-26:** stored as plain-text `.vec` files (Appendix A notation with `# key: value` metadata), not JSON; see `SPEC-core-protocol.md` open question 1.
