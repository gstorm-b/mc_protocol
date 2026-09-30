# Spec: build-packaging

- **Module id:** `build-packaging` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** nothing. Every other module depends on this one.
- **Inputs:** `docs/intent/mc_protocol_library.md`, `docs/ideas/mc_protocol_library.md` §4.4 and §7, capability-map assumptions 1, 3, 4, 9.

## Objective

Turn the library into **one self-contained folder** that a project consumes in one of three ways, without editing any header of its own:

1. **CMake:** `add_subdirectory(third_party/mc_protocol)` then link `mc::core`, `mc::device`, or `mc::mock`.
2. **qmake:** `include($$PWD/third_party/mc_protocol/mc_protocol.pri)`; the library's sources compile into the consumer's target and the `.pri` adds the Qt modules it needs.
3. **No git:** copy the folder over the old one. Nothing inside the folder reaches outside it, so a replacement is atomic.

The folder also carries a version, a changelog, a one-command build-and-test script, and tests that keep the two build systems and the public surface honest.

**User stories**

- As a CMake consumer I link `mc::device` and get `mc::core` transitively, and the library's tests and examples are **not** built into my project.
- As a qmake consumer I include one `.pri` and do not have to know which Qt modules the library uses.
- As a non-Qt consumer I link `mc::core` only, and CMake never searches for Qt.
- As the maintainer I run one script and know that both build systems build and every test passes.
- As the maintainer, if I add a source file to CMake and forget the `.pri`, a test fails before I tag a release.

## Tech Stack

| Item | Choice | Note |
|---|---|---|
| Language | C++17 | `cxx_std_17` on every target; no compiler extensions |
| CMake | 3.16 minimum for consumers | The dev workflow uses whatever is installed; no presets in v1 |
| qmake | The one shipped with the Qt used to build | Standalone qmake build is a `subdirs` project that proves the `.pri` files work |
| Qt | 6.2 LTS minimum | Only `mc::device` and its tests use Qt: `Core`, `Network`, `SerialPort`, `Test` |
| Test framework | doctest, vendored single header (MIT) at `tests/third_party/doctest/doctest.h` | For every std-only test binary. `qt-device` tests use QtTest |
| Scripts | PowerShell 5.1 (`scripts/check.ps1`) and POSIX sh (`scripts/check.sh`) | No Python, no CI in v1 |
| Compilers | MSVC 2019+, MinGW GCC, GCC, Clang | Warnings-as-errors only when the library is the top-level project. v1 is verified with MSVC and MinGW GCC on Windows only; GCC/Clang on Linux are not verified. |
| Library type | Static only | Shared builds and export macros are out of scope; ABI is not promised |

## Commands

All build output goes under `build/` at the repository root, one sub-folder per flavour, never anywhere else.

**CMake, full build with Qt (developer default)**

```powershell
cmake -S . -B build/cmake-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2019_64"
cmake --build build/cmake-debug --config Debug
ctest --test-dir build/cmake-debug -C Debug --output-on-failure
```

**CMake, core only, no Qt on the machine**

```powershell
cmake -S . -B build/cmake-core -DCMAKE_BUILD_TYPE=Debug -DMC_BUILD_DEVICE=OFF
cmake --build build/cmake-core
ctest --test-dir build/cmake-core --output-on-failure
```

**qmake, standalone (tests + examples through the `.pri` files)**

```powershell
New-Item -ItemType Directory -Force build/qmake-debug
Set-Location build/qmake-debug
qmake ../../mc_protocol.pro CONFIG+=debug
nmake            # or: mingw32-make / jom / make
nmake check      # runs every test target (CONFIG += testcase)
```

**Everything, one command**

```powershell
scripts/check.ps1 -QtDir "C:/Qt/6.7.2/msvc2019_64"      # Windows
scripts/check.sh C:/Qt/6.7.2/mingw_64                      # Git Bash, MinGW
```

The script runs, in order: CMake full build + ctest, CMake core-only configure + build + ctest with Qt removed from the environment, qmake build + `check`, the two consumer smoke projects. It stops at the first failure and prints which stage failed.

**Consumer, CMake**

```cmake
add_subdirectory(third_party/mc_protocol)
target_link_libraries(my_app PRIVATE mc::device)   # or mc::core for a non-Qt program
```

**Consumer, qmake**

