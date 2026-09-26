# Decisions — mc_protocol

Curated by the team leader. One entry, one decision, newest first.
Format: `- [T-xxx or date] Decision — why. (supersedes: entry, if any)`

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
