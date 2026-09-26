# Spec: core-model

- **Module id:** `core-model` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** `build-packaging`
- **Depended on by:** `core-protocol`, `core-session`, `mock-plc`, `qt-device`
- **Inputs:** reference spec `docs/mc_reference/mc-protocol-frame-spec.md` §2.3, §2.4, §3, §4.4, §7, §8.3, §8.5, §8.7, §9.3; ideas doc §4.3 and §9.3; capability-map assumptions 4, 5, 7, 8.

## Objective

Provide the **pure data layer** every other module speaks in: what a device is, what a request is, what an error is, how a frame is configured, how many points a request may carry, and how to turn normalized payload bytes into application values. Nothing in this module knows how bytes look on the wire; that is `core-protocol`.

This module exists on its own so that `core-session` and the Qt config code can be written and tested against stable types **before** any codec exists, and so that the tables the whole library depends on (device codes, point limits) live in one place as data rather than as `if`/`else` scattered across frames, which is what the old module did with only X, Y, M and D.

**User stories**

- As `core-protocol`, I look up a device's code for a frame family in O(1) and never parse a string on the hot path.
- As `core-session`, I ask "how many points may this request carry on this frame?" and get one number from a table, then split the request into chunks without allocating.
- As an application, I write `mc::convert::float64At(payload, 0)` after reading four words and get a `double`.
- As an application, I plug my logger in through one small interface and pay nothing for log lines at levels I have disabled.
- As a maintainer, every row of the device table and the limits table is checked by a test against the reference spec.

## Tech Stack

C++17, standard library only. No exceptions cross the public boundary; every public function is `noexcept` unless it takes a `std::vector` by value. Test framework: doctest (vendored by `build-packaging`).

## Commands

Inherited from `build-packaging`. Module-specific:

```powershell
cmake --build build/cmake-debug --target mc_core_model_tests
ctest --test-dir build/cmake-debug -R core_model --output-on-failure
```

## Project Structure

```text
include/mc/core/
├── types.h          ByteView, MutableByteView, ByteBuf, kNoCode
├── result.h         ErrorCategory, ErrorCode, Error, Expected<T>
├── device.h         DeviceKind, Radix, DeviceType, DeviceInfo, deviceInfo(), Device, parseDevice(), formatDevice()
├── frame_config.h   FrameType, DataCode, SerialFormat, PlcSeries, TargetFamily, C1CommandSet, FrameConfig
├── request.h        Op, BitLayout, Request, validate()
├── limits.h         maxPoints(), Chunk, chunkCount(), chunk()
├── convert.h        namespace convert: word/dword/float/string/bit helpers
└── log.h            LogLevel, LogSink, NullLogSink, hexDump()
src/core/model/
├── device_table.cpp     the constexpr table (spec §3.2) and deviceInfo()
├── device_parse.cpp     parseDevice(), formatDevice()
├── frame_config.cpp     named constructors, validate(), effectiveTimeoutMs()
├── validate.cpp         validate(Request, FrameConfig)
├── limits_table.cpp     the constexpr limits table (spec §4.4) and maxPoints()
├── chunk.cpp            chunkCount(), chunk()
├── convert.cpp
└── log.cpp              NullLogSink, hexDump()
tests/core/model/
├── test_device.cpp      DEV-01..06, DEV-08..13
├── test_frame_config.cpp
├── test_limits.cpp
├── test_chunk.cpp
├── test_convert.cpp     PRIM-15, PRIM-16 (value side), §8.7 examples
├── test_result.cpp
├── test_log.cpp
└── test_alloc.cpp       zero-allocation checks with a counting global operator new
```

## Public API

### `types.h`

```cpp
namespace mc {
/// Non-owning view of bytes. The library never stores a view past the call it was passed to,
/// except where a function's documentation says so (Session::submit copies).
struct ByteView { const uint8_t* data{nullptr}; size_t size{0}; };
struct MutableByteView { uint8_t* data{nullptr}; size_t size{0}; };
using ByteBuf = std::vector<uint8_t>;      ///< Owning convenience type for callers that want one.
inline constexpr uint16_t kNoCode = 0xFFFF; ///< "This family has no code for this device".
}
```

