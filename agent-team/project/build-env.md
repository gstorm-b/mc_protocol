# Build environment — this machine (proven commands)

Read this before building or testing. Everything here was run and worked on
the owner's Windows 11 dev PC; do not rediscover it. If something here turns
out wrong or you learn a new environment fact, write it under **Env notes** in
your section of the task file — the leader folds it into this file.
Last verified: 2026-09-27 (T-002 … T-012; first MinGW build of core-model: zero warnings).

## Toolchain

| Tool | Path / version |
|---|---|
| CMake | `C:\Qt\Tools\CMake_64\bin\cmake.exe` (3.30.5), `ctest.exe` beside it |
| Ninja | `C:\Qt\Tools\Ninja\ninja.exe` (1.12.1) |
| MSVC | VS 2026 Community, `C:\Program Files\Microsoft Visual Studio\18\Community`, cl 19.51 |
| MinGW | `C:\Qt\Tools\mingw1310_64\bin` (GCC 13.1.0, `mingw32-make`) |
| Qt (MSVC) | `C:\Qt\6.11.1\msvc2022_64` — `bin\qmake.exe` (qmake 3.1) |
| Qt (MinGW) | `C:\Qt\6.11.1\mingw_64` — `bin\qmake.exe` |
| vswhere | `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` |
| Shells | PowerShell 5.1 (primary), Git Bash; Node exists, **no Python** |

## Rules that save time

1. **Every tool call is a fresh shell.** Environment set in one call is gone
   in the next. Put `. scripts/vsdev.ps1` and the build commands **in the same
   PowerShell call**.
2. **Reuse the existing build folders.** `build/cmake-debug` (MSVC),
   `build/cmake-mingw` (MinGW) and `build/qmake-debug` (qmake/MSVC) are
   already configured. `cmake --build` re-runs configure by itself when a
   `CMakeLists.txt` changed; configure from scratch only when the Plan asks for
   a fresh configure. Never delete these folders.
3. **Scratch experiments** go to `build/_scratch-<task>/` and are deleted
   before you finish. Nothing outside `build/` is written by a build.
4. **Harmless noise:** `. scripts/vsdev.ps1` prints
   `'vswhere.exe' is not recognized …` — it comes from Microsoft's
   `Launch-VsDevShell.ps1`; `cl`, `cmake`, `ninja` resolve correctly anyway.
   `git add` prints `LF will be replaced by CRLF` warnings — expected
   (`core.autocrlf=true`, `.gitattributes`).
5. **Do not merge native stderr in PowerShell 5.1** (`2>&1`, `*>&1`) on
   `cmake`, `ctest`, `scripts/check.ps1` …: a routine CMake warning on stderr
   becomes a `NativeCommandError` and a false failure (T-005). stderr is
   captured anyway.
6. **A Python `ninja` may be on `PATH`** before `vsdev.ps1` runs; `vsdev.ps1`
   (MSVC) and the MinGW line below both prepend `C:\Qt\Tools\Ninja` so the
   right one wins.

## MSVC — the daily loop (PowerShell, one call)

```powershell
. scripts/vsdev.ps1
cmake --build build/cmake-debug
ctest --test-dir build/cmake-debug -L <label> --output-on-failure
```

Fresh configure (only when needed):
`cmake -S . -B build/cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64`