```qmake
include($$PWD/third_party/mc_protocol/mc_protocol.pri)   # core + device
# or: include($$PWD/third_party/mc_protocol/mc_core.pri) # core only, no Qt modules added
```

## Project Structure

```text
mc_protocol/
├── CMakeLists.txt                # project(), options, add_subdirectory(src), tests/examples when top-level
├── mc_protocol.pro               # qmake subdirs: tests/qmake + examples/qmake (dev only)
├── mc_protocol.pri               # includes mc_core.pri and mc_device.pri
├── mc_core.pri                   # INCLUDEPATH, HEADERS/SOURCES of src/core, CONFIG += c++17; include guard variable
├── mc_device.pri                 # QT += core network serialport; HEADERS/SOURCES of src/device; includes mc_core.pri
├── mc_mock.pri                   # HEADERS/SOURCES of src/mock; includes mc_core.pri
├── cmake/
│   ├── mc_warnings.cmake         # mc_apply_warnings(target): /W4 or -Wall -Wextra -Wpedantic; -Werror when MC_WARNINGS_AS_ERRORS
│   ├── mc_version.cmake          # reads MC_VERSION_* from include/mc/version.h into CMake variables
│   ├── check_pri_sync.cmake      # ctest script: qmake lists == CMake lists
│   └── check_include_hygiene.cmake # ctest script: public-surface and no-Qt-in-core rules
├── include/mc/                   # THE public surface. Owned per sub-folder by the other module specs.
│   ├── version.h                 # owned here
│   ├── core/                     # core-model, core-protocol, core-session
│   ├── mock/                     # mock-plc
│   └── device/                   # qt-device
├── src/
│   ├── CMakeLists.txt            # defines mc_core, mc_mock, mc_device
│   ├── core/                     # private headers and sources; sub-folders per module
│   ├── mock/
│   └── device/
├── tests/
│   ├── CMakeLists.txt
│   ├── third_party/doctest/doctest.h
│   ├── vectors/                  # golden vectors (.vec text), owned by core-protocol
│   │   └── captured/             # hardware captures per profile, owned by hil-capture
│   ├── common/                   # vector loader shared by core and mock tests, owned by core-protocol
│   ├── hil/                      # profiles (*.example.json committed), plans, replay + tool tests; owned by hil-capture
│   ├── core/                     # doctest binaries per core module
│   ├── mock/                     # incl. mock/integration: Session ↔ MockPlc matrix (label "integration")
│   ├── device/                   # QtTest binaries
│   ├── bench/                    # regression benchmarks, MC_BUILD_BENCH=ON only
│   ├── consumer_cmake/           # external project: add_subdirectory(../..) and link mc::device
│   ├── consumer_qmake/           # external .pro: include(../../mc_protocol.pri)
│   └── qmake/                    # .pro files that build the test binaries via the .pri files
├── examples/
│   ├── CMakeLists.txt
│   └── qmake/
├── tools/
│   ├── CMakeLists.txt            # MC_BUILD_TOOLS only
│   └── hil_capture/              # owned by hil-capture
├── scripts/
│   ├── vsdev.ps1                 # dot-sourced: loads the VS developer environment
│   ├── check.ps1
│   └── check.sh
├── docs/                         # intent, ideas, spec, rules (doc comment style), mc_reference, ADRs; docs/hil owned by hil-capture
├── CHANGELOG.md                  # Keep a Changelog format; Unreleased section on top
├── LICENSE                       # open question 1
└── README.md                     # what it is, the three consumption paths, version policy
```

**Ownership rule.** This spec owns everything in the tree above **except** the contents of `include/mc/core`, `include/mc/mock`, `include/mc/device`, `src/core`, `src/mock`, `src/device`, `tests/core`, `tests/common`, `tests/mock`, `tests/device`, `tests/hil`, `tests/vectors` (incl. `tests/vectors/captured`), `examples/*`, `tools/*` and `docs/hil`, which belong to the module specs named in the capability map. This spec does own the *build wiring* of those folders (the `CMakeLists.txt` and `.pri` entries).

**Build directories.** `build/` at the root only, one sub-folder per flavour (`cmake-debug`, `cmake-core`, `qmake-debug`, …). Tests, examples and consumer projects build into that same tree, never beside their own sources.

## Targets and options

