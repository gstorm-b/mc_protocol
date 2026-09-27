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
- `mc/core/protocol.h`: `McProtocol` and `Parser` (`ParseStatus`), the codec facade that turns a
  validated `Request` into one complete frame and a response frame back into a normalized payload
  or an `Error` — the public contract `core-session` (Phase 3) builds on. 3E is fully implemented,
  Binary and ASCII (request envelope, response parsing, error mapping per spec §5.1/§7.2);
  `Parser` is a fixed-size, trivially copyable value type (`sizeof(Parser) == 96`); every other
  frame family/wire code not implemented yet reports `ErrorCode::UnsupportedCommand` rather than
  undefined behaviour. Golden-vector round trips: every Appendix A.1/A.2 vector (3E Binary/ASCII);
  A.3/A.4 (4E) transcribed and tagged `v2`, skipped. Streaming: byte-at-a-time and coalesced-frame
  parsing, `reset()` recovery (STR-01, STR-02, STR-04); zero allocations across encode, `feed()`
  and `payload()` (ALC-01).
- Supporting internal layers under `src/core/protocol/` (`mc::detail`, not part of the public
  surface): hex-ASCII and 8-bit sum-check primitives (`hexascii.h`, `sumcheck.h`); the
  `AsciiCodec`/`BinaryCodec` field codec (`field_codec.h`) sharing one code path per field kind,
  with no ASCII/Binary branch outside device encoding; on-wire device address encoding for all
  eight device families (`device_encode.h`, DEV-07 wire half); the QnA `0401`/`1401` batch
  read/write command layer (`command_qna.h`), shared unchanged by 3E and (later) 3C. Golden
  vectors: `prim.vec`, `cmd.vec`, `cmdd.vec`, `3e_binary.vec`, `3e_ascii.vec`, `4e_binary.vec`,
  `4e_ascii.vec`, each self-checked against the reference spec's own bytes before being typed in.
  The shared `.vec` loader (`tests/common/vectors.h`/`.cpp`) and its own transcription guard
  (VEC-02: every vector's hex byte count matches its own `bytes:` heading) are reused by every
  test file above and will be by `tests/mock` and `tests/replay` later.
