# Implementation Plan: MC Protocol Library (`mc`) v1

- **Date:** 2026-09-26
- **Specs:** `docs/spec/CAPABILITY-MAP.md` and the seven approved module specs in `docs/spec/`
- **Task list:** `tasks/todo.md` (acceptance criteria, verification, files per task)
- **Status:** approved by the owner 2026-09-27 (open questions decided the same day, see "Owner decisions"). Execution runs through the agent-team workflow: each task becomes a baton file in `tasks/active/` with its own Gate 1 and Gate 2.

## Overview

Build the library in the order of the capability map, but cut it **vertically by frame**. Frame 3E goes through the whole stack first: model, codec, session, mock, Qt device, examples. That proves every layer and every boundary on one frame before the other three frames are added. Two of the ideas-doc assumptions are "if wrong, the idea dies" (ideas §5): the sans-I/O `Session`, and 3E bytes on a real PLC. The plan tests both as early as the dependencies allow.

## Architecture decisions that shape the plan

- **3E first, then 1E, 3C, 1C.** It is the frame the old app uses and the one with the most vectors, so the codec facade, the `Parser` contract, the `Session`, the mock and `McDevice` all get designed against it. The later frames plug into interfaces that already exist and are already tested.
- **`Session` right after the 3E codec, before the mock and Qt.** Ideas §5 names it the riskiest assumption, with fallback "direction B" if the engine turns out forced. Checkpoint C is the explicit go/no-go on it.
- **Build wiring travels with the code.** `build-packaging` creates the skeleton and the guard tests (BLD-04 pri sync, BLD-05 include hygiene). After that, every task that adds a source file updates `CMakeLists.txt` **and** the matching `.pri` in the same change, and BLD-04 proves it.
- **Each module's tests live in its own binary and label**, as in `SPEC-build-packaging.md`, so every task verifies with one `ctest -L <label>`.
- **Zero-allocation tests arrive with the code they guard**, not at the end: core-model (T10), core-protocol (T16), session (T23).
- **Two compilers from day one.** MSVC (VS 2026, Qt `msvc2022_64`) and MinGW GCC 13.1 (Qt `mingw_64`) are both on this machine. Every checkpoint builds with both.
- **HIL comes last and is owner-gated.** The capture tool is verified against `virtual_plc` first. Real captures wait for the two prerequisites the owner left open: scratch areas, and the FX3 manual checks (see `SPEC-hil-capture.md`, operator procedure).

## Dependency graph

```text
T01–T04 build skeleton ── A0
   └── T05–T10 core-model ── A
          └── T11–T16 codec: loader, primitives, device encode, QnA, 3E ── B
                 ├── T17–T24 session on 3E ── C1 (after T20) ── C (go/no-go)
                 └── T25–T27 mock on 3E (parallel with T17–T24)
                        T24 + T27 ── T28 integration rig 3E ── T29 session_loop
                        T24 + T28 ── T30–T34 qt-device over TCP ── D
                                        └── T35–T37 1E through every layer ── E1
                                        └── T38–T42 serial codec 3C, 1C ── E2
                                               └── T43–T46 serial session, mock, matrix, SerialTransport ── E
                                                      └── T47–T53 hil-capture tool + replay ── F (+ owner gate)
                                                             └── T54–T55 bench captures, findings
                                                                    └── T56 release 1.0 ── G
```

## Task index

Details (acceptance criteria, verification, files) are in `tasks/todo.md`.

### Phase 0: Build skeleton (`build-packaging`)

- [ ] T01 CMake skeleton with `mc::core`, version header, doctest
- [ ] T02 qmake mirror
- [ ] T03 Build guard tests (pri sync, include hygiene, consumer smokes)
- [ ] T04 `check` scripts, formatting, README, CHANGELOG, LICENSE

**Checkpoint A0:** both build systems and both compilers green on an almost empty library.

### Phase 1: Data layer (`core-model`)

- [ ] T05 `Error`, `Expected<T>`, byte views
- [ ] T06 Device table, parsing, formatting, ordering
- [ ] T07 `FrameConfig`, `Request`, `validate()`
- [ ] T08 Limits table and `chunk()`
- [ ] T09 `convert` helpers
- [ ] T10 `LogSink`, `hexDump`, zero-allocation test