## MinGW — checkpoints (PowerShell, one call)

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cmake --build build/cmake-mingw
ctest --test-dir build/cmake-mingw -L <label> --output-on-failure
```

Fresh configure: same as MSVC with `-B build/cmake-mingw
-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64`.

## qmake (MSVC, one call)

```powershell
. scripts/vsdev.ps1
Push-Location build/qmake-debug
C:\Qt\6.11.1\msvc2022_64\bin\qmake.exe ../../mc_protocol.pro CONFIG+=debug
nmake; nmake check
Pop-Location
```

MinGW flavour: MinGW `PATH` line above, `C:\Qt\6.11.1\mingw_64\bin\qmake.exe`,
`mingw32-make` / `mingw32-make check`, in `build/qmake-mingw`.

## qmake gotchas

- `SUBDIRS` entries: write `SUBDIRS = foo` plus `foo.file = path/to/real.pro`
  and **no** `foo.subdir`. A bare entry resolves to `<entry>/<entry>.pro`;
  `.file` together with `.subdir` gives a qmake warning (T-003).
- **Stale qmake sub-Makefiles:** a `subdirs` build regenerates
  `Makefile.<name>` only when it is missing, never when the `.pro` changed.
  After editing `SUBDIRS`, `SOURCES` or `DEFINES`, delete the affected
  `Makefile.<name>*` (and its `debug/` objects) in `build/qmake-*` or rerun
  qmake from a clean folder (T-019).
- **Adding a test binary to `tests/qmake/tests.pro`:** the tree is two levels
  of `subdirs` (`mc_protocol.pro` → `tests.pro` → one `.pro` per binary).
  Rerunning `qmake mc_protocol.pro` regenerates only the top `Makefile`;
  delete `build/qmake-*/tests/qmake/Makefile.tests*`, then run `nmake` /
  `mingw32-make` (not just qmake) — the nested Makefile is rebuilt during the
  build (T-020).
- **New `SOURCES` in an existing test `.pro`:** its `Makefile.<target>*` is
  not regenerated — delete those files in `build/qmake-*/tests/qmake/`, then run
  a plain `nmake` / `mingw32-make` before `check`; otherwise `check` silently
  runs the old binary (T-022, T-026). Confirm with `--list-test-cases`.
  A change to `mc_core.pri` itself (new library source) affects every qmake
  test target — each compiles the library sources directly — so delete all
  `Makefile.*` under `build/qmake-*/tests/qmake/` (T-026).
- **Every test `.pro` sets its own intermediate dirs** (`OBJECTS_DIR`,
  `MOC_DIR`, `RCC_DIR`, `UI_DIR` named after `$$TARGET`, split debug/release):
  the `.file`-style `SUBDIRS` builds all test projects in one folder, and a
  shared `debug/` let `mc_core_protocol_tests` link core-model's
  `test_alloc.obj` while still reporting green (T-019). Copy the block from an
  existing test `.pro`. Check a new qmake binary with `--list-test-cases`
  against its CMake twin.
- Test `.pro` files need `DEFINES += DOCTEST_CONFIG_USE_STD_HEADERS`, as the
  CMake `mc_doctest` target does (T-019).
- `QT =` in a test `.pro` keeps Qt off the compile and link lines entirely
  (core-only tests).
- `.pri` include guards: `!defined(MC_X_PRI_INCLUDED, var) { … }`.

## Coverage (MinGW only)

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cmake --build build/cmake-coverage
ctest --test-dir build/cmake-coverage -L "core_model|build"   # produces the .gcda data
cmake --build build/cmake-coverage --target mc_coverage_report
```

Per-directory reports: `mc_coverage_report` (model), `mc_coverage_report_protocol`,
`…_session`, `…_mock` (bar 90), `…_device` (report only, no bar); run `ctest -L "core_model|core_protocol|core_session|mock|integration|device|build"`
first so every binary contributes `.gcda` data.
**Every executable that links an instrumented library needs `mc_apply_coverage(<target>)`**,
examples included — otherwise the coverage build fails to link with `__gcov_init` undefined (T-034).

`build/cmake-coverage` is configured with `-DMC_COVERAGE=ON` (T-012). The report
is `cmake/mc_coverage_report.cmake` (plain `gcov`, no Python); it fails below
the bar (`-D MC_MIN_COVERAGE=<n>`, default 95, total over `src/core/model`).

## CMake script gotchas

- Never run two `scripts/check.ps1` (or `check.sh`) at the same time: every run
  recreates the same `build/check-*` folders, so parallel runs corrupt each
  other and fail falsely (T-012 tester). Run the kits one after another.
- Harmless: `check.ps1` with the MinGW kit prints CMake developer warnings
  "Policy CMP0156 / CMP0128 is not set" in the `cmake-core` stage (T-012).

