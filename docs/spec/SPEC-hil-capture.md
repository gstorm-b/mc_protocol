# Spec: hil-capture

- **Module id:** `hil-capture` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** `qt-device` (the capture tool), `core-protocol`, `core-session`, `mock-plc` (the replay tests)
- **Depended on by:** nothing; it validates the others
- **Inputs:** owner interview 2026-09-26 (hardware on the bench, safety, divergence policy, timing benchmark); intent ("có tests với data thực tế thu được cho các dạng frame"); ideas doc §5 (assumptions "3E Binary gives the old module's bytes on a real PLC", "serial: wire-unverified"); reference spec `docs/mc_reference/mc-protocol-frame-spec.md` §9.1 (L7 HIL), §10.1 (Q1–Q8), Appendix A; all other module specs.
- **Companion documents:** `docs/hil/COMMAND-CATALOGUE.md` (what to send, per frame family), `docs/hil/FINDINGS.md` (where the PLC and the spec disagree).

## Objective

Once `McProtocol` and `McDevice` pass their own suites against the mock, run the library against **real PLCs on the owner's bench**, record every request and response byte, and turn those recordings into **hardware-sourced test vectors** that run on every later build without hardware. Measure transmission and response times along the way as reference data.

Four outcomes:

1. **Evidence.** The library talks to real Q-series, iQ-F (FX5) and F-series (FX3) CPUs over 3E, 1E, 3C and 1C, in both data codes and all serial formats the hardware offers.
2. **Answers.** The reference spec's open hardware questions (§10.1 Q1–Q5, Q7) get answered on the hardware that has the frame (Q8 needs hardware this bench does not have), and every disagreement is logged for the owner to decide.
3. **Regression data.** Captured frames become `.vec` files. A hardware-free test binary replays them against the encoder, the parser, the mock and the `Session` on every build.
4. **Timing reference.** Time to first byte, receive time and round-trip time per request type, per PLC and transport, in one table.

**User stories**

- As the owner at the bench, I describe a PLC once in a profile (identity, connection, frame settings, scratch area), run one command, and get a capture set and a report of what passed, failed or diverged.
- As the owner, I can trust that the tool never writes outside the scratch area I declared.
- As a maintainer months later, a codec change that alters one byte for FX3 over 1C fails a replay test that names the profile and the step, without an FX3 on my desk.
- As a maintainer, I can see whether `MockPlc` answers exactly like the real Q-series C24, and where it does not.
- As an application developer, I look up how long a 64-word read takes over 3E Binary on a Q CPU versus 1C at 9600 baud before I choose a polling interval.

### Decisions this spec implements (owner interview, 2026-09-26)

| # | Decision |
|---|---|
| H1 | Hardware: Q series, iQ-F (FX5U), F series (FX3…). PLC parameters can be changed and downloaded, so every frame (1E, 3E, 1C, 3C), both codes, and every serial format the modules support can be tested. No iQ-R, L or A series: the iQ-R subcommands and the "A-series target through QnA" rules are **not** verified on hardware (A series is obsolete; not needed). |
| H2 | Safety: PLCs are on a test bench, connected to no machinery. The owner declares a **scratch area** per PLC; the tool refuses any run in which a write could land outside it. |
| H3 | Divergence policy: a hardware result that differs from the reference spec or its golden vectors becomes a finding in `docs/hil/FINDINGS.md`; the replay step is tagged `known-divergence` until the owner decides (change the codec, change a `FrameConfig` default, or keep). Golden vectors transcribed from the manual are never edited, only tagged. |
| H4 | Timing benchmark is part of the capture: reference data, not pass/fail. |

## Hardware capability table (owner fills before the first run)

The library does not assume which module speaks which frame. The owner fills this table from the module manuals and the bench wiring; the capture plan runs only the cells marked yes. `?` = not yet known.

