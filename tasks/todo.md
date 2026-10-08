# Task List: MC Protocol Library (`mc`) v1

- **Plan:** `tasks/plan.md` · **Specs:** `docs/spec/`
- **Conventions for every task**
  - Done = acceptance criteria met **and** the Definition of Done (`agent-team/project/definition-of-done.md`): runs, tests fail without the change and pass with it, no regressions, scoped, documented per `docs/rules/doc_comment_style.md` (`/** */` + tags on every public symbol, `@par Complexity` with allocation on every public `core-*` function).
  - Adding a source file updates `CMakeLists.txt` **and** the matching `.pri` in the same task (BLD-04 enforces it from T-004 on).
  - `cmake`/`ctest` = `C:\Qt\Tools\CMake_64\bin\*.exe`. `build/cmake-debug` = MSVC, the daily loop of every task (VS 2026 environment via `. scripts/vsdev.ps1` from T-002, `-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64`), `build/cmake-mingw` = MinGW 13.1 (`C:/Qt/6.11.1/mingw_64`), run at checkpoints. "Both compilers" means both folders.
  - v1 is verified on Windows only (owner decision 1); nothing touches a real PLC before P9-1 (owner decision 5); Qt APIs stay within 6.2 (owner decision 4, checked in review).
  - One commit per task, only after the owner approves it; author `dev28`, no AI trailer.
- **Size:** S = 1–2 files · M = 3–5 files · a task listing more files counts small boilerplate or data files.

---

## Phase 0: Build skeleton (`SPEC-build-packaging.md`)

### T-002: CMake skeleton with `mc::core`, version header and doctest

**Description:** Root and `src/` CMake projects that define `mc_core` / `mc::core` (static, C++17, `include/` public, `src/` private), the option set of the spec (`MC_BUILD_*`, top-level detection for 3.16), warning and version helpers, the vendored doctest header, and the first test binary with the version test. Also `scripts/vsdev.ps1`, dot-sourced to load the VS developer environment (x64) found by `vswhere` into the current PowerShell; it does nothing when `cl.exe` is already on `PATH`.

**Acceptance criteria:**
- [ ] Configure and build succeed with both compilers; `ctest -L build` runs BLD-08 (`mc::version()` equals the `MC_VERSION_*` macros) green.
- [ ] `MC_BUILD_TESTS`, `MC_BUILD_EXAMPLES` and `MC_WARNINGS_AS_ERRORS` are ON when top-level and OFF when consumed; `PROJECT_VERSION` equals `MC_VERSION_STRING`.
- [ ] Warnings (`/W4` or `-Wall -Wextra -Wpedantic`) are errors in the top-level build, and the build is clean.

**Verification:**
- [ ] From a plain PowerShell (no VS environment): `. scripts/vsdev.ps1; cmake -S . -B build/cmake-debug -G Ninja …; cmake --build build/cmake-debug; ctest --test-dir build/cmake-debug -L build --output-on-failure`
- [ ] Same in `build/cmake-mingw`.

**Dependencies:** None

**Files likely touched:** `scripts/vsdev.ps1`, `CMakeLists.txt`, `src/CMakeLists.txt`, `cmake/mc_warnings.cmake`, `cmake/mc_version.cmake`, `include/mc/version.h`, `src/core/version.cpp`, `tests/CMakeLists.txt`, `tests/third_party/doctest/doctest.h` (vendored, MIT, approved in capability-map assumption 2), `tests/core/test_version.cpp`

**Scope:** M (boilerplate)

### T-003: qmake mirror

**Description:** `mc_core.pri`, `mc_device.pri`, `mc_mock.pri`, `mc_protocol.pri` with include guards and `$$PWD` paths, and the dev-only `mc_protocol.pro` (`subdirs`) that builds the test binaries through the `.pri` files.

**Acceptance criteria:**
- [ ] `qmake mc_protocol.pro` + `make check` builds and passes the version test through `mc_core.pri` (BLD-03).
- [ ] Including `mc_core.pri` twice (directly and through `mc_device.pri`) adds no duplicate source.
- [ ] No `.pri` sets `TARGET`, `TEMPLATE` or `DESTDIR`; `mc_device.pri` adds `QT += core network serialport`.

**Verification:**
- [ ] In `build/qmake-debug`: `qmake ../../mc_protocol.pro CONFIG+=debug; nmake; nmake check` (MSVC dev shell).

**Dependencies:** T-002

**Files likely touched:** `mc_core.pri`, `mc_device.pri`, `mc_mock.pri`, `mc_protocol.pri`, `mc_protocol.pro`, `tests/qmake/tests.pro`, `tests/qmake/core_version.pro`

**Scope:** M (boilerplate)

### T-004: Build guard tests

**Description:** The ctest scripts that keep the build honest: `pri_sync` (BLD-04), `include_hygiene` (BLD-05 a–d), and the two consumer smoke projects (BLD-06 CMake, BLD-07 qmake). The qmake consumer links QtCore, which also proves early that MSVC 2026 links the Qt `msvc2022_64` kit.

**Acceptance criteria:**
- [ ] Deleting a source entry from a `.pri` makes BLD-04 fail and name the file; adding `#include <QString>` to a core header makes BLD-05 fail.
- [ ] `tests/consumer_cmake` builds as its own project with `add_subdirectory` + `target_link_libraries(mc::core)` and creates no library test target.
- [ ] `tests/consumer_qmake/app.pro` builds with one `include(…mc_protocol.pri)` line and runs.

**Verification:**
- [ ] `ctest -L build` green; the two negative checks above done by hand once and reverted.

**Dependencies:** T-002, T-003

**Files likely touched:** `cmake/check_pri_sync.cmake`, `cmake/check_include_hygiene.cmake`, `tests/consumer_cmake/CMakeLists.txt`, `tests/consumer_cmake/main.cpp`, `tests/consumer_qmake/app.pro`, `tests/consumer_qmake/main.cpp`

**Scope:** M

### T-005: `check` scripts, formatting and repository documents

**Description:** `scripts/check.ps1` and `scripts/check.sh` running every stage in order and stopping at the first failure (full CMake build, core-only build with Qt removed from the environment, qmake build, consumer smokes); `.clang-format`; README (three consumption paths, version policy), CHANGELOG (Keep a Changelog, `Unreleased`), LICENSE (MIT, decided).

**Acceptance criteria:**
- [ ] `scripts/check.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64` exits 0 and prints each stage; a failing stage stops it with the stage name.
- [ ] The core-only stage configures without Qt and its CMake trace contains no `find_package(Qt6` (BLD-02).
- [ ] README explains the three consumption paths and the version policy in under one screen.

**Verification:**
- [ ] Run `scripts/check.ps1`; run `scripts/check.sh` in Git Bash with MinGW on `PATH`.

**Dependencies:** T-004

**Files likely touched:** `scripts/check.ps1`, `scripts/check.sh`, `.clang-format`, `README.md`, `CHANGELOG.md`, `LICENSE`

**Scope:** M. Note: ask the owner for the copyright holder name in `LICENSE`.

### Checkpoint A0: skeleton (in baton T-005)
- [x] `scripts/check.ps1` green; both compilers; qmake and CMake. (T-005, 2026-09-27)
- [x] Owner reviews the repository layout before code lands in it. (Owner confirmed 2026-09-30.)

---

## Phase 1: Data layer (`SPEC-core-model.md`)

### T-006: `Error`, `Expected<T>`, byte views

