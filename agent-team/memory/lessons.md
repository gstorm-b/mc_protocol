# Lessons — mc_protocol

Curated by the team leader after task close: things that cost time once and
should not cost time twice. Newest first.
Format: `- [T-xxx] What happened → what to do instead.`

- [T-007] A spec sketch declared a `constexpr` function defined in a `.cpp` — impossible in C++; found only at link time. When planning a task, check that the spec's sketch compiles as declared (constexpr/inline/template bodies must be visible to callers).
- [T-002] The leader fixed `.gitignore` (an "Ask first" file) mid-task without recording owner approval → review FAIL. Get and record the owner's approval in the task file before touching an owner-gated file, even for an obvious fix.
- [T-001] A Plan said "not a Linux Qt path" and the developer fixed the comment but kept `/opt/Qt/…` → when a Plan asks for changed text, write the exact target text in the Plan.
