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
3. **Temporary files never go to `%TEMP%`** or anywhere outside the project
   (Access boundaries, `rules.md`): use `build/_scratch-<task>/` or `temp-docs/`.
   `clang-format` lives at `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\`
   (not on PATH after `vsdev.ps1`; running the toolchain binary is fine) (T-051).
4. **Scratch experiments** go to `build/_scratch-<task>/` and are deleted
   before you finish. Nothing outside `build/` is written by a build.
5. **Harmless noise:** `. scripts/vsdev.ps1` prints
   `'vswhere.exe' is not recognized …` — it comes from Microsoft's
   `Launch-VsDevShell.ps1`; `cl`, `cmake`, `ninja` resolve correctly anyway.
   `git add` prints `LF will be replaced by CRLF` warnings — expected
   (`core.autocrlf=true`, `.gitattributes`).
6. **Do not merge native stderr in PowerShell 5.1** (`2>&1`, `*>&1`) on
   `cmake`, `ctest`, `scripts/check.ps1` …: a routine CMake warning on stderr
   becomes a `NativeCommandError` and a false failure (T-005). stderr is
   captured anyway.
7. **A Python `ninja` may be on `PATH`** before `vsdev.ps1` runs; `vsdev.ps1`
   (MSVC) and the MinGW line below both prepend `C:\Qt\Tools\Ninja` so the
   right one wins.

## Parallel builds — always (owner request 2026-10-01)

This PC has **32 logical cores**. Every build and test run uses them:

| Step | Use | Never |
|---|---|---|
| CMake build (Ninja) | `cmake --build <dir> --parallel 32` (Ninja is parallel by default; the flag makes it explicit) | `-j 1`, `--parallel 1` |
| qmake, MSVC kit | `C:\Qt\Tools\QtCreator\bin\jom\jom.exe -j 32` and `jom.exe -j 32 check` (jom is an nmake-compatible parallel make) | plain `nmake` (single-threaded) |
| qmake, MinGW kit | `mingw32-make -j32` and `mingw32-make -j32 check` | `mingw32-make` without `-j` |
| ctest | `ctest ... -j 8` (device tests use OS-chosen ports; `-j 6`/`-j 8` stress runs were green, T-040) | — |

Still **never two check scripts or two builds of the same folder at once** — parallelism
goes inside one build, not across builds sharing a folder.

**Several toolsets at once** (owner 2026-10-08, `rules.md` "Parallel multi-toolset builds"): start each
kit's build as its own background process in its own tree and split the cores (`--parallel 8`, `jom -j 8`).
For two `check.ps1` runs, or for a tester and a reviewer building at the same time, use the worktree slots:

```powershell
# from the main tree: mirror its current files (uncommitted edits included) into the slots
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/wt-sync.ps1            # all slots
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/wt-sync.ps1 -Name wt2  # one slot
# then build inside a slot exactly as in the main tree, e.g.
Set-Location .wt/wt2; . scripts/vsdev.ps1; scripts/check.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64 -Jobs 8
```

Slots: `.wt/wt1` … `.wt/wt4` (git-ignored worktrees, detached). Each has its own `build/`: the first
build in a slot is a full build, later ones are incremental. Never edit sources in a slot; edit the main
tree and sync again. ADS is not rebuilt per slot: the synced `CMakeUserPresets.json` and `mc_local.pri` point
at the main tree's `build/ads-*`; for a hand-written configure pass the main tree's absolute ADS path. New
slot: `git worktree add --detach .wt/wt5 HEAD`, then sync.

The check scripts are parallel too (T-056): `check.ps1 -Jobs <n>` (default: logical
processors) and `-JomPath` (default the Qt Creator jom; jom on PATH next; else nmake with a
"single-threaded" note); `check.sh` reads `MC_CHECK_JOBS` (default `nproc`). Wall times on
this PC: `check.ps1` MSVC ~70 s, MinGW ~97 s, `check.sh` ~92 s (were 320–550 s). jom (like
nmake) writes its own link response files to `%TEMP%` — the tool's behaviour, accepted.

## MSVC — the daily loop (PowerShell, one call)

```powershell
. scripts/vsdev.ps1
cmake --build build/cmake-debug --parallel 32
ctest --test-dir build/cmake-debug -L <label> -j 8 --output-on-failure
```

Fresh configure (only when needed):
`cmake -S . -B build/cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64`

## MinGW — checkpoints (PowerShell, one call)

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cmake --build build/cmake-mingw --parallel 32
ctest --test-dir build/cmake-mingw -L <label> -j 8 --output-on-failure
```

Fresh configure: same as MSVC with `-B build/cmake-mingw
-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64`.

## qmake (MSVC, one call)