**Description:** `types.h` (`ByteView`, `MutableByteView`, `ByteBuf`, `kNoCode`) and `result.h` (`ErrorCategory`, `ErrorCode` including `NotSubscribed`, `ErrorInfo`, `Error`, `Expected<T>` and `Expected<void>`, move-only `T` supported), header-only.

**Acceptance criteria:**
- [ ] RES-01…04 pass (value/error semantics, `Error::ok()`, static `message`, move-only `T`, copy of a move-only `Expected` does not compile).
- [ ] `Error` and `Expected<int>` are trivially copyable (static assertions).

**Verification:**
- [ ] `cmake --build build/cmake-debug --target mc_core_model_tests; ctest --test-dir build/cmake-debug -L core_model`

**Dependencies:** T-002

**Files likely touched:** `include/mc/core/types.h`, `include/mc/core/result.h`, `tests/core/model/test_result.cpp`, `tests/CMakeLists.txt`

**Scope:** S

### T-007: Device table, parsing, formatting, ordering

**Description:** The constexpr table of all 29 symbols of spec §3.2 with every code column and footnote, `deviceInfo()`, `parseDevice()` (case-insensitive, longest match, radix per symbol), `formatDevice()`, and `Device` `==`/`!=`/`<`.

**Acceptance criteria:**
- [ ] DEV-01…06 and DEV-14 pass; DEV-07-data checks every row against spec §3.2, one `SUBCASE` per row.
- [ ] `static_assert` that the table size equals `DeviceType::Count` and every row's `type` equals its index.

**Verification:**
- [ ] `ctest -L core_model` (both compilers).

**Dependencies:** T-006

**Files likely touched:** `include/mc/core/device.h`, `src/core/model/device_table.cpp`, `src/core/model/device_parse.cpp`, `tests/core/model/test_device.cpp`

**Scope:** M

### T-008: `FrameConfig`, `Request`, `validate()`

**Description:** `FrameConfig` with the named constructors and spec §8.3 defaults (`checkRoute` true for 3C/1C), `validate()` and `effectiveTimeoutMs()`; `Request` with its builders; `validate(Request, FrameConfig)` implementing the seven rules in order.

**Acceptance criteria:**
- [ ] CFG-01…05 pass, including format 5 rejected for every frame and `Ascii` forced for 3C/1C.
- [ ] DEV-08…13 pass; each of the seven rules has a test that fails only that rule.

**Verification:**
- [ ] `ctest -L core_model` (both compilers).

**Dependencies:** T-007

**Files likely touched:** `include/mc/core/frame_config.h`, `include/mc/core/request.h`, `src/core/model/frame_config.cpp`, `src/core/model/validate.cpp`, `tests/core/model/test_frame_config.cpp`

**Scope:** M

### T-009: Limits table and `chunk()`

**Description:** The full constexpr transcription of spec §4.4 (random-access rows included for v1.1), `maxPoints()`, `chunkCount()` and `chunk()` per spec §8.5 (step 16 for word access to bit devices, `splitWrites` policy).

**Acceptance criteria:**
- [ ] LIM-01 covers every cell of the v1 table; LIM-02 checks the 1E column and marks it unverified in code.
- [ ] CHK-01…06 pass, including `BufferTooSmall` and the write-split policy.

**Verification:**
- [ ] `ctest -L core_model` (both compilers).

**Dependencies:** T-008

**Files likely touched:** `include/mc/core/limits.h`, `src/core/model/limits_table.cpp`, `src/core/model/chunk.cpp`, `tests/core/model/test_limits.cpp`, `tests/core/model/test_chunk.cpp`

**Scope:** M

### T-010: `convert` helpers

**Description:** Word/dword/float/string accessors and writers, bit packing, words ↔ bits, owning `from*` builders, all bounds-checked (spec §2.3, §2.4, §8.7).

**Acceptance criteria:**
- [ ] CNV-01…08 pass (170F56ABH, 0.75f, "ABCD", −1, float64 round trip, PRIM-15 bit positions, pack/unpack round trip, out-of-range returns 0/false).

**Verification:**
- [ ] `ctest -L core_model` (both compilers).

**Dependencies:** T-006

**Files likely touched:** `include/mc/core/convert.h`, `src/core/model/convert.cpp`, `tests/core/model/test_convert.cpp`

**Scope:** S

### T-011: `LogSink`, `hexDump`, zero-allocation test

**Description:** `LogSink`, `NullLogSink`, `hexDump` with control-code names; a counting global `operator new` shared by every later allocation test; ALC-01 for core-model.

**Acceptance criteria:**
- [ ] LOG-01 (no `write()`, no string built when disabled) and LOG-02 (`<STX>` names) pass.
- [ ] ALC-01: zero allocations across `parseDevice`, `validate`, `chunk`, `convert::float64At`, `hexDump`, `formatDevice`.

**Verification:**
- [ ] `ctest -L core_model` (both compilers).

**Dependencies:** T-007–T-010

**Files likely touched:** `include/mc/core/log.h`, `src/core/model/log.cpp`, `tests/core/model/test_log.cpp`, `tests/common/alloc_counter.h`, `tests/core/model/test_alloc.cpp`

**Scope:** M

### Checkpoint A: core-model (baton T-012)
- [x] `ctest -L core_model` green with both compilers; BLD-04/05 green. (T-012)
- [x] MinGW with `-DMC_COVERAGE=ON`: line coverage of `src/core/model` ≥ 95 %; every `ErrorCode` value appears in a test. (97.94 %, T-012)

---

## Phase 2: Wire codec, 3E first (`SPEC-core-protocol.md`)

### T-013: Vector loader and `.vec` format

**Description:** `tests/common/vectors.h/.cpp`: parses `# key: value` metadata, one hex line per vector, `<STX>`-style names, tags (`v1.1`, `v2`), and exposes every metadata key as data for the mock and replay tests. Every vector carries `bytes:` transcribed from its reference-spec heading.

**Acceptance criteria:**
- [ ] VEC-01: a sample file with every syntax element round-trips to the expected records; malformed lines fail with file and line.
- [ ] VEC-02: for every vector in `tests/vectors/`, hex length equals its `bytes:` value (the transcription guard).

**Verification:**
- [ ] `ctest -L core_protocol`.

**Dependencies:** T-011

**Files likely touched:** `tests/common/vectors.h`, `tests/common/vectors.cpp`, `tests/core/protocol/test_vectors_format.cpp`, `tests/vectors/sample.vec`

**Scope:** M

### T-014: Primitives

**Description:** Hex-ASCII (upper-case encode, lower-case accepted on decode), 8-bit sum check, `AsciiCodec` / `BinaryCodec` field codecs (byte-wise, no `reinterpret_cast`), with PRIM vectors in `prim.vec`.

**Acceptance criteria:**
- [ ] PRIM-01…09 and PRIM-12…17 pass (PRIM-10, 11, 18 tagged `v2`).
- [ ] Non-hex character → `InvalidCharacter`; field overflow → `PointCount` / `InvalidDevice` per spec.

**Verification:**
- [ ] `ctest -L core_protocol` (both compilers).

**Dependencies:** T-013

**Files likely touched:** `src/core/protocol/hexascii.h/.cpp`, `src/core/protocol/sumcheck.h/.cpp`, `src/core/protocol/field_codec.h`, `tests/vectors/prim.vec`, `tests/core/protocol/test_primitives.cpp`

**Scope:** M

### T-015: Device encoding for all eight families