| CMake target | Alias | Type | Sources | Public include | Links |
|---|---|---|---|---|---|
| `mc_core` | `mc::core` | static | `src/core/**` | `include/` | nothing |
| `mc_mock` | `mc::mock` | static | `src/mock/**` | `include/` | `mc::core` |
| `mc_device` | `mc::device` | static, `AUTOMOC ON` | `src/device/**` | `include/` | `mc::core`, `Qt6::Core`, `Qt6::Network`, `Qt6::SerialPort` |

Every target gets `target_compile_features(... PUBLIC cxx_std_17)`, `target_include_directories(... PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> PRIVATE src)` and `mc_apply_warnings()`.

**Who may link `mc::mock`.** Test binaries (`tests/mock`, `tests/mock/integration`, `tests/device`, `tests/hil`), the examples `session_loop`, `qt_console_poller`, `virtual_plc`, and the tool `tools/hil_capture`. The library targets never do: `mc_device` must not link `mc_mock` (BLD-05 d). Anything that links `mc::mock` is built only when `MC_BUILD_MOCK=ON`.

| Test binary | Label | Built when |
|---|---|---|
| `mc_core_model_tests`, `mc_core_protocol_tests`, `mc_core_session_tests` | `core_model`, `core_protocol`, `core_session` | `MC_BUILD_TESTS` |
| `mc_mock_tests` | `mock` | `MC_BUILD_TESTS` and `MC_BUILD_MOCK` |
| `mc_integration_tests` | `integration` | `MC_BUILD_TESTS` and `MC_BUILD_MOCK` |
| `mc_tcp_transport_tests`, `mc_config_json_tests` (one QtTest binary per `tst_*.cpp`; amended 2026-09-30, owner decision) | `device` | `MC_BUILD_TESTS` and `MC_BUILD_DEVICE` |
| `mc_device_tests`, `mc_device_thread_tests` (link `mc::mock`) | `device` | `MC_BUILD_TESTS`, `MC_BUILD_MOCK` and `MC_BUILD_DEVICE` |
| `mc_replay_tests` (std-only; skips cleanly when no capture exists) | `replay` | `MC_BUILD_TESTS` and `MC_BUILD_MOCK` |
| `mc_hil_tool_tests` | `hil_tool` | `MC_BUILD_TESTS`, `MC_BUILD_MOCK`, `MC_BUILD_DEVICE` and `MC_BUILD_TOOLS` |

| Option | Default | Meaning |
|---|---|---|
| `MC_BUILD_DEVICE` | `ON` | Build `mc_device`; when `OFF`, `find_package(Qt6)` is never called |
| `MC_BUILD_MOCK` | `ON` | Build `mc_mock` |
| `MC_BUILD_TESTS` | `ON` when top-level, `OFF` when consumed | Test binaries and `ctest` registration |
| `MC_BUILD_EXAMPLES` | `ON` when top-level, `OFF` when consumed | Example programs |
| `MC_BUILD_BENCH` | `OFF` | Regression benchmarks |
| `MC_BUILD_TOOLS` | `ON` when top-level and `MC_BUILD_DEVICE` and `MC_BUILD_MOCK` are on, `OFF` when consumed | Developer tools under `tools/` (`hil_capture`) |
| `MC_WARNINGS_AS_ERRORS` | `ON` when top-level, `OFF` when consumed | A consumer's newer compiler must never break the build with a new warning |
| `MC_COVERAGE` | `OFF` | GCC/Clang only: `--coverage` on core targets and tests |

"Top-level" is `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` (works on 3.16; `PROJECT_IS_TOP_LEVEL` needs 3.21).

**qmake `.pri` contract.** Each `.pri` starts with an include guard on a variable (`!defined(MC_CORE_PRI_INCLUDED, var) { MC_CORE_PRI_INCLUDED = 1 ... }`) so `mc_device.pri` and `mc_mock.pri` can both include `mc_core.pri` without duplicate sources. Paths are `$$PWD`-relative. `mc_device.pri` adds `QT += core network serialport`. No `.pri` sets `TARGET`, `TEMPLATE` or `DESTDIR`; those belong to the consumer.

**Version.** `include/mc/version.h` is the single source of truth:

```cpp
#pragma once
#define MC_VERSION_MAJOR 0
#define MC_VERSION_MINOR 1
#define MC_VERSION_PATCH 0
#define MC_VERSION_STRING "0.1.0"
namespace mc {
struct Version { int major; int minor; int patch; };
/// Returns the compiled-in library version, for a consumer to log at startup.
constexpr Version version() noexcept { return {MC_VERSION_MAJOR, MC_VERSION_MINOR, MC_VERSION_PATCH}; }
}
```

