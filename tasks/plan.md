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
- **Zero-allocation tests arrive with the code they guard**, not at the end: core-model (T-011), core-protocol (T-018), session (T-026).
- **Two compilers from day one.** MSVC (VS 2026, Qt `msvc2022_64`) and MinGW GCC 13.1 (Qt `mingw_64`) are both on this machine. Every checkpoint builds with both.
- **HIL comes last and is owner-gated.** The capture tool is verified against `virtual_plc` first. The two owner prerequisites (scratch areas, FX3 manual checks; `SPEC-hil-capture.md`, operator procedure) were decided on 2026-10-04 (Checkpoint F owner gate); the owner now runs the real captures.

## Dependency graph

```text
T-002–T-005 build skeleton ── A0
   └── T-006–T-011 core-model ── A
          └── T-013–T-018 codec: loader, primitives, device encode, QnA, 3E ── B
                 ├── T-020–T-027 session on 3E ── C1 (after T-023) ── C (go/no-go)
                 └── T-029–T-031 mock on 3E (parallel with T-020–T-027)
                        T-027 + T-031 ── T-032 integration rig 3E ── T-033 session_loop
                        T-027 + T-032 ── T-035–T-039 qt-device over TCP ── D
                                        └── T-041–T-043 1E through every layer ── E1
                                        └── T-045–T-049 serial codec 3C, 1C ── E2
                                               └── T-051–T-054 serial session, mock, matrix, SerialTransport ── E
                                                      └── T-056–T-063 hil-capture tool + replay ── F (+ owner gate)
                                                             └── T-066 X/Y octal, T-067–T-073 GUI tool (McDevice in an app) ── H
                                                                    └── T-075–T-077 follow-ups (mock streams, typed confirmation, Release builds)
                                                                    └── T-078 Qt 5.15 library + tools ── T-079 Qt 5.15 GUI
                                                                    └── P9-1–P9-2 bench captures, findings
                                                                           └── P9-3 release 1.0 ── G
```

## Task index

Details (acceptance criteria, verification, files) are in `tasks/todo.md`.

**One numbering (owner request 2026-10-03).** Every item is named by its baton id `T-0xx`
(`tasks/active|done/T-0xx.md`), numbered in execution order. Ids are not contiguous inside a
phase because housekeeping tasks (e.g. `T-001`, `T-056`, `T-065`) and every checkpoint (coverage,
both compilers, phase tester and reviewer) are baton tasks too. Items of a phase not yet planned
are labelled `P<phase>-<n>` and get their baton ids when the phase is planned. Baton task files
created before 2026-10-03 still name the old plan numbers (`T01`…`T56`) in `Plan ref`.

### Phase 0: Build skeleton (`build-packaging`)

- [x] T-002 CMake skeleton with `mc::core`, version header, doctest
- [x] T-003 qmake mirror
- [x] T-004 Build guard tests (pri sync, include hygiene, consumer smokes)
- [x] T-005 `check` scripts, formatting, README, CHANGELOG, LICENSE

**Checkpoint A0** (in T-005): both build systems and both compilers green on an almost empty library. Done.

### Phase 1: Data layer (`core-model`)

- [x] T-006 `Error`, `Expected<T>`, byte views
- [x] T-007 Device table, parsing, formatting, ordering
- [x] T-008 `FrameConfig`, `Request`, `validate()`
- [x] T-009 Limits table and `chunk()`
- [x] T-010 `convert` helpers
- [x] T-011 `LogSink`, `hexDump`, zero-allocation test

**Checkpoint A** → T-012: every table row verified; ALC-01; coverage ≥ 95 % (MinGW). Done (97.94 %, commit `b45d704`).

### Phase 2: Wire codec, 3E first (`core-protocol`)

- [x] T-013 Vector loader and `.vec` format (with the transcription guard)
- [x] T-014 Primitives
- [x] T-015 Device encoding for all eight families
- [x] T-016 QnA batch read/write commands
- [x] T-017 `McProtocol` and `Parser` facade with 3E Binary
- [x] T-018 3E ASCII, streaming, sizes, zero allocation