**Description:** `qnaDevice()`, `e1Device()`, `c1Device()` per spec §3.3 (QnA ASCII/Binary × Q/L/iQ-R, 1E ASCII/Binary, 1C ACPU/AnA).

**Acceptance criteria:**
- [ ] DEV-07 (wire half): every cell of the spec §3.3 example table (D100, X1F, TN10, M1234, M9000 × 8 families).
- [ ] `*` never a space, leading zeros never spaces.

**Verification:**
- [ ] `ctest -L core_protocol`.

**Dependencies:** T-014

**Files likely touched:** `src/core/protocol/device_encode.h/.cpp`, `tests/core/protocol/test_device_encode.cpp`

**Scope:** S

### T-016: QnA batch read/write commands

**Description:** 0401/1401 request data and response decoding written once against the field codec (no ASCII/Binary branches except device encoding); 0403/1402 tables present but unreachable in v1.

**Acceptance criteria:**
- [ ] CMD-01…10 byte-for-byte (CMD-11…14 transcribed, tagged `v1.1`).
- [ ] CMDD QnA rows pass, including odd bit counts and invalid nibble (CMDD-18).

**Verification:**
- [ ] `ctest -L core_protocol`.

**Dependencies:** T-015

**Files likely touched:** `src/core/protocol/command_qna.h/.cpp`, `tests/vectors/cmd.vec`, `tests/vectors/cmdd.vec`, `tests/core/protocol/test_commands.cpp`

**Scope:** M

### T-017: `McProtocol` and `Parser` facade with 3E Binary

**Description:** The public `protocol.h` (`McProtocol`, `Parser`, `ParseStatus`), dispatch by `FrameType`, and the 3E envelope and response parse for Binary. First end-to-end slice of the codec.

**Acceptance criteria:**
- [ ] Every A.1 vector round-trips (requests byte-for-byte, responses to the stated payload or `Error`).
- [ ] 3E-01…13 pass for Binary; SZ-01 holds for 3E Binary; `sizeof(Parser) <= 128`, trivially copyable.
- [ ] `protocol.h` exposes only `McProtocol`, `Parser`, `ParseStatus`; `encode()` always runs `validate()`.

**Verification:**
- [ ] `ctest -L core_protocol` (both compilers).

**Dependencies:** T-016

**Files likely touched:** `include/mc/core/protocol.h`, `src/core/protocol/protocol.cpp`, `src/core/protocol/frame_3e.h/.cpp`, `tests/vectors/3e_binary.vec`, `tests/core/protocol/test_frame_3e.cpp`

**Scope:** M

### T-018: 3E ASCII, streaming, sizes, zero allocation

**Description:** 3E ASCII, byte-at-a-time and coalesced parsing, `reset()`, the 4E vectors transcribed and tagged `v2`, and ALC-01 for the codec.

**Acceptance criteria:**
- [ ] Every A.2 vector round-trips; 3E-01…13 pass for ASCII; A.3/A.4 exist tagged `v2` and are skipped.
- [ ] STR-01, STR-02 (3E), STR-04 pass.
- [ ] ALC-01: zero allocations for encode-into-buffer, byte-at-a-time `feed`, and `payload()` on 3E.

**Verification:**
- [ ] `ctest -L core_protocol` (both compilers).

**Dependencies:** T-017

**Files likely touched:** `tests/vectors/3e_ascii.vec`, `tests/vectors/4e_binary.vec`, `tests/vectors/4e_ascii.vec`, `tests/core/protocol/test_parser_stream.cpp`, `tests/core/protocol/test_alloc.cpp`

**Scope:** M

### Checkpoint B: 3E codec (baton T-019)
- [x] `ctest -L core_protocol` green with both compilers; BLD-04/05 green. (T-019; coverage 97.03 %)
- [x] Owner reviews `protocol.h`: it is the contract Phases 3 and 4 build on. (owner approved 2026-09-27, header as committed in `e71c7cc`)

---

## Phase 3: Engine on 3E (`SPEC-core-session.md`)

### T-020: `RangeSet` and `ReadPlan`

**Description:** Subscription bookkeeping and the planner: bits-as-words alignment (incl. M9000 + 16k on 1E/1C), union, gap merge with `autoGap()`, chunking with `chunk()`, per-type contiguous chunk order.

**Acceptance criteria:**
- [ ] PLN-01, 02, 04…10 pass; PLN-03 passes for the 3E values (1E value added in T-043).
- [ ] `build()` failure keeps the previous plan and names the failing subscription's error.

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-018

**Files likely touched:** `include/mc/core/poll_plan.h`, `src/core/session/range_set.cpp`, `src/core/session/read_plan.cpp`, `tests/core/session/test_range_set.cpp`

**Scope:** M

### T-021: `ValueStore`

**Description:** Per-type segments with contiguous value and state arrays, `apply()` with silent baselines, `markFailed`, `markStale`, `resetBaselines`, `rebuild()` carry-over, bulk reads in the normalized layout.

**Acceptance criteria:**
- [ ] STO-01…04 pass.
- [ ] `apply()` and `markFailed()` allocate nothing (checked with the alloc counter).

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-020

**Files likely touched:** `include/mc/core/value_store.h`, `src/core/session/value_store.cpp`, `tests/core/session/test_value_store.cpp`

**Scope:** M

### T-022: `Session` scheduling skeleton and test harness

**Description:** `Session::create`, `linkUp` / `linkDown`, round scheduling (`FixedRate`, `FixedDelay`, interval 0), pre-encoded frames, `nextDeadline`, output ring and `nextOutput`; the harness (`FakeClock`, 3E `ScriptedPeer`, `OutputRecorder`).

**Acceptance criteria:**
- [ ] SES-01, SES-07, SES-08 pass.
- [ ] No clock read, no thread, no I/O in `src/core/session` (checked by review and by the harness driving all time).

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-021

**Files likely touched:** `include/mc/core/session.h`, `src/core/session/session.cpp`, `src/core/session/output_ring.h`, `tests/core/session/harness.h/.cpp`, `tests/core/session/test_session_poll.cpp`

**Scope:** M

### T-023: Receive path and value publishing

**Description:** Receive buffer, `Parser` driving, decode into the store, `ValuesChanged` per response from round 2, `Snapshot` per device type (held back to the end of round 1), `CycleDone`.

**Acceptance criteria:**
- [ ] SES-02…06 pass: the timeline of the spec's "Publishing values" section is reproduced exactly.
- [ ] SES-23 (byte-at-a-time delivery gives identical outputs) and SES-26 (clock going backwards) pass.

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-022

**Files likely touched:** `src/core/session/session_rx.cpp`, `src/core/session/session.cpp`, `tests/core/session/test_session_poll.cpp`

**Scope:** M

### Checkpoint C1: polling path (inside baton T-023)
- [x] The engine polls, publishes and schedules on 3E with a fake clock. Quick owner look at the harness style before the remaining engine tasks.

### T-024: Dynamic subscriptions

**Description:** `subscribe` / `unsubscribe` at run time, deferred re-plan at the next round boundary, carry-over of values and baselines.

**Acceptance criteria:**
- [ ] SES-22 passes; the current round is never re-planned mid-way.

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-023

**Files likely touched:** `src/core/session/session.cpp`, `tests/core/session/test_session_poll.cpp`

**Scope:** S

### T-025: Ad-hoc requests

**Description:** Ad-hoc arena (FIFO ring, `adHocArenaBytes`), queue with `adHocCapacity`, `maxAdHocBurst` dispatch, chunked ad-hoc reads and split writes, exactly-once `RequestDone`.