`cmake/mc_version.cmake` reads the three macros with `file(STRINGS … REGEX)` and feeds `project(mc_protocol VERSION …)`. Releases are git tags `v<major>.<minor>.<patch>` on the same commit that bumps the header and moves the `Unreleased` section of `CHANGELOG.md`. Semantic versioning: a change to any file under `include/mc/` that is not purely additive bumps `major` once 1.0 is tagged.

## Code Style

These conventions apply to every module; they are stated once, here.

```cpp
/**
 * @file device.h
 * @brief PLC device addresses: the device table, parsing and formatting.
 */
#pragma once

#include "mc/core/result.h"      // library headers: always "mc/..." relative to include/
#include <cstdint>               // then standard headers
#include <string_view>

namespace mc {

/**
 * @struct Device
 * @brief One PLC device address: a symbol such as D or X plus its number.
 *
 * Value type, trivially copyable, no invariants beyond what parseDevice() enforces.
 */
struct Device {
    DeviceType type{DeviceType::D};   ///< Symbol, indexes the device table.
    uint32_t number{0};               ///< Device number in the symbol's own radix.
};

/**
 * @brief Parses "D100", "x1F", "TN10". Longest symbol wins; number radix follows the symbol.
 *
 * @param[in] text Device text without surrounding spaces.
 * @return The parsed device.
 * @retval ErrorCode::InvalidDevice Unknown symbol, no number, or a digit outside the radix.
 * @par Complexity
 * O(len); no allocation.
 */
Expected<Device> parseDevice(std::string_view text) noexcept;

}  // namespace mc
```

- Files `snake_case.h` / `snake_case.cpp`; one public type family per header.
- Types `PascalCase`; functions and methods `camelCase`; private data members `m_camelCase`; public struct fields `camelCase`; constants `kPascalCase`; enumerators `PascalCase` inside `enum class`.
- Namespaces: public API flat in `mc`, internals in `mc::detail`, nothing else public.
- `#pragma once`; includes ordered: own header, `mc/...`, third-party, standard.
- Doc comments follow `docs/rules/doc_comment_style.md` (owner decision 2026-09-27, amending the earlier `///` rule): Doxygen `/** */` with tags, `///<` for fields and enumerators. Every public class, function, enum and field carries one; private members only when not evident from name and type; complexity and allocation behaviour stated in `@par Complexity` on every public function of `core-*`. The `///` comments in the header sketches of `docs/spec/` state contracts, not comment style.
- No exceptions cross a public boundary; `noexcept` on every public function that cannot throw.
- No macros in the public API other than `MC_VERSION_*`.
- `.clang-format` at the root: `BasedOnStyle: LLVM`, `IndentWidth: 4`, `ColumnLimit: 100`, `BreakBeforeBraces: Attach`, `PointerAlignment: Left`. Formatting is enforced by review, not by a build step.
- All comments, documents and commit messages in English.

## Testing Strategy

The build is the thing under test. Every item below runs from `scripts/check.*` and, where possible, as a `ctest` entry so it also runs under `ctest` alone.

| ID | Test | How |
|---|---|---|
| BLD-01 | Full CMake build with Qt configures, builds, and `ctest` is green | script stage 1 |
| BLD-02 | Core-only build never touches Qt | script stage 2 runs with `CMAKE_PREFIX_PATH` unset and `Qt*` removed from `PATH`; configure must succeed and the CMake trace must not contain `find_package(Qt6` |
| BLD-03 | qmake standalone build compiles every test and example through the `.pri` files, and `make check` passes | script stage 3 |
| BLD-04 | `pri_sync` | `ctest` script: parse `HEADERS`/`SOURCES` from each `.pri`, compare as sets with the CMake source lists exported to `build/…/mc_sources.txt`; any difference fails and is printed |
| BLD-05 | `include_hygiene` | `ctest` script over `include/mc/**` and `src/**`: (a) every `#include "…"` resolves under `mc/`; (b) no `#include <Q`, `QT_`, or `Q_OBJECT` token in `include/mc/core`, `include/mc/mock`, `src/core`, `src/mock`; (c) no file under `include/mc/` includes anything from `src/`; (d) no file under `include/mc/device` or `src/device` includes `mc/mock/…`, and the `LINK_LIBRARIES` property of `mc_device` does not contain `mc_mock` |
| BLD-06 | Consumer smoke, CMake | `tests/consumer_cmake` is configured as its **own** project pointing at the library folder; links `mc::device` and `mc::core`; its build must not create any library test target |
| BLD-07 | Consumer smoke, qmake | `tests/consumer_qmake/app.pro` includes `mc_protocol.pri` and links; builds and runs |
| BLD-08 | Version agrees | A doctest case asserts `mc::version()` equals the `MC_VERSION_*` macros, and `check.*` asserts the CMake `PROJECT_VERSION` equals `MC_VERSION_STRING` |
| BLD-09 | Warnings clean | Top-level build with `MC_WARNINGS_AS_ERRORS=ON` on MSVC and on GCC or Clang |

