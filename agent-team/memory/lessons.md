# Lessons — mc_protocol

Curated by the team leader after task close: things that cost time once and
should not cost time twice. Newest first.
Format: `- [T-xxx] What happened → what to do instead.`

- [T-028] Three separate output-lifetime mechanisms (plan refs T-024, ad-hoc confirmDrained T-025, drain discard T-026) were each correct alone; the later drain discard bypassed the other two and could stop the engine for good. When a new code path removes items from a queue, route it through the same release routine as the normal path — and let the phase reviewer look across tasks, which is what caught it.
- [T-028] The coverage report (since T-012) read gcov output with `file(STRINGS)`, which splits on `;` — most C++ lines were merged and one file showed 0/0 = 100 %. Earlier numbers happened to be unaffected. A tool that produces a gate number needs its own sanity test (a file with known coverage) before its result is trusted.
- [T-023] A dangling-pointer bug (queued outputs pointing into a plan replaced in the same call) was found by the developer reading code, not by a test — no test combined a re-plan with back-to-back rounds. For output records that point into engine storage, test the lifetime contract explicitly (T-024 2a).
- [T-019] Two qmake test `.pro` files shared one object directory; the protocol binary linked core-model's `test_alloc.obj` and still reported all green — a test binary can pass while its real tests never ran. When a checkpoint compares build systems, compare the test-case lists (`--list-test-cases`), not only pass counts.
- [T-019] Phase 1 added `mc_core_model_tests` without a qmake `.pro`; BLD-03 ("every test through the .pri files") was in no acceptance criterion, so nobody checked it → every task that adds a test binary lists its `tests/qmake` `.pro` (now in the Definition of Done).
- [T-007] A spec sketch declared a `constexpr` function defined in a `.cpp` — impossible in C++; found only at link time. When planning a task, check that the spec's sketch compiles as declared (constexpr/inline/template bodies must be visible to callers).
- [T-002] The leader fixed `.gitignore` (an "Ask first" file) mid-task without recording owner approval → review FAIL. Get and record the owner's approval in the task file before touching an owner-gated file, even for an obvious fix.
- [T-001] A Plan said "not a Linux Qt path" and the developer fixed the comment but kept `/opt/Qt/…` → when a Plan asks for changed text, write the exact target text in the Plan.
