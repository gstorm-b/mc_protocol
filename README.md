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

v1 is verified on **Windows only**: MSVC and MinGW GCC 13.1, both against Qt 6.11.1. Linux
(GCC/Clang) is not verified. The library's own Qt usage stays within the Qt 6.2 LTS API
surface, even though only Qt 6.11 is installed on the verification machine.

## Building this repository

`scripts/check.ps1 -QtDir <kit>` (PowerShell) or `scripts/check.sh <kit>` (Git Bash, MinGW
only) builds and tests everything: full CMake+Qt, CMake core-only (no Qt), qmake, and both
consumer smoke projects. See `agent-team/project/build-env.md` for the individual commands.
