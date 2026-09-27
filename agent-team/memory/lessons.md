# Lessons — mc_protocol

Curated by the team leader after task close: things that cost time once and
should not cost time twice. Newest first.
Format: `- [T-xxx] What happened → what to do instead.`

- [T-019] Two qmake test `.pro` files shared one object directory; the protocol binary linked core-model's `test_alloc.obj` and still reported all green — a test binary can pass while its real tests never ran. When a checkpoint compares build systems, compare the test-case lists (`--list-test-cases`), not only pass counts.
- [T-019] Phase 1 added `mc_core_model_tests` without a qmake `.pro`; BLD-03 ("every test through the .pri files") was in no acceptance criterion, so nobody checked it → every task that adds a test binary lists its `tests/qmake` `.pro` (now in the Definition of Done).
- [T-007] A spec sketch declared a `constexpr` function defined in a `.cpp` — impossible in C++; found only at link time. When planning a task, check that the spec's sketch compiles as declared (constexpr/inline/template bodies must be visible to callers).
- [T-002] The leader fixed `.gitignore` (an "Ask first" file) mid-task without recording owner approval → review FAIL. Get and record the owner's approval in the task file before touching an owner-gated file, even for an obvious fix.
- [T-001] A Plan said "not a Linux Qt path" and the developer fixed the comment but kept `/opt/Qt/…` → when a Plan asks for changed text, write the exact target text in the Plan.