```powershell
. scripts/vsdev.ps1
Push-Location build/qmake-debug
C:\Qt\6.11.1\msvc2022_64\bin\qmake.exe ../../mc_protocol.pro CONFIG+=debug
C:\Qt\Tools\QtCreator\bin\jom\jom.exe -j 32; C:\Qt\Tools\QtCreator\bin\jom\jom.exe -j 32 check
Pop-Location
```

MinGW flavour: MinGW `PATH` line above, `C:\Qt\6.11.1\mingw_64\bin\qmake.exe`,
`mingw32-make -j32` / `mingw32-make -j32 check`, in `build/qmake-mingw`.
Where the gotchas below say "`nmake`", jom does the same job.

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
  `Makefile.*` under `build/qmake-*/tests/qmake/` (T-026). A new `SUBDIRS` entry in
  `tests/qmake/tests.pro` (e.g. `mc_replay_tests`, T-061) needs
  `build/qmake-*/tests/qmake/Makefile.tests*` deleted before the first build.
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

## GUI tool `mc_workbench` (T-067)

- ADS (docking) is a local-path dependency, never in the repo; qpb is vendored in `components/qpb`.
- MSVC: prebuilt ADS `C:/build_packages/qtadvanceddocking-5.1.1` (default of `MC_ADS_DIR`).
- MinGW: build ADS once into the project: `scripts/build-ads.ps1` (source
  `C:/build_packages/Qt-Advanced-Docking-System`, read only) → `build/ads-mingw_64/install`; then
  configure MinGW CMake with `-DMC_ADS_DIR=<that path>` (or `$env:MC_ADS_DIR`).
- qmake: copy `mc_local.pri.example` to the git-ignored `mc_local.pri`; it sets `MC_ADS_DIR`
  (MSVC kits) and `MC_ADS_DIR_MINGW` (MinGW kits).
- CMake skips the GUI with a message when ADS is missing or built for the other compiler, and
  needs CMake ≥ 3.22 for it. The check scripts' MinGW CMake stages therefore skip the GUI unless
  `$env:MC_ADS_DIR` points at the MinGW ADS; their qmake stage builds it through `mc_local.pri`.
- After pulling T-067, delete `build/qmake-*/tools/qmake/Makefile.tools*` and
  `build/qmake-*/tests/qmake/Makefile.tests*` before the next qmake build.
- Harmless: MSVC qmake prints C4996 `compressEvent` warnings from Qt headers in qpb moc files.
- GUI tests: label `gui`, `QT_QPA_PLATFORM=offscreen`. One binary `mc_workbench_tests`
  (`tests/gui/gui_tests_main.cpp` runs every QtTest class; register a new class there and in
  `gui_suites.h`). It holds `RESOURCE_LOCK mc_serial_pair` (a case holds COM54 open when
  `MC_TEST_SERIAL_PAIR` is set) (T-068).
- New sources in `tools/mc_workbench/mc_workbench.pri` or `tests/qmake/mc_workbench_tests.pro`: delete
  `build/qmake-*/tools/qmake/Makefile.mc_workbench*` and `build/qmake-*/tests/qmake/Makefile.mc_workbench_tests*`
  before the next qmake build (T-068).
- QtTest macros (`QVERIFY`, `QTRY_*`) `return;` — not usable in a function that returns a value;
  `QTest::qWaitFor` is `[[nodiscard]]` (C4834 under `/WX`) (T-068).
- qmake `$$first()` takes a variable name, not a value (use `$$cat(file, singleline)`); a `.pri` that
  writes a generated header does it with `write_file()` under `$$OUT_PWD`. ADS MinGW artifacts:
  `libqtadvanceddocking-qt6[d].dll` / `.dll.a` (T-067).
- `mc_workbench_core` links Qt Core + Network only: no `QColor` / QtGui there (use other roles than
  `ForegroundRole` in core models). `QAbstractItemView::state()` is protected (find an open editor
  with `viewport()->findChild<QLineEdit*>()`). `qpb::PropertyModel::setValue` returns false and emits
  `validationFailed` when a property validator refuses. A well-formed test frame:
  `mc::McProtocol(cfg).encode(request)`; a test mock listens on `127.0.0.1` port 0 (T-069, T-070).
- Run `scripts/check.ps1` / `check.sh` with `MC_TEST_SERIAL_PAIR` **unset**: their qmake stage runs
  `make -j check`, which starts test binaries side by side, and two of them opening COM54 at once
  fail with "Access is denied" (T-074). Serial tests run in the ctest runs, where `RESOURCE_LOCK` works.
