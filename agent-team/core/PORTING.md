# Porting the Agent Team to Another Project

Five steps, ~15 minutes. The generic layer is copied verbatim; only the project
layer is written fresh.

1. **Copy the generic layer.** Into the new project root:
   - `agent-team/core/` — entire folder, unmodified. Never edit it there either;
     improvements go back to the origin copy and re-copy forward.
   - `.claude/agents/team-developer.md`, `team-tester.md`, `team-reviewer.md`.

2. **Create the project layer.** Make `agent-team/project/` and fill the three
   templates from `core/templates/`:
   - `project-context.md` — what/where/how-to-build (keep under two screens)
   - `rules.md` — hard rules (commit identity, boundaries, gates)
   - `task-classification.md` — start from the defaults, extend as needed

3. **Create the live folders.**
   - `agent-team/memory/decisions.md` and `lessons.md` from their templates
   - `tasks/active/` and `tasks/done/` (empty)

4. **Wire the leader.** In the project's `CLAUDE.md`, add:

   ```markdown
   ## Agent team
   This project runs the agent-team workflow. At session start read
   `agent-team/core/roles/team-leader.md` and act as team leader.
   Workflow: `agent-team/core/WORKFLOW.md`.
   ```

5. **Dry-run one small real task** end to end (plan → gates → done) before
   trusting the setup. Watch that each subagent actually read the project layer
   — its section in the task file shows whether it followed project commands.

Checklist of what is generic vs project:

| Generic (copy, never edit per project) | Project (write fresh) |
|---|---|
| `core/WORKFLOW.md`, `core/roles/*`, `core/templates/*`, this file | `project/*.md` |
| `.claude/agents/team-*.md` | `memory/*.md` (live), `tasks/` |
| The CLAUDE.md snippet above | Rest of CLAUDE.md |