| PLC / module on the bench | Model and firmware | 3E Bin | 3E ASCII | 1E Bin | 1E ASCII | 3C formats | 1C formats | Notes (USB-serial adapter, cable, …) |
|---|---|---|---|---|---|---|---|---|
| Q CPU, built-in Ethernet | | O | O | — | — | — | — | |
| Q, Ethernet module (E71) | | — | — | — | — | — | — | |
| Q, serial module (C24) | | — | — | — | — | O | — | |
| FX5U, built-in Ethernet | | O | O | — | — | — | — | |
| FX5U, serial (built-in / ADP) | | — | — | — | — | — | — | |
| FX3, Ethernet adapter | | — | — | O | O | — | — | |
| FX3, serial adapter / computer link | | — | — | — | — | — | O | |

Rows 2 (Q + E71) and 5 (FX5U serial) are not tested: the owner judged them unnecessary (2026-09-26).

### Profiles on this bench (decided 2026-09-26)

Fourteen profiles, one parameter download each:

| # | PLC / module | Frame | Profiles |
|---|---|---|---|
| 1 | Q CPU, built-in Ethernet | 3E | Binary · ASCII |
| 2 | Q, C24 | 3C | F1 sum on · F1 sum off · F2 (block 5AH) · F3 · F4 |
| 3 | FX5U, built-in Ethernet | 3E | Binary · ASCII |
| 4 | FX3, Ethernet adapter | 1E | Binary · ASCII |
| 5 | FX3, serial / computer link | 1C | F1 sum on · F1 sum off · F4 |

**Coverage consequences, recorded so nobody reads more into a green run than it proves:**

- 1E and 1C run only on FX3. 1C formats 2 and 3 stay **wire-unverified**.
- §10.1 Q1 (F3 short-response SUM) and Q2 (F2 block echo) are answered for **3C only**.
- §10.1 Q8 (M9008 word access) is **not testable on this bench**: it needs a CPU exposing M9000–M9255 through 1E/1C, and FX3 uses its own special-device range. The library keeps the spec rule (9000 + 16k).
- The 1E limit table (Q7) is checked on FX3, which may differ from the A-series figures in the PDF; either result is a finding.
- iQ-R subcommands and the "A-series target via QnA" rules are not verified on hardware (decision H1).

## Concepts

### Profile

One profile = one PLC + one transport + one frame configuration, i.e. one set of PLC parameters downloaded to the bench. Changing the code or the serial format means a new profile. Profiles are JSON (the tool is a Qt program and `McDeviceConfig` already has a JSON form):

```json
{
  "schema": 1,
  "profile": {
    "id": "q03ude-c24-3c-f4",
    "plc": "Q03UDECPU", "module": "QJ71C24N", "firmware": "…",
    "adapter": "FTDI USB-RS232, latency timer 1 ms",
    "plcState": "RUN, online change enabled",
    "scratch": ["D100-D2099", "W100-W1FF", "R0-R99", "M100-M2099", "B100-B1FF", "Y20-Y2F"],
    "deviceEnd": { "D": 12287, "M": 8191, "W": "1FFF", "B": "1FFF", "X": "1FFF", "Y": "1FFF" },
    "supports": ["D", "W", "R", "ZR", "M", "L", "F", "B", "X", "Y", "TS", "TC", "TN", "CS", "CC", "CN", "SM", "SD"],
    "scanTimeDevice": "",
    "specialBit": "SM0",
    "specialWord": "SD0",
    "families": ["qna-serial"]
  },
  "device": { "…": "an McDeviceConfig, exactly as SPEC-qt-device.md defines it" }
}
```