- The program source of `mc_workbench` is `workbench_main.cpp`, not `main.cpp`: with
  `hil_capture.pri` in the same qmake project, jom picked `tools/hil_capture/main.cpp` for `main.obj`
  (qmake MSVC only). Never name a source `main.cpp` in a qmake project that includes another tool's
  `.pri` (T-074). `build/cmake-coverage` is configured with the MinGW ADS, so it builds the GUI;
  reports `mc_coverage_report_gui_core` / `_gui_ui` (report only).
- `gui.mc_workbench_tests` is `RUN_SERIAL` (GUI-05 floods four busy threads; beside other binaries
  its GUI-gap limit and their timing cases starve). A full `ctest -j 8` takes about 85 s (T-071).
- After `. scripts/vsdev.ps1`, `bash` is not on PATH: delete qmake `Makefile.*` from the Bash tool.
  `QTRY_*` macros re-run their condition (no `++` inside). `QSortFilterProxyModel::invalidateFilter`
  is deprecated from Qt 6.9 (use `begin/endFilterChange()` behind a version check). MinGW `-Wextra`
  flags a brace initialiser that skips a new defaulted member (T-071).
- Edit `.pri` / `.pro` files with the Edit tool: a perl `s///` with `\\\n` wrote a literal `\n` into a
  qmake continuation line (T-069).

## Qt Creator (owner's IDE, 2026-10-04)

