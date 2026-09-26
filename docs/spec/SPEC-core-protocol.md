# Spec: core-protocol

- **Module id:** `core-protocol` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** `core-model`
- **Depended on by:** `core-session`, `mock-plc`
- **Inputs:** reference spec `docs/mc_reference/mc-protocol-frame-spec.md` §2, §3.3, §4.1–4.4, §5.1, §5.3, §5.5, §5.6, §6.3, §7, §8.2, §9.2, §9.4–9.8, Appendix A; ideas doc §4.2 and §9.3; capability-map assumptions 5, 8, 10; `SPEC-core-model.md`.

## Objective

Turn a validated `Request` into the exact bytes of one complete frame, and turn the bytes of one response frame into a **normalized payload** or an `Error`, for frames **3E, 1E, 3C and 1C** in both data codes and serial formats 1–4. No I/O, no timers, no knowledge of polling. Every byte this module produces or accepts is checked against the golden vectors of the reference spec; that is the module's definition of correct.

This is the `mc::McProtocol` class from the intent document, plus its `Parser`.

**User stories**

- As `core-session`, I pre-encode every polling request once into my own buffer, and per response I hand the parser my receive buffer and get the decoded values written straight into my value store, with no allocation in between.
- As a non-Qt integrator writing my own transport, I use `McProtocol` and `Parser` alone: bytes in, bytes out, and I never see a frame-specific type.
- As a maintainer, adding 4E or 4C later means adding a frame envelope and vectors, not touching command encoding, because the QnA command layer is shared by 3E and 3C already.
- As a maintainer, a wrong byte anywhere is caught by a vector test that names the vector ID.

## Tech Stack

C++17, standard library only, doctest. Same rules as `core-model`: `noexcept` public functions, no exceptions across the boundary, no Qt.

## Commands

```powershell
cmake --build build/cmake-debug --target mc_core_protocol_tests
ctest --test-dir build/cmake-debug -R core_protocol --output-on-failure
```

## Project Structure

```text
include/mc/core/
└── protocol.h            McProtocol, Parser, ParseStatus
src/core/protocol/
├── hexascii.h/.cpp       ASCII hex encode (upper-case) / decode (accepts lower-case)
├── sumcheck.h/.cpp       8-bit sum, 2-character ASCII hex
├── field_codec.h         FieldCodec concept: AsciiCodec, BinaryCodec (u8/u16/u32, fixed, bits, words, sizes, reader)
├── device_encode.h/.cpp  qnaDevice(), e1Device(), c1Device() per spec §3.3
├── command_qna.h/.cpp    0401 / 1401 request data + response decoding (0403 / 1402 tables present, not exposed in v1)
├── command_a1e.h/.cpp    00H–03H (04H/05H tables present, not exposed in v1)
├── command_a1c.h/.cpp    BR/JR, WR/QR, BW/JW, WW/QW (BT/JT, WT/QT tables present, not exposed in v1)
├── frame_3e.h/.cpp       3E envelope + response parse (spec §5.1)
├── frame_1e.h/.cpp       1E envelope + response parse (spec §5.3)
├── frame_serial.h/.cpp   3C and 1C envelopes, formats 1–4 (spec §5.5, §5.6)
├── serial_parser.h/.cpp  incremental receive state machine, formats 1–4 (spec §6.3)
└── protocol.cpp          McProtocol dispatch by FrameType, Parser state union
tests/core/protocol/
├── test_primitives.cpp   PRIM-xx
├── test_device_encode.cpp DEV-07 (wire half)
├── test_commands.cpp     CMD-xx, CMDD-xx
├── test_frame_3e.cpp     3E-xx + A.1, A.2
├── test_frame_1e.cpp     1E-xx + A.5, A.6
├── test_frame_serial.cpp 3C-xx, 1C-xx + A.12–A.19
├── test_parser_stream.cpp byte-at-a-time, coalesced frames, junk before STX
└── test_alloc.cpp
tests/common/
└── vectors.h/.cpp        loader for tests/vectors/**/*.vec, shared with tests/mock (owned here)
tests/vectors/
├── 3e_binary.vec, 3e_ascii.vec, 1e_binary.vec, 1e_ascii.vec
├── 3c_f1.vec … 3c_f4.vec, 1c_f1.vec … 1c_f4.vec
└── cmd.vec, cmdd.vec, prim.vec
```