- `scratch`: inclusive ranges in the device's own radix. **Every write must fall inside.** Recommended: include D100–D102 and M100–M107 so the Appendix A mirror steps (catalogue group `GV`) can run.
- `deviceEnd`: the last existing number of each device type as configured in the PLC parameters; used by the boundary steps.
- `supports`: device types to exercise; the device-code sweep also probes the family's other types and expects a PLC error from them.
- `scanTimeDevice`: optional device holding the CPU's current scan time (the owner takes the address from the CPU manual); read before each bench so timings have context.
- `specialBit`, `specialWord`: the first special relay and special register **as addressed through this profile's frame**, taken by the owner from the CPU manual. Q and FX5 over 3E/3C: `SM0`, `SD0`. FX3 over 1E/1C: FX3's own special range (the owner confirms the addresses, e.g. `M8000`, `D8000`). Used by catalogue steps G1-09, G2-10 and G3-04 instead of a fixed M9000/D9000.

Example profiles are committed as `tests/hil/profiles/*.example.json`. Real profiles stay local and git-ignored: they hold IP addresses and COM port names. Everything replay needs is copied into `run.meta`.

### Plan

A plan is the machine form of one frame family's part of `COMMAND-CATALOGUE.md`: `tests/hil/plans/qna_ethernet.json` (3E), `a1e.json` (1E), `qna_serial.json` (3C), `a1c.json` (1C). A step has an `id` equal to its catalogue id, a `kind`, device references relative to the profile, and an expectation.

| Step kind | What the tool does |
|---|---|
| `write` / `read` | Goes through `McDevice::submit` (the full library path). `read` may carry `expect` values or `record` only. |
| `poll` | Subscribes a set of ranges, runs N rounds, applies ad-hoc writes between rounds, records the whole session (every frame, every event). |
| `mutate` | Encodes a request with `McProtocol`, then edits bytes (`setByte`, `setNibble`, `append`, `truncate`, `replaceSum`), sends it on the raw transport, records whatever comes back. For probes the library's API rightly refuses (spec §10.1 probes, malformed requests). |
| `raw` | Sends a literal hex frame (read-only probes only; for commands the library does not have yet, e.g. 0403). |
| `bench` | Repeats one request N times and records timings (see "Timing benchmark"). |

Device references: `D@s` (first scratch D), `D@s+10`, `M@s16` (first scratch M aligned to 16), `D@end` (`deviceEnd`), `D@end+1` (first number that does not exist), literal `D100`.

Expectations: `ok`, `ok + values`, `plcError` (any PLC error, code recorded), `timeout`, `noResponse` (silence expected, e.g. other station), `notSent` (the library must refuse client-side, e.g. an unaligned word read on 1E), `record` (no expectation; a probe).

### Safety gate (decision H2)

Before connecting, the tool resolves every step of the run and checks every write:

