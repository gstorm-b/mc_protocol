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
- `mc/core/device.h`: the device model (`DeviceKind`, `Radix`, `DeviceType`, `DeviceInfo`,
  `deviceInfo()`, `Device`, `parseDevice()`, `formatDevice()`) with the full spec §3.2 device
  table (DEV-01 through DEV-07-data, DEV-14).
- `mc/core/frame_config.h` (`FrameType`, `DataCode`, `SerialFormat`, `PlcSeries`, `TargetFamily`,
  `C1CommandSet`, `FrameConfig` with its named constructors, `validate()`, `effectiveTimeoutMs()`,
  `isSerial()`) and `mc/core/request.h` (`Op`, `BitLayout`, `Request` with its builders,
  `validate(Request, FrameConfig)`) (CFG-01 through CFG-05, DEV-08 through DEV-13).
- `mc/core/limits.h`: the spec §4.4 point-limit table transcription (`maxPoints()`) and request
  splitting into commands that fit it (`Chunk`, `chunkCount()`, `chunk()`) per spec §8.5
  (LIM-01, LIM-02, CHK-01 through CHK-06). The random-access rows of spec §4.4 ("0403", "1402")
  are transcribed too, as internal (non-public) data in `src/core/model/limits_table.h`/`.cpp`,
  for v1.1 (LIM-03).
- `mc/core/convert.h` (namespace `mc::convert`): word/dword/float32/float64/string accessors and
  writers, `packBits`/`unpackBits`, `wordsToBits`/`bitsToWords`, and the owning `from*` builders,
  all bounds-checked (spec §2.3, §2.4, §8.7; CNV-01 through CNV-08).
- `mc/core/log.h`: `LogLevel`, `LogSink`, `NullLogSink`, `hexDump()` (spec §8.6; LOG-01, LOG-02).
  `tests/common/alloc_counter.h`: a shared counting override of global `operator new`/`delete`
  for every zero-allocation test; ALC-01 proves zero allocations across `parseDevice`,
  `validate`, `chunk`, `convert::float64At`, `hexDump` and `formatDevice`.
- Checkpoint A: `MC_COVERAGE` implemented (`cmake/mc_coverage.cmake`, GCC/Clang only, no effect
  on MSVC) and `mc_coverage_report` (`cmake/mc_coverage_report.cmake`), a `gcov`-based report of
  `src/core/model`'s line coverage per file and in total (no Python on this machine, so no
  `gcovr`). `core-model` verified on MinGW GCC 13.1 for the first time this phase, with coverage
  ≥ 95 %.