### `result.h`

```cpp
namespace mc {
enum class ErrorCategory : uint8_t { None, Config, Encode, Transport, Protocol, Plc };
enum class ErrorCode : uint16_t {
    Ok = 0,
    // Config
    InvalidConfig, NotSubscribed,   // NotSubscribed: ValueStore / RangeSet (core-session)
    // Encode
    InvalidDevice, PointCount, UnsupportedCommand, DataSizeMismatch, BufferTooSmall,
    // Transport (raised by Session / device layer, defined here so every layer shares them)
    Timeout, LinkDown, QueueFull,
    // Protocol
    FrameMismatch, LengthMismatch, SumCheck, InvalidCharacter,
    // Plc
    PlcError
};
/// 3E/4E error information block, present only when frame is 3E/4E and the PLC answered
/// with a non-zero end code (spec §5.1, field 8b).
struct ErrorInfo { uint8_t network; uint8_t pc; uint16_t io; uint8_t station; uint16_t command; uint16_t subcommand; };

struct Error {
    ErrorCategory category{ErrorCategory::None};
    ErrorCode code{ErrorCode::Ok};
    uint16_t plcCode{0};        ///< Raw end code / error code from the PLC (spec §7.2); 0 unless category == Plc.
    uint8_t abnormalCode{0};    ///< 1E only: abnormal code when end code is 5BH.
    ErrorInfo info{};           ///< 3E/4E only.
    const char* message{""};    ///< Static, never allocated; safe to keep forever.
    constexpr bool ok() const noexcept { return code == ErrorCode::Ok; }
};

/// Minimal expected-or-error. Value semantics, no allocation of its own, no exceptions.
/// T may be move-only (core-session returns Expected<Session>); Expected is then move-only too.
template <class T> class Expected {
public:
    Expected(T v) noexcept;                 // implicit from value (moved in)
    Expected(Error e) noexcept;             // implicit from error
    bool hasValue() const noexcept; explicit operator bool() const noexcept;
    T& value() noexcept; const T& value() const noexcept;   // precondition: hasValue()
    const Error& error() const noexcept;                    // precondition: !hasValue()
};
template <> class Expected<void> { /* same surface without value() */ };
}
```

Detailed context (a hex dump, a route) never goes into `Error`; it goes to the `LogSink` at the point of failure. `Error` stays trivially copyable and small enough to pass by value.

### `device.h`

```cpp
namespace mc {
enum class DeviceKind : uint8_t { Bit, Word, DWord };
enum class Radix : uint8_t { Dec, Hex };
/// Every symbol in spec §3.2, in table order. `Count` is the table size.
enum class DeviceType : uint8_t {
    SM, SD, X, Y, M, L, F, V, B, D, W, TS, TC, TN, STS, STC, STN, CS, CC, CN, SB, SW, S, DX, DY, Z, R, ZR, RD,
    Count
};

/// One row of spec §3.2. All codes are stored as data; kNoCode / empty string mean "not supported".
struct DeviceInfo {
    DeviceType type;
    char symbol[4];        ///< "D", "TN", "STS"; NUL-terminated.
    DeviceKind kind;
    Radix radix;
    char qnaAsciiQL[3];    ///< "D*", "TN"; empty if unsupported.
    uint16_t qnaBinQL;     ///< 0xA8; kNoCode if unsupported.
    char qnaAsciiIqr[5];   ///< "D***", "STS*".
    uint16_t qnaBinIqr;    ///< 0x00A8.
    uint16_t e1Code;       ///< 0x4420; kNoCode if unsupported.
    char c1Code[3];        ///< "D", "TN"; empty if unsupported.
};

/// O(1): indexes the constexpr table by enum.
constexpr const DeviceInfo& deviceInfo(DeviceType t) noexcept;

struct Device {
    DeviceType type{DeviceType::D};
    uint32_t number{0};
};
/// Ordering by (type in table order, number); used by core-session to sort subscriptions. O(1).
constexpr bool operator==(const Device& a, const Device& b) noexcept;
constexpr bool operator!=(const Device& a, const Device& b) noexcept;
constexpr bool operator<(const Device& a, const Device& b) noexcept;

/// Parses "D100", "x1F", "TN10" (spec §3.4). Symbol match is case-insensitive and longest-first;
/// the number is parsed in the symbol's radix. "T10", "C5", "", "D", "100", "Q10" are errors.
/// Does NOT check field width for any frame family; that is validate().
/// Complexity O(len); no allocation.
Expected<Device> parseDevice(std::string_view text) noexcept;

/// Writes the canonical text ("D100", "X1F") into out; returns the number of characters that
/// would be written, like snprintf. Never allocates. Used for logs and by qt-device for tag names.
size_t formatDevice(const Device& d, char* out, size_t capacity) noexcept;
}
```