## Public API

### `protocol.h`

```cpp
namespace mc {

enum class ParseStatus : uint8_t { NeedMore, Done, Failed };

/// Incremental response parser for exactly one request. A small value type (fixed size, no
/// allocation) that keeps a cursor over a buffer THE CALLER OWNS. The caller appends received
/// bytes to its buffer and calls feed() with the whole buffer each time; the parser examines only
/// the bytes past its cursor. Bytes already fed must not change between calls.
class Parser {
public:
    /// Advances over new bytes of `buffer`. O(k) in the new bytes.
    ///   NeedMore: keep receiving.
    ///   Done:     frameLength() bytes of `buffer` form this frame; the rest belong to the next one.
    ///   Failed:   error() is set. frameLength() is the number of bytes to discard (the whole
    ///             malformed frame when its end was found, otherwise everything examined).
    ParseStatus feed(ByteView buffer) noexcept;

    /// Valid after Done or Failed. Bytes of the buffer consumed by this frame, including any
    /// junk skipped before STX/ACK/NAK on serial frames.
    size_t frameLength() const noexcept;
    /// Bytes skipped before the frame start (serial only; 0 for Ethernet). The caller logs it.
    size_t skipped() const noexcept;

    /// After Done: decodes the response data into `out` in the normalized layout of the
    /// request (see "Payload contract"). `buffer` must be the same bytes that produced Done.
    /// Returns bytes written or BufferTooSmall. O(n), no allocation.
    Expected<size_t> payload(ByteView buffer, MutableByteView out) const noexcept;

    /// After Failed: a Protocol or Plc error (see "Error mapping").
    const Error& error() const noexcept;

    /// Back to the initial state for the same request.
    void reset() noexcept;
};

/// Stateless codec for one FrameConfig. Copyable; cheap to construct.
class McProtocol {
public:
    /// Precondition: cfg.validate() is Ok. A config that fails validate() makes every encode()
    /// return its Config error rather than producing bytes.
    explicit McProtocol(const FrameConfig& cfg) noexcept;
    const FrameConfig& config() const noexcept;

    /// Wire size of the complete request frame for r. Runs validate(r, cfg). O(1).
    Expected<size_t> encodedSize(const Request& r) const noexcept;

    /// Encodes r as ONE complete frame into out (no chunking; the caller chunks with
    /// mc::chunk()). Returns bytes written, or BufferTooSmall, or the validate() error.
    /// O(n), no allocation.
    Expected<size_t> encode(const Request& r, MutableByteView out) const noexcept;

    /// Convenience: one ByteBuf allocation.
    Expected<ByteBuf> encode(const Request& r) const;

    /// Upper bound of the wire size of any successful or error response to r, so a caller can
    /// size its receive buffer once. O(1).
    size_t maxResponseSize(const Request& r) const noexcept;

    /// Size of the normalized payload of a successful response to r. O(1).
    size_t payloadSize(const Request& r) const noexcept;

    /// A parser for the response to r, bound to this config. O(1), no allocation.
    Parser parser(const Request& r) const noexcept;
};

}  // namespace mc
```

**Why a cursor over a caller-owned buffer.** The parser cannot own storage without allocating, and the normalized payload is not a slice of the wire bytes (binary bit data is nibble-packed, ASCII data is hex text), so a decode step exists anyway. Making `payload()` decode straight into the caller's destination lets `core-session` write into its value store with one pass and zero copies of its own. `frameLength()` is what makes coalesced TCP frames work (spec §6.1, TRN-02).

### Payload contract (capability-map assumption 5, binding for the major version)