**Acceptance criteria:**
- [ ] SES-09…15 pass (incl. the W1–W4, C1, W5–W8 wire order and arena wrap-around).
- [ ] ALC-02: zero allocations across 1000 submit/complete cycles including `linkDown` with a full queue.

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-024

**Files likely touched:** `src/core/session/adhoc_queue.h/.cpp`, `src/core/session/session.cpp`, `tests/core/session/test_session_adhoc.cpp`

**Scope:** M

### T-026: Ethernet faults, drain contract, heartbeat

**Description:** The Ethernet column of the fault table (timeout, protocol error, unsolicited bytes, overflow → `LinkFault{…, reopen}`), the drain-contract check, the optional heartbeat.

**Acceptance criteria:**
- [ ] SES-16, SES-19 (Ethernet), SES-20, SES-24 (Ethernet), SES-25 pass.
- [ ] SES-21: heartbeat off by default; on → first frame of every round writes 1, 0, 1, …; no `RequestDone` for it.

**Verification:**
- [ ] `ctest -L core_session`.

**Dependencies:** T-025

**Files likely touched:** `src/core/session/session.cpp`, `src/core/session/session_rx.cpp`, `tests/core/session/test_session_fault.cpp`, `tests/core/session/test_session_heartbeat.cpp`

**Scope:** M

### T-027: Steady-state zero allocation and session benchmark

**Description:** ALC-01 for the engine and the regression benchmark (`MC_BUILD_BENCH`).

**Acceptance criteria:**
- [ ] ALC-01: zero `operator new` calls across rounds 3–10 with changes every round and the heartbeat on.
- [ ] `mc_bench_session` builds with `-DMC_BUILD_BENCH=ON` and prints per-round cost against subscribed points (no threshold).

**Verification:**
- [ ] `ctest -L core_session`; Release build of `build/cmake-bench` runs the benchmark.

**Dependencies:** T-026

**Files likely touched:** `tests/core/session/test_alloc.cpp`, `tests/bench/bench_session.cpp`, `tests/bench/CMakeLists.txt`

**Scope:** S

### Checkpoint C: engine go/no-go (ideas §5) (baton T-028)
- [x] `ctest -L core_session` green with both compilers; coverage of `src/core/session` ≥ 95 % (MinGW). (T-028; 97.77 %)
- [x] **Owner decision:** the sans-I/O `Session` holds without contortions → continue; otherwise fall back to direction B (engine moves to the Qt layer, `McProtocol` API unchanged) and re-plan Phases 3–5. (Owner 2026-09-30: **go**, direction C kept.)

---

## Phase 4: Mock PLC on 3E (`SPEC-mock-plc.md`). T-029–T-031 can run in parallel with Phase 3.

### T-029: `MockPlc` facade and memory image

**Description:** `mc_mock` target (CMake + `mc_mock.pri`), public `mock_plc.h`, sparse per-type memory where word access to a bit device sees 16 points per word, device limits, request log.

**Acceptance criteria:**
- [x] MCK-04 (writes change memory, bit/word views agree) and MCK-12 (device limit → out-of-range error) pass.
- [x] `mc_mock` builds without Qt; BLD-04/05 green.

**Verification:**
- [x] `ctest -L mock`.

**Dependencies:** T-018

**Files likely touched:** `include/mc/mock/mock_plc.h`, `src/mock/memory_image.h/.cpp`, `src/mock/mock_plc.cpp`, `tests/mock/test_mock_memory.cpp`

**Scope:** M

### T-030: 3E server direction against the vectors

**Description:** 3E request decoding and response building from the spec tables, reusing only the allowed primitives (independence rule), echoing the request route.

**Acceptance criteria:**
- [x] MCK-01…03 pass for A.1 and A.2 (requests decode to their metadata; responses reproduced byte-for-byte; V-3E-x-10 reproduced with `failRange`).
- [x] MCK-HYG passes; adding `#include "core/protocol/frame_3e.h"` to a mock source makes it fail.

**Verification:**
- [x] `ctest -L mock`.

**Dependencies:** T-029

**Files likely touched:** `src/mock/request_decode_ethernet.cpp`, `src/mock/command_exec.cpp`, `src/mock/response_build.cpp`, `tests/mock/test_mock_vectors.cpp`, `tests/mock/check_mock_includes.cmake`

**Scope:** M

### T-031: Fault injection and corruption (Ethernet)

**Description:** `failRange`, `mute`, `muteNext`, `corruptNext` for the Ethernet modes, unsupported commands.

**Acceptance criteria:**
- [x] MCK-09 and the Ethernet cases of MCK-11 pass with exactly the documented bytes.

**Verification:**
- [x] `ctest -L mock`.

**Dependencies:** T-030

**Files likely touched:** `src/mock/corruption.cpp`, `src/mock/mock_plc.cpp`, `tests/mock/test_mock_faults.cpp`

**Scope:** S

### T-032: Integration rig and the 3E part of the matrix

**Description:** In-memory pipe with seeded fragmentation, the rig (`Session` + `MockPlc` + fake clock), `mc_integration_tests` (label `integration`), scenarios on 3E Binary and 3E ASCII.

**Acceptance criteria:**
- [x] INT-01…03, 07, 08, 10, 11, 12, 13, 14, 15, 16 pass for 3E Binary and 3E ASCII.
- [x] The binary finishes in under 5 s of wall time.

**Verification:**
- [x] `ctest -L integration` (both compilers).

**Dependencies:** T-027, T-031

**Files likely touched:** `tests/mock/integration/pipe.h`, `tests/mock/integration/rig.h/.cpp`, `tests/mock/integration/test_integration.cpp`, `tests/CMakeLists.txt`

**Scope:** M

### T-033: `examples/session_loop`

**Description:** The non-Qt usage example of the core-session spec, driving `Session` against `MockPlc` through an in-memory pipe with `std::chrono::steady_clock`.

**Acceptance criteria:**
- [x] Builds with no Qt on the include path; prints round-1 snapshots, then changes the example makes in the mock.

**Verification:**
- [x] Build and run `build/cmake-core/examples/session_loop` (core-only configure).

**Dependencies:** T-032

**Files likely touched:** `examples/session_loop/main.cpp`, `examples/CMakeLists.txt`, `examples/qmake/session_loop.pro`

**Scope:** S

### Checkpoint C4: mock and integration on 3E (baton T-034; added by the leader for phase-batched verification)
- [x] `ctest -L "mock|integration"` green with both compilers and through qmake; MCK-HYG green; coverage of `src/mock` ≥ 90 % (MinGW); `mc_integration_tests` < 5 s.
- [x] Phase tester and reviewer pass; one commit for Phase 4.

---

## Phase 5: Qt device over TCP (`SPEC-qt-device.md`)

### T-035: `Transport`, `TcpTransport`, meta types

**Description:** `mc_device` target (CMake + `mc_device.pri`, `AUTOMOC`), the abstract `Transport`, `TcpTransport` (async connect with timeout timer, `LowDelayOption`, `KeepAliveOption`, `lost()`), `meta_types.h` with `registerMetaTypes()`.

**Acceptance criteria:**
- [x] QDV-12 passes; connect to a closed port reports `openFailed` within `connectTimeoutMs + 500 ms`.
- [x] No `waitFor*`, `QEventLoop`, `QThread` or `QMutex` in `src/device`.

**Verification:**
- [x] `ctest -L device` (MSVC and MinGW Qt kits).