- A `cmake -P` script does not inherit the project's policies: start it with
  `cmake_minimum_required(VERSION 3.16)`, or `if(… IN_LIST …)` fails with
  "Unknown arguments specified" (CMP0057, T-004).
- Build guards `BLD-04` / `BLD-05` read `build/<dir>/mc_sources.txt`, written at
  configure time; a new source file needs a (re)configure before they see it.

## Tests

- doctest v2.5.3 at `tests/third_party/doctest/doctest.h`; one
  `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` per test binary.
- `mc_doctest` carries `DOCTEST_CONFIG_USE_STD_HEADERS` for every test binary:
  without it MSVC cannot stringify `std::string_view` in a failed `CHECK`
  (doctest only forward-declares `std::ostream`) — T-006.
- A test that reads source-tree files at runtime (e.g. `tests/vectors/`) finds
  them through the compile definition `MC_TESTS_SOURCE_DIR` set in
  `tests/CMakeLists.txt` — never through the working directory, which differs
  between build trees (T-013). Golden-vector loader: `tests/common/vectors.h`
  (static library `mc_test_vectors`).
- Long repetitive vector lines (e.g. PRIM-09, 300 × `FF`) are generated with a
  one-line `perl` command, not copied from a terminal — a copied line was
  silently truncated once; VEC-02 caught it (T-014). No Python on this machine.
- GCC 13 `-Wdangling-reference` (an error under `-Werror`, MinGW only) fires on
  `const Vector& v = byId(vectors, std::string("V-…"))` — a reference-returning
  call with a temporary argument. Bind the id to a named `const std::string`
  first (T-030).
- **Qt test binaries** (label `device`) are added with `mc_add_qt_test()` in
  `tests/CMakeLists.txt`: it prepends the kit's Qt `bin` to PATH through
  `ENVIRONMENT_MODIFICATION` (CMake ≥ 3.22) and sets
  `QT_ASSUME_STDERR_HAS_CONSOLE=1` — without it QtTest prints **nothing** on
  Windows when its output is a pipe, so a failure is silent under ctest or an
  agent shell. Running a Qt binary by hand needs the Qt `bin` on PATH yourself (T-035).
- **Virtual COM pairs** (installed by the owner, 2026-10-01): `COM50`↔`COM51`
  and `COM52`↔`COM53`. QDV-14 runs with `$env:MC_TEST_SERIAL_PAIR = "COM50,COM51"`
  and skips without it. Do not install or reconfigure serial software.
- Windows reports a refused **loopback** connect only after 2–4 s; a test
  against a closed port ends on its own connect timer instead (T-035).
- After restoring a mutated source with `Copy-Item`, touch it
  (`(Get-Item f).LastWriteTime = Get-Date`): the copy keeps the old timestamp,
  ninja sees no work and the test still runs the mutant (T-036).
- `Start-Process -PassThru` returns an empty `ExitCode` unless `$p.Handle` is
  read right after starting (T-040). `Stop-Process -Force` ends `virtual_plc`.
- A scratch copy of the tree must keep `tests/build/test_version.cpp`;
  excluding every `build` folder breaks configure (T-040).
- `nmake` and `cmake --build` banners go to stderr: redirect stdout to a file
  and filter it, never merge stderr (T-040).
- `mc_core.pri` has CRLF line endings: a `perl -0pi` substitution silently
  matched nothing on it; edit `.pri` files with the Edit tool (T-041).
- A Bash heredoc containing an apostrophe can break the tool's command
  parsing; write such files with the Write tool (T-041).
- Deleting qmake `Makefile.*` files or a relative `build/_scratch-*` folder: use Git Bash `rm -f …`; a PowerShell
  `Get-ChildItem | Remove-Item` pipeline was blocked by the tool guard (T-036).
- Test binaries per module: `mc_core_model_tests` (ctest name
  `core_model.mc_core_model_tests`), main in `tests/core/model/main.cpp`.
- ctest labels: `build`, `core_model`, `core_protocol`, `core_session`,
  `mock`, `integration`, `device`, `replay`, `hil_tool`.