| Request op | Response payload written by `payload()` |
|---|---|
| `ReadBits`, `BitLayout::BytePerPoint` | `count` bytes, one per point in device order, value 0 or 1 |
| `ReadBits`, `BitLayout::PackedLsbFirst` | `ceil(count / 8)` bytes, point head+i at bit i mod 8 of byte i div 8; unused high bits 0 |
| `ReadWords` (any device kind) | `2 × count` bytes, little-endian words in device order; on a bit device word k holds points head+16k … head+16k+15 (spec §2.4) |
| `WriteBits`, `WriteWords` | 0 bytes |

`Request::data` for writes uses the same layouts. `payloadSize()` returns these sizes.

## Encoding rules this module implements

Every rule below cites the reference spec; the implementation must not "improve" on it.

### Field codecs (spec §2.1, §2.1.1)

- `AsciiCodec`: u8 → 2 hex chars, u16 → 4, u32 → 8, upper-case on encode (PRIM-02), lower-case accepted on decode (PRIM-05), non-hex → `Protocol/InvalidCharacter` (PRIM-06). Value wider than the field → `Encode/PointCount` for point counts, `Encode/InvalidDevice` for device numbers (PRIM-04).
- `BinaryCodec`: little-endian, byte-wise, no `reinterpret_cast` (assumption 8).
- Fixed byte strings (E1): subheaders `50 00`/`"5000"`, `D0 00`/`"D000"`; never encoded as u16.
- Command and subcommand are u16: `"0401"` / `01 04`.

### Device encoding (spec §3.3, table row per family)

| Family | Layout | Number width |
|---|---|---|
| QnA ASCII Q/L | code(2) + number(6, device radix) | 6 |
| QnA ASCII iQ-R | code(4) + number(8, device radix) | 8 |
| QnA Binary Q/L | number LE3 + code(1) | ≤ 0xFFFFFF |
| QnA Binary iQ-R | number LE4 + code LE2 | ≤ 0xFFFFFFFF |
| 1E ASCII | code hex(4) + number **hex**(8), regardless of device radix (E4) | 8 |
| 1E Binary | number LE4 + code LE2 | ≤ 0xFFFFFFFF |
| 1C ACPU | code(1–2) + number(4, T/C: 3), device radix | 4 / 3 |
| 1C AnA/AnU | code(1–2) + number(6, T/C: 5), device radix | 6 / 5 |

The `*` in QnA ASCII codes is emitted as `*`, never space; leading zeros are `0`, never space (spec §3.2 notes). Width violations are reported by `core-model`'s `validate()`; this module trusts it and additionally asserts in debug builds.

### Commands

**QnA (3E, 3C)** — spec §4.1.1, §4.1.2. Subcommand from `cfg.series`: Q/L `0000`/`0001`, iQ-R `0002`/`0003`. Request data = command, subcommand, head device, points u16, [write data]. Response data: words 2N bytes / 4N chars; bits ⌈N/2⌉ bytes (nibble-packed, first point in the **high** nibble, odd count → low nibble of the last byte is 0, spec §2.2, Q4) / N chars. A nibble other than 0 or 1 on decode → `Protocol/InvalidCharacter` (PRIM-13, CMDD-18).

**1E** — spec §4.2. Command code goes into the frame subheader, not the request data. Request data = head device, points u8 (256 → `00`, spec E8), fixed `00`, [write data]. Response size is computed from the request because 1E has no length field: bits ASCII N + (N mod 2) chars with a trailing dummy on odd N (1E-10), bits Binary ⌈N/2⌉, words 2N / 4N. Writes with odd N in Binary pad the last nibble with 0 (1E-11); ASCII writes send exactly N characters (spec Q3).

**1C** — spec §4.3. Request data = command(2) + message wait(1 hex char, spec E7) + character area. Command pair from `cfg.commandSet`: ACPU `BR/WR/BW/WW`, AnA `JR/QR/JW/QW`. Points 2 chars, 256 → `"00"`. Bit data `'0'`/`'1'`; words 4 hex chars. Always ASCII.