**Checkpoint B** → T-019: every A.1 / A.2 vector round-trips; owner reviews `protocol.h`, the contract of Phases 3 and 4. Done: commit `e71c7cc`; `protocol.h` approved by the owner 2026-09-27.

### Phase 3: Engine on 3E (`core-session`)

- [x] T-020 `RangeSet` and `ReadPlan`
- [x] T-021 `ValueStore`
- [x] T-022 `Session` scheduling skeleton and test harness
- [x] T-023 Receive path and value publishing
- [x] T-024 Dynamic subscriptions
- [x] T-025 Ad-hoc requests (arena, burst cap, exactly-once)
- [x] T-026 Ethernet faults, drain contract, heartbeat
- [x] T-027 Steady-state zero allocation and session benchmark

**Checkpoint C1 (after T-023, inside T-023):** polling path works on a fake clock. Done.
**Checkpoint C (go/no-go)** → T-028: the sans-I/O engine holds, or fall back to direction B (ideas §3) before anything is built on it.

### Phase 4: Mock PLC on 3E (`mock-plc`); T-029–T-031 in parallel with Phase 3

- [x] T-029 `MockPlc` facade and memory image
- [x] T-030 3E server direction against the vectors
- [x] T-031 Fault injection and corruption (Ethernet)
- [x] T-032 Integration rig and the 3E part of the matrix
- [x] T-033 `examples/session_loop`

**Checkpoint C4** → T-034 (added 2026-09-30 for phase-batched verification): mock and integration green on both compilers and qmake, `src/mock` coverage ≥ 90 %, one commit for Phase 4. Checkpoint C was passed as **go** (owner, 2026-09-30). Done: `src/mock` 96.74 %, one Phase 4 commit.

### Phase 5: Qt device over TCP (`qt-device`)

- [x] T-035 `Transport`, `TcpTransport`, meta types
- [x] T-036 `McDeviceConfig` and JSON
- [x] T-037 `McDevice` link state, pump and signal queue
- [x] T-038 `McDevice` faults, re-entrancy, threads, destructor
- [x] T-039 Examples `qt_console_poller` and `virtual_plc`

**Checkpoint D** → T-040: 3E through the whole stack; demo `virtual_plc` ↔ `qt_console_poller`. No real PLC here (owner decision 5); real hardware waits for Phase 9. Done: commit `4e4d015`; demo confirmed by the owner 2026-10-01.

### Phase 6: Frames 1E, 3C, 1C through every layer

- [x] T-041 1E commands (A1E)
- [x] T-042 1E frame and vectors
- [x] T-043 1E in the mock, integration, planner and device
- [x] T-045 Serial receive state machine
- [x] T-046 3C formats 1 and 4
- [x] T-047 3C formats 2 and 3, serial options, 4C vectors (tagged v2)
- [x] T-048 1C commands, formats 1 and 4
- [x] T-049 1C formats 2 and 3, AnA command set, message wait
- [x] T-051 Session serial behaviour (EOT, flush, inter-character timeout, retries)
- [x] T-052 Mock serial server direction
- [x] T-053 Integration matrix complete (12 combinations)
- [x] T-054 `SerialTransport` and serial loopback

**Checkpoint E1** → T-044 (after T-043): 1E complete. **Checkpoint E2** → T-050 (after T-049): codec complete. **Checkpoint E** → T-055: all four frames through every layer. Run as three batches 6a/6b/6c, one commit each (owner decision 2026-10-01). Done: commits `ae51bb7` (E1), `d2dfee5` (E2), `0e6053c` (E); owner review approved 2026-10-02.

### Phase 7: Hardware capture tooling (`hil-capture`)

- [x] T-057 `tools/` wiring, profile and plan loading
- [x] T-058 Safety gate and dry run
- [x] T-059 `RecordingTransport` and capture writer
- [x] T-060 Step runner and end-to-end run against `virtual_plc`
- [x] T-061 Replay tests `mc_replay_tests`
- [x] T-062 Timing benchmark and `BENCH.md`
- [x] T-056 Parallel check scripts (owner request)
- [x] T-063 Plans for the four frame families
- [x] T-065 Owner follow-ups: `maxConsecutiveLinkErrors` 0 rejected, `MockPlc::skippedBytes()`