The table content is transcribed **verbatim** from spec §3.2 including footnotes: SM/SD have no 1E/1C code (footnote 1); L and S have no 1E code of their own (footnote 2, see `FrameConfig::e1AliasLS`); RD exists only for iQ-R. Every row is asserted by DEV-07-data (below).

### `frame_config.h`

```cpp
namespace mc {
enum class FrameType : uint8_t { F3E, F1E, F3C, F1C, /* reserved for v2: */ F4E, F4C };
enum class DataCode : uint8_t { Binary, Ascii };
enum class SerialFormat : uint8_t { Format1 = 1, Format2, Format3, Format4, /* reserved: */ Format5 };
enum class PlcSeries : uint8_t { QL, IqR };            ///< Chooses Q/L or iQ-R subcommands and device widths.
enum class TargetFamily : uint8_t { IqR_Q_L, QnA, A }; ///< Chooses the point-limit column (spec §4.4).
enum class C1CommandSet : uint8_t { ACPU, AnA };       ///< 1C: BR/WR/… or JR/QR/…

/// One flat struct for every frame; fields a frame does not use are ignored by it and by
/// validate(). Defaults follow spec §8.3. Named constructors set the frame-specific defaults.
struct FrameConfig {
    FrameType frame{FrameType::F3E};
    DataCode code{DataCode::Binary};

    // Ethernet frames (3E; 1E uses pc and monitoringTimer only; 4E reserved)
    uint8_t network{0x00};
    uint8_t pc{0xFF};
    uint16_t io{0x03FF};
    uint8_t station{0x00};
    uint16_t monitoringTimer{0x0010};  ///< ×250 ms; 0 = wait forever (then timeoutMs is mandatory). 1E default is 0x000A.
    PlcSeries series{PlcSeries::QL};
    bool checkRoute{false};            ///< Spec Q5: compare response route with request route. Ethernet default
                                       ///< false; frame3C()/frame1C() set true (spec §8.3 serial defaults, 4C-13).
    uint16_t serialStart{0};           ///< 4E only, reserved, unused in v1.

    // Serial frames (3C, 1C; 4C reserved)
    SerialFormat format{SerialFormat::Format1};
    uint8_t stationNo{0x00};
    uint8_t selfStation{0x00};
    bool sumCheck{true};
    uint8_t blockNo{0x00};             ///< Format 2 only.
    bool checkBlockNo{true};           ///< Spec Q2.
    bool sendEotOnError{true};         ///< Spec §6.3.
    bool f3ShortResponseHasSum{false}; ///< Spec Q1.

    // 1C
    uint8_t messageWait{0};            ///< 0–15, ×10 ms.
    C1CommandSet commandSet{C1CommandSet::ACPU};

    // 1E
    bool e1AliasLS{false};             ///< Accept L/S on 1E and encode them as M (spec §3.2 footnote 2, DEV-11).

    // Common
    TargetFamily targetFamily{TargetFamily::IqR_Q_L};
    bool highPerformanceQcpu{false};   ///< ZR counts double in 0403 (v1.1).
    bool aSeriesTarget{false};         ///< Enforce the multiple-of-16 head rule for QnA word access to bit devices.
    bool splitWrites{false};           ///< Allow chunk() to split a write; off = PointCount error instead.
    uint32_t timeoutMs{0};             ///< 0 = derived: monitoringTimer×250+1000 for Ethernet, 3000 for serial.
                                       ///< Ethernet: whole response. Serial: time to the FIRST byte only; the
                                       ///< rest is governed by SessionConfig::serialInterCharMs (core-session).
    uint8_t readRetries{0};            ///< Acts on serial links only: an Ethernet timeout faults the link (spec §6.1).

    static FrameConfig frame3E(DataCode code = DataCode::Binary) noexcept;
    static FrameConfig frame1E(DataCode code = DataCode::Binary) noexcept;   // monitoringTimer = 0x000A
    static FrameConfig frame3C(SerialFormat f = SerialFormat::Format1) noexcept;
    static FrameConfig frame1C(SerialFormat f = SerialFormat::Format1) noexcept;

    /// Spec §8.3 last bullet: out-of-range values are a Config error at construction time, not at
    /// send time. Checks: format ≤ 4 for 3C/1C; messageWait ≤ 15; monitoringTimer == 0 requires
    /// timeoutMs > 0; code == Ascii is forced for 3C/1C; F4E/F4C/Format5 rejected in v1.
    Expected<void> validate() const noexcept;
    uint32_t effectiveTimeoutMs() const noexcept;
    bool isSerial() const noexcept;    ///< F3C, F1C (F4C)
};
}
```