Random-access and test commands (0403, 1402, 1E 04H/05H, 1C BT/WT) have their tables and encoders present in the source (so the CMD-11…14, 22…24, 31, 32, 35…37 vectors can be enabled without restructuring) but are **not reachable** through `Op` in v1 (decision 2).

### Frame 3E (spec §5.1)

- Request: subheader, network, pc, io LE2, station, **request data length** = wire bytes of (monitoring timer + request data), monitoring timer, request data. Header 9 bytes / 18 chars.
- Response parse: read the header; subheader must be `D0 00`/`"D000"` else `Protocol/FrameMismatch` (3E-08); `checkRoute` compares the 5-byte route with the request (default off, spec Q5); read L = response data length; L < end-code size → `Protocol/LengthMismatch` (3E-09); end code 0 → payload size must equal `payloadSize()` else `LengthMismatch` (3E-10); end code ≠ 0 → `Plc` error with `plcCode` = end code and `ErrorInfo` filled when the remaining bytes are ≥ 9 / 18 (3E-07), otherwise left zero (3E-11).
- `maxResponseSize` = header + L_max where L_max = end code + max(payload wire size, error information size).

### Frame 1E (spec §5.3)

- Request: subheader = command code (`00`–`03` in v1), pc, monitoring timer LE2 / 4 chars, request data. Default timer `000A` comes from `FrameConfig::frame1E()`.
- Response parse: subheader must equal command | 0x80 else `FrameMismatch` (1E-07); end code `00` → read exactly the computed response size (1E-03); end code `5B` → read one more byte / 2 chars as `abnormalCode` and fail with `Plc` (1E-05); any other end code → fail with `Plc` after exactly 2 bytes / 4 chars, **never waiting for more** (1E-06).

### Frames 3C and 1C, formats 1–4 (spec §5.4 table, §5.5, §5.6, §2.5, §2.7)

`P` is the frame ID plus access route: 3C `"F9"` + station(2) network(2) pc(2) self(2); 1C no frame ID, station(2) pc(2). `RD` is the request data; `SUM` present when `cfg.sumCheck`; `BLK` = `cfg.blockNo` for format 2.

| Format | Request | Response with data | Response without data | Error |
|---|---|---|---|---|
| 1 | `ENQ P RD SUM` | `STX P data ETX SUM` | `ACK P` | `NAK P err` |
| 2 | `ENQ BLK P RD SUM` | `STX BLK P data ETX SUM` | `ACK BLK P` | `NAK BLK P err` |
| 3 | `STX P RD ETX SUM` | `STX P "QACK" data ETX SUM` | `STX P "QACK" ETX` | `STX P "QNAK" err ETX` |
| 4 | as 1 + `CR LF` | as 1 + `CR LF` | as 1 + `CR LF` | as 1 + `CR LF` |

For 1C, format 3 uses `"GG"` / `"NN"` in place of `"QACK"` / `"QNAK"`, and `err` is 2 characters (3C: 4).

Sum check ranges (spec §2.5): F1/F4 request from after ENQ to the end of RD, response from after STX **including** ETX; F2 from BLK; F3 from after STX including ETX. ACK/NAK responses carry no SUM. F3 short responses (`QACK ETX`, `QNAK err ETX`) carry no SUM unless `cfg.f3ShortResponseHasSum` (spec Q1). A wrong SUM → `Protocol/SumCheck` (4C-09 applied to 3C/1C). With `cfg.sumCheck == false` neither side carries SUM (4C-10).

Parse checks (spec §5.4 "Checks when parsing"): frame ID (3C) → `FrameMismatch`; route equals the request's when `cfg.checkRoute` (default **on** for serial, 4C-13) → `FrameMismatch`; BLK equals `cfg.blockNo` when `cfg.checkBlockNo` (4C-14) → `FrameMismatch`; F3 end code is one of the two expected strings → `FrameMismatch`; data size equals `payloadSize()` → `LengthMismatch`. Junk before STX/ACK/NAK is skipped and reported through `skipped()` (4C-15). ETX cannot appear inside ASCII data, so the body scan is a plain search for `03H` over new bytes only.