**Checkpoint A:** every table row verified; ALC-01; coverage ≥ 95 % (MinGW).

### Phase 2: Wire codec, 3E first (`core-protocol`)

- [ ] T11 Vector loader and `.vec` format (with the transcription guard)
- [ ] T12 Primitives
- [ ] T13 Device encoding for all eight families
- [ ] T14 QnA batch read/write commands
- [ ] T15 `McProtocol` and `Parser` facade with 3E Binary
- [ ] T16 3E ASCII, streaming, sizes, zero allocation

**Checkpoint B:** every A.1 / A.2 vector round-trips; owner reviews `protocol.h`, the contract of Phases 3 and 4.

### Phase 3: Engine on 3E (`core-session`)

- [ ] T17 `RangeSet` and `ReadPlan`
- [ ] T18 `ValueStore`
- [ ] T19 `Session` scheduling skeleton and test harness
- [ ] T20 Receive path and value publishing
- [ ] T21 Dynamic subscriptions
- [ ] T22 Ad-hoc requests (arena, burst cap, exactly-once)
- [ ] T23 Ethernet faults, drain contract, heartbeat
- [ ] T24 Steady-state zero allocation and session benchmark

**Checkpoint C1 (after T20):** polling path works on a fake clock.
**Checkpoint C (go/no-go):** the sans-I/O engine holds, or fall back to direction B (ideas §3) before anything is built on it.

### Phase 4: Mock PLC on 3E (`mock-plc`); T25–T27 in parallel with Phase 3

- [ ] T25 `MockPlc` facade and memory image
- [ ] T26 3E server direction against the vectors
- [ ] T27 Fault injection and corruption (Ethernet)
- [ ] T28 Integration rig and the 3E part of the matrix
- [ ] T29 `examples/session_loop`

### Phase 5: Qt device over TCP (`qt-device`)

- [ ] T30 `Transport`, `TcpTransport`, meta types
- [ ] T31 `McDeviceConfig` and JSON
- [ ] T32 `McDevice` link state, pump and signal queue
- [ ] T33 `McDevice` faults, re-entrancy, threads, destructor
- [ ] T34 Examples `qt_console_poller` and `virtual_plc`

**Checkpoint D:** 3E through the whole stack; demo `virtual_plc` ↔ `qt_console_poller`. No real PLC here (owner decision 5); real hardware waits for Phase 8.

### Phase 6: Frames 1E, 3C, 1C through every layer

- [ ] T35 1E commands (A1E)
- [ ] T36 1E frame and vectors
- [ ] T37 1E in the mock, integration, planner and device
- [ ] T38 Serial receive state machine
- [ ] T39 3C formats 1 and 4
- [ ] T40 3C formats 2 and 3, serial options, 4C vectors (tagged v2)
- [ ] T41 1C commands, formats 1 and 4
- [ ] T42 1C formats 2 and 3, AnA command set, message wait
- [ ] T43 Session serial behaviour (EOT, flush, inter-character timeout, retries)
- [ ] T44 Mock serial server direction
- [ ] T45 Integration matrix complete (12 combinations)
- [ ] T46 `SerialTransport` and serial loopback

**Checkpoint E1 (after T37):** 1E complete. **Checkpoint E2 (after T42):** codec complete. **Checkpoint E:** all four frames through every layer.

### Phase 7: Hardware capture tooling (`hil-capture`)

- [ ] T47 `tools/` wiring, profile and plan loading
- [ ] T48 Safety gate and dry run
- [ ] T49 `RecordingTransport` and capture writer
- [ ] T50 Step runner and end-to-end run against `virtual_plc`
- [ ] T51 Replay tests `mc_replay_tests`
- [ ] T52 Timing benchmark and `BENCH.md`
- [ ] T53 Plans for the four frame families

**Checkpoint F:** tooling proven against `virtual_plc`. **Owner gate:** scratch areas decided; FX3 manual checked.

### Phase 8: Bench and release

- [ ] T54 Capture the 14 bench profiles (owner-operated)
- [ ] T55 Findings triage
- [ ] T56 Release 1.0

**Checkpoint G:** ready to tag `v1.0.0`.

## Parallelization

