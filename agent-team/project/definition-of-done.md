# Definition of Done — mc_protocol

The standing bar every task clears, on top of its own acceptance criteria.
Acceptance criteria answer "did we build this thing?"; this list answers "is it
finished to this project's standard?". A task is done only when both hold.

Tailored once for a C++17 / Qt 6 static library from the generic checklist of
addyosmani/agent-skills (`references/definition-of-done.md`, MIT). Dropped:
deploy, rollout, feature flags, database migrations, service observability.
Added: build wiring, the library contract, allocation and protocol rules.

**Who checks what.** The developer self-checks every section before handing
over (Dev notes). The tester re-verifies Correctness by running things. The
reviewer checks Build, Library contract, Quality and Documentation against the
diff. The leader checks Process at Gate 2. Items that do not apply to a task
are marked "n/a" with a reason, never silently skipped.

## Correctness

- [ ] Every acceptance criterion is met and was verified by running something,
      not by reading code.
- [ ] New behaviour is covered by tests that **fail without the change and pass
      with it**; the test IDs of the spec (e.g. `SES-12`, `PLN-03`) appear in the
      test names, so each criterion maps to a test.
- [ ] Error paths are tested, not only the happy path: short, malformed and
      oversized frames, limits, timeouts — as the spec's test list names them.
- [ ] The task's label (`ctest -L <label>`) is green in `build/cmake-debug`
      (MSVC), and a full `ctest` shows no regression in any other label.

## Build

- [ ] Top-level build is warning-free with `MC_WARNINGS_AS_ERRORS=ON`.
- [ ] A new source file is added to `CMakeLists.txt` **and** the matching `.pri`
      in the same change; BLD-04 is green (from T03 on).
- [ ] A new test binary gets its `tests/qmake/*.pro` (built through the `.pri`
      files, listed in `tests/qmake/tests.pro`) in the same change (BLD-03).
- [ ] Core and mock headers include nothing from Qt or the OS; BLD-05 is green
      (from T03 on).
- [ ] Checkpoint tasks only: both compilers (MSVC and MinGW GCC 13.1) green,
      and `scripts/check.ps1` green (from T04 on).

## Library contract

- [ ] Public headers under `include/mc/` change only as the spec says. A public
      API change the spec does not describe is a blocker, raised to the leader.
- [ ] `core-*` code: zero-allocation guarantees ship with their ALC tests in the
      same task; complexity matches the spec's complexity table.
- [ ] No exception crosses a public boundary; `noexcept` wherever the spec
      requires it.
- [ ] Qt code compiles on Qt 5.15 and Qt 6.2+ (GUI: Qt 5.15 or 6.5+).
- [ ] No golden vector (`.vec`) was edited to make a test pass. A vector change
      is its own `protocol-core` task, citing the `docs/mc_reference/` section.

## Quality

- [ ] The change is scoped to the task: no unrelated refactors, no drive-by
      reformatting of untouched code.
- [ ] No dead code, debug output or commented-out blocks left behind.
- [ ] Naming and layout follow `SPEC-build-packaging.md` (Code Style) and
      `.clang-format` (from T04 on).
- [ ] No logic duplicated that an existing primitive already provides (hex
      ASCII, sum check, field codec, device encoding).

## Documentation

- [ ] Doc comments follow `docs/rules/doc_comment_style.md`.
- [ ] Any deviation from the spec is recorded in Dev notes and raised to the
      leader; a spec changes only by owner decision.
- [ ] A consumer-visible change has a line under `Unreleased` in `CHANGELOG.md`
      (from T04 on).
- [ ] Documentation describes the current state, not the history of the change.

## Process

- [ ] Dev notes, Test report and Review verdict are filled in as the class
      requires, with the commands run and their outcomes.
- [ ] Nothing was committed or pushed by a subagent; the commit waits for the
      owner's Gate 2.
- [ ] Nothing was read or written outside the project folder without the
      owner's permission; no real PLC was touched outside an approved `hil` task.
