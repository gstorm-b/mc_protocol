# Project Context — mc_protocol

## What this project is

A C++/Qt library (`mc`) implementing the Mitsubishi MC protocol (frames 3E, 1E,
3C, 1C) with a sans-I/O `Session` engine, a mock PLC, a Qt device layer, and an
owner-gated hardware-capture (HIL) toolchain. Specs are approved; the project is
at the start of implementation (Phase 0 of an 8-phase plan, tasks T01–T56).

## Key documents

| What | Where |
|---|---|
| Capability map + 7 module specs | `docs/spec/` (`CAPABILITY-MAP.md`, `SPEC-*.md`) |
| Implementation plan (phases, checkpoints, risks) | `tasks/plan.md` |
| Task details (acceptance criteria per task) | `tasks/todo.md` |
| HIL command catalogue & findings | `docs/hil/` |
| Doc comment style (Doxygen `/** */` + tags) | `docs/rules/doc_comment_style.md` |
| Definition of Done (every task) | `agent-team/project/definition-of-done.md` |
| Skills per role (vendored, MIT) | `agent-team/project/skill-pack/` — mapping in `rules.md` |
| Intent / original idea | `docs/intent/` |
| MC protocol reference | `docs/mc_reference/` |

## How to build and test

**Read `agent-team/project/build-env.md` first** — proven commands, paths and
gotchas for this machine (one PowerShell call per build, reuse the existing
`build/` folders). Report new environment facts under "Env notes" in your
section of the task file.

Toolchain: CMake + Ninja from `C:\Qt\Tools`, Qt 6.11.1 kits `msvc2022_64` and
`mingw_64`, MSVC (VS 2026 developer shell) and MinGW GCC 13.1.

```powershell
# MSVC — the daily loop. vsdev.ps1 (from T01) loads the VS 2026 environment;
# before T01 exists, use Launch-VsDevShell.ps1 of the install vswhere reports.
. scripts/vsdev.ps1
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build/cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/cmake-debug
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build/cmake-debug -L <label> --output-on-failure

# MinGW (PATH += C:\Qt\Tools\mingw1310_64\bin)
cmake -S . -B build/cmake-mingw -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64

# Everything (exists after T04)
scripts/check.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64
```

Test labels: `build`, `core_model`, `core_protocol`, `core_session`, `mock`,
`integration`, `device`, `replay`, `hil_tool`. Each module's tests live in its
own binary; every task verifies with one `ctest -L <label>`.

Checkpoints build with **both** compilers. v1 is verified on Windows only
(no Linux); Qt APIs stay within 6.2 (see `rules.md`).

## Layout in ten lines

- `docs/` — specs, HIL docs, intent, protocol reference
- `tasks/` — `plan.md`, `todo.md`; agent-team baton files in `active/`, `done/`
- `agent-team/` — this workflow system (core / project / memory)
- `reference_source/` — reference material (read-only)
- `temp-docs/` — scratch documents, never shipped
- Library source tree (`src/`, `tests/`, `tools/`, `examples/`, `scripts/`)
  appears from T01 onward per `SPEC-build-packaging.md`

## Current state

Updated 2026-10-04. Phases 0–8 are implemented and committed:
- the library (core-model, core-protocol, core-session, mock-plc, qt-device) with frames 3E, 1E, 3C and 1C;
- the HIL capture tool and replay tests (Phase 7);
- the GUI tool `mc_workbench` (Phase 8, commit `75ece90`) and its follow-ups T-075…T-077 (`11bc5f0`).

Checkpoint F's owner gate is ticked (HIL prerequisites decided). Still open:
- the Checkpoint H owner demo;
- **Phase 9 (bench and release)**, which waits until the owner has finished the real-PLC captures.

Progress lives in `tasks/plan.md` (phase checklist and checkpoint "Done" lines) and `tasks/todo.md`.
The leader updates **both** at every commit.
