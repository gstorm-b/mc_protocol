# Decisions — mc_protocol

Curated by the team leader. One entry, one decision, newest first.
Format: `- [T-xxx or date] Decision — why. (supersedes: entry, if any)`

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
  `build-env.md`. Making `scripts/check.ps1`/`check.sh` parallel too is **pending**: the edit was
  refused by the permission layer (T-050) and waits for the owner's permission.
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