- **Contract first:** `McProtocol` / `Parser` (T15) before Phases 3 and 4 start.
- **Safe in parallel after Checkpoint B:** T17–T24 (session) and T25–T27 (mock); they share only `core-protocol`. T28 joins them.
- **Safe in parallel in Phase 6:** the 1E track (T35–T37) and the serial codec track (T38–T42); T43–T46 need both.
- **Sequential:** tasks that change `Session` (T19–T24, T43) and `McDevice` (T32–T33).

## Commands used by every task

Toolchain on this machine: CMake and Ninja from `C:\Qt\Tools`, Qt 6.11.1 kits `msvc2022_64` and `mingw_64`, MSVC from Visual Studio 2026 (needs the VS developer shell), MinGW GCC 13.1 in `C:\Qt\Tools\mingw1310_64`.

The everyday loop of every task is **MSVC** in `build/cmake-debug` (owner decision 2; the old app's kit). MinGW runs at every checkpoint. `scripts/vsdev.ps1` (added in T01) loads the VS developer environment into the current PowerShell through `vswhere`, so every role enters MSVC the same way.

```powershell
# MSVC flavour: the daily loop
. scripts/vsdev.ps1
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build/cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/cmake-debug
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build/cmake-debug -L <label> --output-on-failure

# MinGW flavour (PATH += C:\Qt\Tools\mingw1310_64\bin)
cmake -S . -B build/cmake-mingw -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64

# Everything (after T04)
scripts/check.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64
```

Labels: `build`, `core_model`, `core_protocol`, `core_session`, `mock`, `integration`, `device`, `replay`, `hil_tool`.

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| The sans-I/O `Session` turns out contorted (ideas §5) | High | Built right after the 3E codec (T17–T24); Checkpoint C is a go/no-go; fallback is direction B with the `McProtocol` API unchanged |
| Transcription error in a golden vector hides a codec bug or invents one | High | T11's guard checks every vector's length against the byte count printed in its reference-spec heading; the mock checks the same vectors in reverse (T26, T37, T44); real captures (T54) are the final judge |
| Zero-allocation promise breaks quietly as code grows | Med | ALC tests arrive with each layer (T10, T16, T22, T24, T36, T42) and run at every checkpoint |
| Serial frames were never verified on the wire (old app) | Med | Vectors plus mock in Phase 6; bench captures in T54 (3C on Q C24, 1C on FX3); 1C F2/F3 stay labelled wire-unverified |
| Linux is not verified in v1 | Low | Owner decision 1: v1 is verified on Windows only (MSVC + MinGW GCC); `check.sh` still runs in Git Bash with MinGW; README says Linux is unverified; `SPEC-build-packaging` success criterion 1 amended accordingly |
| Qt 6.2 minimum claimed, only Qt 6.11 installed | Low | Owner decision 4: the reviewer checks the "since" version of every Qt API a task adds (T30–T34, T46); README says "verified on 6.11, uses only the 6.2 API" |
| MSVC in VS 2026 with the Qt `msvc2022_64` kit | Low | MSVC 2015–2026 toolsets are binary compatible; T03's qmake consumer links QtCore at the very start to prove it |
| A real PLC disagrees with the reference spec | Med (expected) | Findings process (decision H3): log, tag `known-divergence`, owner decides; no silent library change |

## Owner decisions (2026-09-27)

The five open questions of the draft, decided one by one with the owner.

| # | Question | Decision |
|---|---|---|
| 1 | Linux / GCC on Linux | **v1 is verified on Windows only** (MSVC + MinGW GCC). No concrete plan to use the library on Linux yet. `SPEC-build-packaging` success criterion 1 drops the Linux clause; README states the verified platforms. |
| 2 | Daily compiler | **MSVC** in `build/cmake-debug`, entered through `scripts/vsdev.ps1` (T01). Both compilers at every checkpoint. |
| 3 | Commit cadence | **One commit per task**, after the owner approves it (agent-team Gate 2, `agent-team/project/rules.md`). No standing permission. |
| 4 | Qt minimum | **Keep Qt 6.2**, enforced by review: the reviewer checks the "since" version of every Qt API a task adds. Only 6.11 is tested. |
| 5 | Real-PLC smoke at Checkpoint D | **No.** Nothing touches a real PLC before Phase 8 (T54). |

## Open questions

None for the plan. Owner input still to come: the LICENSE copyright holder (asked at T04); scratch areas and the FX3 manual checks (asked at Checkpoint F).