### `request.h`

```cpp
namespace mc {
enum class Op : uint8_t { ReadBits, ReadWords, WriteBits, WriteWords /* v1.1: ReadRandom, WriteRandomBits, WriteRandomWords */ };

/// Layout of bit data in normalized payloads (capability-map assumption 5).
enum class BitLayout : uint8_t {
    BytePerPoint,   ///< One byte per point, value 0 or 1, device order. Default.
    PackedLsbFirst  ///< Eight points per byte, point (head+i) at bit (i % 8) of byte (i / 8).
};

/// A read or write of `count` consecutive points from `head`. Non-owning: `data` must outlive
/// the call it is passed to (encode); Session::submit copies it.
struct Request {
    Op op{Op::ReadWords};
    Device head{};
    uint16_t count{0};            ///< Points: bits for *Bits ops, words for *Words ops.
    ByteView data{};              ///< Write payload in normalized form; empty for reads.
    BitLayout bitLayout{BitLayout::BytePerPoint};  ///< Applies to bit data in `data` and in the response.

    static Request readBits(Device head, uint16_t count) noexcept;
    static Request readWords(Device head, uint16_t count) noexcept;
    static Request writeBits(Device head, ByteView data, BitLayout layout = BitLayout::BytePerPoint) noexcept;  // count derived
    static Request writeWords(Device head, ByteView data) noexcept;   // count = data.size / 2
    bool isWrite() const noexcept; bool isBitOp() const noexcept;
};

/// Everything that can be decided from data alone before encoding (spec §3.4 item 4, §3.5, §4.4
/// field maxima). Called by core-protocol's encode and by Session::submit/subscribe.
/// Rules, in this order, first failure wins:
///  1. count >= 1                                                  → PointCount
///  2. Bit op on a non-bit device                                  → InvalidDevice   (DEV-13)
///  3. Device unsupported by the frame family (code == kNoCode),
///     honouring e1AliasLS for L/S on 1E                           → InvalidDevice   (DEV-10, DEV-11)
///  4. head.number exceeds the family's field width
///     (QnA Q/L 6 digits / 0xFFFFFF, iQ-R 8 digits, 1E 0xFFFFFFFF,
///      1C ACPU 4 digits (T/C 3), AnA 6 (T/C 5))                    → InvalidDevice   (DEV-08, DEV-09)
///  5. Word op on a bit device with head not a multiple of 16 when
///     frame is 1E or 1C, or aSeriesTarget; in M9000–M9255 the rule
///     is head == 9000 + 16k (spec Q8)                             → InvalidDevice   (DEV-12)
///  6. count > field maximum (u16 for QnA; 256 for 1E/1C)          → PointCount
///  7. data.size matches count and layout for writes                → DataSizeMismatch
/// Complexity O(1); no allocation.
Expected<void> validate(const Request& r, const FrameConfig& cfg) noexcept;
}
```

