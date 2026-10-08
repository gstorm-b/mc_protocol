# mc_protocol

A C++17 / Qt static library implementing the Mitsubishi MC protocol (3E/1E/3C/1C frames): a
sans-I/O `Session` engine, a mock PLC for testing without hardware, and a Qt-based device layer
(serial + TCP). See `docs/spec/` for the full module specs.

## Consuming this library

Copy this folder into your project (e.g. `third_party/mc_protocol/`) and consume it one of
three ways. Nothing inside the folder reaches outside it, so replacing it later is atomic.

**CMake**

```cmake
add_subdirectory(third_party/mc_protocol)
target_link_libraries(my_app PRIVATE mc::device)   # or mc::core for a non-Qt program
```

**qmake**

```qmake
include($$PWD/third_party/mc_protocol/mc_protocol.pri)   # core + device
# or: include($$PWD/third_party/mc_protocol/mc_core.pri) # core only, no Qt modules added
```

**No build system:** copy the folder over the old one; both include paths above still work.

## Version policy

`include/mc/version.h` is the single source of truth (`MC_VERSION_*` macros, `mc::version()`).
Semantic versioning: releases are git tags `vX.Y.Z` on the commit that bumps the header and
moves `CHANGELOG.md`'s `Unreleased` section. Once `v1.0.0` is tagged, any non-additive change
to a header under `include/mc/` bumps `major`.

## Verified platforms

v1 is verified on **Windows only**: MSVC and MinGW GCC 13.1 against Qt 6.11.1, and MSVC against
Qt 5.15.0 (`msvc2019_64` kit; no Qt 5 MinGW kit is verified). Linux (GCC/Clang) is not verified.
The library builds with **Qt 5.15, or Qt 6.2 and later**: its Qt usage stays within the API
surface the two have in common. The MC Workbench GUI (`tools/mc_workbench`) needs Qt 5.15, or
Qt 6.5 and later (the minimums of its property browser, qpb 1.7.0), and the docking library built
for the same Qt major: `qtadvanceddocking-qt5` for Qt 5 (`scripts/build-ads.ps1 -QtDir <Qt 5
kit>`; CMake `MC_ADS_DIR`, qmake `MC_ADS_DIR_QT5` in `mc_local.pri`), `qtadvanceddocking-qt6` for
Qt 6. With an older Qt, or without that docking library, both build systems skip it with a
message.

CMake uses the Qt that `CMAKE_PREFIX_PATH` points at, Qt 6 first when a prefix holds both. On a
machine with both majors, `-DMC_QT_MAJOR=5` (or `6`) forces one; switching the major of an
existing build folder needs a fresh configure. qmake uses the Qt of the `qmake` that runs.

Qt 5.15 with MSVC needs a toolset older than 14.50: the Qt 5.15 headers use the `stdext` checked
iterators, which the VS 2026 (14.50) standard library removed. Load an older toolset of the same
Visual Studio before configuring, e.g. `. scripts/vsdev.ps1 -VcVarsVer 14.44` (the verified one);
`scripts/check.ps1` does this by itself for a Qt 5 kit. CMake and qmake warn when a Qt 5 build
meets a newer `cl`.

## Building this repository

`scripts/check.ps1 -QtDir <kit>` (PowerShell) or `scripts/check.sh <kit>` (Git Bash, MinGW
only) builds and tests everything: full CMake+Qt, CMake core-only (no Qt), qmake, and both
consumer smoke projects. See `agent-team/project/build-env.md` for the individual commands.