Format 5 and 4C are rejected by `FrameConfig::validate()` in v1 (3C-03); `hexascii` and `sumcheck` are written so the DLE layer can be added beside them for 4C later without touching them.

### Error mapping (spec §7.2)

| Frame | Condition | `Error` |
|---|---|---|
| 3E | end code ≠ 0 | `{Plc, PlcError, plcCode = end code, info = route+cmd+sub if present}` |
| 1E | end code ≠ 00 | `{Plc, PlcError, plcCode = end code (u8), abnormalCode when 5BH}` |
| 3C F1/F2/F4 | NAK | `{Plc, PlcError, plcCode = err4}` |
| 3C F3 | QNAK | same |
| 1C F1/F2/F4 | NAK | `{Plc, PlcError, plcCode = err2 (u8)}` |
| 1C F3 | NN | same |
| any | subheader / frame ID / route / block / F3 code wrong | `{Protocol, FrameMismatch}` |
| any | length or size disagreement | `{Protocol, LengthMismatch}` |
| serial | sum check wrong | `{Protocol, SumCheck}` |
| ASCII / nibble | invalid character or nibble | `{Protocol, InvalidCharacter}` |

`Error::message` is one of a fixed set of static strings; the offending bytes go to the caller's `LogSink` at `Trace` by `core-session`, never into the error.

## Internal design

- **No virtual dispatch on the hot path.** `McProtocol::encode` and `Parser::feed` switch on `cfg.frame` (an enum) into free functions per frame; `Parser` is a fixed-size struct holding `FrameType`, a state enum, the cursor, the expected sizes and route bytes, and the `Error`. `static_assert(sizeof(Parser) <= 128)`.
- **One QnA command implementation** used by 3E and 3C through the field codec (ASCII or Binary); no ASCII/Binary branches inside command code except device encoding (spec §8.2).
- **Sizes are computed, never discovered.** `encodedSize`, `payloadSize` and `maxResponseSize` are closed-form from `FrameConfig` and `Request`, so `core-session` can size buffers at subscribe time.
- Every table (subcommands, command codes per family, header sizes) is a `constexpr` array indexed by enum, mirroring `core-model`.

## Complexity and allocation (binding, from ideas §9.3)

| Function | Time | Allocation |
|---|---|---|
| `McProtocol` ctor, `config` | O(1) | none |
| `encodedSize`, `payloadSize`, `maxResponseSize` | O(1) | none |
| `encode(r, out)` | O(n) in points | none |
| `encode(r)` | O(n) | one `ByteBuf` |
| `parser` | O(1) | none |
| `Parser::feed` | O(k) in bytes newly examined; total O(L) per frame | none |
| `Parser::payload` | O(n) | none |
| `Parser::reset`, `error`, `frameLength`, `skipped` | O(1) | none |

`test_alloc.cpp` asserts zero calls to `operator new` across encode-into-buffer, a full byte-at-a-time parse, and `payload()` for every vector family.

## Golden vector files

Stored as plain text in the Appendix A notation so no JSON dependency is needed in v1 (open question 1):

```text
# id: V-3E-B-01
# frame: 3E  code: Binary  op: ReadWords  device: D100  count: 3
# kind: request
50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00

# id: V-3E-B-02
# kind: response  of: V-3E-B-01  expect: words 1995 1202 1130
D0 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11
```

The loader (`tests/common/vectors.h`, shared with `mock-plc`) accepts `#`-prefixed `key: value` metadata lines, one hex line per vector, and `<STX>`-style names in ASCII vectors. It exposes every vector's metadata as data (`id`, `frame`, `code`, `format`, `op`, `device`, `count`, `kind`, `of`, `expect`, write data, tags), not only the hex line, so the mock can run the same files in the reverse direction. Every vector of Appendix A sections A.1, A.2, A.5, A.6, A.12–A.19 is transcribed; A.3, A.4, A.7–A.11 (4E, 4C) are transcribed too but tagged `v2` and skipped by the runner, so enabling them later is a tag change. CMD-xx, CMDD-xx and PRIM-xx tables are transcribed the same way.