**Checkpoint F** → T-064: tooling proven against `virtual_plc`. **Owner gate:** scratch areas decided; FX3 manual checked. Done: commit `70cca94` (T-065: `657f814`); owner gate ticked 2026-10-04 (answers in `temp-docs/hil-scratch-areas.md`).

### Phase 8: GUI tool `mc_workbench` (`gui-tool`, spec approved 2026-10-03)

- [x] T-066 X/Y octal numbering for FX CPUs (library, before the GUI)
- [x] T-067 Wiring: build options, ADS local path and MinGW build script, qpb, empty main window
- [x] T-068 Runner threads: DeviceRunner, MockRunner, containment, shutdown
- [x] T-069 Device tab: config grid, connect, subscriptions, trend, ad-hoc console
- [x] T-070 Mock PLC tab: TCP and COM serving, memory editor, faults, request log
- [x] T-071 Frame trace, debug log, back-pressure, capture and export
- [x] T-072 HIL runner view
- [x] T-073 Workspace save / load and dock layout

**Checkpoint H** → T-074: GUI proven against MockPlc and `virtual_plc` on MSVC and MinGW; owner demo. First item done: commit `75ece90` (T-066: `cd2764a`); the owner demo is open (steps in `tasks/done/T-074.md`).

Follow-ups (owner 2026-10-04, leader gates as Phase 8), done in commit `11bc5f0`:

- [x] T-075 `MockPlc` input streams (one parser per client, MCK-13) and the GUI mock on them
- [x] T-076 HIL view: confirm without typing only for loopback profiles
- [x] T-077 Release builds green on MSVC and MinGW; Release CMake presets

Also: CMake presets for Qt Creator (`6da2242`); known issues noted without a task in `tasks/todo.md` (owner 2026-10-04).

### Qt 5.15 support (owner 2026-10-07; follow-up before Phase 9, leader gates as Phase 8)

Scope: everything, the GUI included. Qt 5.15.0 is installed with the `msvc2019_64` kit only, so Qt5 is verified on MSVC; Qt 6 stays supported unchanged.

- [x] T-078 Library, examples, Qt tests and `hil_capture` on Qt 5.15 (CMake and qmake, presets, check script); spec minimum "Qt 5.15 or 6.2+"
- [x] T-079 GUI `mc_workbench` on Qt 5.15: ADS built for Qt5, qpb 1.7.0 (Qt 5.15 support, from the owner), app sources

Batch tester and reviewer after T-079; one commit for both. Done 2026-10-08 (tester and reviewer PASS after one rework).

**Pre-release v0.1.0** (owner 2026-10-08), after T-078/T-079 are committed:

- [x] Build and test again through the Qt Creator MCP server: kit `msvc-release` built (0 warnings) and `ctest`
  35/35; the MCP cannot switch kits, so the other five kits were skipped by the owner (2026-10-08) — all six were
  verified on the command line by the batch tester
- [x] `CHANGELOG.md`: `Unreleased` → `[0.1.0]` (pre-release); `include/mc/version.h` already 0.1.0; tag `v0.1.0`
  on that commit (spec "Versioning"); export a new git bundle of the repository

### Phase 9: Bench and release

Waits until the owner has finished the HIL captures (owner, 2026-10-04); not delegated.

- [ ] P9-1 Capture the 14 bench profiles (owner-operated)
- [ ] P9-2 Findings triage
- [ ] P9-3 Release 1.0

**Checkpoint G:** ready to tag `v1.0.0`.

## Parallelization

- **Contract first:** `McProtocol` / `Parser` (T-017) before Phases 3 and 4 start.
- **Safe in parallel after Checkpoint B:** T-020–T-027 (session) and T-029–T-031 (mock); they share only `core-protocol`. T-032 joins them.
- **Safe in parallel in Phase 6:** the 1E track (T-041–T-043) and the serial codec track (T-045–T-049); T-051–T-054 need both.
- **Sequential:** tasks that change `Session` (T-022–T-027, T-051) and `McDevice` (T-037–T-038).