**Dependencies:** T-027

**Files likely touched:** `include/mc/device/transport.h`, `include/mc/device/tcp_transport.h`, `src/device/tcp_transport.cpp`, `include/mc/device/meta_types.h`, `src/device/meta_types.cpp`

**Scope:** M

### T-036: `McDeviceConfig` and JSON

**Description:** The plain config struct, `validate(where)`, `toJson` / `fromJson` with `"schema": 1`, every `FrameConfig` and `SessionConfig` key, `QSerialPort` enums as strings.

**Acceptance criteria:**
- [x] QDV-10 passes (round trip, defaults, error path, schema 2 rejected).
- [x] An invalid subscription reports `subscriptions[i].device` as its path.

**Verification:**
- [x] `ctest -L device`.

**Dependencies:** T-035

**Files likely touched:** `include/mc/device/mc_device_config.h`, `src/device/mc_device_config.cpp`, `include/mc/device/serial_transport.h` (settings struct only), `tests/device/tst_config_json.cpp`

**Scope:** M

### T-037: `McDevice` link state, pump and signal queue

**Description:** The link state machine, drain → FIFO signal queue → emit at the outermost call, single-shot deadline timer on `QElapsedTimer`, conversion of outputs to Qt value types; the `MockPlcServer` test helper.

**Acceptance criteria:**
- [x] QDV-01…04, QDV-07, QDV-15 pass over loopback TCP on 3E Binary.
- [x] Every `connectToPlc()` ends in at least one `linkStateChanged`.

**Verification:**
- [x] `ctest -L device` (both kits).

**Dependencies:** T-036, T-032

**Files likely touched:** `include/mc/device/mc_device.h`, `src/device/mc_device.cpp`, `tests/device/mock_plc_server.h/.cpp`, `tests/device/tst_mc_device.cpp`

**Scope:** M

### T-038: `McDevice` faults, re-entrancy, threads, destructor

**Description:** `linkFault` without auto-reconnect, reconnect from `Faulted`, re-entrant slots, `moveToThread`, destructor warning log.

**Acceptance criteria:**
- [x] QDV-05, 06, 08, 09, 11, 16, 17 pass; QDV-13 passes for 3E ASCII.

**Verification:**
- [x] `ctest -L device` (both kits).

**Dependencies:** T-037

**Files likely touched:** `src/device/mc_device.cpp`, `tests/device/tst_mc_device.cpp`, `tests/device/tst_mc_device_thread.cpp`

**Scope:** M

### T-039: Examples `qt_console_poller` and `virtual_plc`

**Description:** The Qt console poller (host, port, frame, subscriptions from arguments) and the virtual PLC (`QTcpServer` in front of `MockPlc`, `--set`, `--wiggle`).

**Acceptance criteria:**
- [x] On one machine `virtual_plc --frame 3E --port 5000 --wiggle D105` and `qt_console_poller --host 127.0.0.1 --port 5000 --sub D100:64` show round-1 snapshots, then a change every second.

**Verification:**
- [x] Manual run of both programs (MSVC build); both also build through qmake.

**Dependencies:** T-038

**Files likely touched:** `examples/qt_console_poller/main.cpp`, `examples/virtual_plc/main.cpp`, `examples/CMakeLists.txt`, `examples/qmake/*.pro`

**Scope:** M

### Checkpoint D: 3E through the whole stack (baton T-040)
- [x] `scripts/check.ps1` green (both compilers, qmake, consumers); BLD-05(d) proves `mc_device` does not link `mc_mock`. (T-040)
- [x] Owner runs the Checkpoint D demo (`virtual_plc` ↔ `qt_console_poller`). (Owner confirmed 2026-10-01.)

---

## Phase 6: Frames 1E, 3C, 1C through every layer

### T-041: 1E commands (A1E)

**Description:** 00H–03H request data (command code goes to the subheader), response sizes computed from the request, dummy character and padding rules; 04H/05H tables present, unreachable.

**Acceptance criteria:**
- [x] CMD-15…21 byte-for-byte; 1E rows of CMDD pass (odd count dummy, zero nibble).

**Verification:**
- [x] `ctest -L core_protocol`.

**Dependencies:** Checkpoint D

**Files likely touched:** `src/core/protocol/command_a1e.h/.cpp`, `tests/vectors/cmd.vec`, `tests/vectors/cmdd.vec`, `tests/core/protocol/test_commands.cpp`

**Scope:** M

### T-042: 1E frame and vectors

**Description:** 1E envelope and response parse (5BH + abnormal code; other end codes stop after 2 bytes / 4 characters).

**Acceptance criteria:**
- [x] Every A.5 and A.6 vector round-trips; 1E-01…11, 13, 14 pass; STR-02 passes with 1E; ALC-01 extended to 1E.

**Verification:**
- [x] `ctest -L core_protocol` (both compilers).

**Dependencies:** T-041

**Files likely touched:** `src/core/protocol/frame_1e.h/.cpp`, `src/core/protocol/protocol.cpp`, `tests/vectors/1e_binary.vec`, `tests/vectors/1e_ascii.vec`, `tests/core/protocol/test_frame_1e.cpp`

**Scope:** M

### T-043: 1E in the mock, integration, planner and device

**Description:** 1E server direction in the mock; 1E Binary/ASCII rows of the integration matrix; PLN-03's 1E `autoGap` value; QDV-13's 1E cases.

**Acceptance criteria:**
- [x] MCK-01…03 and MCK-10 pass for A.5/A.6; INT scenarios pass for 1E Binary and 1E ASCII.
- [x] PLN-03 (1E Binary words = 7) and QDV-13 (1E Binary, 1E ASCII over TCP) pass.

**Verification:**
- [x] `ctest -L "mock|integration|core_session|device"`.

**Dependencies:** T-042

**Files likely touched:** `src/mock/request_decode_ethernet.cpp`, `src/mock/response_build.cpp`, `tests/mock/test_mock_vectors.cpp`, `tests/mock/integration/test_integration.cpp`, `tests/device/tst_mc_device.cpp`

**Scope:** M

### Checkpoint E1: 1E complete (baton T-044; batch 6a)
- [x] Both Ethernet frames through every layer; `scripts/check.ps1` green. (T-044)

### T-045: Serial receive state machine

**Description:** The incremental F1–F4 receive parser of spec §6.3 (STX/ACK/NAK start, junk skipping, ETX scan over new bytes only, SUM, CR LF), shared by 3C and 1C.

**Acceptance criteria:**
- [x] Synthetic frames for every format and response kind parse byte-at-a-time; junk before the start is reported in `skipped()` (STR-03); STR-04 passes for serial.

**Verification:**
- [x] `ctest -L core_protocol`.

**Dependencies:** T-042

**Files likely touched:** `src/core/protocol/serial_parser.h/.cpp`, `tests/core/protocol/test_serial_parser.cpp`

**Scope:** S

### T-046: 3C formats 1 and 4

**Description:** 3C envelopes (frame ID F9, route, sum-check ranges of spec §2.5) for formats 1 and 4 over the shared QnA commands.

**Acceptance criteria:**
- [x] Every A.12 and A.15 vector round-trips; 3C-01 and 3C-02 pass for F1/F4; 4C-09, 4C-10, 4C-13, 4C-15 re-applied to 3C pass.

**Verification:**
- [x] `ctest -L core_protocol`.

**Dependencies:** T-045