### `limits.h`

```cpp
namespace mc {
/// Maximum points per single command for this frame/code/op/device kind/series/target family,
/// from the constexpr transcription of spec §4.4 (v1 subset below). O(1).
uint16_t maxPoints(const FrameConfig& cfg, Op op, DeviceKind kind) noexcept;

struct Chunk {
    uint32_t headNumber;   ///< Device number of this chunk's head (type is the request's).
    uint16_t count;        ///< Points in this chunk.
    uint32_t dataOffset;   ///< Byte offset into Request::data for writes; 0 for reads.
};

/// Number of chunks chunk() would produce, or the error it would return.
Expected<size_t> chunkCount(const Request& r, const FrameConfig& cfg) noexcept;

/// Splits r per spec §8.5: max from maxPoints(); head step is 16 per word for word ops on bit
/// devices, else 1; a write that needs more than one chunk is PointCount unless cfg.splitWrites.
/// Writes at most `capacity` chunks into `out`; returns the number written or BufferTooSmall.
/// Complexity O(C); no allocation.
Expected<size_t> chunk(const Request& r, const FrameConfig& cfg, Chunk* out, size_t capacity) noexcept;
}
```

**Limits table, v1 subset** (transcribed from spec §4.4; the full table including random-access rows is stored in `limits_table.cpp` so v1.1 needs no table change):

| Op | Device kind | Frame / code | IqR_Q_L | QnA | A |
|---|---|---|---|---|---|
| ReadWords / WriteWords | word | QnA (3E, 3C) | 960 | 480 | 64 |
| ReadWords | bit (words) | QnA | 960 | 480 | 32 |
| WriteWords | bit (words) | QnA | 960 | 480 | 10 |
| ReadBits | bit | 3C | 7904 | 3952 | 256 |
| ReadBits | bit | 3E ASCII | 3584 | 1792 | 256 |
| ReadBits | bit | 3E Binary | 7168 | 3584 | 256 |
| WriteBits | bit | 3C / 3E ASCII / 3E Binary | 7904 / 3584 / 7168 | 3952 / 1792 / 3584 | 160 |
| ReadBits (00H) | bit | 1E | 256 | | |
| ReadWords (01H) | bit (words) / word | 1E | 128 / 256 | | |
| WriteBits (02H) | bit | 1E | 256 | | |
| WriteWords (03H) | bit (words) / word | 1E | 40 / 256 | | |
| ReadBits (BR/JR) | bit | 1C | 256 | | |
| ReadWords (WR/QR) | bit (words) / word | 1C | 32 / 64 | | |
| WriteBits (BW/JW) | bit | 1C | 160 | | |
| WriteWords (WW/QW) | bit (words) / word | 1C | 10 / 64 | | |

The 1E column is flagged in the code as "from PDF Appendix 5, not md-verified" (spec Q7).

### `convert.h`

All functions are `noexcept`, O(n) in the number of elements touched, and allocate nothing unless they return a `ByteBuf`. Word order: **low word at the lower device number** (spec §2.3, §8.7). Precondition violations (offset past the view) are checked and return the zero value or `false`; they never read out of bounds.