## Code Style

Inherited from `SPEC-build-packaging.md`. Frame code reads like the spec's tables:

```cpp
// src/core/protocol/frame_3e.cpp
namespace mc::detail {
constexpr uint8_t kSub3eReqBin[] = {0x50, 0x00};
constexpr char    kSub3eReqAsc[] = "5000";
constexpr size_t  kHeader3eBin = 9;     // subheader 2 + route 5 + length 2
constexpr size_t  kHeader3eAsc = 18;

/// Wire size of a 3E request for `rd` bytes of request data (spec §5.1 table, fields 1–8).
constexpr size_t frame3eRequestSize(DataCode code, size_t rd) noexcept {
    return code == DataCode::Binary ? kHeader3eBin + 2 + rd : kHeader3eAsc + 4 + rd;
}
}
```

## Testing Strategy

doctest binary `mc_core_protocol_tests`, label `core_protocol`. Vector-driven tests iterate the `.vec` files and name the vector ID in every assertion. Table below lists what is enabled in v1; IDs for random-access commands, 4E and 4C are transcribed and tagged `v1.1` or `v2`.

| ID | Covers | Notes |
|---|---|---|
| PRIM-01 … 09, 12 … 17 | hex/LE fields, upper/lower case, overflow, invalid char, sum check (1C manual example, wrap), nibble packing incl. invalid nibble, ASCII bits, dword, property round trip | PRIM-10, 11, 18 (DLE) → v2 |
| DEV-07 | Every cell of spec §3.3's example table (D100, X1F, TN10, M1234, M9000 × 8 families) | wire half; data half is in core-model |
| CMD-01 … 10 | QnA 0401/1401 request data, Binary and ASCII | byte-for-byte |
| CMD-15 … 21 | 1E 00H–03H request data | |
| CMD-25 … 30, 33 | 1C BR/JR, WR/QR, BW, WW request data; manual sum check | CMD-31, 32 (WT/BT) → v1.1 |
| CMDD-01 … 06, 09 … 18 | response data decoding incl. odd bit counts, 1E dummy char, wrong length, invalid nibble | CMDD-07, 08 → v1.1 |
| 3E-01 … 13 | full 3E encode/parse, both codes, length fields, error with and without info, wrong subheader, short length, wrong payload size, other station, multiple CPU | 3E-14 → v1.1 |
| 1E-01 … 11, 13, 14 | full 1E encode/parse, 5BH + abnormal, error without abnormal stops after 2 bytes, 256 points, 257 points, odd bit read dummy, odd bit write padding, other PC No., alignment | 1E-12 → v1.1 |
| 3C-01 … 03 | F1–F4 encode/parse, NAK/QNAK, format 5 rejected | plus 4C-09, 4C-10, 4C-13, 4C-14, 4C-15 re-applied to 3C vectors |
| 1C-01 … 08 | F1–F4, 2-char NAK, NN, AnA command set, message wait, 256 points, WR on bit device, manual sum check | 1C-09 → v1.1 |
| STR-01 | Every response vector fed one byte at a time reaches Done with the same payload (TRN-01 generalized) | |
| STR-02 | Two response frames in one buffer: first parses, `frameLength()` points at the second (TRN-02 with 3E and with 1E) | |
| STR-03 | Junk bytes before STX on 3C/1C: parses, `skipped()` equals the junk length | |
| STR-04 | A frame cut short then `reset()`: the parser recovers | |
| SZ-01 | For every request vector `encodedSize()` equals the vector length; for every response vector the length ≤ `maxResponseSize()` and the payload length equals `payloadSize()` | |
| ALC-01 | Zero allocations for encode-into-buffer, feed, payload across all families | |