**Files likely touched:** `src/core/protocol/frame_serial.h/.cpp`, `src/core/protocol/protocol.cpp`, `tests/vectors/3c_f1.vec`, `tests/vectors/3c_f4.vec`, `tests/core/protocol/test_frame_serial.cpp`

**Scope:** M

### T-047: 3C formats 2 and 3, serial options, 4C vectors

**Description:** Format 2 (block number, `checkBlockNo`), format 3 (`QACK`/`QNAK`, `f3ShortResponseHasSum`), format 5 rejected (3C-03), 4C vectors transcribed and tagged `v2`.

**Acceptance criteria:**
- [x] Every A.13 and A.14 vector round-trips; 3C-03 and 4C-14 pass; A.7–A.11 exist tagged `v2` and are skipped.

**Verification:**
- [x] `ctest -L core_protocol`.

**Dependencies:** T-046

**Files likely touched:** `src/core/protocol/frame_serial.cpp`, `tests/vectors/3c_f2.vec`, `tests/vectors/3c_f3.vec`, `tests/vectors/4c_f1.vec … 4c_f5.vec`, `tests/core/protocol/test_frame_serial.cpp`

**Scope:** M (data-heavy)

### T-048: 1C commands, formats 1 and 4

**Description:** BR/WR/BW/WW over ASCII with message wait, 1C envelopes for F1 and F4 (2-character NAK codes).

**Acceptance criteria:**
- [x] CMD-25…30 and CMD-33 byte-for-byte; A.16 and A.19 vectors round-trip; 1C-01, 1C-02, 1C-06, 1C-07 pass.

**Verification:**
- [x] `ctest -L core_protocol`.

**Dependencies:** T-047

**Files likely touched:** `src/core/protocol/command_a1c.h/.cpp`, `src/core/protocol/frame_serial.cpp`, `tests/vectors/1c_f1.vec`, `tests/vectors/1c_f4.vec`, `tests/core/protocol/test_frame_serial.cpp`

**Scope:** M

### T-049: 1C formats 2 and 3, AnA command set, message wait

**Description:** 1C F2/F3 (`GG`/`NN`), JR/QR/JW/QW, message-wait character, 256 points as `"00"`, ALC-01 over every family.

**Acceptance criteria:**
- [x] A.17 and A.18 vectors round-trip; 1C-03…05 and 1C-08 pass; ALC-01 covers every v1 family; coverage of `src/core/protocol` ≥ 95 % (MinGW).

**Verification:**
- [x] `ctest -L core_protocol` (both compilers).

**Dependencies:** T-048

**Files likely touched:** `src/core/protocol/command_a1c.cpp`, `src/core/protocol/frame_serial.cpp`, `tests/vectors/1c_f2.vec`, `tests/vectors/1c_f3.vec`, `tests/core/protocol/test_frame_serial.cpp`

**Scope:** M

### Checkpoint E2: codec complete (baton T-050; batch 6b)
- [x] Every enabled Appendix A vector round-trips (core-protocol success criteria 1–5). (T-050; VEC-RT 309/309)

### T-051: Session serial behaviour

**Description:** The serial column of the fault table: EOT (F4: EOT CR LF), silence-based flush capped at `effectiveTimeoutMs()`, first-byte + inter-character deadlines, read retries, never retried writes, `maxConsecutiveLinkErrors`.

**Acceptance criteria:**
- [x] SES-17, SES-18, SES-19 (serial), SES-24 (serial), SES-27 pass using the 3C golden vectors.

**Verification:**
- [x] `ctest -L core_session` (both compilers).

**Dependencies:** T-049

**Files likely touched:** `src/core/session/session_rx.cpp`, `src/core/session/session.cpp`, `tests/core/session/test_session_fault.cpp`

**Scope:** M

### T-052: Mock serial server direction

**Description:** 3C and 1C request decoding for F1–F4 (command-aware length for F1/F2/F4, ETX for F3), station filtering, EOT reset, junk skipping, SUM verification, serial corruption modes.

**Acceptance criteria:**
- [x] MCK-01…03 pass for A.12–A.19; MCK-05…08 pass; serial cases of MCK-11 pass.

**Verification:**
- [x] `ctest -L mock`.

**Dependencies:** T-049, T-031

**Files likely touched:** `src/mock/request_decode_serial.cpp`, `src/mock/response_build.cpp`, `src/mock/corruption.cpp`, `tests/mock/test_mock_stream.cpp`, `tests/mock/test_mock_vectors.cpp`

**Scope:** M

### T-053: Integration matrix complete

**Description:** Add 3C F1–F4 and 1C F1–F4 to the matrix, with the serial variants of INT-11 (EOT, retries) and INT-12 (sum-check corruption retried).

**Acceptance criteria:**
- [x] All 12 combinations pass INT-01…03, 07, 08, 10…16; the binary stays under 5 s.

**Verification:**
- [x] `ctest -L integration` (both compilers).

**Dependencies:** T-051, T-052

**Files likely touched:** `tests/mock/integration/test_integration.cpp`, `tests/mock/integration/rig.cpp`

**Scope:** S

### T-054: `SerialTransport` and serial loopback

**Description:** `SerialTransport` (open completes on the next event-loop turn, `lost()` on port errors), `virtual_plc --serial`, the serial bridge helper, QDV-13 for 3C F1 over TCP, QDV-14.

**Acceptance criteria:**
- [x] QDV-13 (3C F1 over TCP) passes; QDV-14 passes with `MC_TEST_SERIAL_PAIR` set and is skipped without it.
- [x] No blocking call in `SerialTransport`.

**Verification:**
- [x] `ctest -L device`; QDV-14 once with a virtual COM pair if the owner installs one (installing is ask-first).

**Dependencies:** T-053, T-039

**Files likely touched:** `src/device/serial_transport.cpp`, `include/mc/device/serial_transport.h`, `tests/device/serial_bridge.h/.cpp`, `tests/device/tst_serial.cpp`, `examples/virtual_plc/main.cpp`

**Scope:** M

### Checkpoint E: all four frames through every layer (baton T-055; batch 6c)
- [x] `scripts/check.ps1` green with both compilers; `check.sh` in Git Bash; coverage targets of every std module met.
- [x] Owner review before the tooling phase. (owner approved 2026-10-02)

---

## Phase 7: Hardware capture tooling (`SPEC-hil-capture.md`)

### T-057: `tools/` wiring, profile and plan loading

**Description:** `MC_BUILD_TOOLS`, `tools/hil_capture` target, profile JSON (identity, scratch, `deviceEnd`, `supports`, `specialBit`/`specialWord`, embedded `McDeviceConfig`), plan JSON with device references (`D@s`, `M@s16`, `D@end+1`), example profiles.

**Acceptance criteria:**
- [x] HIL-01 passes (example profiles load; bad radix or unknown type rejected with its path).
- [x] Every device reference form resolves against an example profile.

**Verification:**
- [x] `ctest -L hil_tool`.

**Dependencies:** Checkpoint E

**Files likely touched:** `tools/CMakeLists.txt`, `tools/hil_capture/profile.h/.cpp`, `tools/hil_capture/plan.h/.cpp`, `tests/hil/profiles/*.example.json`, `tests/hil/test_tool.cpp`

**Scope:** M

### T-058: Safety gate and dry run

**Description:** Scratch checks for `write`, `poll`, `mutate` and `raw` steps (the last two decoded through `MockPlc`), whole-run refusal, profile-id confirmation, `--dry-run` printing the encoded frames.