```cpp
namespace mc::convert {
uint16_t wordAt(ByteView words, size_t wordIndex) noexcept;
int16_t  int16At(ByteView words, size_t wordIndex) noexcept;
uint32_t uint32At(ByteView words, size_t wordIndex) noexcept;   // words[i] low, words[i+1] high
int32_t  int32At(ByteView words, size_t wordIndex) noexcept;
float    float32At(ByteView words, size_t wordIndex) noexcept;  // IEEE-754 from uint32At
double   float64At(ByteView words, size_t wordIndex) noexcept;  // four words, lowest first

bool putWord(MutableByteView words, size_t wordIndex, uint16_t v) noexcept;
bool putInt16(MutableByteView, size_t, int16_t) noexcept;
bool putUint32(MutableByteView, size_t, uint32_t) noexcept;
bool putInt32(MutableByteView, size_t, int32_t) noexcept;
bool putFloat32(MutableByteView, size_t, float) noexcept;
bool putFloat64(MutableByteView, size_t, double) noexcept;

/// Spec §8.7 string rule: first character in the low byte of the first word; stops at NUL or
/// wordCount words. Returns characters written (no NUL appended if capacity is exhausted).
size_t stringAt(ByteView words, size_t wordIndex, size_t wordCount, char* out, size_t capacity) noexcept;
/// Writes s into wordCount words, NUL-padded; returns false if it does not fit.
bool putString(MutableByteView words, size_t wordIndex, size_t wordCount, std::string_view s) noexcept;

/// Bit layouts (Request::BitLayout). n = number of points.
bool packBits(ByteView bytePerPoint, MutableByteView packedLsbFirst) noexcept;
bool unpackBits(ByteView packedLsbFirst, size_t n, MutableByteView bytePerPoint) noexcept;
/// Spec §2.4: bit i of word k is device head + 16k + i. Both directions.
bool wordsToBits(ByteView words, MutableByteView bytePerPoint) noexcept;   // out.size == 16 × word count
bool bitsToWords(ByteView bytePerPoint, MutableByteView words) noexcept;   // in.size must be a multiple of 16

// Owning conveniences for application code and Request builders (allocate once, off the hot path).
ByteBuf fromWords(std::initializer_list<uint16_t>);
ByteBuf fromInt16(std::initializer_list<int16_t>);
ByteBuf fromInt32(std::initializer_list<int32_t>);
ByteBuf fromFloat32(std::initializer_list<float>);
ByteBuf fromFloat64(std::initializer_list<double>);
ByteBuf fromString(std::string_view s, size_t wordCount);
ByteBuf fromBits(std::initializer_list<bool>);
}
```

### `log.h`

```cpp
namespace mc {
enum class LogLevel : uint8_t { Trace, Debug, Info, Warn, Error, Off };

/// The one hook an application implements to receive the library's log lines
/// (intent: "log sink để app cắm log của họ vào").
class LogSink {
public:
    virtual ~LogSink() = default;
    /// Consulted BEFORE any message string is built. Must be cheap and thread-safe.
    virtual bool enabled(LogLevel level) const noexcept = 0;
    /// category is a static string such as "mc.session" or "mc.protocol"; message is only valid
    /// during the call.
    virtual void write(LogLevel level, std::string_view category, std::string_view message) noexcept = 0;
};

class NullLogSink final : public LogSink { /* enabled() → false, write() → no-op */ };

/// Hex dump into a caller buffer: "50 00 00 FF FF 03 00 …"; with names=true control characters
/// of ASCII frames render as <STX>, <ETX>, <ENQ>, <ACK>, <NAK>, <CR>, <LF>, <DLE> (spec §8.6).
/// Returns the characters that would be written, like snprintf. No allocation.
size_t hexDump(ByteView bytes, char* out, size_t capacity, bool names = false) noexcept;
}
```

Library code follows one pattern, enforced by review: `if (sink.enabled(level)) { build message in a stack buffer; sink.write(...); }`. The `enabled()` check is the only cost on a disabled level.

