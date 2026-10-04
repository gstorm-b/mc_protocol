# Decisions — mc_protocol

Curated by the team leader. One entry, one decision, newest first.
Format: `- [T-xxx or date] Decision — why. (supersedes: entry, if any)`

- [2026-10-04] Owner decisions after Phase 8: (C) the HIL view's "confirm without typing" only for a
  loopback profile without `readOnly` frames, any other host or COM always types the id
  (`SPEC-gui-tool.md` amended; T-076); (a1) `MockPlc` input streams — one parser per client,
  shared memory / faults / log (`SPEC-mock-plc.md` amended; T-075); Checkpoint F owner gate
  ticked; CMake presets for Qt Creator committed; `build/qmake-mingw` rebuild allowed.
- [T-074] Phase 8 leader decisions: GUI mock tabs serve up to 32 TCP clients over one shared
  `MockPlc` (one parser — interleaved or abandoned partial frames corrupt; plans with truncated
  frames such as E-10 run against `virtual_plc`; a fix needs a `MockPlc` API, owner question);
  `gui.mc_workbench_tests` is `RUN_SERIAL`; export of a capture under `tests/vectors/captured` only
  for Real PLC, checked on the OS-resolved final folder; workspace files refuse unknown keys
  entirely; `MC_WORKBENCH_CONFIG_DIR` redirects the app's config folder. Open owner questions:
  the "confirm without typing" box for runs without readOnly frames (spec Functions vs Boundaries).
- [2026-10-03] `SPEC-gui-tool.md` approved by the owner; delegation extended to Phase 8. Owner
  answers: no unit tests from the GUI — it exports real-PLC captures as replay test data; GUI on
  MSVC **and** MinGW (ADS built from source for MinGW, into `build/` only); name `mc_workbench`;
  `examples/virtual_plc` stays unchanged, the GUI has its own mock serving code; a small custom
  trend widget (no Qt Charts). X/Y octal numbering for FX (T-066) is implemented before Phase 8.

- [2026-10-03] Owner (scratch file answers): FX3 computer link **does** offer 1C formats 1 and 4 (the
  three 1C profiles stay); FX5U built-in RS-485 is added with 3C (the owner creates its profiles when
  needed, e.g. in the GUI), `SPEC-hil-capture.md` tables amended; Q CPU and
  Q + C24 share one scratch; Y goes into scratch by default, the owner decides at capture time.
  (supersedes: the "computer link supports neither format" note of the same day)
- [2026-10-03] Owner: Q CPU scratch stays `D100-D2099` (GV steps unchanged). FX3 and FX5 number
  X/Y in **octal** (X0–X7, X10–X17) and the PLC does not convert (owner checked the vendor manuals).
  The manuals added to `docs/mc_reference/` say: FX5 `Binary` / `ASCII (X,Y HEX)` carry the point
  index in hex, `ASCII (X,Y OCT)` carries octal digits; FX3 computer link 1C always carries octal
  digits. The library reads and encodes X/Y as hex (Q style) → an X/Y octal option is proposed to
  the owner (library change, owner decision).

- [2026-10-03] Owner decisions: `maxConsecutiveLinkErrors` default 3, `validate()` rejects 0
  (`SPEC-core-session.md` amended); `MockPlc::skippedBytes()` counts serial junk, mainly for debug
  traces (`SPEC-mock-plc.md` amended) — both in T-065. FX3 computer link: see the next entry (superseded). Phase 7 Checkpoint F owner gate: scratch proposal in `temp-docs/hil-scratch-areas.md`
  for the owner to correct. New phase requested: a Qt GUI tool (tests, debug trace, frame capture,
  HIL, MockPlc; qmake + CMake; qpb property browser; a docking system from `C:\build_packages`
  via a local path; tabs; parallel McDevice/MockPlc; every McDevice on a runner thread) — spec to
  be drafted for the owner.