- CMake presets: `CMakePresets.json` (committed, hidden bases only) + `CMakeUserPresets.json`
  (git-ignored, this PC's paths; template `CMakeUserPresets.json.example`). Presets `msvc-debug` and
  `mingw-debug` build into `build/qtc-<preset>/` (`cmake --preset msvc-debug` inside
  `scripts/vsdev.ps1`; MinGW needs only the preset). `msvc-release` / `mingw-release` too (T-077):
  all four verified 35/35. Agents use the presets with `-B build/<own folder>`, never the default
  `build/qtc-*`. Release/RelWithDebInfo notes (T-077): drain tests expect the handler only in Debug;
  GCC `-O2+` elides an unobserved `new`/`delete` (allocation probes must be observable); ADS ships
  Debug and Release only, so `RelWithDebInfo`/`MinSizeRel` map to Release; a hung
  `mc_workbench_tests.exe` survives a ctest timeout (end it with `taskkill`).
- Qt Creator's qmake and CMake projects use their own `build/qtc-*` folders. **Agents never build
  `build/qtc-*` or `build/Desktop_Qt_*`** (the owner's IDE builds there), and the owner's IDE must not
  use `build/qmake-*`, `build/check-*` or `build/cmake-*`.
- Qt Creator MCP server (when the owner opens it): SSE transport, `http://127.0.0.1:<port>/sse`; a
  scratch client can post JSON-RPC to the `/message?session=` endpoint it announces. Tools include
  `build`, `run_tests`, `list_build_configs`, `call_action`.

## Qt 5.15 (T-078, 2026-10-07)

- Kit: `C:/Qt/5.15.0/msvc2019_64` only (no Qt 5 MinGW kit): Qt 5 is verified on MSVC only.
- **Toolset 14.44, never 14.51:** Qt 5.15.0's headers fail with the VS 2026 (14.51) standard library,
  and qpb 1.7.0 excludes MSVC 2026 for Qt 5. In a **fresh** PowerShell:
  `. scripts/vsdev.ps1 -VcVarsVer 14.44` (refuses if the shell already holds another toolset).
  `scripts/check.ps1 -QtDir C:/Qt/5.15.0/msvc2019_64` loads 14.44 by itself. CMake and qmake warn
  when Qt 5 meets MSVC ≥ 14.50.
- Folders: `build/cmake-qt5-msvc` (Debug), `build/cmake-qt5-msvc-release`, `build/qmake-qt5-msvc`
  (all configured with 14.44); presets `qt5-msvc-debug` / `qt5-msvc-release` (use with `-B`).
- `_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING` is defined (PUBLIC on `mc_device`, and in
  `mc_device.pri`) only for Qt 5 + MSVC: 14.44 deprecates the `stdext` iterators Qt 5's headers use.
- CMake: `cmake/mc_qt.cmake` (`MC_QT_MAJOR`, `mc_find_qt()`, versionless `Qt::` targets);
  `$<TARGET_FILE_DIR:Qt::Core>` does not work on Qt 5 — use `Qt${QT_VERSION_MAJOR}::Core`.
- GUI on Qt 5 (T-079): qpb 1.7.0; `tools/mc_workbench/qt_compat.h` holds the only version checks
  (`jsonInteger`, `useUtf8`, `staticConnect` for C4573 on 14.44). The CMake GUI on Qt 5 needs
  `MC_ADS_DIR` = the Qt 5 ADS (presets set it; `check.ps1` skips the GUI in its CMake stage otherwise).
  Qt 5.15.0 `QSerialPort` re-open of a virtual COM port right after close fails "Access is denied"
  now and then (Qt 6.11 does not): `GUI_02_comPair…` flakes ~1 in 12 on Qt 5 only.
- ADS for Qt 5 (any kit, the script detects the major): `scripts/build-ads.ps1 -QtDir C:/Qt/5.15.0/msvc2019_64` →
  `build/ads-qt5-msvc2019_64/install` (`mc_local.pri` key `MC_ADS_DIR_QT5`).

## Qt 5.15 MSVC 32-bit / x86 (T-083, 2026-10-08)

- Kit: `C:/Qt/5.15.0/msvc2019` (no `_64`: 32-bit). The owner's Qt Creator kit "Desktop Qt 5.15.0 MSVC2019
  32bit" uses it with toolset 14.44, `HostX64/x86`, Ninja and `/WX` (trees `build/Desktop_Qt_5_15_0_MSVC2019_32bit_*`:
  never built by agents).
- Shell, in a **fresh** PowerShell: `. scripts/vsdev.ps1 -VcVarsVer 14.44 -Arch x86` (`-Arch x64|x86`, default x64;
  an explicit `-Arch` refuses a shell that already holds another target). `scripts/check.ps1 -QtDir C:/Qt/5.15.0/msvc2019`
  and `scripts/build-ads.ps1 -QtDir C:/Qt/5.15.0/msvc2019` take the arch from the kit name (no `_64` = x86).
- ADS for x86: `scripts/build-ads.ps1 -QtDir C:/Qt/5.15.0/msvc2019` -> `build/ads-qt5-msvc2019/install`
  (`mc_local.pri` key `MC_ADS_DIR_QT5_X86`, read by `mc_gui_deps.pri` when `QT_ARCH` is `i386`).
- Folders: `build/cmake-qt5-msvc32` (Debug), `build/cmake-qt5-msvc32-release`, `build/qmake-qt5-msvc32`; presets
  `qt5-msvc32-debug` / `qt5-msvc32-release` (use with `-B`, e.g. `cmake --preset qt5-msvc32-debug -B build/cmake-qt5-msvc32`).
  Put `C:/Qt/5.15.0/msvc2019/bin` on PATH for a hand-run of the Qt test binaries.
- x86 pitfall: `size_t` is 32-bit, so a `uint64_t` loop index that subscripts a `std::vector` gives C4244 under `/WX`;
  use `size_t` for the index (`src/mock/command_exec.cpp`).
- Do not kill build processes by a broad name match: other builds (and the owner's IDE) run `jom`/`cl` too.

## Other tools on this PC (T-065, T-066)

- Vendor manuals (PDF) are in `docs/mc_reference/`. Extract text with Git Bash's
  `/mingw64/bin/pdftotext -layout <pdf> build/_scratch-<task>/<name>.txt` (the Read tool cannot
  render PDFs here: no `pdftoppm`). Python 3.12 is installed (`pypdf` available) for scratch scripts.
- `.vec` files accept no free comment lines (the loader rejects them): explanations go in the tests
  or in a `source:` metadata key.
- A mutant that stops using a parameter fails to compile under `/WX` / `-Werror`
  (unused parameter): mark it `(void)param;` in the mutant.

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
- **Virtual COM pairs** (Electronic Team Virtual Serial Port, set up by the
  owner): **`COM54`↔`COM55` replaces `COM50`↔`COM51`** (owner, 2026-10-01; the
  `COM50->COM51` device went to PnP state Error, code 43, in T-054 — retired for
  good, no repair, owner 2026-10-02: never use it), and `COM52`↔`COM53`. Default:
  `$env:MC_TEST_SERIAL_PAIR = "COM54,COM55"` (verified by the leader: 10 passed,
  0 skipped); `COM52,COM53` is the second pair. QDV-14 skips without the variable. Never install, reconfigure or repair serial
  software (owner only). Tests that open the pair must not run concurrently
  (ctest `RESOURCE_LOCK`): the pair broke during a `ctest -j 8` run (T-054).
  `RESOURCE_LOCK` only works inside **one** `ctest` invocation: two agents (or the leader and
  an agent) running serial tests at the same time collide on the port — the leader saw
  `hil_tool.mc_hil_tool_serial` hang to the 120 s ctest timeout that way (T-063). When two
  agents work in parallel, only one runs with `MC_TEST_SERIAL_PAIR` set at a time.
- Windows reports a refused **loopback** connect only after 2–4 s; a test
  against a closed port ends on its own connect timer instead (T-035).
- Mutation runs: touching a restored source with Node `fs.utimesSync` did **not**
  make ninja rebuild, so the "restored" run still used the mutant's objects.
  Delete the affected object files before each mutation build (T-050).
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