Coverage: with `MC_COVERAGE=ON`, line coverage of `src/core/protocol` ≥ 95 %; every `Error` mapping row above has a test.

## Boundaries

**Always**

- Derive every byte from the reference spec and its vectors; when `reference_source/` differs, the spec wins and the difference is listed under "Divergences" (assumption 10).
- Keep command encoding free of ASCII/Binary branches except device encoding.
- Keep `Parser` fixed-size and allocation-free.

**Ask first**

- Exposing any command beyond batch read/write through `Op`.
- Changing the payload contract or the `.vec` format.
- Adding a JSON or other parsing dependency for vectors.

**Never**

- Perform I/O, sleep, or read a clock.
- Redirect a bit read into a word read inside the codec (the old module did; it is a `core-session` policy now).
- Accept a `Request` that `validate()` rejects.

## Divergences from the old module (recorded, intentional)

- Old `Frame3E` silently turned an X/Y/M bit read of more than 8 points into a word read and unpacked it; that policy moves to `core-session` and is configurable there.
- Old codecs wrote decoded values into a device map owned by the context; the new parser writes into a caller buffer and knows nothing about maps.
- Old `Frame3E::parseReceiveFrame` treated fewer than 9 bytes as "waiting" without a frame-length notion; the new parser reports `frameLength()` so coalesced TCP frames work.
- Old 1C and 3C codecs were written from third-party reference programs and were never verified on hardware (old `AGENTS.md`); the new ones are derived from the reference spec and its vectors only. Any behaviour of the old codecs not backed by the spec is dropped.
- Old 3E device support was X/Y/M/D; the new encoder covers every symbol of the device table.

## Changes required in `core-model` (applied to `SPEC-core-model.md` on 2026-09-26)

1. `FrameConfig::frame3C()` and `frame1C()` set `checkRoute = true` (spec §8.3 gives serial frames `check_route = true` by default; Ethernet stays `false`).
2. `FrameConfig::validate()` rejects `format == Format5` for every frame in v1 and forces `code = Ascii` for 3C/1C (already listed there; restated because 3C-03 tests it here).

## Success Criteria

1. Every vector in A.1, A.2, A.5, A.6 and A.12–A.19 round-trips: request vectors are reproduced byte-for-byte by `encode()`, response vectors parse to the stated payload or `Error`.
2. All enabled PRIM, DEV-07, CMD, CMDD, 3E, 1E, 3C, 1C, STR, SZ and ALC tests pass on MSVC and on GCC or Clang.
3. `sizeof(Parser) <= 128` and `Parser` is trivially copyable.
4. The 4E and 4C vectors exist in `tests/vectors/` tagged `v2` and are skipped, not failing.
5. `include/mc/core/protocol.h` exposes only `McProtocol`, `Parser` and `ParseStatus`; no frame- or codec-specific type is public.

## Open Questions

1. **Vector file format.** Plain-text `.vec` in the Appendix A notation (proposed, no dependency) versus JSON as the reference spec suggests (needs a vendored parser). *Decision:* `.vec` plain text (2026-09-26); capability-map assumption 10 amended to match.
2. **`Parser::payload()` destination for `ReadWords` on a bit device.** Keep raw words (spec §8.4: "read_words on a bit device returns raw words") and let `core-session` call `convert::wordsToBits`, or offer a `BitLayout` option on word reads too. *Proposal:* raw words; conversion stays in `core-session`. *Decision:* raw words (2026-09-26).
3. **Debug-build assertions on `validate()` preconditions** (`assert` in `encode` that the request passed `validate()`), or always re-run `validate()` inside `encode()`. *Proposal:* always run it; it is O(1) and the cost is negligible next to the encode itself. *Decision:* always run `validate()` inside `encode()`, debug and release (2026-09-26).
4. **Lower-case hex on decode** is a SHOULD in the spec (PRIM-05). Accept it (proposed) or reject to be strict. *Decision:* accept lower-case on decode; encode stays upper-case (2026-09-26).