- [2026-10-03] Owner: `MockOptions` gains `LogSink* log{nullptr}` (category `mc.mock`, Trace line
  per skipped junk byte run); in format 4 the `CR LF` of `EOT CR LF` is not counted as skipped
  (leader); `.gitignore` gains `mc_local.pri`; GUI may require Qt 6.5 (library stays 6.2); qpb
  vendored as `components/qpb`; GUI is Phase 8, bench and release move to Phase 9; plan and todo
  use the baton ids only. Commit allowed for T-065 after tester and reviewer pass; the docs
  changes (renumbering, `components/qpb`, the GUI spec draft) wait for a later owner OK.

- [T-064] Safety gate tightened after the Phase 7 tester wrote outside scratch with a
  `readOnly` truncated write (the PLC completed it with the next frame's bytes): a `mutate`
  of a write is never `readOnly`; every `readOnly` frame needs `recover: reconnect|eot`; any
  part a mock decodes as a write is checked; `--yes` does not skip the confirmation when
  `readOnly` frames exist — leader, within spec decision H2 (supersedes the `--yes` half of
  T-057 D4). `run.meta` text is scrubbed of the profile's host, port and COM name. Splitting
  `runner.cpp` (reviewer S5) is deferred. **Owner approved 2026-10-02:** `SPEC-hil-capture.md`
  "Safety gate" amended to these rules; catalogue G8-Q3 (b) left out of the v1 plans (note in
  `COMMAND-CATALOGUE.md`).

- [T-060] Safety gate: every write of a poll (heartbeat, ad-hoc) must lie in scratch; a poll
  subscription outside scratch must be marked `"input": true` in the plan, otherwise it is
  refused. `SPEC-hil-capture.md` "Safety gate" amended — owner decision 2026-10-02 (the spec
  contradicted catalogue G6, which subscribes `X0 × 32`).

- [T-060] Catalogue G9-02, G9-03, G9-04 (writes in `1402`, 1E `04H/05H`, 1C `BT/WT`) are left
  out of the v1 capture plans: `MockPlc` cannot decode them, so the safety gate cannot prove
  the writes stay in scratch; noted in `COMMAND-CATALOGUE.md` — owner decision 2026-10-02.

- [2026-10-02] Checkpoint E approved by the owner. Delegation extended to Phase 7
  (capture tooling against `virtual_plc` only; real-PLC work and Checkpoint F's owner gate
  stay the owner's); `scripts/check.ps1`/`check.sh` may be made parallel (T-056) — owner
  decisions. `COM50`↔`COM51` is retired for good (no repair); `COM54`↔`COM55` replaces it.
  The vanished `build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug/` was deleted by the owner.

- [T-055] Serial inter-character deadline is re-armed only once a start byte has been seen;
  while only junk has arrived the first-byte deadline stays, so a line sending junk forever
  still times out (SES-27 "once a response has started") — re-review should-fix, leader.
- [T-054] Batch 6c device decisions: every ctest entry that opens the COM pair holds
  `RESOURCE_LOCK mc_serial_pair`; `McDevice::ensureTransport()` is `void`; any `QSerialPort`
  error while Open except `NoError`/`TimeoutError` → `lost()`; the untestable open-port-error /
  failed-write paths of `SerialTransport` are accepted with coverage reported — leader.
- [T-052] Batch 6c mock decisions: Ethernet and serial decoders share device/subcommand/3C
  request-data decoding; MCK-02 skips the 10 vectors marked `checkroute: off` /
  `blockcheck: off`; skipped junk is not logged as a request (no accessor); check order
  station → mute → SUM → execute, EOT counted while muted; serial `Corruption` modes as in
  `mock_plc.h`; 1C error codes one byte; points `00` = 256 on every 1C command — leader.
- [T-051] Batch 6c session readings: an item resolves when the flush ends; EOT is also sent on
  the faulting error; `sendEotOnError` off skips only the EOT; the flush cap counts a link
  error but not `stats.timeouts`; `maxConsecutiveLinkErrors = 0` acts as 1; a pending read is
  resent before queued ad-hoc; deadlines are evaluated in `tick()` only, which is called at
  `nextDeadline()` even while bytes arrive (`McDevice` arms its timer independently) — leader.

- [T-055] Serial receive-buffer bound counts only bytes from the frame's start byte
  (STX/ACK/NAK); junk before it is skipped (spec 4C-15) and never causes an overflow. Before,
  one stray byte before a maximum-size read response forced EOT + flush + retry — owner
  decision 2026-10-02 (supersedes the T-051 "open question A" literal reading).
- [2026-10-01] Virtual COM pair `COM54`↔`COM55` replaces `COM50`↔`COM51` (broken since
  T-054) as the default `MC_TEST_SERIAL_PAIR`; `COM52`↔`COM53` stays the second pair — owner
  decision.
- [T-050] Batch 6b leader decisions: the serial scan cursor reuses `Parser`'s private members
  (no `protocol.h` layout change); unlisted serial error mappings (F4 without CR LF, unknown F3
  end code, NAK of another route → `FrameMismatch`; short body → `LengthMismatch`); unimplemented
  frame/code → `UnsupportedCommand`; no ETX wait cap in the parser (Session deadlines bound it);
  `messageWait > 15` → `InvalidConfig` in `encode()`; BT/WT `n > 255` → `PointCount`.
  Carried into batch 6c: `protocol.h:219` 101 columns, 1E test-encoder `n` cast, `n = 255`
  accept test. `McProtocol` not re-running `FrameConfig::validate()` goes to the owner.
- [2026-10-01] Every build and test run uses the PC's 32 cores: `cmake --build --parallel 32`,
  jom (not nmake) for qmake/MSVC, `mingw32-make -j32`, `ctest -j 8` — owner request. Commands in
  `build-env.md`. `scripts/check.ps1`/`check.sh` made parallel in T-056 after the owner's
  permission (2026-10-02).
- [T-044] Batch 6a leader decisions: a non-hex 1E ASCII subheader → `FrameMismatch`
  (as 3E); `bit_payload.h` shared by QnA and A1E; `WrongRoute` leaves a 1E response
  unchanged (no route field); `chunkCount()`/`chunk()` skip rule 6 on the unsplit request
  (rule 6 per chunk / on encode); `subscribe()` checks the word-aligned range;
  `autoGap(ReadBits)` uses the fractional per-point wire cost (0.5 byte for packed Binary
  bits) — 65/64/29/29. Spec wording for the validate() callers (SPEC-core-model:284) and
  the bit-unit reading of autoGap are proposed to the owner.
- [2026-10-01] Checkpoint D demo run and confirmed by the owner. Delegation
  extended to Phase 6, run as three batches (6a → E1, 6b → E2, 6c → E), each
  with its own tester, reviewer and commit. The owner installed virtual COM
  pairs COM50–COM51 and COM52–COM53 for QDV-14 — owner decisions.
- [2026-09-30] `.gitignore` ignores `.qtcreator/` (any level) — owner request.
- [T-040] `Transport::lastLossWasPeerClose()` kept as public API (virtual, default
  false) so `lost()` maps to `PeerClosed` or `TransportError`; `SPEC-qt-device.md`
  amended (Transport sketch, link table, Connecting re-publish, JSON value spellings,
  missing `schema` = 1) and `SPEC-build-packaging.md` (four QtTest binaries, one per
  `tst_*.cpp`) — owner decisions 2026-09-30.
- [2026-09-30] Delegation of Gates 1–2 extended to Phase 5 on Phase 4's terms;
  the Checkpoint D demo stays with the owner. `SPEC-mock-plc.md` amended: INT-10
  snapshots in `DeviceType` order (X, M, D); `MockPlc` is pimpl, movable, not
  copyable — owner decisions. Checkpoint A0 layout review confirmed by the owner.
- [T-028] Checkpoint C: **go** — direction C kept, the sans-I/O `Session`
  stays in `mc_core`; no fallback to direction B — owner decision 2026-09-30.
  Delegation of Gates 1–2 extended to Phase 4; in Phase 4 the leader chooses
  how many developers to spawn and when (`rules.md`).
- [T-026] Drain contract stays as specified: debug asserts through an internal,
  test-replaceable handler; release discards pending outputs and logs `Error`
  — owner decision (not "always discard").
- [T-021] `ValueStore::markStale`/`resetBaselines` are O(P), eager — the spec's O(1)
  epoch design contradicted `SegmentView::states` being read directly; spec
  complexity table amended (also `linkUp`/`linkDown`) — owner decision.
- [T-019] `protocol.h` approved by the owner as committed (`e71c7cc`); the
  `Parser` default constructor stays private — `Session` holds its parser in a
  `std::optional<Parser>`. Delegation of Gates 1–2 extended to Phase 3
  (through Checkpoint C) — owner decision.
- [2026-09-27] Phase-batched verification from T-008: developer implements a
  whole phase task by task, the leader re-runs each task's checks (no commit);
  tester and reviewer run once, at the phase checkpoint, over the whole phase —
  owner decision, to cut per-task overhead. One commit per phase, after the
  checkpoint passes — owner decision. Fresh developer, tester and reviewer
  per phase, re-used within it (one role each) — owner request. Details in `rules.md`.
- [T-007] `deviceInfo()` is not `constexpr`: the device table stays private to
  `device_table.cpp`; `SPEC-core-model.md` amended — owner decision (the spec
  contradicted itself: constexpr sketch vs private table).
- [2026-09-27] Machine-specific build facts live in
  `agent-team/project/build-env.md` (linked first from project-context);
  roles report new ones as "Env notes", the leader folds them in — owner asked,
  so subagents stop re-deriving the environment.
- [2026-09-27] Owner delegates Gate 1 and Gate 2 to the leader for T-003–T-005
  and Phases 1–2 (todo T05–T16, through Checkpoint B); exclusions and quote in
  `rules.md` "Version control". `.gitignore` `build/` anchored to `/build/`
  (owner-approved in T-002) so `tests/build/` is tracked.
- [2026-09-27] `.claude/` stays fully untracked. Team-relevant material lives
  in `agent-team/project/`: six skills of addyosmani/agent-skills vendored
  unmodified in `skill-pack/` (overrides in `rules.md`), the project's own
  `definition-of-done.md`, and the subagent-model hook; the required
  `.claude/settings.json` content is written out in `rules.md`. Web-only
  skills (browser-testing, frontend-ui, shipping-and-launch, ci-cd,
  web-performance-auditor, /webperf) removed from `.claude/` — owner decision.
- [2026-09-27] Implementation plan (`tasks/plan.md`, `tasks/todo.md`, T01–T56)
  approved by the owner; baton IDs run T-001, T-002, … with `Plan ref` to the
  plan's T-numbers.
- [2026-09-27] Every subagent at every level runs on Sonnet, model passed
  explicitly on each spawn (`agent-team/project/rules.md`, "Subagents") —
  owner decision.
- [2026-09-27] Doc comments: Doxygen `/** */` + tags, ported from the owner's
  reference rule to `docs/rules/doc_comment_style.md`; public symbols
  mandatory, private only when not evident — owner decision. (supersedes: the
  `///` rule in `SPEC-build-packaging.md` Code Style, now amended)
- [2026-09-27] Plan open questions decided by the owner (`tasks/plan.md`,
  "Owner decisions"): v1 verified on Windows only; MSVC is the daily compiler;
  one commit per task after Gate 2; Qt 6.2 API kept, enforced by review; no
  real PLC before Phase 8 (no Checkpoint D smoke).
- [2026-09-27] Agent team runs as Claude Code subagents (leader = main session,
  dev/tester/reviewer spawned from `.claude/agents/team-*.md`) — real context
  isolation for the reviewer; portable by copying folders.
- [2026-09-27] Workflow state lives in per-task baton files `tasks/active/T-xxx.md`;
  two human gates: plan approval and commit approval — empty sections make
  skipped steps visible; survives session loss.
- [2026-09-27] Generic/project split is physical: `agent-team/core/` (never
  edited per project) vs `agent-team/project/` — the boundary is the directory
  tree, not naming discipline.
- [2026-09-27] No pre-commit enforcement hook yet — revisit after ~2 weeks of
  real use if steps still slip past Gate 2. (design doc: `temp-docs/agent-team-design.md`)