- `write` steps and every write of a `poll` step (heartbeat, ad-hoc writes): head … head + count − 1 must lie inside one scratch range of that type. A poll subscription only reads: one outside scratch (e.g. inputs `X0` × 32 in catalogue G6) must be marked `"input": true` in the plan, otherwise it is refused like a write (amended 2026-10-02, owner decision).
- `mutate` and `raw` steps: the tool feeds the frame to an `mc::MockPlc` configured like the profile and reads `requests()` to learn what the frame asks for; a write outside scratch refuses the run. Every part of a frame the mock decodes as a write (words or bits, also one starting mid-frame) is checked by the point numbers it covers. A frame the mock cannot fully decode must be declared `readOnly: true` in the plan and is listed for confirmation before sending (amended 2026-10-02, owner decision, after a truncated write declared `readOnly` was completed by the next frame's bytes and wrote outside scratch):
  - a `mutate` whose base request is a write can never be `readOnly`;
  - a `readOnly` frame must carry `recover: reconnect` (Ethernet) or `recover: eot` (serial); the tool performs that recovery before the next frame, so no later byte can complete it or be swallowed by it;
  - **default deny** (amended 2026-10-03, enforcing decision H2): in every `raw` and `mutate` frame, `readOnly` or not, the tool itself locates every command at every frame start the port could accept (Ethernet: 3E and 4E in both codes at any offset, 1E on a 1E profile; serial: every ENQ/STX with any frame ID, after EOT too). Each command must be either in the family's allow-list of read-only commands (the batch, random and block reads the reference spec lists) or a write the mock decodes at that offset with every covered point in scratch; anything else refuses the run. A frame with no locatable command is a fragment and relies on the `recover` rule above. Catalogue steps that write through commands the mock does not decode stay out of the plans (G9-02…04, G8-Q3 (b)).
  - `frameOverride` may not change `frame`, `code`, `format`, `sumCheck` or routing (network, PC, I/O, station, `stationNo`, `selfStation`) on `raw` and `mutate` steps, and no step that writes may change routing: a write must reach the PLC whose scratch was declared. Reads may change routing (e.g. catalogue G7-04).
  - Residual risk, documented in `tools/hil_capture/safety_gate.h`: the gate knows the command sets of the reference spec; a vendor command outside them that writes is out of its sight.
- Any violation refuses the **whole run** and prints every offending step. There is no override flag.

The tool then prints the PLC identity, transport and scratch area, and waits for the operator to type the profile id. `--yes` skips the prompt for repeated runs of an already-checked profile, **except** when the run contains `readOnly` frames: then the profile id must always be typed (amended 2026-10-02, owner decision).

## The capture tool: `tools/hil_capture`

A Qt 6 console program linking `mc::device` and `mc::mock`.

```powershell
hil_capture --profile tests/hil/profiles/q03ude-c24-3c-f4.json --plan tests/hil/plans/qna_serial.json --dry-run
hil_capture --profile tests/hil/profiles/q03ude-c24-3c-f4.json --plan tests/hil/plans/qna_serial.json
hil_capture --profile … --plan … --only G1,G2,G8 --bench-reps 200
hil_capture --report          # rebuild docs/hil/BENCH.md from every bench.csv
```

- `--dry-run` resolves the steps, runs the safety gate, prints every frame it would send (encoded by `McProtocol`), and never connects.
- The tool injects a **`RecordingTransport`** into `McDevice` (the public `Transport` interface allows it; nothing in the library changes). It wraps the real `TcpTransport` / `SerialTransport`, and stamps every written chunk and every received chunk with a monotonic time in nanoseconds (`QElapsedTimer::nsecsElapsed()`).
- `mutate` and `raw` steps use the same `RecordingTransport` while `McDevice` is disconnected from it, so all frames share one clock and one recording.
- On `LinkFault` the tool reconnects (`connectToPlc()`), records the recovery, and continues with the next step; a step that faulted is recorded with its outcome.
- End of run: a console summary (passed, failed, diverged, not supported, skipped) and the files below.

## Output

```text
tests/vectors/captured/<profile-id>/
├── run.meta          key: value lines: tool and library version, git commit, date, operator note,
│                     PLC state, profile identity (PLC, module, firmware, adapter), scratch area,
│                     deviceEnd, serial line settings, and every FrameConfig and SessionConfig
│                     field (so replay needs no JSON). Never IP addresses, TCP ports or COM names.
├── steps.vec         one request + one response vector per api / mutate / raw step
├── session.vec       the poll step's full transcript: tx / rx chunks with times, then events
├── bench.csv         one row per bench repetition
└── divergences.txt   step ids tagged known-divergence, each with its FINDINGS id (owner-maintained)
```

`steps.vec` uses the `.vec` format of `SPEC-core-protocol.md` with extra metadata keys. The shared loader (`tests/common/vectors.h`) already exposes all metadata as data, so it reads these files unchanged.

```text
# id: CAP-q03ude-e71-3e-bin-GV-01
# source: plc  profile: q03ude-e71-3e-bin  step: GV-01  mirrors: V-3E-B-01
# frame: 3E  code: Binary  op: ReadWords  device: D100  count: 3
# kind: request
50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00

# id: CAP-q03ude-e71-3e-bin-GV-01-R
# kind: response  of: CAP-q03ude-e71-3e-bin-GV-01
# outcome: ok  expect: words 1995 1202 1130
# ttfb_ms: 2.814  rx_ms: 0.041  rtt_ms: 2.855
D0 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11
```

- `outcome`: `ok`, `plcError <code> [abnormal <code>] [info <route/cmd/sub>]`, `timeout`, `noResponse`, `protocolError <code>`, `notSent <error>`.
- A timeout that received part of a frame records the bytes as `kind: response-partial`.
- Re-running a profile **replaces** its folder; git history keeps the old captures, and a diff shows exactly what changed on the wire.

## Replay tests (no hardware): `mc_replay_tests`

A doctest binary, label `replay`, std-only (links `mc::core` and `mc::mock`, never Qt). It walks `tests/vectors/captured/*/` and skips cleanly when the folder is empty. Steps listed in a profile's `divergences.txt` are reported as `known-divergence` and do not fail the run.

| ID | Check |
|---|---|
| RPL-01 | Every captured `write`/`read` request is re-encoded from its metadata with the profile's `FrameConfig`: bytes equal the captured request. (A change means the encoder changed: fix it or re-capture on purpose.) |
| RPL-02 | Every captured response parses with a parser for its request to the recorded `outcome` and values. |
| RPL-03 | **Mock conformance.** Steps are replayed in order into an `MockPlc` with the profile's config and `deviceEnd` limits; memory the run did not write itself (inputs, special registers, timers) is seeded from the captured read values first. The mock's response bytes equal the captured response bytes. |
| RPL-04 | **Session replay.** A `Session` with the profile's config and the poll step's subscriptions is fed the captured rx chunks at the captured times (fake clock). It emits exactly the captured tx frames in the same order, and its events (snapshots, changes, `CycleDone`) match the recorded ones. |
| RPL-05 | Capture sanity: every `write` step is followed by a read-back that matched; every `GV` step's bytes equal the Appendix A vector it mirrors (request always; response when the route and values are the defaults). |
| RPL-06 | Every step id in a `divergences.txt` has a matching open or decided entry in `docs/hil/FINDINGS.md`. |

## Timing benchmark (decision H4)

Each profile runs catalogue group `GB`. For every repetition the tool records three instants from the `RecordingTransport`:

```text
t_send ──(request on the wire + PLC processing, incl. waiting for END of scan)──▶ t_first_byte ──(response on the wire)──▶ t_complete
       └──────────────────────────── ttfb (time to first byte) ───────────────┘              └──── rx (receive time) ─────┘
       └────────────────────────────────────────── rtt (round-trip time) ─────────────────────────────────────────────────┘
```

- `bench.csv` columns: `profile, plc_state, step, op, device, count, req_bytes, resp_bytes, rep, ttfb_ms, rx_ms, rtt_ms, scan_ms`.
- **Two passes per profile:** every group runs with the PLC in **RUN** (the application's condition; online writing enabled in the module parameters). Then group `GB` alone runs once more with the PLC in **STOP** (`hil_capture … --only GB --plc-state STOP`), so the report can separate the scan's share of `ttfb` from the communication cost. The operator switches RUN/STOP by hand; the tool asks for confirmation and records the state, it never changes it.
- Default 200 repetitions per request, 1 warm-up repetition discarded, requests spaced by the profile's `cycleIntervalMs` so the load looks like polling.
- The poll step adds a round-duration series from `CycleInfo::durationMs`.
- `hil_capture --report` writes `docs/hil/BENCH.md`: one table per profile with, per request, bytes out/in and min / median / p95 / max of `ttfb`, `rx` and `rtt`, in a RUN column and a STOP column.
- **Caveats printed at the top of the report:** timestamps are taken in the Qt event loop, so they include OS and driver latency; USB-serial adapters buffer received bytes (e.g. an FTDI latency timer), which inflates `ttfb` and hides `rx` on serial links, so the adapter and its latency setting are part of the profile.
- Nothing passes or fails on a timing. The numbers serve as reference, and as evidence when choosing defaults such as `timeoutMs` and `serialInterCharMs`.

## Operator procedure (one profile)

**Before the first capture: open by the owner's choice until then (2026-09-26).** Planning and implementation do not wait for these; the first capture run does.

- [ ] Scratch area decided for each PLC (Q + Ethernet, Q + C24, FX5U, FX3 Ethernet, FX3 serial); preferably including D100–D102 and M100–M107 for group `GV`.
- [ ] FX3 manual checked: the special relay / register addresses as seen through 1E/1C (→ `specialBit` / `specialWord`; guessed M8000 / D8000, unverified), and that its computer link offers formats 1 and 4 (→ the three 1C profiles).

1. Fill the hardware capability table, then write the profile. Check that the scratch area is free for testing on that PLC.
2. Download the PLC parameters matching the profile with GX Works: code, format, sum check, station numbers, open settings for MC protocol, and "enable online change" if the PLC stays in RUN. The tool does not download parameters.
3. `hil_capture … --dry-run`; read the frames and the safety summary.
4. Run for real; confirm the profile id.
5. Read the summary. For every divergence, add or update an entry in `docs/hil/FINDINGS.md` and, while undecided, the step id in `divergences.txt`.
6. `ctest -L replay` must pass (with the divergences tagged).
7. Commit the capture folder, `FINDINGS.md` and `BENCH.md` (the owner confirms the file list before `git add`, per the build-packaging rule).

**When to capture:** the first full round before tagging 1.0 (it validates the ideas-doc §5 assumptions); again for a profile whenever its frame's encoder or parser changes intentionally, or when a new PLC or module joins the bench.

## Project Structure

```text
tools/hil_capture/
├── main.cpp
├── profile.h/.cpp          profile JSON → McDeviceConfig + scratch/limits/supports
├── plan.h/.cpp             plan JSON, device reference resolution
├── safety_gate.h/.cpp      scratch checks incl. MockPlc decoding of mutate/raw frames
├── recording_transport.h/.cpp
├── runner.h/.cpp           executes steps, reconnects after faults
├── capture_writer.h/.cpp   steps.vec, session.vec, run.meta, bench.csv
└── bench_report.h/.cpp     docs/hil/BENCH.md
tests/hil/
├── profiles/*.example.json
├── plans/{qna_ethernet,a1e,qna_serial,a1c}.json
├── test_tool.cpp           HIL-xx (tool logic, Qt, no hardware)
└── test_replay.cpp         RPL-xx (std-only)
tests/vectors/captured/<profile-id>/…
docs/hil/
├── COMMAND-CATALOGUE.md
├── FINDINGS.md
└── BENCH.md                generated
```

## Testing the tool itself (no hardware)

QtTest binary `mc_hil_tool_tests`, label `hil_tool`:

| ID | Covers |
|---|---|
| HIL-01 | Profile parsing: example profiles load; a scratch range with a bad radix or an unknown device type is rejected with its path |
| HIL-02 | Safety gate refuses a `write`, a `poll`, a `mutate` and a `raw` step that reach outside scratch; the refusal lists every offending step; nothing is sent |
| HIL-03 | `--dry-run` prints exactly the frames `McProtocol::encode` produces for the resolved steps |
| HIL-04 | End-to-end against `examples/virtual_plc` (loopback TCP) with a 3E Binary profile: a full capture set is written, and `mc_replay_tests` passes on it |
| HIL-05 | Capture writer output is read back by `tests/common/vectors.h` with every metadata key intact |
| HIL-06 | `--report` on a hand-made `bench.csv` produces the expected min / median / p95 / max |

## Boundaries

**Always**

- Run the safety gate before connecting; never write outside the declared scratch area.
- Record every step's outcome, including failures and timeouts; never drop a frame from a capture.
- Log every hardware-versus-spec difference as a finding; the owner decides (decision H3).

**Ask first**

- Changing library behaviour or a `FrameConfig` default because of a finding.
- Editing or re-tagging a golden vector from Appendix A.
- Committing a real profile (they stay local by decision; they contain network addresses and port names).

**Never**

- A flag that bypasses the safety gate.
- Download parameters to a PLC or change its RUN/STOP state from the tool.
- Let replay tests depend on Qt, a network or a serial port.

## Changes required in other specs (applied on 2026-09-26)

1. **`build-packaging`:** add `tools/` (owned by this spec) with option `MC_BUILD_TOOLS` (`ON` when top-level and `MC_BUILD_DEVICE` and `MC_BUILD_MOCK` are on, `OFF` when consumed); add `tests/hil/`, `tests/vectors/captured/` and `docs/hil/` to the tree and the ownership rule; register `mc_replay_tests` (label `replay`, std-only, built with tests + mock) and `mc_hil_tool_tests` (label `hil_tool`, built with tests + mock + device + tools); add `tools/hil_capture` to the list of targets allowed to link `mc::mock`; add `tests/hil/profiles/*.json` with the exception `!tests/hil/profiles/*.example.json` to the proposed `.gitignore` (owner confirms before `git add`).
2. **`CAPABILITY-MAP.md`:** add the `hil-capture` row and put it last in the build order.

## Success Criteria

1. Every profile the owner lists in the capability table has a capture folder with `run.meta`, `steps.vec`, `session.vec` and `bench.csv`.
2. `ctest -L replay` passes on MSVC and on GCC or Clang, with every divergence tagged and matched to a FINDINGS entry (RPL-06).
3. §10.1 Q1, Q2, Q3, Q4, Q5 and Q7 each have a FINDINGS entry with the observed behaviour on at least one profile that has the frame; Q8 is recorded as not testable on this bench; Q6 has an error-code table built from every PLC error seen.
4. The ideas-doc §5 assumption "3E Binary on a real PLC" is closed by `GV` steps whose request and response bytes equal Appendix A.
5. `docs/hil/BENCH.md` exists with one table per captured profile.
6. HIL-01…06 pass.

## Open Questions

1. **Repetitions.** 200 repetitions per bench request (proposed) take ~200 × 100 ms ≈ 20 s per request at the default interval, so the ~10 requests of group `GB` add ~3–4 minutes per profile. More for a smoother p95, fewer for faster runs? *Decision:* 200 repetitions, overridable with `--bench-reps` (2026-09-26).
2. **RUN or STOP.** Capture in RUN with online change enabled (proposed; this is how applications run, and scan time shows in `ttfb`), or also a STOP pass to see the difference in timing? *Decision:* every group in RUN with online writing enabled, plus one `GB`-only pass in STOP; the operator switches the PLC state by hand (2026-09-26).
3. **Real profiles in git.** Commit them (reproducible) or keep them local and commit only the `run.meta` copy of the settings (proposed; addresses stay private)? *Decision:* local and git-ignored (`tests/hil/profiles/*.json` except `*.example.json`; the owner confirms the ignore list before `git add`); `run.meta` records frame settings, PLC model, module, firmware, serial line settings and scratch area, never IP addresses, TCP ports or COM port names (2026-09-26).
4. **Serial profiles.** Which formats and variants to run on Q + C24 (3C) and FX3 (1C)? *Decision:* 3C: F1 sum on, F1 sum off, F2 (block 5AH), F3, F4; 1C on FX3: F1 sum on, F1 sum off, F4 (the owner confirms FX3 computer link offers formats 1 and 4). 1C F2/F3 stay wire-unverified (2026-09-26).
5. **Special devices and §10.1 Q8.** 1E/1C run only on FX3, whose special devices are not M9000/D9000. *Decision:* each profile declares `specialBit` / `specialWord`; steps G1-09, G2-10, G3-04 use them; Q8 is recorded as not testable on this bench and the spec rule (9000 + 16k) stays (2026-09-26).