## Commands used by every task

Toolchain on this machine: CMake and Ninja from `C:\Qt\Tools`, Qt 6.11.1 kits `msvc2022_64` and `mingw_64`, MSVC from Visual Studio 2026 (needs the VS developer shell), MinGW GCC 13.1 in `C:\Qt\Tools\mingw1310_64`.

The everyday loop of every task is **MSVC** in `build/cmake-debug` (owner decision 2; the old app's kit). MinGW runs at every checkpoint. `scripts/vsdev.ps1` (added in T-002) loads the VS developer environment into the current PowerShell through `vswhere`, so every role enters MSVC the same way.

```powershell
# MSVC flavour: the daily loop
. scripts/vsdev.ps1
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build/cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/cmake-debug
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build/cmake-debug -L <label> --output-on-failure

# MinGW flavour (PATH += C:\Qt\Tools\mingw1310_64\bin)
cmake -S . -B build/cmake-mingw -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64

# Everything (after T-005)
scripts/check.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64
```

Labels: `build`, `core_model`, `core_protocol`, `core_session`, `mock`, `integration`, `device`, `replay`, `hil_tool`.

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| The sans-I/O `Session` turns out contorted (ideas §5) | High | Built right after the 3E codec (T-020–T-027); Checkpoint C is a go/no-go; fallback is direction B with the `McProtocol` API unchanged |
| Transcription error in a golden vector hides a codec bug or invents one | High | T-013's guard checks every vector's length against the byte count printed in its reference-spec heading; the mock checks the same vectors in reverse (T-030, T-043, T-052); real captures (P9-1) are the final judge |
| Zero-allocation promise breaks quietly as code grows | Med | ALC tests arrive with each layer (T-011, T-018, T-025, T-027, T-042, T-049) and run at every checkpoint |
| Serial frames were never verified on the wire (old app) | Med | Vectors plus mock in Phase 6; bench captures in P9-1 (3C on Q C24, 1C on FX3); 1C F2/F3 stay labelled wire-unverified |
| Linux is not verified in v1 | Low | Owner decision 1: v1 is verified on Windows only (MSVC + MinGW GCC); `check.sh` still runs in Git Bash with MinGW; README says Linux is unverified; `SPEC-build-packaging` success criterion 1 amended accordingly |
| Qt 6.2 minimum claimed, only Qt 6.11 installed | Low | Owner decision 4: the reviewer checks the "since" version of every Qt API a task adds (T-035–T-039, T-054); README says "verified on 6.11, uses only the 6.2 API" |
| MSVC in VS 2026 with the Qt `msvc2022_64` kit | Low | MSVC 2015–2026 toolsets are binary compatible; T-004's qmake consumer links QtCore at the very start to prove it |
| A real PLC disagrees with the reference spec | Med (expected) | Findings process (decision H3): log, tag `known-divergence`, owner decides; no silent library change |

## Owner decisions (2026-09-27)

The five open questions of the draft, decided one by one with the owner.

| # | Question | Decision |
|---|---|---|
| 1 | Linux / GCC on Linux | **v1 is verified on Windows only** (MSVC + MinGW GCC). No concrete plan to use the library on Linux yet. `SPEC-build-packaging` success criterion 1 drops the Linux clause; README states the verified platforms. |
| 2 | Daily compiler | **MSVC** in `build/cmake-debug`, entered through `scripts/vsdev.ps1` (T-002). Both compilers at every checkpoint. |
| 3 | Commit cadence | **One commit per task**, after the owner approves it (agent-team Gate 2, `agent-team/project/rules.md`). No standing permission. |
| 4 | Qt minimum | **Keep Qt 6.2**, enforced by review: the reviewer checks the "since" version of every Qt API a task adds. Only 6.11 is tested. |
| 5 | Real-PLC smoke at Checkpoint D | **No.** Nothing touches a real PLC before Phase 9 (P9-1). |

## Open questions

None for the plan. Owner input still to come: the LICENSE copyright holder (asked at T-005); scratch areas and the FX3 manual checks (asked at Checkpoint F).