**Acceptance criteria:**
- [x] HIL-02 and HIL-03 pass; there is no flag that bypasses the gate.

**Verification:**
- [x] `ctest -L hil_tool`.

**Dependencies:** T-057

**Files likely touched:** `tools/hil_capture/safety_gate.h/.cpp`, `tools/hil_capture/main.cpp`, `tests/hil/test_tool.cpp`

**Scope:** M

### T-059: `RecordingTransport` and capture writer

**Description:** The recording decorator (nanosecond stamps on every chunk) and the writers for `steps.vec`, `session.vec`, `run.meta` (no IP, TCP port or COM name) and `bench.csv`.

**Acceptance criteria:**
- [x] HIL-05 passes: the shared loader reads every written file with all metadata keys intact.
- [x] `run.meta` contains every `FrameConfig` and `SessionConfig` field and no network or port identifier.

**Verification:**
- [x] `ctest -L hil_tool`.

**Dependencies:** T-058

**Files likely touched:** `tools/hil_capture/recording_transport.h/.cpp`, `tools/hil_capture/capture_writer.h/.cpp`, `tests/hil/test_tool.cpp`

**Scope:** M

### T-060: Step runner and end-to-end run against `virtual_plc`

**Description:** Execution of `write`, `read`, `poll`, `mutate`, `raw` steps with expectations, reconnect after faults, the console summary.

**Acceptance criteria:**
- [x] HIL-04: a 3E Binary profile against `virtual_plc` writes a full capture set.

**Verification:**
- [x] `ctest -L hil_tool`; manual `hil_capture --dry-run` then a real run against `virtual_plc`.

**Dependencies:** T-059

**Files likely touched:** `tools/hil_capture/runner.h/.cpp`, `tools/hil_capture/main.cpp`, `tests/hil/test_tool.cpp`

**Scope:** M

### T-061: Replay tests `mc_replay_tests`

**Description:** The std-only replay binary (label `replay`): re-encode, parse, mock conformance, session replay, capture sanity, divergence bookkeeping; skips cleanly when no capture exists.

**Acceptance criteria:**
- [x] RPL-01…06 pass on the capture set produced in T-060; with an empty `tests/vectors/captured/` the binary passes with a "no captures" note.

**Verification:**
- [x] `ctest -L replay` (both compilers).

**Dependencies:** T-060

**Files likely touched:** `tests/hil/test_replay.cpp`, `tests/CMakeLists.txt`

**Scope:** S

### T-062: Timing benchmark and `BENCH.md`

**Description:** `bench` steps (200 repetitions, 1 warm-up, spacing by `cycleIntervalMs`, `--plc-state`), `bench.csv`, `hil_capture --report` generating `docs/hil/BENCH.md` with RUN and STOP columns and the measurement caveats.

**Acceptance criteria:**
- [x] HIL-06 passes; a bench run against `virtual_plc` produces a readable `BENCH.md`.

**Verification:**
- [x] `ctest -L hil_tool`; manual `--only GB` run against `virtual_plc`, then `--report`.

**Dependencies:** T-060

**Files likely touched:** `tools/hil_capture/bench_report.h/.cpp`, `tools/hil_capture/runner.cpp`, `tests/hil/test_tool.cpp`

**Scope:** M

### T-063: Plans for the four frame families

**Description:** `tests/hil/plans/{qna_ethernet,a1e,qna_serial,a1c}.json`, the machine form of `docs/hil/COMMAND-CATALOGUE.md` with identical step ids.

**Acceptance criteria:**
- [x] Every catalogue step id appears in exactly one plan (checked by a test); each plan passes `--dry-run` against a matching example profile.

**Verification:**
- [x] `ctest -L hil_tool`; four dry runs.

**Dependencies:** T-061, T-062

**Files likely touched:** `tests/hil/plans/qna_ethernet.json`, `tests/hil/plans/a1e.json`, `tests/hil/plans/qna_serial.json`, `tests/hil/plans/a1c.json`, `tests/hil/test_tool.cpp`

**Scope:** M

### Checkpoint F: tooling ready (baton T-064; parallel check scripts T-056)
- [x] `scripts/check.ps1` green; the whole capture flow proven against `virtual_plc` (Ethernet and serial). (T-064)
- [x] **Owner gate** (owner, 2026-10-04, answers in `temp-docs/hil-scratch-areas.md`): scratch area decided for each PLC; FX3 manual checked (special relay/register addresses through 1E/1C; computer link formats 1 and 4).

---

## Phase 8: GUI tool `mc_workbench` (`gui-tool`, spec approved 2026-10-03)

Task details live in the baton files `tasks/done/T-066.md` (X/Y octal numbering, library) and
`T-067.md` … `T-074.md` (GUI), each with its acceptance criteria; the spec's GUI-01…09 map onto them.
Phase 8 committed in `75ece90`; follow-ups T-075…T-077 in `11bc5f0`.

- [x] T-066 X/Y octal numbering for FX CPUs
- [x] T-067 Wiring (GUI-09)
- [x] T-068 Runner threads (GUI-01, 03, 04)
- [x] T-069 Device tab (GUI-06)
- [x] T-070 Mock PLC tab (GUI-02)
- [x] T-071 Trace, log, capture and export (GUI-05)
- [x] T-072 HIL runner view (GUI-07)
- [x] T-073 Workspace (GUI-08)

### Checkpoint H: GUI tool ready (baton T-074)
- [x] GUI proven against MockPlc and `virtual_plc` on MSVC and MinGW (CMake and qmake); full verification.
- [ ] Owner demo.

---

### Phase 8 follow-ups (owner, 2026-10-04; leader gates as Phase 8)
- [x] T-075 MockPlc input streams (one parser per client; MCK-13) and the GUI mock on them
- [x] T-076 HIL view: confirm without typing only for loopback profiles (GUI-07 amended)
- [x] T-077 Release builds green (MSVC and MinGW), Release presets back

Known issues, noted only (owner 2026-10-04: no task):
- GUI-08 workspace tests (`tst_gui_workspace.cpp:418`, `:646`, probe `T8_moreDamagedWorkspaceFiles…`) read
  `liveRunners()` at once while runners are created and destroyed asynchronously: they can fail under
  heavy CPU load (also on the tree before T-075). Fix if it recurs: poll with `QTRY_COMPARE`.
- The skip message of the serial tests still gives `"COM50,COM51"` as the example (use COM54,COM55).
- `MockPlc::closeStream()` from inside the `MockOptions::log` sink during `bytesIn()` crashes: documented
  as not re-entrant (doc comment only, owner decision); the GUI never does it.
