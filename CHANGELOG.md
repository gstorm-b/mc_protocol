# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- CMake build skeleton: `mc::core` static library, the `MC_BUILD_*` option set, warning and
  version helpers, vendored doctest, and the first test binary (BLD-01, BLD-08, BLD-09).
- qmake mirror: `mc_core.pri`, `mc_device.pri`, `mc_mock.pri`, `mc_protocol.pri`, and the
  dev-only `mc_protocol.pro` (BLD-03).
- Build guard tests: `pri_sync` (BLD-04) and `include_hygiene` (BLD-05), plus the CMake and
  qmake consumer smoke projects (BLD-06, BLD-07).
- `scripts/check.ps1` and `scripts/check.sh`: one command that builds and tests both build
  systems, both compilers, and the consumer smoke projects (BLD-01 through BLD-09).
- `.clang-format`, this changelog, `README.md` and `LICENSE`.
- `mc/core/types.h` (`ByteView`, `MutableByteView`, `ByteBuf`, `kNoCode`) and `mc/core/result.h`
  (`ErrorCategory`, `ErrorCode`, `ErrorInfo`, `Error`, `Expected<T>`, `Expected<void>`), the first
  headers of `core-model`; test binary `mc_core_model_tests` (RES-01 through RES-04).