## Complexity and allocation (binding, from ideas §9.3)

| Function | Time | Allocation |
|---|---|---|
| `deviceInfo` | O(1) | none |
| `parseDevice` | O(len) | none |
| `formatDevice` | O(digits) | none |
| `FrameConfig::validate` | O(1) | none |
| `validate(Request, FrameConfig)` | O(1) | none |
| `maxPoints` | O(1) | none |
| `chunkCount`, `chunk` | O(C) | none |
| `convert::*At`, `put*`, `packBits`, `unpackBits`, `wordsToBits`, `bitsToWords` | O(n) | none |
| `convert::from*` | O(n) | one `ByteBuf` |
| `hexDump` | O(n) | none |
| `LogSink::enabled` | O(1) | none (contract on the implementer) |

`test_alloc.cpp` overrides global `operator new` with a counter and asserts zero calls across `parseDevice`, `validate`, `chunk`, `convert::float64At`, `hexDump` and `formatDevice`.

## Code Style

Inherited from `SPEC-build-packaging.md`. Tables are `constexpr` arrays indexed by the enum, with a `static_assert` that the array size equals `DeviceType::Count` and that every row's `type` equals its index:

```cpp
// src/core/model/device_table.cpp
namespace mc::detail {
constexpr DeviceInfo kDeviceTable[] = {
    //  type            sym    kind              radix       asciiQL binQL  asciiIqr binIqr  e1     c1
    {DeviceType::SM,   "SM",  DeviceKind::Bit,  Radix::Dec, "SM",   0x91,  "SM**",  0x0091, kNoCode, ""  },
    {DeviceType::SD,   "SD",  DeviceKind::Word, Radix::Dec, "SD",   0xA9,  "SD**",  0x00A9, kNoCode, ""  },
    {DeviceType::X,    "X",   DeviceKind::Bit,  Radix::Hex, "X*",   0x9C,  "X***",  0x009C, 0x5820,  "X" },
    // … every row of spec §3.2, in order …
};
static_assert(std::size(kDeviceTable) == static_cast<size_t>(DeviceType::Count));
}
```

## Testing Strategy

doctest binary `mc_core_model_tests`, registered with `ctest` under the label `core_model`. Table-driven where the reference spec is table-driven, one `SUBCASE` per row so a failure names the row.

| ID | Covers | Source |
|---|---|---|
| DEV-01 … DEV-06 | `parseDevice` happy paths, radix, longest match, ambiguity, malformed | spec §9.3 |
| DEV-07-data | Every row of `kDeviceTable` equals the corresponding row of spec §3.2 (codes, kind, radix); the on-wire encoding half of DEV-07 belongs to `core-protocol` | spec §3.2 |
| DEV-08, DEV-09 | Width overflow per family via `validate()` | spec §3.4 item 4 |
| DEV-10, DEV-11 | Unsupported symbol per family; `e1AliasLS` | spec §3.2 footnotes |
| DEV-12 | Multiple-of-16 rule incl. the M9000 special case | spec §3.5, Q8 |
| DEV-13 | Bit op on word device | spec §3.5 |
| LIM-01 | Every cell of the v1 limits table above returns the stated value | spec §4.4 |
| LIM-02 | 1E column values match the PDF Appendix 5 numbers in the spec | spec Q7 |
| CHK-01 … CHK-06 | `chunk()`: exact fit, remainder, word-op-on-bit step 16, write without `splitWrites` → PointCount, with `splitWrites` → split, `BufferTooSmall` | spec §8.5 |
| CFG-01 … CFG-05 | Named-constructor defaults equal spec §8.3 (including `checkRoute` true for 3C/1C, false for 3E/1E); `validate()` rejects format 5 for every frame, messageWait 16, timer 0 without timeout, and forces `Ascii` for 3C/1C; `effectiveTimeoutMs` derivation | spec §8.3 |
| CNV-01 … CNV-08 | D350/D351 → 170F56ABH; D0/D1 → 0.75f; "ABCD" → 4241H 4443H and back; FFFFH → −1; float64 round trip over four words; `wordsToBits([1234H,0002H])` ON at 2,4,5,9,12,17 (PRIM-15); pack/unpack round trip; out-of-range index returns 0/false | spec §2.3, §2.4, §8.7, §9.2 |
| RES-01 … RES-03 | `Expected<T>` and `Expected<void>` value/error semantics; `Error::ok()`; `message` is static | this spec |
| RES-04 | `Expected<T>` with a move-only `T` (`std::unique_ptr<int>`) moves in and out; copying does not compile (static check) | core-session |
| DEV-14 | `Device` `==`, `!=`, `<` order by table order then number | core-session |
| LOG-01, LOG-02 | A counting sink proves `write()` is never called and no string is built when `enabled()` is false; `hexDump` renders control names | this spec |
| ALC-01 | Zero allocations for the functions listed under Complexity | ideas §9.3 |

