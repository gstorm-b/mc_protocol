# Task Classification — mc_protocol

The leader classifies every task at intake; the class and its consequences are
recorded in the task file. When in doubt, take the stricter class. A task that
grows beyond its class is reclassified immediately and the skipped steps run.

| Class | What qualifies here | Tester | Reviewer |
|---|---|---|---|
| `full` *(default)* | Any change to library source, tests, build wiring (`CMakeLists.txt`, `.pri`), scripts, tools, examples | required | required |
| `docs` | Changes under `docs/`, `README`, comments only — no behaviour can change | skip | required |
| `trivial` | Typo/whitespace in docs or comments | skip | skip |
| `protocol-core` | Changes to codec encode/decode paths, golden vectors (`.vec`), or the `Parser`/`McProtocol` contract | required | required — reviewer must diff against the relevant `docs/mc_reference/` section, not just the spec |
| `hil` | Anything executing against real hardware, or changing the capture tool's safety gate | required | required + owner hardware-prep confirmed before Gate 1 (see `rules.md`) |

Notes:

- T-tasks from `tasks/todo.md` map to `full` unless they touch codec/vectors
  (`protocol-core`) or hardware (`hil`).
- Golden-vector edits are never `trivial` or `docs` — a wrong byte in a vector
  fabricates or hides a codec bug. Always `protocol-core`.
- Checkpoint tasks (A0, A, B, …) additionally require both compilers green
  before Gate 2 (`rules.md`).
