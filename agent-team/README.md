# Agent Team System

A markdown-based workflow for running a four-role agent team in Claude Code:
**team leader** (the main session), **developer**, **tester**, **reviewer**
(subagents spawned from `.claude/agents/team-*.md`).

## Layout

| Where | What | Layer |
| --- | --- | --- |
| `agent-team/core/` | Workflow, role contracts, templates, porting guide | **Generic** — copy verbatim to other projects, never edit per project |
| `agent-team/project/` | Project context, hard rules, task classification | **Project-specific** — written fresh for each project |
| `agent-team/memory/` | `decisions.md`, `lessons.md` — curated by the leader | Project-specific (living files) |
| `.claude/agents/team-*.md` | Subagent shims for developer / tester / reviewer | Generic (copy verbatim) |
| `tasks/active/`, `tasks/done/` | Per-task baton files (`T-xxx.md`) | Project-specific (living files) |
| `CLAUDE.md` (repo root) | Points the main session at the team-leader role | Generic snippet inside a project file |

## Reading the system (5 minutes)

1. `core/WORKFLOW.md` — the rules of the game: task lifecycle, the two human
   gates (plan approval, commit approval), handoff rules.
2. `core/roles/*.md` — the four roles. The leader is the main session; the
   other three are subagents.
3. `core/templates/task.md` — the baton file: all workflow state lives in it;
   an empty section is proof a step was skipped.
4. `project/*.md` — context and hard rules for this project.
5. `core/PORTING.md` — take the system to another project in five steps.

Original design one-pager and decisions: `temp-docs/agent-team-design.md`.