No code-coverage target for this module; coverage applies to the core modules.

## Boundaries

**Always**

- Run `scripts/check.*` before tagging a version or handing the folder to a consumer.
- Keep every `.pri` and the CMake lists in step in the same change (BLD-04 enforces it).
- Keep `include/mc/` the only public surface; a new public header goes there and nowhere else.
- Put build output under `build/` only.

**Ask first**

- Adding or updating any third-party code, including a new doctest version.
- Raising the minimum CMake, Qt or C++ standard.
- Adding a shared-library build, export macros, or an install/`find_package` target.
- Changing the public directory layout under `include/mc/`.
- Creating `.gitignore` or running any `git add` / `git commit`: the owner decides the ignore list first (capability-map assumption 3).

**Never**

- Include a Qt header from `include/mc/core`, `include/mc/mock`, `src/core` or `src/mock`.
- Let a `.pri` set `TARGET`, `TEMPLATE` or `DESTDIR`.
- Reference `reference_source/` from any build file; it is study material, not a source tree.
- Build artefacts outside `build/`.

## Success Criteria

1. From a clean checkout, `scripts/check.ps1 -QtDir <dir>` exits 0 on Windows with MSVC; `scripts/check.sh <dir>` exits 0 in Git Bash with MinGW GCC; Linux is not verified in v1 (owner decision 2026-09-27, plan decision 1).
2. A CMake project outside this folder builds against it with exactly two lines (`add_subdirectory`, `target_link_libraries`) and produces no library test or example target.
3. A qmake project outside this folder builds against it with exactly one `include(...)` line.
4. Configuring with `-DMC_BUILD_DEVICE=OFF` on a machine without Qt succeeds and builds `mc::core`, `mc::mock` and their tests.
5. Deleting one source file's entry from a `.pri` makes BLD-04 fail; adding `#include <QString>` to a core header makes BLD-05 fail.
6. `mc::version()` and `MC_VERSION_STRING` agree, and `CHANGELOG.md` has an `Unreleased` section.
7. `README.md` documents the three consumption paths and the version policy in under one screen.

## Open Questions

1. **Licence.** The folder will be copied into other projects, so a `LICENSE` file should exist even for internal use. Proposal: MIT with the owner as copyright holder. *Decision:* as assumed.
2. **`.gitignore` proposal** for the owner to accept or edit before `git add`: `build/`, `*.user`, `*.pro.user*`, `.vs/`, `.idea/`, `.cache/`, `CMakeUserPresets.json`, `compile_commands.json`, `*.autosave`. `.vscode/` is **not** proposed for ignoring; the owner may want shared settings there. *Decision:* add all suggesstion to gitignore, and confirm with owner again before git add. **Amended 2026-09-26 (hil-capture decision 3):** also `tests/hil/profiles/*.json` with the exception `!tests/hil/profiles/*.example.json` (real bench profiles hold IP addresses and COM port names). **Final list approved 2026-09-26:** the repository's `.gitignore`, which additionally ignores `reference_source/`, the whole `.claude/` folder and `CLAUDE.local.md`; `.vscode/` and `docs/mc_reference/Mc-protocol.pdf` stay versioned.
3. **Test binaries by default when top-level.** Proposed `MC_BUILD_TESTS=ON` when top-level so a fresh clone builds tests without extra flags. *Decision:* as assumed
4. **Root qmake project name** `mc_protocol.pro` (dev-only `subdirs`). *Decision:* as assumed.
