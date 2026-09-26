# Task Classification — <project name>

> TEMPLATE. Copy to `agent-team/project/task-classification.md`. The leader
> classifies every task at intake and records the class in the task file.
> These defaults are sane for most projects; tighten or extend per project —
> never loosen silently mid-project (that is a Gate 1 conversation).

| Class | What qualifies | Tester | Reviewer |
|---|---|---|---|
| `full` *(default)* | Any change to source code, build wiring, or tests | required | required |
| `docs` | Documentation/comment-only changes; no behaviour can change | skip | required |
| `trivial` | Typo/whitespace in docs; mechanical renames with zero logic | skip | skip |
| `hil` | Anything touching real hardware or gated environments | required | required + human hardware-prep confirmed **before** Gate 1 |

Rules of thumb:

- When in doubt between two classes, take the stricter one.
- A task that starts `docs` and grows a code change is reclassified `full`
  immediately — the leader updates the task file and the missing steps run.
- `trivial` still passes both human gates. Nothing skips the human.
- Add project-specific classes below this line (e.g. `protocol-core` requiring
  extra review) rather than editing the four above.