- Qt 5.15.0 only: `QSerialPort` re-opening COM54 right after a close fails "Access is denied" now and then, so
  `GUI_02_comPairMockServesADeviceOverSerial` (`tst_gui_mock_tab.cpp:937`) flakes ~1 in 12 on Qt 5 (tester: 2 of 25 runs; Qt 6: 0).
  A Qt 5.15.0 limitation; noted like the other flakes (T-079). Qt 5 debug JSON loaders of `mc_hil_tool` run on
  1 MB stacks (the workbench's file threads got 4 MB); very deep nesting is not tested there.
- T-080: `run.meta` does not record `specialFrom`, and the replay (RPL-03) builds its `MockPlc` from `deviceEnd`
  only, so a real FX3 capture of G5-02 (and, already before, G1-09 / G2-10 / G3-04 reading M8000/D8000) will
  replay as a divergence. To settle at P9-2 findings triage (tag, or teach the replay the special range).
- T-081..T-083 batch (LOW, documented): SES-28 does not pin the serial flush cap to `effectiveTimeoutMs()`
  (a mutant survives; code correct); QDV-19 cannot tell delete-on-`disconnected` from the grace timer, nor pin
  "no closing socket" at grace 0; SES-28 answers at +4999 where the spec row says +4000; `-Arch` inferred from
  the kit folder name ("no `_64`" = x86; a future arm64 kit would be misread); `virtual_plc --hold-first-ms`
  has no automated test (`qt_console_poller` cannot set the session config); QDV-19's stuck-peer case is
  verified on Windows only; `gui.mc_workbench_tests` runs 67–91 s against the 120 s ctest timeout and timed
  out once under five parallel builds (passed alone).
- LOW from the T-075..T-077 review: `localhost` is trusted by name; one GUI interleave case relies on a
  3 ms pause; two doc-comment format nits (`hil_types.h:62` width, `mock_runner.h` `///` vs `///<`).

---

## Qt 5.15 support (owner 2026-10-07; follow-up before Phase 9, leader gates as Phase 8)

Details and acceptance criteria in the baton files `tasks/done/T-078.md` and `T-079.md`.

- [x] T-078 Library, examples, Qt tests and `hil_capture` on Qt 5.15 (MSVC `msvc2019_64` kit; CMake and qmake)
- [x] T-079 GUI `mc_workbench` on Qt 5.15 (ADS for Qt5; qpb 1.7.0)
- [x] Batch tester and reviewer PASS; Qt 6 kits unchanged and green; one commit

### Pre-release v0.1.0 (owner 2026-10-08)
- [x] Build + test through the Qt Creator MCP server with every build option (Qt 6 MSVC/MinGW Debug/Release presets,
  Qt 5 MSVC Debug/Release presets, qmake kits); results recorded
  (owner 2026-10-08: if the Qt Creator MCP server cannot be reached then, skip this item and go on)
  Done 2026-10-08: `msvc-release` built in Qt Creator (0 warnings), `ctest` 35/35 through the MCP; the MCP cannot
  switch kits, the owner skipped the other five (verified on the command line by the T-078/T-079 tester).
- [x] `CHANGELOG.md` `Unreleased` → `[0.1.0] - <date>` marked pre-release; `version.h` stays 0.1.0; tag `v0.1.0`
- [x] Export a new git bundle (`git bundle create … --all`) to `build/release/` and verify it (`git bundle verify`, clone test)
  — `build/release/mc_protocol-v0.1.0.bundle`; published 2026-10-08 with the owner's permission: `main` and tag
  `v0.1.0` pushed, GitHub pre-release https://github.com/gstorm-b/mc_protocol/releases/tag/v0.1.0 with the bundle

---

## Capture preparation (owner 2026-10-08, before P9-1)

- [x] T-080 `specialFrom` for the FX3 boundary steps, example profiles fixed, 16 local draft profiles dry-run green,
  profiles README (details: `tasks/done/T-080.md`); tester and reviewer PASS; commit
- [x] Pack the draft profiles and the plans (zip) and attach it to the GitHub release v0.1.0 (owner request) —
  `mc_protocol-v0.1.0-hil-profiles-plans.zip` (16 drafts, 5 examples, 4 plans, README; placeholders only, packed
  before any filling); needs the tool from commit `6674d06` on (`specialFrom`), stated in the release notes

---

## Reconnect follow-up (owner 2026-10-08, after the v0.1.0 field report; leader gates as Phase 8)

Details and acceptance criteria in the baton files `tasks/done/T-081.md`, `T-082.md` and `T-083.md`; spec amendments in
`SPEC-core-session.md` (SES-28) and `SPEC-qt-device.md` (QDV-12, QDV-18, QDV-19).

- [x] T-081 First-response grace after `linkUp` (`SessionConfig::firstResponseTimeoutMs`), JSON, GUI field, loopback
  hold-first fixture
- [x] T-082 TCP graceful close (`closeGraceMs`), switchable `lowDelay` / `keepAlive`, JSON, GUI fields,
  `virtual_plc --hold-first-ms`
- [x] T-083 Qt 5.15 MSVC 32-bit (`msvc2019` kit): x86 warnings in `command_exec.cpp`, `-Arch x86` scripts, presets,
  full ctest x86 (GUI when ADS x86 builds), qmake x86 (owner approved 2026-10-08)
- [x] Worktree slots `.wt/wt1…wt4` (git-ignored) and `scripts/wt-sync.ps1` for parallel builds (owner 2026-10-08;
  leader; the script is reviewed in this batch)
- [x] Batch tester and reviewer; one commit; `CHANGELOG.md` `Unreleased`
- [ ] Owner: v0.1.1 pre-release or not; reply to the app team (bench steps: `firstResponseTimeoutMs`, PLC existence
  confirmation, Wireshark trace)

---

## Phase 9: Bench and release

### P9-1: Capture the 14 bench profiles

**Description:** The owner runs the operator procedure of `SPEC-hil-capture.md` for each of the 14 profiles (RUN pass, then `GB` in STOP); the agent helps write profiles, reads summaries and prepares FINDINGS entries.

**Acceptance criteria:**
- [ ] Each profile has `run.meta`, `steps.vec`, `session.vec`, `bench.csv`; `docs/hil/BENCH.md` regenerated.
- [ ] `ctest -L replay` passes with every divergence tagged and matched to a FINDINGS entry (RPL-06).

**Verification:**
- [ ] Per profile: dry run, real run, summary reviewed; `ctest -L replay`.

**Dependencies:** Checkpoint F (owner gate ticked 2026-10-04). Phase 9 waits until the owner reports the captures done (owner, 2026-10-04); captures may be run with `hil_capture` or the `mc_workbench` HIL view.

**Files likely touched:** `tests/vectors/captured/<profile>/*` (generated), `docs/hil/FINDINGS.md`, `docs/hil/BENCH.md`

**Scope:** owner-operated, spread over bench sessions

### P9-2: Findings triage

**Description:** Walk through every FINDINGS entry with the owner; each decided change (codec, `FrameConfig` default, documentation) becomes its own follow-up task.

**Acceptance criteria:**
- [ ] Every entry is `decided`; §10.1 Q1–Q5, Q7 answered; Q8 recorded as not testable; error-code table filled.

**Verification:**
- [ ] `ctest -L replay` and `scripts/check.ps1` green after the follow-up tasks.

**Dependencies:** P9-1

**Files likely touched:** `docs/hil/FINDINGS.md`, `tests/vectors/captured/*/divergences.txt`, follow-up task files

**Scope:** S (+ follow-ups)

### P9-3: Release 1.0

**Description:** Public API review against the capability-map boundary rules, README (consumption paths, version policy, `bitsAsWords` risk, verified platforms), CHANGELOG, version 1.0.0, tag.

**Acceptance criteria:**
- [ ] A consuming project compiles unchanged across two internal revisions of the library before the tag (ideas §5).
- [ ] `scripts/check.ps1` green; `v1.0.0` tagged on the commit that bumps `version.h` and the CHANGELOG (owner does or approves the tag).

**Verification:**
- [ ] Full check; consumer build; owner sign-off.

**Dependencies:** P9-2

**Files likely touched:** `include/mc/version.h`, `CHANGELOG.md`, `README.md`

**Scope:** S

### Checkpoint G: ready to tag `v1.0.0`
- [ ] All specs' success criteria met or explicitly waived by the owner.