Coverage: with `MC_COVERAGE=ON` on GCC or Clang, line coverage of `src/core/model` ≥ 95 %; every table row and every `ErrorCode` value appears in at least one test regardless of coverage tooling.

## Boundaries

**Always**

- Transcribe tables from the reference spec; when the reference implementation in `reference_source/` disagrees, the spec wins and the difference is recorded in this file under "Divergences".
- State complexity and allocation on every public function's Doxygen comment.
- Keep every type in this module trivially copyable except `ByteBuf` and `LogSink`.

**Ask first**

- Adding a `DeviceType`, an `Op`, a `FrameType` value or a `FrameConfig` field (all are public API).
- Changing any value in the limits table.
- Adding a dependency on anything outside the standard library.

**Never**

- Put wire encoding (hex ASCII, little-endian byte order of frames, subheaders) in this module.
- Parse a string on any function that the session hot path calls.
- Include a Qt header.

## Divergences from the old module (recorded, intentional)

- Old `MCRequest` capped `amount` at 64/128 by constructor; new limits come from the §4.4 table.
- Old device support was X/Y/M/D only with codes in `mc_define.h`; new table covers all 29 symbols.
- Old `McMsgItfConfig` timeouts lived with the transport; `FrameConfig::timeoutMs` and `monitoringTimer` live with the frame because spec §5.1 ties them together.

## Success Criteria

1. `mc_core_model_tests` passes every test ID above on MSVC and on GCC or Clang.
2. `parseDevice` accepts every symbol of spec §3.2 and rejects DEV-03/05/06 inputs.
3. `validate()` implements the seven rules in the stated order; each rule has a test that fails only that rule.
4. `maxPoints()` returns every value of the v1 limits table.
5. `chunk()` reproduces the spec §8.5 algorithm, including the step-16 rule and the `splitWrites` policy.
6. `convert` reproduces every spec §8.7 example and PRIM-15/16 on the value side.
7. ALC-01 reports zero allocations.
8. No header in `include/mc/core` from this module includes anything but `mc/core/*.h` and standard headers (BLD-05).

## Open Questions

1. **`Expected<T>` name.** Keep `mc::Expected` (matches the idea doc) or `mc::Result<T>`? `Result` reads better next to `Error` but collides with the payload-carrying `Result` that `core-session` will define for a finished request. *Proposal:* keep `Expected`. *Decision:* keep `Expected`.
2. **`Device` ordering and hashing.** `core-session` needs to sort devices by (type, number); propose `operator<` and `operator==` on `Device` in this module. *Decision:* as assumed.
3. **`formatDevice` zero padding.** Canonical form `D100`, no padding. The old app used `M0100`-style four-digit tags for its signal map; that spelling belongs to the consumer, not the library. *Decision:* no padding.
4. **1E limits provenance.** Keep the 1E column as transcribed and mark it unverified (spec Q7), or omit 1E limits until checked on hardware and fall back to the field maximum 256? *Proposal:* keep, marked. *Decision:* as proposal.
