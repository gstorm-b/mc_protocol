# Frame Protocol Specification — MC Protocol (1E / 3E / 4E / 1C / 3C / 4C)

**Primary (normative) version.** A Vietnamese translation is available at [mc-protocol-frame-spec.vi.md](mc-protocol-frame-spec.vi.md). Both versions share section numbers, test case IDs and vector IDs; if they differ, this English version prevails.

> - **Purpose:** specification for implementing a module that talks to Mitsubishi PLCs over MC protocol (frame layer + bit/word read/write commands), and for writing its tests.
> - **Sources:** `part2_message_formats.md`, `part3_commands.md`, `part5_a_series.md` (transcriptions of `Mc-protocol.pdf`, verified — see `verification/`). Point-count limits were checked against PDF Appendix 5 (printed pages 466–470); the 1E limits exist only there, not in the md files.
> - **Version:** 1.0 — 2026-09-24
> - **Test vectors:** every byte sequence in this document was generated and cross-checked with a reference encoder; the 37 vectors taken from the manual match 37/37.

---

## Contents

- [0. Introduction](#0-introduction)
- [1. Frame overview](#1-frame-overview)
- [2. Common encoding rules](#2-common-encoding-rules)
- [3. Device model](#3-device-model)
- [4. Command layer (request data / response data)](#4-command-layer-request-data--response-data)
- [5. Frame layer](#5-frame-layer)
- [6. Transport and frame reception](#6-transport-and-frame-reception)
- [7. Error model](#7-error-model)
- [8. Proposed module architecture and API](#8-proposed-module-architecture-and-api)
- [9. Test plan and test cases](#9-test-plan-and-test-cases)
- [10. Notes, PDF misprints and open questions](#10-notes-pdf-misprints-and-open-questions)
- [Appendix A — Golden vectors (complete frames)](#appendix-a--golden-vectors-complete-frames)
- [Appendix B — Source traceability](#appendix-b--source-traceability)

---

## 0. Introduction

### 0.1 Scope

**In scope (v1):**

| Frame | Medium | Code | Commands to implement |
|---|---|---|---|
| 3E | Ethernet (TCP/UDP) | ASCII, Binary | 0401, 1401 (word/bit), 0403, 1402 (word/bit) |
| 4E | Ethernet (TCP/UDP) | ASCII, Binary | same as 3E |
| 1E | Ethernet (TCP/UDP) | ASCII, Binary | 00H, 01H, 02H, 03H, 04H, 05H |
| 4C | Serial (C24) | Format 1–4 (ASCII), Format 5 (Binary) | same as 3E |
| 3C | Serial (C24) | Format 1–4 (ASCII) | same as 3E |
| 1C | Serial (C24) | Format 1–4 (ASCII) | BR/JR, WR/QR, BW/JW, WW/QW, BT/JT, WT/QT |

**Out of scope for v1** (the design must leave extension points, see §4.5):

- 2C frame; label access (041A…); buffer memory (0613, 0601…); remote control, file control, C24-specific commands (1610, 1612, 1615, 1618, 0630…).
- Device extension specification (subcommand `008□`, `00C0`: link direct device `J□\□`, module access device `U□\G`) and monitor condition (subcommand `0040`).
- Batch read/write multiple blocks (0406/1406), monitor (0801/0802, 1E 06H–09H, 1C BM/WM/MB/MN…) — **phase 2**.
- Extended file register (1C ER/EW/NR/NW…, 1E 17H–3CH), special function module buffer (TR/TW, 0EH/0FH), loopback.
- iQ-R double word devices (LTN, LSTN, LCN, LZ…) — phase 2.

### 0.2 Conventions

| Notation | Meaning |
|---|---|
| `1AH`, `1A` in byte tables | Hexadecimal value |
| `"5000"` | ASCII character string (each character is one byte: `35 30 30 30`) |
| **ASCII hex N** | Convert the number to hexadecimal, UPPERCASE, left-pad with `0` to N characters, send the most significant digit first (manual: *"send it from the upper digits"*) |
| **LE N** | N-byte integer, little-endian (manual: *"send from the lower byte (L: bits 0 to 7)"*) |
| **MUST / SHOULD / MAY** | Mandatory / Recommended / Optional |
| `part3:1086` | `part3_commands.md` line 1086 (likewise `part2:`, `part5:`) |
| Q/L, iQ-R | The two subcommand types of QnA commands: for MELSEC-Q/L series (0000/0001) and for MELSEC iQ-R series (0002/0003) |
| request data | The command part: command + subcommand + parameters (identical across frames of the same command family) |
| frame | Request data wrapped with header, access route, length, checksum and control codes |

### 0.3 Terms

| Term | Meaning |
|---|---|
| E71 | Ethernet interface module (3E/4E/1E). C24: serial communication module (4C/3C/1C) |
| Access route | Fields that select the target station (network No., PC No., station No., …) |
| End code | Result code in a response (0 = normal completion) |
| Sum check code | 8-bit checksum of serial frames |
| Additional code | Extra `10H` byte inserted before every `10H` in Format 5 data (DLE stuffing) |
| Monitoring timer | Processing wait time (unit 250 ms) in 3E/4E/1E |
| Message wait | Delay before C24 sends the response (unit 10 ms) in 1C |

---

## 1. Frame overview

### 1.1 Comparison

| Property | 3E | 4E | 1E | 4C | 3C | 1C |
|---|---|---|---|---|---|---|
| Medium | TCP/UDP | TCP/UDP | TCP/UDP | Serial | Serial | Serial |
| Code | ASCII / Binary | ASCII / Binary | ASCII / Binary | F1–F4 ASCII, F5 Binary | F1–F4 ASCII | F1–F4 ASCII |
| Request starts with | Subheader `5000` | Subheader `5400` + serial + `0000` | Subheader = command code (`00`…`05`) | Control code + Frame ID `F8` | Control code + Frame ID `F9` | Control code (no Frame ID) |
| Access route | Network, PC, Req. dest. I/O, Req. dest. station | same as 3E | PC No. | Station, Network, PC, Req. dest. I/O, Req. dest. station, Self-station | Station, Network, PC, Self-station | Station, PC |
| Length field | Request/Response data length | same as 3E | None | F5: Number of data bytes; F1–F4: none | None | None |
| Timer | Monitoring timer | Monitoring timer | ACPU monitoring timer | — | — | Message wait |
| Checksum | — | — | — | Sum check (PLC setting) | Sum check (PLC setting) | Sum check (PLC setting) |
| Result | 2-byte end code + error information | same as 3E | 1-byte end code (+ abnormal code when `5BH`) | ACK/NAK (F1, F2, F4), QACK/QNAK (F3), `FFFF` + `0000`/error (F5) | same as 4C (no F5) | ACK/NAK (F1, F2, F4), GG/NN (F3) |
| Command family | QnA (0401…) | QnA | A-series (00H…) | QnA | QnA | A-series (BR…) |

Sources: `part2:24-55`, `part2:454-490`, `part2:657-820`, `part5:29-41`.

**Key observations for the design:**

1. **3E, 4E, 3C and 4C share the request data of the QnA command family.** One QnA command encoder/decoder serves four frames; only the frame wrapping differs. 4C Format 5 uses the same binary request data as 3E binary; 4C/3C Formats 1–4 use the same ASCII request data as 3E ASCII.
2. **1E and 1C have their own (A-series compatible) command families**, with different device and data encodings.
3. Only 3E/4E and 4C Format 5 carry a length field in the response (4C F5 still needs DLE ETX for delimiting). 1E must **compute the response length** from the request; serial Formats 1–4 are delimited by **control codes** (STX/ETX, ACK/NAK, CR LF).

### 1.2 Layering

```text
┌────────────────────────────────────────────────────────────────┐
│ Client API   read_bits / read_words / write_bits / write_words │  argument checks, chunking,
│              read_random / write_random_bits / _words           │  frame capability checks
├────────────────────────────────────────────────────────────────┤
│ Command      QnaCommand | A1eCommand | A1cCommand               │  build request data, decode response data
├────────────────────────────────────────────────────────────────┤
│ Field codec  AsciiCodec | BinaryCodec                           │  u8/u16/u32, device, bits, words, dwords
├────────────────────────────────────────────────────────────────┤
│ Frame        Frame3E | Frame4E | Frame1E | Frame4C | Frame3C | Frame1C │  header, route, length, checksum,
│                                                                │  DLE, control codes, response parsing
├────────────────────────────────────────────────────────────────┤
│ Transport    TcpTransport | UdpTransport | SerialTransport      │  moves bytes + timeouts only
└────────────────────────────────────────────────────────────────┘
```

Principles:

- The **Command** and **Frame** layers MUST be pure code (no I/O): `encode(...) -> bytes`, `parser.feed(bytes)`. The whole protocol can then be tested with vectors, without a PLC.
- The **Transport** layer knows nothing about the protocol; it only reads/writes bytes and reports timeouts.
- The **Client** layer holds the use-case logic: choose the command for the frame, split requests that exceed limits, merge results.

---

## 2. Common encoding rules

### 2.1 Numeric fields

| Logical size | ASCII | Binary | Example value 0018H |
|---|---|---|---|
| u8 (1 byte) | ASCII hex 2 | 1 byte | — |
| u16 (2 bytes) | ASCII hex 4 | LE 2 | ASCII `"0018"` = `30 30 31 38`; Binary `18 00` |
| u32 (4 bytes) | ASCII hex 8 | LE 4 | — |

Sources: `part2:465-478`, `part2:588-593`.

- The encoder MUST use uppercase (`A`–`F`) (`part3:406`, `part5:2527`). The decoder SHOULD also accept lowercase.
- The encoder MUST reject values that do not fit the field width (e.g. u8 = 256), except the 1-byte "number of points" fields of 1E/1C, where 256 is encoded as `00` (see §2.1.1).

#### 2.1.1 Exceptions — fields that do NOT follow the general rule (MUST read)

| # | Field | Rule | Source |
|---|---|---|---|
| E1 | 3E/4E subheader | Fixed byte sequence, **not** a u16: request `50 00` / `"5000"`, response `D0 00` / `"D000"`; 4E: `54 00` / `"5400"`, `D4 00` / `"D400"`. The 4E serial No. is followed by `00 00` / `"0000"` | `part2:541-578` |
| E2 | QnA device code | ASCII is a **text code** (`"D*"`, `"TN"`, `"D***"`), binary is a **numeric code** (`A8H`, `00A8H`). Two different tables | `part3:210-239` |
| E3 | QnA device number (ASCII) | 6 digits (Q/L) or 8 digits (iQ-R) **in the device's radix**: decimal for D, M, T…; hexadecimal for X, Y, B, W… | `part3:241-281` |
| E4 | 1E device number (ASCII) | Always 8 **hexadecimal** digits (D1234 → `"000004D2"`) | `part5:2354-2372` |
| E5 | 1C device number | In the device's radix; 4 digits (T/C: 3) for ACPU commands, 6 digits (T/C: 5) for AnA/AnU commands | `part5:341-375` |
| E6 | Sum check code | Always 2 ASCII hex characters (even in binary Format 5), most significant digit first | `part2:403-412` |
| E7 | Message wait (1C) | 1 ASCII hex character (`0`–`F`, unit 10 ms) | `part5:187-203` |
| E8 | 1E / 1C number of points | u8; 256 points encoded as `00` | `part5:414-427`, `part5:2517-2540` |
| E9 | 1C error code | 2 ASCII hex characters (4C/3C: 4 characters; Format 5: LE 2) | `part5:237-245`, `part2:437-442` |

### 2.2 Bit data

| Frame / code | Write (request) | Read (response) |
|---|---|---|
| QnA ASCII (3E/4E ASCII, 4C/3C F1–F4), 1C | 1 character per point: `'1'` (31H) = ON, `'0'` (30H) = OFF | same as write |
| QnA Binary (3E/4E binary, 4C F5), 1E Binary | 4 bits per point; the first point is in the **high nibble**; odd count → low nibble of the last byte = 0 | same as write |
| 1E ASCII | 1 character per point | 1 character per point; **odd count → the PLC appends one dummy `'0'`** |

Example M10..M14 = ON, OFF, ON, OFF, ON: ASCII `"10101"` = `31 30 31 30 31`; Binary (E71) `10 10 10` (`part3:506-510`).
Example M100..M107 = 0,0,0,1,0,0,1,1: Binary `00 01 00 11` (`part3:1089`).

```text
pack_bits(bits):                    # binary
    for i in 0, 2, 4, ...:
        hi = bits[i]; lo = bits[i+1] if i+1 < len(bits) else 0
        out.append((hi << 4) | lo)  # each nibble is only 0 or 1
unpack_bits(buf, n):
    for i in 0..n-1: bit = (buf[i/2] >> (4 if i even else 0)) & 0x0F  → MUST be 0 or 1, otherwise protocol error
```

### 2.3 Word and double word data

| | ASCII | Binary |
|---|---|---|
| Word (16 bits) | ASCII hex 4 per word | LE 2 per word |
| Double word (32 bits) | ASCII hex 8 per dword | LE 4 per dword |

- D350 = 56ABH, D351 = 170FH, read as 2 words: ASCII `"56AB170F"`; Binary `AB 56 0F 17` (`part3:534-558`).
- D350 read as a double word: ASCII `"170F56AB"`; Binary `AB 56 0F 17` → value 170F56ABH (low word = D350) (`part3:573-581`).

### 2.4 Bit devices accessed in word units

One word holds 16 consecutive points: **bit i (bit 0 = LSB) ↔ device (head + i)**. A double word holds 32 points by the same rule.

Example: reading M100 as 2 words returns `1234H`, `0002H` → M102, M104, M105, M109, M112 = ON (from `1234H`), M117 = ON (bit 1 of `0002H`) (`part3:980-989`).

```text
words_to_bits(words):  [ (w >> i) & 1  for w in words for i in 0..15 ]
bits_to_words(bits):   len(bits) MUST be a multiple of 16; word k = Σ bits[16k+i] << i
```

### 2.5 Sum check code

**Algorithm:** add all bytes in the sum check range, keep the low 8 bits, encode as ASCII hex 2 (`part2:375-412`).

```text
sumcheck(data) = ascii_hex2( sum(data) & 0xFF )
```

| Example (from the manual) | Range | Sum | Sum check |
|---|---|---|---|
| 1C Format 1: `00FFBR3M0000` | `30 30 46 46 42 52 33 4D 30 30 30 30` | 2C0H | `"C0"` = `43 30` |
| 4C Format 5 (see vector `V-4C5-M1`) | `12 00 F8 05 … 05 00` (additional codes excluded) | 205H | `"05"` = `30 35` |

**Range per format** (`part2:57-213`, `part2:365-373`):

| Format | Request | Response with data (STX … ETX) |
|---|---|---|
| F1 | After ENQ → end of request data | After STX → **including** ETX |
| F2 | From Block No. → end of request data | From Block No. → including ETX |
| F3 | After STX → **including** ETX | After STX → including ETX |
| F4 | Same as F1 (CR LF follows the sum check and is not summed) | Same as F1, CR LF after the sum check |
| F5 | From Number of data bytes → end of request data (DLE ETX excluded, **additional codes excluded**) | Same |

- ACK/NAK responses of F1, F2, F4 have **no** sum check.
- F3 responses without data (`QACK`/`GG` + ETX) and F3 error responses (`QNAK`/`NN` + error code + ETX) have **no** sum check as printed in the PDF (see §10, Q1).
- When the PLC is set to sum check "None", requests carry no sum check and responses have none (`part2:357-363`). This setting MUST be a parameter of the frame codec.

### 2.6 Additional code (DLE stuffing) — Format 5

Source: `part2:266-319`.

```text
raw     = number_of_data_bytes(LE 2) + frame_id(F8) + access_route(7) + request_data
count   = len(frame_id + access_route + request_data)       # additional codes NOT counted
sum     = sumcheck(raw)                                     # additional codes NOT summed
wire    = 10 02 + stuff(raw) + 10 03 + sum
stuff(x): replace every 10H byte with 10H 10H (also inside Number of data bytes)
```

Decoding (receiver): after `10 02`, read sequentially; `10 10` → one `10H` byte; `10 03` → end of body, then read the 2 sum check characters (if enabled); `10` followed by anything else → protocol error.

### 2.7 Control codes

| Symbol | Code | Used in |
|---|---|---|
| STX | 02H | F1–F5 |
| ETX | 03H | F1–F5 |
| EOT | 04H | Reset of the transmission sequence (F1–F4) |
| ENQ | 05H | Start of F1, F2, F4 requests |
| ACK | 06H | Normal response without data (F1, F2, F4) |
| LF | 0AH | End of F4 frames |
| CL | 0CH | Reset of the transmission sequence (F1–F4) |
| CR | 0DH | End of F4 frames |
| DLE | 10H | F5 |
| NAK | 15H | Error response (F1, F2, F4) |

EOT / CL (`part2:239-264`): send `EOT` (F1–F3) or `EOT CR LF` (F4) to cancel the request in progress and put C24 back into the command wait state. C24 sends **no** response to EOT/CL. Format 5 uses command 1615 instead (out of scope for v1).

---

## 3. Device model

### 3.1 Data model

```text
enum DeviceKind { BIT, WORD, DWORD }
enum Radix      { DEC, HEX }

record DeviceType {
    symbol        : string          # "D", "X", "TN", "SM" ...
    kind          : DeviceKind
    radix         : Radix           # radix of the device number (D100 decimal, X1F hexadecimal)
    qna_ascii_ql  : string?         # "D*"    (2 characters)   null = not supported
    qna_bin_ql    : u8?             # 0xA8
    qna_ascii_iqr : string?         # "D***"  (4 characters)
    qna_bin_iqr   : u16?            # 0x00A8
    e1_code       : u16?            # 0x4420  (1E)
    c1_code       : string?         # "D"     (1C)
}

record Device { type: DeviceType, number: u32 }
```

### 3.2 Device code table

Sources: QnA `part3:291-340`; 1E `part5:2374-2392`; 1C `part5:377-402`.

| Symbol | Name | Kind | Radix | QnA ASCII Q/L | QnA Bin Q/L | QnA ASCII iQ-R | QnA Bin iQ-R | 1E code | 1C code |
|---|---|---|---|---|---|---|---|---|---|
| SM | Special relay | bit | DEC | `SM` | 91 | `SM**` | 0091 | — ¹ | — ¹ |
| SD | Special register | word | DEC | `SD` | A9 | `SD**` | 00A9 | — ¹ | — ¹ |
| X | Input | bit | HEX | `X*` | 9C | `X***` | 009C | 5820 | `X` |
| Y | Output | bit | HEX | `Y*` | 9D | `Y***` | 009D | 5920 | `Y` |
| M | Internal relay | bit | DEC | `M*` | 90 | `M***` | 0090 | 4D20 | `M` |
| L | Latch relay | bit | DEC | `L*` | 92 | `L***` | 0092 | 4D20 ² | `L` |
| F | Annunciator | bit | DEC | `F*` | 93 | `F***` | 0093 | 4620 | `F` |
| V | Edge relay | bit | DEC | `V*` | 94 | `V***` | 0094 | — | — |
| B | Link relay | bit | HEX | `B*` | A0 | `B***` | 00A0 | 4220 | `B` |
| D | Data register | word | DEC | `D*` | A8 | `D***` | 00A8 | 4420 | `D` |
| W | Link register | word | HEX | `W*` | B4 | `W***` | 00B4 | 5720 | `W` |
| TS | Timer — contact | bit | DEC | `TS` | C1 | `TS**` | 00C1 | 5453 | `TS` |
| TC | Timer — coil | bit | DEC | `TC` | C0 | `TC**` | 00C0 | 5443 | `TC` |
| TN | Timer — current value | word | DEC | `TN` | C2 | `TN**` | 00C2 | 544E | `TN` |
| STS | Retentive timer — contact | bit | DEC | `SS` | C7 | `STS*` | 00C7 | — | — |
| STC | Retentive timer — coil | bit | DEC | `SC` | C6 | `STC*` | 00C6 | — | — |
| STN | Retentive timer — current value | word | DEC | `SN` | C8 | `STN*` | 00C8 | — | — |
| CS | Counter — contact | bit | DEC | `CS` | C4 | `CS**` | 00C4 | 4353 | `CS` |
| CC | Counter — coil | bit | DEC | `CC` | C3 | `CC**` | 00C3 | 4343 | `CC` |
| CN | Counter — current value | word | DEC | `CN` | C5 | `CN**` | 00C5 | 434E | `CN` |
| SB | Link special relay | bit | HEX | `SB` | A1 | `SB**` | 00A1 | — | — |
| SW | Link special register | word | HEX | `SW` | B5 | `SW**` | 00B5 | — | — |
| S | Step relay | bit | DEC | `S*` | 98 | `S***` | 0098 | 4D20 ² | `S` |
| DX | Direct access input | bit | HEX | `DX` | A2 | `DX**` | 00A2 | — | — |
| DY | Direct access output | bit | HEX | `DY` | A3 | `DY**` | 00A3 | — | — |
| Z | Index register | word | DEC | `Z*` | CC | `Z***` | 00CC | — | — |
| R | File register (block switching) | word | DEC | `R*` | AF | `R***` | 00AF | 5220 | `R` |
| ZR | File register (serial number access) | word | HEX | `ZR` | B0 | `ZR**` | 00B0 | — | — |
| RD | Refresh data register | word | DEC | — | — | `RD**` | 002C | — | — |

¹ 1E/1C access special relays/registers as M9000–M9255 (→ SM1000–SM1255) and D9000–D9255 (→ SD1000–SD1255) (`part5:320-325`, `part5:2329-2334`).
² 1E has no separate code for L and S: "For L and S, perform accessing by specifying 'M'" (`part5:2448`). The library MAY accept L/S and encode them as `4D20`, keeping the number.

iQ-R long devices (LTS/LTC/LTN, LSTS/LSTC/LSTN, LCS/LCC/LCN, LZ; only LTN, LSTN, LCN and LZ are double word, the others are bit devices) are out of scope for v1; their specific restrictions are in `part3:365-386`, `part3:945-969`.

- The `*` in QnA ASCII device codes may be replaced by a space (20H) (`part3:225`). The encoder MUST use `*`.
- Leading `0` digits of ASCII device numbers may be replaced by spaces (`part3:254`, `part5:381`). The encoder MUST use `0`.

### 3.3 Encoding one device per frame family

| Family | Field order | Size |
|---|---|---|
| QnA ASCII Q/L | code (2) → number (6, device radix) | 8 characters |
| QnA ASCII iQ-R | code (4) → number (8, device radix) | 12 characters |
| QnA Binary Q/L | number (LE 3) → code (1) | 4 bytes |
| QnA Binary iQ-R | number (LE 4) → code (LE 2) | 6 bytes |
| 1E ASCII | code (ASCII hex 4) → number (ASCII hex 8) | 12 characters |
| 1E Binary | number (LE 4) → code (LE 2) | 6 bytes |
| 1C ACPU commands (BR, WR…) | code (1) → number (4); T/C: code (2) → number (3) | 5 characters |
| 1C AnA/AnU commands (JR, QR…) | code (1) → number (6); T/C: code (2) → number (5) | 7 characters |

Examples (verified):

| Device | QnA ASCII Q/L | QnA Bin Q/L | QnA ASCII iQ-R | QnA Bin iQ-R | 1E ASCII | 1E Bin | 1C ACPU | 1C AnA/AnU |
|---|---|---|---|---|---|---|---|---|
| D100 | `D*000100` | `64 00 00 A8` | `D***00000100` | `64 00 00 00 A8 00` | `442000000064` | `64 00 00 00 20 44` | `D0100` | `D000100` |
| X1F | `X*00001F` | `1F 00 00 9C` | `X***0000001F` | `1F 00 00 00 9C 00` | `58200000001F` | `1F 00 00 00 20 58` | `X001F` | `X00001F` |
| TN10 | `TN000010` | `0A 00 00 C2` | `TN**00000010` | `0A 00 00 00 C2 00` | `544E0000000A` | `0A 00 00 00 4E 54` | `TN010` | `TN00010` |
| M1234 | `M*001234` | `D2 04 00 90` | `M***00001234` | `D2 04 00 00 90 00` | `4D20000004D2` | `D2 04 00 00 20 4D` | `M1234` | `M001234` |
| M9000 | `M*009000` | `28 23 00 90` | `M***00009000` | `28 23 00 00 90 00` | `4D2000002328` | `28 23 00 00 20 4D` | `M9000` | `M009000` |

### 3.4 Parsing device address strings

`parse_device("D100") -> Device(D, 100)`

1. The symbol is case-insensitive and matched **longest first** against the symbol list (e.g. `STS` before `S`, `SM`/`SD`/`SB`/`SW` before `S`, `DX`/`DY` before `D`, `ZR` before `Z`; `CS`/`CC`/`CN` and `TS`/`TC`/`TN` are 2-character symbols).
2. The number is parsed in the device's radix: HEX devices accept `[0-9A-Fa-f]+`, DEC devices accept `[0-9]+`. A digit outside the radix (e.g. `M1F`) → `InvalidDeviceError`.
3. `T10`, `C10` (contact/coil/current value not stated) → `InvalidDeviceError` (MUST use `TN`/`TS`/`TC`, `CN`/`CS`/`CC`). The library MAY offer configurable aliases, disabled by default.
4. The width check for the frame family happens at **encoding** time (not parsing):

| Family | Device number limit |
|---|---|
| QnA Q/L | ASCII 6 digits (DEC ≤ 999999, HEX ≤ FFFFFF), Binary ≤ FFFFFFH |
| QnA iQ-R | ASCII 8 digits, Binary ≤ FFFFFFFFH |
| 1E | ≤ FFFFFFFFH |
| 1C ACPU | 4 digits (T/C: 3) |
| 1C AnA/AnU | 6 digits (T/C: 5) |

5. The library does **not** check the CPU's actual device range (it depends on PLC parameters); the PLC returns an error. The A-series device range tables (`part5:377-402`, `part5:2402-2515`) MAY be used for optional validation.

### 3.5 Access restrictions

| Rule | Applies to | Source |
|---|---|---|
| Bit-unit commands are for bit devices (the library MUST reject word devices) | All frames | `part3:1578` (1402 bit), `part5:253` (1C), `part5:2548` (1E) |
| Bit devices accessed in word units: the head number MUST be a multiple of 16 | 1C (WR/WW/WT), 1E (01/03/05), QnA commands when the target is A-series | `part5:408-410`, `part5:542-543`, `part5:2398-2400`, `part3:943`, `part3:1155`, `part3:1491` |
| Special relays M9000–M9255 accessed in word units (1C WR/WW/WT, 1E 01H/03H/05H): the head number MUST be 9000 + a multiple of 16 (in this range this replaces the multiple-of-16 rule, so M9008 is rejected although 9008 = 16 × 563; see §10 Q8) | 1C, 1E | `part5:410`, `part5:2400` |
| Not allowed in 0401: LTS, LTC, LSTS, LSTC, LZ. In 1401 (word and bit) additionally LTN, LSTN; 1401 bit units also excludes LCN | QnA | `part3:923-927`, `part3:1135-1139`, `part3:1236-1241` |
| Not allowed in 0403: LTS, LTC, LSTS, LSTC, LCS, LCC | QnA | `part3:1373-1377` |
| Not accessible from 1C/1E when the target is not an ACPU: added devices, L, S (1C: L/S → M), R (1C: only R of a QnACPU) | 1C, 1E | `part5:305-318`, `part5:2318-2327` |

For Q/L/iQ-R targets through QnA commands, the head number of a bit device read in word units does **not** have to be a multiple of 16. The library SHOULD offer an option `a_series_target = true` to enable this check for QnA frames.

---

## 4. Command layer (request data / response data)

Each command is a pure object that knows:

- `encode(codec) -> bytes`: builds the request data.
- `has_response_data`: true for reads, false for writes.
- `response_size(codec) -> int`: expected size of the response data (used for validation, and mandatory for 1E, which has no length field).
- `decode(codec, payload) -> result`: decodes the response data; MUST raise `LengthMismatchError` if the size differs from `response_size`.

### 4.1 QnA command family (used by 3E, 4E, 3C, 4C)

Sources: `part3:62-85`, `part3:875-1597`.

| Operation | Command | Sub Q/L | Sub iQ-R | Response data |
|---|---|---|---|---|
| Batch read, word units | 0401 | 0000 | 0002 | N words |
| Batch read, bit units | 0401 | 0001 | 0003 | N bit points |
| Batch write, word units | 1401 | 0000 | 0002 | none |
| Batch write, bit units | 1401 | 0001 | 0003 | none |
| Random read, word units | 0403 | 0000 | 0002 | m words + n dwords |
| Random write, word units (test) | 1402 | 0000 | 0002 | none |
| Random write, bit units (test) | 1402 | 0001 | 0003 | none |

- Command and subcommand are u16: ASCII `"0401"`, Binary `01 04`.
- iQ-R/iQ-L targets can still access devices equivalent to Q/L with the Q/L subcommands (`part3:85`). **The default MUST be the Q/L subcommands**; the iQ-R subcommands are used only when configured with `series = IQR` (needed for device numbers above 6 digits or iQ-R-only devices). Note that the random command limits are halved with iQ-R subcommands.

#### 4.1.1 Batch read — 0401

| # | Field | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"0401"` | `01 04` |
| 2 | Subcommand | `"0000"` word / `"0001"` bit (iQ-R: `"0002"` / `"0003"`) | `00 00` / `01 00` (iQ-R: `02 00` / `03 00`) |
| 3 | Head device | §3.3 (8 or 12 characters) | §3.3 (4 or 6 bytes) |
| 4 | Number of device points | u16 → 4 characters | u16 → LE 2 |

Response data:

| Kind | ASCII | Binary |
|---|---|---|
| Word units (N words) | 4N characters | 2N bytes |
| Bit units (N points) | N characters | ⌈N/2⌉ bytes |

Manual examples:

- Read words TN100..TN102 = 1234H, 0002H, 1DEFH. Binary request `01 04 00 00 64 00 00 C2 03 00`, response `34 12 02 00 EF 1D`; ASCII request `"04010000TN0001000003"`, response `"123400021DEF"` (`part3:991-1008`).
- Read bits M100..M107. Binary request `01 04 01 00 64 00 00 90 08 00`, response `00 01 00 11`; ASCII request `"04010001M*0001000008"`, response `"00010011"` (`part3:1073-1089`).

#### 4.1.2 Batch write — 1401

| # | Field | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"1401"` | `01 14` |
| 2 | Subcommand | `"0000"` word / `"0001"` bit (iQ-R `"0002"`/`"0003"`) | `00 00` / `01 00` (iQ-R `02 00`/`03 00`) |
| 3 | Head device | §3.3 | §3.3 |
| 4 | Number of device points | 4 characters | LE 2 |
| 5 | Write data | word: 4 characters per word; bit: 1 character per point | word: LE 2 per word; bit: nibble-packed |

Response data: none.

Manual examples: write D100..D102 = 1995H, 1202H, 1130H → Binary `01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11`; write M100..M107 = 1,1,0,0,1,1,0,0 → Binary `01 14 01 00 64 00 00 90 08 00 11 00 11 00` (`part3:1177-1190`, `part3:1260-1273`).

#### 4.1.3 Random read — 0403

| # | Field | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"0403"` | `03 04` |
| 2 | Subcommand | `"0000"` (iQ-R `"0002"`) | `00 00` (iQ-R `02 00`) |
| 3 | Number of word access points (m) | u8 → 2 characters | 1 byte |
| 4 | Number of double word access points (n) | u8 → 2 characters | 1 byte |
| 5 | m devices for word access | §3.3 | §3.3 |
| 6 | n devices for double word access | §3.3 | §3.3 |

Response data: m words then n dwords → ASCII 4m + 8n characters; Binary 2m + 4n bytes.

- A bit device reads 16 points in the word group and 32 points in the dword group (`part3:1356-1361`).
- If m = 0 or n = 0 the corresponding group is omitted; m + n ≥ 1.
- With Q/L subcommands, the file register ZR of a High Performance model QCPU counts as 2 points per access point (`part3:1363-1365`); the Client MUST count each ZR access point as 2 when `high_performance_qcpu = true` (§8.3, §8.5).
- The monitor-condition subcommand (`0040`) is out of scope for v1.
- Not usable with A-series targets (`part3:1354`).
- Full example in the manual: `part3:1381-1402` (vector `CMD-11` in §9).

#### 4.1.4 Random write, word units (test) — 1402 / 0000

| # | Field | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"1402"` | `02 14` |
| 2 | Subcommand | `"0000"` (iQ-R `"0002"`) | `00 00` (iQ-R `02 00`) |
| 3 | m (word access points) | 2 characters | 1 byte |
| 4 | n (double word access points) | 2 characters | 1 byte |
| 5 | m × [device, write data u16] | device + 4 characters | device + LE 2 |
| 6 | n × [device, write data u32] | device + 8 characters | device + LE 4 |

Response data: none. Example: `part3:1501-1517`.

#### 4.1.5 Random write, bit units (test) — 1402 / 0001

| # | Field | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"1402"` | `02 14` |
| 2 | Subcommand | `"0001"` (iQ-R `"0003"`) | `01 00` (iQ-R `03 00`) |
| 3 | Number of bit access points (n) | 2 characters | 1 byte |
| 4 | n × [device, set/reset] | Set/reset Q/L: `"01"`/`"00"`; iQ-R: `"0001"`/`"0000"` | Q/L: `01`/`00`; iQ-R: `01 00`/`00 00` |

Response data: none. Set/reset source: `part3:621-637`. Example M50 OFF, Y2F ON: Binary `02 14 01 00 02 32 00 00 90 00 2F 00 00 9D 01` (`part3:1586-1597`).

### 4.2 1E command family

Source: `part5:2272-2967`. In 1E the **command code is carried in the subheader** of the frame (§5.3); the request data starts right after the monitoring timer. The response subheader = command code OR 80H.

| Command | Operation | Request data | Response data |
|---|---|---|---|
| 00H | Batch read, bit units | head device → points (u8) → `00` | N bit points |
| 01H | Batch read, word units | head device → points (u8) → `00` | N words |
| 02H | Batch write, bit units | head device → points (u8) → `00` → write data | none |
| 03H | Batch write, word units | head device → points (u8) → `00` → write data | none |
| 04H | Test (random write), bit units | n (u8) → `00` → n × [device → ON/OFF (u8: `00`/`01`)] | none |
| 05H | Test (random write), word units | n (u8) → `00` → n × [device → write data (u16)] | none |

- Devices are encoded per §3.3 (1E): ASCII `code(4 hex) + number(8 hex)`; Binary `number(LE 4) + code(LE 2)`.
- Points / n is a u8, 256 → `00` (`part5:2517-2522`). The `00` field (fixed value) is a u8: ASCII `"00"`, Binary `00`. In ASCII, "n + fixed value" appears as `"0300"` for n = 3 (`part5:2890`).
- ON/OFF of command 04H is a u8: ASCII `"01"`/`"00"`, Binary `01`/`00` (`part5:2870-2877`).
- Bit/word data per §2.2, §2.3. ASCII bit reads with odd N → the response has N + 1 characters, the last one is a dummy (`part5:2564-2565`).
- Bit devices in commands 01H/03H/05H: the head number MUST be a multiple of 16 (`part5:2650`, `part5:2794`, `part5:2924`).

Response data size:

| Command | ASCII | Binary |
|---|---|---|
| 00H (N points) | N + (N mod 2) characters | ⌈N/2⌉ bytes |
| 01H (N words) | 4N characters | 2N bytes |
| 02H–05H | 0 | 0 |

Manual examples (verified): 00H M100 × 12 points: ASCII `"4D20000000640C00"`, Binary `64 00 00 00 20 4D 0C 00` (`part5:2588-2620`); 01H Y40 × 2 words: response ASCII `"829D553E"`, Binary `9D 82 3E 55` (`part5:2662-2695`).

### 4.3 1C command family

Sources: `part5:149-185`, `part5:247-289`, `part5:433-911`. Request data = **command (2 characters) + message wait (1 character) + character area**.

| Operation | ACPU | AnA/AnU | Request character area | Response character area |
|---|---|---|---|---|
| Batch read, bit | `BR` | `JR` | head device → points (2 characters) | N characters |
| Batch read, word | `WR` | `QR` | head device → points (words, 2 characters) | 4N characters |
| Batch write, bit | `BW` | `JW` | head device → points → N characters | none |
| Batch write, word | `WW` | `QW` | head device → points → 4N characters | none |
| Test, bit | `BT` | `JT` | n (2 characters) → n × [device → `'0'`/`'1'` (1 character)] | none |
| Test, word | `WT` | `QT` | n (2 characters) → n × [device → 4 characters] | none |

- Everything is ASCII (1C has no binary format).
- Message wait: `0`–`F`, unit 10 ms (0–150 ms), e.g. 100 ms → `"A"` (`part5:187-203`).
- Devices: §3.3 (1C). ACPU commands (`BR`…) work with every ACPU; AnA/AnU commands (`JR`…) only with AnA/AnUCPU (`part5:277-289`). Configuration `command_set = ACPU | ANA`, default `ACPU`.
- Points are a u8, 256 → `"00"` (`part5:414-427`).
- BT set/reset: `'0'` = OFF, `'1'` = ON, 1 character (`part5:809-812`) — unlike 1E command 04H (2 characters).
- WR/WW/WT with bit devices: the head number MUST be a multiple of 16.

Manual examples: `BR` X40 × 5, message wait 100 ms: `"BRAX004005"`, response `"01101"` (`part5:473-496`); `WW` D0 × 2 = 1234H, ACD7H: `"WW0D0000021234ACD7"` (`part5:745-759`).

### 4.4 Point limits per communication

Sources: the tables on each command page of `part3` and PDF Appendix 5 (printed pages 466, 469, 470).

**QnA family (3E/4E/3C/4C)** — column "iQ-R/Q/L" is a MELSEC iQ-R, iQ-L, Q or L target; "QnA" is a QnA target or another station via a QnA network; "A" is an A-series target:

| Command | Condition | iQ-R/Q/L | QnA | A |
|---|---|---|---|---|
| 0401 word | word device | 960 points | 480 | 64 |
| 0401 word | bit device | 960 words (15360 points) | 480 words | 32 words |
| 0401 bit | C24 (4C/3C) | 7904 points | 3952 | 256 |
| 0401 bit | E71 ASCII | 3584 points | 1792 | 256 |
| 0401 bit | E71 Binary | 7168 points | 3584 | 256 |
| 1401 word | word device | 960 points | 480 | 64 |
| 1401 word | bit device | 960 words | 480 words | 10 words (160 points) |
| 1401 bit | C24 / E71 ASCII / E71 Binary | 7904 / 3584 / 7168 | 3952 / 1792 / 3584 | 160 |
| 0403 | m + n, sub 0000 | 192 | 96 | — |
| 0403 | m + n, sub 0002 | 96 | — | — |
| 1402 word | m×12 + n×14, sub 0000 | ≤ 1920 | ≤ 960 | m ≤ 10 |
| 1402 word | m×12 + n×14, sub 0002 | ≤ 960 | — | — |
| 1402 bit | n, sub 0001 | 188 | 94 | 20 |
| 1402 bit | n, sub 0003 | 94 | — | — |

**1E family:**

| Command | Condition | Maximum |
|---|---|---|
| 00H | — | 256 points |
| 01H | bit device | 128 words (2048 points) |
| 01H | word device | 256 points |
| 02H | — | 256 points |
| 03H | bit device | 40 words (640 points) |
| 03H | word device | 256 points |
| 04H | — | 80 points |
| 05H | bit device / word device | 40 words / 40 points |

**1C family:**

| Command | Condition | Maximum |
|---|---|---|
| BR/JR | — | 256 points |
| WR/QR | bit device / word device | 32 words (512 points) / 64 points |
| BW/JW | — | 160 points |
| WW/QW | bit device / word device | 10 words (160 points) / 64 points |
| BT/JT | — | 20 points |
| WT/QT | bit device / word device | 10 words / 10 points |

Implementation rules:

- The command encoder MUST check `1 ≤ N ≤ max` against the **absolute maximum of each field** and raise `PointCountError`: Number of device points of 0401/1401 is a u16 (≤ 65535); m and n of 0403/1402 and Number of bit access points of 1402 are u8 (≤ 255, `part3:419-436`); 1E/1C point counts are u8 with 256 encoded as `00` (≤ 256).
- 0403 with ZR of a High Performance model QCPU (Q/L subcommand): each point counts × 2 (`part3:1363-1365`).
- The Client layer MUST use the tables above (by frame, code, device kind, `series`, `target_family`) to split requests (§8.5). The limit table MUST be configurable data (not hard-coded in many places), because the actual target may be more restrictive.

### 4.5 Extension points for phase 2

The Command design MUST allow adding commands without changing the Frame layer. Planned commands:

| Command | Frame | Notes | Source |
|---|---|---|---|
| 0406 / 1406 batch read/write multiple blocks | QnA | Word blocks + bit blocks; up to 120 blocks (subcommand 0000), 60 blocks (0002, 008□) | `part3:1599-1858` |
| 0801 / 0802 register monitor / monitor | QnA | 0801 request data is the same as 0403 | `part3:1860-1954` |
| 06H/07H/08H/09H monitor | 1E | 08H reads bits; odd counts have a dummy | `part5:2969-3108` |
| BM/JM, WM/QM, MB/MJ, MN/MQ | 1C | Register, then read | `part5:913-1140` |
| Subcommand 008□ / 00C0 (device extension, monitor condition) | QnA | Different device layout (PDF Appendix 1, printed page 438 = PDF page 440; not in the md files) | `part3:201-208` |

---

## 5. Frame layer

Each frame codec provides:

```text
interface FrameCodec {
    encode_request(cmd: Command) -> bytes
    new_response_parser(cmd: Command) -> ResponseParser   # incremental parser, knows the command just sent
}
interface ResponseParser {
    feed(chunk: bytes) -> NEED_MORE | DONE
    remainder() -> bytes          # bytes left over after the frame (TCP may deliver 2 frames at once)
    result() -> payload           # response data; or raises McPlcError / McProtocolError
}
```

### 5.1 3E frame

Sources: `part2:492-653`, `part2:756-784`, `part2:806-1012`.

**Request:**

| # | Field | ASCII | Binary | Default |
|---|---|---|---|---|
| 1 | Subheader | `"5000"` | `50 00` | fixed |
| 2 | Network No. | u8 → 2 characters | 1 byte | `00` |
| 3 | PC No. | u8 → 2 characters | 1 byte | `FF` |
| 4 | Request destination module I/O No. | u16 → 4 characters | LE 2 | `03FF` |
| 5 | Request destination module station No. | u8 → 2 characters | 1 byte | `00` |
| 6 | Request data length | u16 → 4 characters | LE 2 | = length of (7) + (8) |
| 7 | Monitoring timer | u16 → 4 characters | LE 2 | `0010` (4 s) |
| 8 | Request data | §4.1 | §4.1 | |

- Request data length counts **bytes on the wire** (ASCII: characters) of fields 7 + 8. Example read D100 × 3: binary 2 + 10 = 12 = `0C 00`; ASCII 4 + 20 = 24 = `"0018"`.
- The header (1–6) has a fixed length: Binary 9 bytes, ASCII 18 characters.

**Response:**

| # | Field | ASCII | Binary |
|---|---|---|---|
| 1 | Subheader | `"D000"` | `D0 00` |
| 2–5 | Access route (Network, PC, I/O, station) | 10 characters | 5 bytes |
| 6 | Response data length | 4 characters | LE 2 |
| 7 | End code | u16 → 4 characters | LE 2 |
| 8a | Response data (end code = 0) | §4.1 | §4.1 |
| 8b | Error information (end code ≠ 0): access route (5 bytes / 10 characters) + command (u16) + subcommand (u16) | 18 characters | 9 bytes |

- Response data length = length of (7) + (8a) or (7) + (8b). Write response without data: length = 2 (`02 00`) / 4 (`"0004"`).
- Error information: the access route is that of the **station that returned the error** and may differ from the request (`part2:646-653`).

**Parse algorithm (TCP):**

```text
1. Read exactly H bytes (Binary 9, ASCII 18).
2. subheader ≠ D0 00 / "D000"             → FrameMismatchError
3. (optional, off by default) route ≠ request route → FrameMismatchError
4. L = response data length; read exactly L bytes.
5. L < end code size (2 / 4)              → LengthMismatchError
6. end = end code
   end == 0 : payload = remainder; check len(payload) == cmd.response_size(codec) → return payload
   end != 0 : parse error information if long enough (9 bytes / 18 characters), raise McPlcError(end, error_info)
```

**Monitoring timer** (`part2:595-616`): `0000` = wait forever; `0001`–`FFFF` × 250 ms. Recommended: connected station `0001`–`0028` (0.25–10 s), other station `0002`–`00F0` (0.5–60 s). The client read timeout SHOULD exceed `timer × 250 ms` plus a margin (e.g. + 1 s); with `timer = 0` the client MUST use its own timeout.

**Access route — common values:**

| Target | Network | PC | I/O | Station | Source |
|---|---|---|---|---|---|
| Connected station (host) | `00` | `FF` | `03FF` | `00` | `part2:867-869`, `part2:936-938` |
| Other station (network n, station m) | `01`–`EF` | `01`–`78` | `03FF` | `00` | `part2:879-891` |
| Specified / current control or master station | `01`–`EF` | `7D` / `7E` | `03FF` | `00` | `part2:886-887` |
| According to "Valid Module During Other Station Access" | `FE` | … | … | … | `part2:892-900` |
| Multidrop station on the C24 of the connected station | `00` | `FF` | start I/O of the C24 ÷ 16 | `00`–`1F` | `part2:875-877`, `part2:952-961` |
| Multidrop station via network | network of the relay station | station of the relay station | start I/O of the C24 ÷ 16 | `00`–`1F` | `part2:902-908`, `part2:963-972` |
| Multiple CPU No.1–4 | `00` | `FF` | `03E0`–`03E3` | `00` | `part2:974-984` |
| Redundant: control / standby / system A / system B | `00` | `FF` | `03D0` / `03D1` / `03D2` / `03D3` | `00` | `part2:985-988` |

Examples: network 2, station 3 → Binary `02 03`, ASCII `"0203"` (`part2:918`); multiple CPU No.2 → Binary `E1 03`, ASCII `"03E1"` (`part2:1012`).

Vectors: `V-3E-B-*`, `V-3E-A-*` (Appendix A).

### 5.2 4E frame

Source: `part2:545-562`. Same as 3E except for the subheader:

| | ASCII (12 characters) | Binary (6 bytes) |
|---|---|---|
| Request | `"5400"` + serial (4 hex characters) + `"0000"` | `54 00` + serial (LE 2) + `00 00` |
| Response | `"D400"` + serial + `"0000"` | `D4 00` + serial + `00 00` |

Example serial 1234H: ASCII `"540012340000"`, Binary `54 00 34 12 00 00` (`part2:556-562`).

- Response header: Binary 13 bytes, ASCII 26 characters.
- Serial No. `0000`–`FFFF` is managed by the client (`part2:549-551`): incremented after each request, wrapping `FFFF → 0000`.
- A response whose serial differs from the pending request is a late response to an earlier (timed-out) request: the parser MUST discard it and keep reading until the deadline. This is the main advantage of 4E over 3E.
- (Phase 2) Several requests may be in flight, matched to responses by serial.

Vectors: `V-4E-B-*`, `V-4E-A-*`.

### 5.3 1E frame

Source: `part5:2029-2270`.

**Request:**

| # | Field | ASCII | Binary | Default |
|---|---|---|---|---|
| 1 | Subheader (= command code 00H–05H) | 2 characters | 1 byte | per command |
| 2 | PC No. | 2 characters | 1 byte | `FF` (host); other station `01`–`40` |
| 3 | ACPU monitoring timer | 4 characters | LE 2 | `000A` (2.5 s) |
| 4 | Request data | §4.2 | §4.2 | |

**Response:**

| # | Field | ASCII | Binary | Present when |
|---|---|---|---|---|
| 1 | Subheader (= command code OR 80H) | 2 characters | 1 byte | always |
| 2 | End code | 2 characters | 1 byte | always; `00` = normal |
| 3 | Abnormal code | 2 characters | 1 byte | only when end code = `5B` |
| 4 | Response data | §4.2 | §4.2 | only when end code = `00` and the command is a read |

Error examples: end code `5B` + abnormal code `10` (PC No. error) → ASCII `"5B10"`, Binary `5B 10`; end code `10` → ASCII `"10"`, Binary `10` (`part5:2247-2270`).

**Parse algorithm (TCP)** — 1E has no length field:

```text
1. Read 2 bytes (Binary) / 4 characters (ASCII): subheader + end code.
2. subheader ≠ (cmd | 0x80)               → FrameMismatchError
3. end == 0x00 : read exactly cmd.response_size(codec) bytes (0 for writes) → return payload
   end == 0x5B : read 1 more byte / 2 characters of abnormal code → raise McPlcError(end, abnormal)
   otherwise   : raise McPlcError(end)    # do NOT read any further bytes
```

Monitoring timer: same meaning and recommendations as 3E; the first access to an ACPU/QnACPU needs time to identify the CPU type, so the timer MUST be within the recommended range (`part5:83-92`, `part5:2195-2196`).

Vectors: `V-1E-B-*`, `V-1E-A-*`.

### 5.4 4C frame

Sources: `part2:57-446`, `part2:665-693`.

**4C access route** (fixed order):

| Field | ASCII | Binary (F5) | Default (host) |
|---|---|---|---|
| Station No. | 2 characters | 1 byte | `00` (multidrop: `00`–`1F`; global: `FF`) |
| Network No. | 2 characters | 1 byte | `00` |
| PC No. | 2 characters | 1 byte | `FF` |
| Request destination module I/O No. | 4 characters | LE 2 | `03FF` |
| Request destination module station No. | 2 characters | 1 byte | `00` |
| Self-station No. | 2 characters | 1 byte | `00` (m:n multidrop: `00`–`1F`) |

Host: ASCII `"0000FF03FF0000"`, Binary `00 00 FF FF 03 00 00` (`part2:673-680`). Frame ID No.: ASCII `"F8"` (`46 38`), Binary `F8` (`part2:329-349`).

**Formats 1–4 (ASCII)** — notation: `P` = Frame ID + access route (ASCII), `RD` = ASCII request data (§4.1), `SUM` = sum check (2 characters, only when enabled), `BLK` = Block No. (2 characters, `00`–`FF`):

| Format | Request | Response with data | Response without data | Error response |
|---|---|---|---|---|
| F1 | `ENQ P RD SUM` | `STX P data ETX SUM` | `ACK P` | `NAK P err4` |
| F2 | `ENQ BLK P RD SUM` | `STX BLK P data ETX SUM` | `ACK BLK P` | `NAK BLK P err4` |
| F3 | `STX P RD ETX SUM` | `STX P "QACK" data ETX SUM` | `STX P "QACK" ETX` | `STX P "QNAK" err4 ETX` |
| F4 | `ENQ P RD SUM CR LF` | `STX P data ETX SUM CR LF` | `ACK P CR LF` | `NAK P err4 CR LF` |

`err4` = 4-character ASCII hex error code (`part2:437-442`). Sum check ranges: §2.5.

**Format 5 (Binary)** (`part2:181-213`, `part2:290-319`):

| Kind | Structure (before DLE stuffing) |
|---|---|
| Request | `10 02` · count (LE 2) · `F8` · route (7) · binary request data · `10 03` · SUM |
| Response with data | `10 02` · count · `F8` · route (7) · `FF FF` · `00 00` · data · `10 03` · SUM |
| Response without data | `10 02` · count · `F8` · route (7) · `FF FF` · `00 00` · `10 03` · SUM |
| Error response | `10 02` · count · `F8` · route (7) · `FF FF` · error code (LE 2) · `10 03` · SUM |

- `count` = number of bytes from the Frame ID to the end of the data (additional codes not counted).
- `FF FF` = response ID code; `00 00` = normal completion code.
- DLE stuffing applies from count to the end of the data (§2.6).

**Checks when parsing (4C, 3C, 1C):**

| Check | Level | Error |
|---|---|---|
| Frame ID (`F8`/`F9`) matches | MUST | FrameMismatchError |
| Station No. (and the whole route) matches the request | SHOULD (`check_route`, on by default; important for multidrop) | FrameMismatchError |
| Block No. matches the request (F2) | SHOULD (`check_block_no`; assumes the PLC echoes the request's Block No. — verify on hardware) | FrameMismatchError |
| Sum check is correct | MUST (when enabled) | SumCheckError |
| F3 end code is `QACK`/`QNAK` (`GG`/`NN` for 1C) | MUST | FrameMismatchError |
| F5: response ID code = `FFFF`, count equals the actual byte count | MUST | FrameMismatchError / LengthMismatchError |
| Data size = `cmd.response_size` | MUST | LengthMismatchError |

Vectors: `V-4C1-*` … `V-4C5-*`.

### 5.5 3C frame

Source: `part2:695-714`. Same as 4C Formats 1–4 (no Format 5), except:

- Frame ID `"F9"` (`46 39`).
- The access route has only 4 fields: Station No. (2) → Network No. (2) → PC No. (2) → Self-station No. (2). Host: `"0000FF00"`.
- 4-character error code; F3 end codes are `QACK`/`QNAK`.

Vectors: `V-3C1-*` … `V-3C4-*`.

### 5.6 1C frame

Sources: `part2:57-179`, `part2:735-754`, `part5:100-245`.

- No Frame ID. Access route: Station No. (2) → PC No. (2). Host: `"00FF"`.
- Request data = command (2) + message wait (1) + character area (§4.3).
- **2-character** error code; F3 end codes are `"GG"` (normal) / `"NN"` (error) (`part2:414-423`).
- Formats 1–4 only. Structure as in the 4C F1–F4 table with `P` = `"00FF"` and `err2` instead of `err4`.

Manual sum check example (Format 1): `ENQ "00" "FF" "BR" "3" "M0000" "C0"` (`part2:381-390`).

Vectors: `V-1C1-*` … `V-1C4-*`.

---

## 6. Transport and frame reception

### 6.1 TCP

- A byte stream without boundaries: MUST loop until the required number of bytes has been read (`read_exact`), and handle partial reads and several frames in one read (via `remainder()`).
- One pending request per connection (3E, 1E). 4E can discard stale responses by serial.
- After a timeout with 3E/1E: MUST close and reopen the connection before the next request (a late response could otherwise be taken as the response to the new request).

### 6.2 UDP

- One datagram is one frame. For 3E/4E, MUST check `H + L == len(datagram)`; mismatch → `LengthMismatchError`.
- Non-matching datagrams (wrong subheader/serial) SHOULD be ignored while waiting until the deadline.
- The client MUST use a timeout; write commands are not resent automatically (§7.3).

### 6.3 Serial — receiving ASCII frames (F1–F4)

Parser states (known in advance: frame type, format, sum check on/off, whether the command has response data):

```text
state START:
    c = next byte
    F1/F2/F4: c ∈ {STX, ACK, NAK}; F3: c == STX; other bytes → skip (SHOULD log) or FrameMismatchError
state STX-BODY (F1, F2, F4):
    read up to ETX (ASCII data never contains 03H)
    if sum check enabled: read 2 SUM characters and verify
    F4: read CR LF
state ACK (F1, F2, F4):
    read exactly len(BLK) + len(P)            # 1C: 4, 3C: 10, 4C: 16 (+2 in F2)
    F4: read CR LF
state NAK (F1, F2, F4):
    read exactly len(BLK) + len(P) + len(err) # err: 1C = 2, 3C/4C = 4
    F4: read CR LF
state F3-BODY:
    read up to ETX; split P, end code (4C/3C: 4 characters; 1C: 2 characters)
    end code = QACK/GG with data → if sum check enabled: read 2 SUM characters
    end code = QACK/GG without data, or QNAK/NN → done (no SUM per the PDF, see §10 Q1)
```

Timeouts: an overall response timeout plus (recommended) an inter-character timeout. After a timeout or protocol error in F1–F4, the client SHOULD send `EOT` (or `EOT CR LF` for F4) so that C24 returns to the command wait state before the next request (`part2:239-264`); configurable.

### 6.4 Serial — receiving Format 5 frames

```text
wait for 10 02 (skip any garbage before it)
read and un-stuff until 10 03
if sum check enabled: read 2 SUM characters, computed over the un-stuffed bytes
check count == len(bytes after count)
```

The parser MUST handle a `10 10` or `10 03` pair split across two reads.

---

## 7. Error model

### 7.1 Exception hierarchy

```text
McError
├── McConfigError                  invalid frame/client configuration, detected at construction (§8.3)
├── McEncodeError                  argument errors, detected BEFORE sending
│   ├── InvalidDeviceError         bad symbol/number, wrong radix, field width exceeded, not aligned to 16
│   ├── PointCountError            N = 0 or limit exceeded
│   └── UnsupportedCommandError    operation not supported by the frame (e.g. random read on 1E/1C)
├── McTransportError               connection / I/O failure
│   └── McTimeoutError
├── McProtocolError                malformed response
│   ├── FrameMismatchError         wrong subheader, frame ID, serial, route, block No., F3 end code
│   ├── LengthMismatchError        length mismatch
│   └── SumCheckError
└── McPlcError                     the PLC returned an error
        frame, code (end code / error code), abnormal_code (1E), error_info (3E/4E), raw_response
```

### 7.2 Mapping of PLC errors

| Frame | Condition | Exception | Fields |
|---|---|---|---|
| 3E/4E | end code ≠ 0000H | McPlcError | `code` = end code (u16), `error_info` = {route, command, subcommand} |
| 1E | end code ≠ 00H | McPlcError | `code` = end code (u8), `abnormal_code` when end code = 5BH |
| 4C/3C F1, F2, F4 | NAK | McPlcError | `code` = error code (4 hex characters → u16) |
| 4C/3C F3 | `QNAK` | McPlcError | same |
| 4C F5 | Response ID `FFFF` + code ≠ `0000` | McPlcError | `code` = u16 (LE) |
| 1C F1, F2, F4 | NAK | McPlcError | `code` = error code (2 hex characters → u8) |
| 1C F3 | `NN` | McPlcError | same |

The meaning of individual error codes is not in the three md files (the manual refers to each module's user's manual). The library returns the raw code; an error code lookup table MAY be added later as separate data.

### 7.3 Retry policy

- Read commands MAY be retried (configurable, default 0 retries).
- Write commands MUST NOT be retried automatically unless the user explicitly enables it.
- After a timeout: TCP 3E/1E → reconnect (§6.1); Serial F1–F4 → send EOT (§6.3).

---

## 8. Proposed module architecture and API

This document does not prescribe a programming language; the names below are suggestions.

### 8.1 Directory layout

```text
mcprotocol/
├── core/
│   ├── constants        control codes, subheaders, frame IDs
│   ├── hexascii         ASCII hex encode/decode
│   ├── checksum         sumcheck()
│   ├── dle              stuff() / unstuff() / DleReader
│   └── bits             bit pack/unpack, words<->bits
├── device/
│   ├── device_types     the §3.2 table (data, not if/else)
│   ├── device           Device, parse_device()
│   └── encode           qna_device(), e1_device(), c1_device()
├── codec/
│   └── field_codec      FieldCodec, AsciiCodec, BinaryCodec
├── command/
│   ├── qna              0401, 1401, 0403, 1402
│   ├── a1e              00H–05H
│   ├── a1c              BR/JR, WR/QR, BW/JW, WW/QW, BT/JT, WT/QT
│   └── limits           the §4.4 tables (data)
├── frame/
│   ├── frame3e, frame4e, frame1e
│   ├── frame4c, frame3c, frame1c
│   └── serial_parser    incremental F1–F5 parser shared by 4C/3C/1C
├── transport/
│   ├── tcp, udp, serial
├── client/
│   └── client           McClient + chunking
└── errors
tests/
├── vectors/             golden vectors as data (JSON/YAML) — Appendix A + the CMD-xx table
├── unit/                primitives, device, command, frame
├── transport/           fake socket / fake serial
└── integration/         mock PLC server
```

### 8.2 Main interfaces

```text
interface FieldCodec {
    kind : ASCII | BINARY
    u8(v) -> bytes ; u16(v) -> bytes ; u32(v) -> bytes        # §2.1
    fixed(binary: bytes, ascii: string) -> bytes              # fixed subheaders (§2.1.1 E1)
    bits(values) -> bytes ; words(values) -> bytes ; dwords(values) -> bytes
    size_u8() ; size_u16() ; size_bits(n) ; size_words(n) ; size_dwords(n)
    reader(buf) -> FieldReader    # u8(), u16(), u32(), bits(n), words(n), dwords(n), remaining()
}

interface Command {
    family            : QNA | A1E | A1C
    code_1e           : u8        # A1E only (goes into the subheader)
    has_response_data : bool
    encode(codec) -> bytes                    # request data
    response_size(codec) -> int               # size of the response data on success
    decode(codec, payload) -> Result
}

interface Transport {
    open() ; close()
    send(data: bytes)
    receive(max: int, deadline) -> bytes      # returns ≥ 1 byte or raises McTimeoutError
}
```

- `AsciiCodec` and `BinaryCodec` are the only two implementations needed for QnA and 1E. QnA/1E commands are written **once** against the codec; there are no ASCII/Binary branches in command code, except device encoding (§3.3).
- 1C commands always use `AsciiCodec` + `c1_device()`.
- The 1E frame reads `cmd.code_1e` for its subheader; the other frames put the command into the request data.

### 8.3 Frame configuration

| Frame | Parameters | Defaults |
|---|---|---|
| 3E | `code` (ASCII/BINARY), `network`, `pc`, `io`, `station`, `monitoring_timer`, `series` (QL/IQR), `check_route` | BINARY, 00, FF, 03FF, 00, 0010H, QL, false |
| 4E | as 3E + `serial_start` | 0 |
| 1E | `code`, `pc`, `monitoring_timer` | BINARY, FF, 000AH |
| 4C | `format` (1–5), `station`, `network`, `pc`, `io`, `module_station`, `self_station`, `sum_check`, `block_no` (F2), `series`, `send_eot_on_error` | 1, 00, 00, FF, 03FF, 00, 00, true, 00, QL, true |
| 3C | `format` (1–4), `station`, `network`, `pc`, `self_station`, `sum_check`, `block_no`, `series`, `send_eot_on_error` | 1, 00, 00, FF, 00, true, 00, QL, true |
| 1C | `format` (1–4), `station`, `pc`, `message_wait` (0–F), `sum_check`, `block_no`, `command_set` (ACPU/ANA), `send_eot_on_error` | 1, 00, FF, 0, true, 00, ACPU, true |
| Serial (4C/3C/1C) | `check_route` (§5.4), `check_block_no` (F2, §10 Q2), `f3_short_response_has_sum` (§10 Q1) | true, true, false |
| Common | `timeout`, `read_retries` (§7.3), `target_family` (IQR_Q_L / QNA / A — selects the limit table), `high_performance_qcpu` (0403 ZR counts × 2, §4.1.3), `split_writes`, `a_series_target` | auto (see below), 0, IQR_Q_L, false, false, false |

- Default `timeout`: 3E/4E/1E use `monitoring_timer × 250 ms + 1 s` (5 s for 0010H, 3.5 s for 000AH); serial frames use 3 s. An explicit `timeout` for 3E/4E/1E SHOULD stay above `monitoring_timer × 250 ms` plus a margin (§5.1); with `monitoring_timer = 0` an explicit `timeout` MUST be given.
- `format`, `sum_check` and the code type must **match the module parameters on the PLC** (set with the engineering tool, `part2:40`, `part2:360`); the library does not auto-detect them.
- Out-of-range values (e.g. `format = 5` for 3C, `message_wait = 16`) → `McConfigError` at construction time.

### 8.4 Client API and command mapping

```text
class McClient(frame: FrameCodec, transport: Transport, options) {
    read_bits(device, count)                          -> list<bool>
    read_words(device, count)                         -> list<u16>
    write_bits(device, values: list<bool>)
    write_words(device, values: list<u16>)
    read_random(word_devices, dword_devices = [])     -> (list<u16>, list<u32>)
    write_random_bits(items: list<(device, bool)>)
    write_random_words(word_items: list<(device, u16)>, dword_items: list<(device, u32)> = [])
}
```

`device` accepts a string (`"D100"`) or a `Device`.

| API | 3E / 4E / 3C / 4C | 1E | 1C (ACPU / AnA) |
|---|---|---|---|
| `read_bits` | 0401 / 0001 (iQ-R 0003) | 00H | BR / JR |
| `read_words` | 0401 / 0000 (iQ-R 0002) | 01H | WR / QR |
| `write_bits` | 1401 / 0001 (iQ-R 0003) | 02H | BW / JW |
| `write_words` | 1401 / 0000 (iQ-R 0002) | 03H | WW / QW |
| `read_random` | 0403 / 0000 (iQ-R 0002) | ✗ `UnsupportedCommandError` | ✗ |
| `write_random_bits` | 1402 / 0001 (iQ-R 0003) | 04H | BT / JT |
| `write_random_words` | 1402 / 0000 (iQ-R 0002) | 05H (words only) | WT / QT (words only) |

`read_words` on a bit device returns raw words (bit i = device head + i); use `words_to_bits` if needed.

### 8.5 Request splitting (chunking)

```text
max = limits.lookup(frame, code, operation, device.kind, series, target_family)
step_per_unit = 16 if (word access to a bit device) else 1
for off in 0, max, 2*max, ... < count:
    n    = min(max, count - off)
    head = device.number + off * step_per_unit
    send command (head, n); append the result
```

- Reads: splitting is always allowed.
- Writes: splitting is **not atomic** (part of the data may already be written when a later request fails). Default `split_writes = false` → exceeding the limit raises `PointCountError` and nothing is sent; set `true` to allow splitting.
- Random: 0403 is split by `m + n ≤ max`, where each ZR access point counts 2 when `high_performance_qcpu = true` and Q/L subcommands are used (§4.1.3); 1402 word by `m×12 + n×14 ≤ max`; result order is preserved.
- The resulting `head` MUST still fit the device number width (§3.4).

### 8.6 Concurrency and logging

- Each `McClient` holds a lock: only one request is on the wire at a time.
- Debug logging: TX/RX hex dump; for ASCII frames also a text form with control codes shown as `<STX>`, `<ETX>`, `<ENQ>`, `<ACK>`, `<NAK>`, `<CR>`, `<LF>`, `<DLE>`.

### 8.7 Data conversion helpers (application level)

| Helper | Rule | Example | Source |
|---|---|---|---|
| `words_to_u32(lo, hi)` | the low word is in the lower-numbered device | D350 = 56ABH, D351 = 170FH → 170F56ABH | `part3:573` |
| `words_to_float32(lo, hi)` | IEEE-754, low word first | D0 = 0000H, D1 = 3F40H → 0.75 | `part3:590` |
| `string_to_words("ABCD")` | the first character is in the low byte of the word | D0 = 4241H, D1 = 4443H, (D2 = 0000H if a NUL is needed) | `part3:595-615` |
| `to_int16(u16)` | two's complement | FFFFH → −1 | — |

---

## 9. Test plan and test cases

### 9.1 Test levels

| Level | Goal | Method |
|---|---|---|
| L1 Primitive | hex, LE, sum check, DLE, bit/word | table-driven + property-based |
| L2 Device | parsing, encoding for the 8 families, restrictions | table-driven |
| L3 Command | request data matches the manual; response data decoding | vectors `CMD-xx`, `CMDD-xx` |
| L4 Frame | complete frames; parsing of normal/error/malformed responses | Appendix A vectors + derived error vectors |
| L5 Transport | fragmentation, coalesced frames, timeouts, foreign datagrams | fake socket / fake serial |
| L6 Integration | client ↔ mock PLC for every frame × code × format combination | in-process mock server |
| L7 HIL (optional) | real PLC | manual or nightly |

Principles:

- Golden vectors MUST be stored as data files (`tests/vectors/*.json|yaml`) with the structure `{id, frame, code, format, options, command, request_hex, response_hex, expected}` so that any language and the mock server can reuse them.
- The mock PLC SHOULD be written from the tables in this document, without importing the client's encoder (so the same bug cannot hide on both sides); the golden vectors are the common reference for both.
- Coverage target: 100% of branches in `core/`, `device/`, `command/`, `frame/`.

### 9.2 L1 — Primitives

| ID | Description | Input | Expected |
|---|---|---|---|
| PRIM-01 | u16 | 0018H | ASCII `"0018"`; Binary `18 00` |
| PRIM-02 | Uppercase | u16 ABCDH, ASCII | `"ABCD"` (not `"abcd"`) |
| PRIM-03 | u32 | 12345678H | `"12345678"`; `78 56 34 12` |
| PRIM-04 | Field overflow | u8(100H), u16(10000H) | `McEncodeError` |
| PRIM-05 | Lowercase decode | `"abcd"` | ABCDH (SHOULD) |
| PRIM-06 | Non-hex character | `"12G4"` | `McProtocolError` |
| PRIM-07 | 1C sum check (manual) | `"00FFBR3M0000"` | `"C0"` (`part2:383-390`) |
| PRIM-08 | 4C F5 sum check (manual) | `12 00 F8 05 07 03 04 00 01 00 01 04 01 00 40 00 00 9C 05 00` | `"05"` (`part2:392-401`) |
| PRIM-09 | Sum check wrapping many times | 300 bytes of `FF` | sum 12AD4H → `"D4"` |
| PRIM-10 | DLE stuff | `01 10 02 10 10` | `01 10 10 02 10 10 10 10` |
| PRIM-11 | Invalid DLE sequence | `10 41` inside a frame body | `McProtocolError` |
| PRIM-12 | Bit packing | [0,0,0,1,0,0,1,1]; [1,0,1,0,1] | `00 01 00 11`; `10 10 10` |
| PRIM-13 | Invalid nibble | `21`, n = 2 | `McProtocolError` |
| PRIM-14 | ASCII bits | `"10101"`; `"1021"` | [1,0,1,0,1]; `McProtocolError` |
| PRIM-15 | words → bits | [1234H, 0002H] | ON indices: 2, 4, 5, 9, 12, 17 |
| PRIM-16 | dword | 170F56ABH | `"170F56AB"`; `AB 56 0F 17` |
| PRIM-17 | Property: round trip | random u8/u16/u32/bits/words/dwords, both codecs | `decode(encode(x)) == x` |
| PRIM-18 | Property: DLE | random byte strings | `unstuff(stuff(x)) == x`; `stuff(x)` has no lone `10` |

### 9.3 L2 — Devices

| ID | Description | Input | Expected |
|---|---|---|---|
| DEV-01 | Basic parse | `"D100"`, `"d100"` | (D, 100) |
| DEV-02 | Hex parse | `"X1F"`, `"x1f"` | (X, 1FH) |
| DEV-03 | Wrong radix | `"M1F"`, `"X1G"` | `InvalidDeviceError` |
| DEV-04 | Longest match | `"SM400"`, `"SD10"`, `"SB1F"`, `"SW10"`, `"DX10"`, `"ZR100"`, `"STS5"`, `"S5"`, `"TN10"` | SM 400; SD 10; SB 1FH; SW 10H; DX 10H; ZR 100H; STS 5; S 5; TN 10 |
| DEV-05 | Ambiguous symbol | `"T10"`, `"C5"` | `InvalidDeviceError` |
| DEV-06 | Malformed | `""`, `"D"`, `"100"`, `"Q10"` | `InvalidDeviceError` |
| DEV-07 | §3.3 encoding table | D100, X1F, TN10, M1234, M9000 × 8 families | every cell of the table |
| DEV-08 | QnA width overflow | D1000000 (ASCII Q/L); X1000000 (Binary Q/L) | `InvalidDeviceError`; iQ-R: `"D***01000000"` is valid |
| DEV-09 | 1C width overflow | ACPU: M10000, TN1000; AnA: TN1000 | error; error; `"TN01000"` |
| DEV-10 | Device not supported by the family | 1E: SM0, SD0, ZR0; 1C: V0; QnA Q/L: RD0 | `InvalidDeviceError` |
| DEV-11 | L/S with 1E | L100 | code `4D20`, number 100 (when the alias is enabled) |
| DEV-12 | Alignment to 16 (1E/1C word-unit bit devices) | X40, X41, M9000, M9008, M9016 | OK, error, OK, error, OK |
| DEV-13 | Bit command on a word device | `read_bits("D0", 1)` | `InvalidDeviceError` |

### 9.4 L3 — Commands: request data matches the manual

The following vectors are taken verbatim from the manual ("Source" column) and were reproduced 37/37 by the reference encoder. Tests MUST compare byte by byte.

| ID | Command / parameters | Code | Expected bytes | Source |
|---|---|---|---|---|
| CMD-01 | 0401 word, TN100 × 3 | Bin | `01 04 00 00 64 00 00 C2 03 00` | `part3:1005` |
| CMD-02 | 0401 word, TN100 × 3 | ASCII | `"04010000TN0001000003"` | `part3:998-999` |
| CMD-03 | 0401 word, M100 × 2 word | Bin | `01 04 00 00 64 00 00 90 02 00` | `part3:985` |
| CMD-04 | 0401 bit, M100 × 8 | Bin | `01 04 01 00 64 00 00 90 08 00` | `part3:1086` |
| CMD-05 | 0401 bit, M100 × 8 | ASCII | `"04010001M*0001000008"` | `part3:1079-1080` |
| CMD-06 | 1401 word, D100 = 1995H, 1202H, 1130H | Bin | `01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11` | `part3:1189` |
| CMD-07 | 1401 word, D100 = 1995H, 1202H, 1130H | ASCII | `"14010000D*0001000003199512021130"` | `part3:1183-1184` |
| CMD-08 | 1401 word, M100 × 2 word = 2347H, AB96H | Bin | `01 14 00 00 64 00 00 90 02 00 47 23 96 AB` | `part3:1173` |
| CMD-09 | 1401 bit, M100 = 1,1,0,0,1,1,0,0 | Bin | `01 14 01 00 64 00 00 90 08 00 11 00 11 00` | `part3:1272` |
| CMD-10 | 1401 bit, M100 = 1,1,0,0,1,1,0,0 | ASCII | `"14010001M*000100000811001100"` | `part3:1266-1267` |
| CMD-11 | 0403, word D0, TN0, M100, X20; dword D1500, Y160, M1111 | Bin | `03 04 00 00 04 03 00 00 00 A8 00 00 00 C2 64 00 00 90 20 00 00 9C DC 05 00 A8 60 01 00 9D 57 04 00 90` | `part3:1397-1398` |
| CMD-12 | 1402 word, D0=0550H, D1=0575H, M100=0540H, X20=0583H; dword D1500=04391202H, Y160=23752607H, M1111=04250475H | Bin | `02 14 00 00 04 03 00 00 00 A8 50 05 01 00 00 A8 75 05 64 00 00 90 40 05 20 00 00 9C 83 05 DC 05 00 A8 02 12 39 04 60 01 00 9D 07 26 75 23 57 04 00 90 75 04 25 04` | `part3:1515-1516` |
| CMD-13 | 1402 bit, M50 OFF, Y2F ON | Bin | `02 14 01 00 02 32 00 00 90 00 2F 00 00 9D 01` | `part3:1595-1596` |
| CMD-14 | 1402 bit, M50 OFF, Y2F ON | ASCII | `"1402000102M*00005000Y*00002F01"` | `part3:1591-1592` |
| CMD-15 | 1E 00H, M100 × 12 | ASCII | `"4D20000000640C00"` | `part5:2599-2600` |
| CMD-16 | 1E 00H, M100 × 12 | Bin | `64 00 00 00 20 4D 0C 00` | `part5:2612-2613` |
| CMD-17 | 1E 01H, Y40 × 2 word | ASCII | `"5920000000400200"` | `part5:2673-2674` |
| CMD-18 | 1E 01H, Y40 × 2 word | Bin | `40 00 00 00 20 59 02 00` | `part5:2687-2688` |
| CMD-19 | 1E 02H, M50 × 12 = 0,1,1,1,0,1,0,0,0,0,0,1 | Bin | `32 00 00 00 20 4D 0C 00 01 11 01 00 00 01` | `part5:2762-2763` |
| CMD-20 | 1E 03H, D100 = 1234H, 9876H, 0109H | ASCII | `"4420000000640300123498760109"` | `part5:2822-2823` |
| CMD-21 | 1E 03H, D100 = 1234H, 9876H, 0109H | Bin | `64 00 00 00 20 44 03 00 34 12 76 98 09 01` | `part5:2830-2831` |
| CMD-22 | 1E 04H, Y94 ON, M60 OFF, B26 ON | Bin | `03 00 94 00 00 00 20 59 01 3C 00 00 00 20 4D 00 26 00 00 00 20 42 01` | `part5:2899-2900` |
| CMD-23 | 1E 04H, Y94 ON, M60 OFF, B26 ON | ASCII | `"0300592000000094014D200000003C0042200000002601"` | `part5:2890-2891` |
| CMD-24 | 1E 05H, Y80 = 7B29H, W26 = 1234H, CN18 = 0050H | Bin | `03 00 80 00 00 00 20 59 29 7B 26 00 00 00 20 57 34 12 12 00 00 00 4E 43 50 00` | `part5:2964-2965` |
| CMD-25 | 1C BR, X40 × 5, message wait 100 ms | ASCII | `"BRAX004005"` | `part5:485-486` |
| CMD-26 | 1C JR, X40 × 5, message wait 100 ms | ASCII | `"JRAX00004005"` | `part5:490-491` |
| CMD-27 | 1C WR, TN123 × 2 | ASCII | `"WR0TN12302"` | `part5:583-584` |
| CMD-28 | 1C QR, TN123 × 2 | ASCII | `"QR0TN0012302"` | `part5:588-589` |
| CMD-29 | 1C BW, M903 = 0,1,1,0,1 | ASCII | `"BW0M09030501101"` | `part5:656-657` |
| CMD-30 | 1C WW, D0 = 1234H, ACD7H | ASCII | `"WW0D0000021234ACD7"` | `part5:757-758` |
| CMD-31 | 1C WT, D500 = 1234H, Y100 = BCA9H, CN100 = 0064H | ASCII | `"WT003D05001234Y0100BCA9CN1000064"` | `part5:897-898` |
| CMD-32 | 1C BT, M50 ON, B31A OFF, Y2F ON | ASCII | `"BT003M00501B031A0Y002F1"` | `part5:826-827` |
| CMD-33 | Sum check 1C Format 1 (`00FFBR3M0000`) | ASCII | `"C0"` = `43 30` | `part2:383-390` |
| CMD-34 | 4C Format 5 full frame (= V-4C5-M1) | Bin | `10 02 12 00 F8 05 07 03 04 00 01 00 01 04 01 00 40 00 00 9C 05 00 10 03 30 35` | `part2:397-399` |
| CMD-35 | 0403 (same as CMD-11) | ASCII | `"040300000403D*000000TN000000M*000100X*000020D*001500Y*000160M*001111"` | `part3:1390-1391` |
| CMD-36 | 1402 word (same as CMD-12) | ASCII | `"140200000403D*0000000550D*0000010575M*0001000540X*0000200583D*00150004391202Y*00016023752607M*00111104250475"` | `part3:1511-1512` |
| CMD-37 | 1E 05H (same as CMD-24) | ASCII | `"03005920000000807B295720000000261234434E000000120050"` | `part5:2953-2954` |

### 9.5 L3 — Commands: response data decoding

| ID | Command | Payload | Expected | Source |
|---|---|---|---|---|
| CMDD-01 | 0401 word, Binary, N = 3 | `34 12 02 00 EF 1D` | [1234H, 0002H, 1DEFH] | `part3:1008` |
| CMDD-02 | 0401 word, ASCII, N = 3 | `"123400021DEF"` | [1234H, 0002H, 1DEFH] | `part3:1001` |
| CMDD-03 | 0401 word on M100, Binary, N = 2 | `34 12 02 00` | [1234H, 0002H] | `part3:988` |
| CMDD-04 | 0401 bit, ASCII, N = 8 | `"00010011"` | [0,0,0,1,0,0,1,1] | `part3:1082` |
| CMDD-05 | 0401 bit, Binary, N = 8 | `00 01 00 11` | [0,0,0,1,0,0,1,1] | `part3:1089` |
| CMDD-06 | 0401 bit, Binary, N = 5 | `10 10 10` | [1,0,1,0,1] | `part3:510` |
| CMDD-07 | 0403, Binary, m = 4, n = 3 | `95 19 02 12 30 20 49 48 4E 4F 54 4C AF B9 DE C3 B7 BC DD BA` | words [1995H, 1202H, 2030H, 4849H]; dwords [4C544F4EH, C3DEB9AFH, BADDBCB7H] | `part3:1400-1402` |
| CMDD-08 | 0403, ASCII, m = 4, n = 3 | `"19951202203048494C544F4EC3DEB9AFBADDBCB7"` | as CMDD-07 | `part3:1393-1395` |
| CMDD-09 | 1E 00H, ASCII, N = 12 | `"101010101010"` | [1,0,1,0,1,0,1,0,1,0,1,0] | `part5:2604-2605` |
| CMDD-10 | 1E 00H, Binary, N = 12 | `10 10 10 10 10 10` | as CMDD-09 | `part5:2617-2619` |
| CMDD-11 | 1E 01H, ASCII, N = 2 (Y40) | `"829D553E"` | [829DH, 553EH] | `part5:2678-2681` |
| CMDD-12 | 1E 01H, Binary, N = 2 | `9D 82 3E 55` | [829DH, 553EH] | `part5:2692-2695` |
| CMDD-13 | 1E 00H, ASCII, N = 3 (odd) | `"1010"` | [1,0,1] (dummy ignored); payload `"101"` → `LengthMismatchError` | `part5:2564-2565` |
| CMDD-14 | 1C BR, N = 5 | `"01101"` | [0,1,1,0,1] | `part5:493-496` |
| CMDD-15 | 1C WR bit device X40, N = 2 | `"1234ABCD"` | [1234H, ABCDH] | `part5:565-569` |
| CMDD-16 | 1C WR TN123, N = 2 | `"7BC91234"` | [7BC9H, 1234H] | `part5:591-595` |
| CMDD-17 | Wrong length | 0401 word Binary N = 3, 5-byte payload | `LengthMismatchError` | — |
| CMDD-18 | Invalid nibble | 0401 bit Binary N = 2, payload `21` | `McProtocolError` | — |

### 9.6 L4 — 3E / 4E frames

| ID | Description | Input | Expected |
|---|---|---|---|
| 3E-01 | Binary encoding | G1, G2, G3, G4, G6, G1/G2 iQ-R | = `V-3E-B-01`, `-03`, `-05`, `-07`, `-08`, `-11`, `-12` |
| 3E-02 | ASCII encoding | same | = `V-3E-A-01`, `-03`, `-05`, `-07`, `-08`, `-11`, `-12` |
| 3E-03 | Length fields | every 3E/4E vector | request: length = bytes (characters) of timer + request data; response: length = bytes (characters) of end code + response data / error information |
| 3E-04 | Parse word read | `V-3E-B-02`, `V-3E-A-02` (command G1) | [1995H, 1202H, 1130H] |
| 3E-05 | Parse bit read | `V-3E-B-04`, `-09`; `V-3E-A-04`, `-09` | [0,0,0,1,0,0,1,1]; [1,0,1,0,1] |
| 3E-06 | Parse write | `V-3E-B-06`, `V-3E-A-06` | success, no data |
| 3E-07 | Parse error | `V-3E-B-10`, `V-3E-A-10` | `McPlcError(code = C051H, error_info = {net 00, pc FF, io 03FF, st 00, cmd 0401, sub 0000})` |
| 3E-08 | Wrong subheader | `V-3E-B-02` with the first 2 bytes changed to `D4 00` | `FrameMismatchError` |
| 3E-09 | Length < end code | `D0 00 00 FF FF 03 00 01 00 00` | `LengthMismatchError` |
| 3E-10 | Wrong payload size | `V-3E-B-02` with length `06 00` and the last 2 bytes removed | `LengthMismatchError` |
| 3E-11 | Error without error information | `D0 00 00 FF FF 03 00 02 00 51 C0` | `McPlcError(C051H)`, `error_info = null` |
| 3E-12 | Other station | network 02, PC 03 | route Binary `02 03 FF 03 00`; ASCII `"0203"` + `"03FF00"` |
| 3E-13 | Multiple CPU No.2 | io = 03E1H | Binary `E1 03`; ASCII `"03E1"` |
| 3E-14 | Complete random read | 0403 command of CMD-11 wrapped in 3E Binary | = `V-3E-B-13` |
| 4E-01 | Encoding | G1, G3 (serial 1234H) | = `V-4E-B-01`, `-03`; `V-4E-A-01`, `-03` |
| 4E-02 | Parsing | `V-4E-B-02`, `-04`, `-05` and the ASCII versions | as 3E-04, 3E-06, 3E-07 |
| 4E-03 | Serial wrap-around | 3 consecutive requests starting at FFFEH | FFFEH, FFFFH, 0000H |
| 4E-04 | Discard stale response | feed a response with serial 1233H, then 1234H, for request 1234H | frame 1233H discarded, result of 1234H returned |
| 4E-05 | 3E header while using 4E | `V-3E-B-02` | `FrameMismatchError` |

### 9.7 L4 — 1E frame

| ID | Description | Input | Expected |
|---|---|---|---|
| 1E-01 | Binary encoding | G1, G2, G3, G4, G6 | = `V-1E-B-01`, `-03`, `-05`, `-07`, `-09` |
| 1E-02 | ASCII encoding | same | = `V-1E-A-01`, `-03`, `-05`, `-07`, `-09` |
| 1E-03 | Parse reads | `V-1E-B-02`, `-04`, `-10`; ASCII versions | [1995H, 1202H, 1130H]; [0,0,0,1,0,0,1,1]; [1,0,1,0,1] |
| 1E-04 | Parse writes | `V-1E-B-06`, `-08` | success |
| 1E-05 | Error 5BH + abnormal code | `V-1E-B-11` (`81 5B 10`), `V-1E-A-11` | `McPlcError(5BH, abnormal = 10H)`; the parser consumes exactly 3 bytes / 6 characters |
| 1E-06 | Error without abnormal code | `V-1E-B-12` (`81 50`) | `McPlcError(50H)`; the parser finishes after 2 bytes and **does not wait for more** |
| 1E-07 | Wrong subheader | `80 00` for command 01H | `FrameMismatchError` |
| 1E-08 | 256 points | read D0 × 256 | = `V-1E-B-13` (points = `00`) |
| 1E-09 | 257 points | read D0 × 257 | `PointCountError` |
| 1E-10 | Dummy on odd bit read (ASCII) | `V-1E-A-10` with N = 5 | [1,0,1,0,1] |
| 1E-11 | Nibble padding on odd bit write (Binary) | write M100 = [1,1,1] | write data `11 10` |
| 1E-12 | Complete 04H/05H frames | request data CMD-22, CMD-24 (Binary); CMD-23, CMD-37 (ASCII) with a 1E header | Binary: header `04 FF 0A 00` / `05 FF 0A 00` + request data; ASCII: header `"04FF000A"` / `"05FF000A"` + request data |
| 1E-13 | PC No. of another station | pc = 03H | Binary `03`; ASCII `"03"` |
| 1E-14 | Alignment to 16 | command 01H with X41 | `InvalidDeviceError` |

### 9.8 L4 — 4C / 3C / 1C frames

| ID | Description | Input | Expected |
|---|---|---|---|
| 4C-01 | F1 | encode G1, G3; parse responses | = `V-4C1-01..05` |
| 4C-02 | F2 (block 00) | same | = `V-4C2-01..05` |
| 4C-03 | F3 | same | = `V-4C3-01..05` |
| 4C-04 | F4 | same | = `V-4C4-01..05` |
| 4C-05 | F5 | same | = `V-4C5-01..05` |
| 4C-06 | F5 manual example | station 05, network 07, PC 03, I/O 0004H, module station 01; 0401/0001 X40 × 5 | = `V-4C5-M1` |
| 4C-07 | DLE in a request | read D16 × 1 | = `V-4C5-06` (count 12H, device number `10 00 00` sent as `10 10 00 00`) |
| 4C-08 | DLE in a response | `V-4C5-07` | [1010H] |
| 4C-09 | Wrong sum check | `V-4C1-02` with `"DE"` changed to `"DF"` | `SumCheckError` |
| 4C-10 | Sum check disabled | `sum_check = false` | request = `V-4C1-06`; a response without SUM parses |
| 4C-11 | NAK | `V-4C1-05` | `McPlcError(7151H)` |
| 4C-12 | QNAK (F3) | `V-4C3-05` | `McPlcError(7151H)` |
| 4C-13 | Different Station No. | `V-4C1-02` with station `"01"` (SUM recomputed) | `FrameMismatchError` |
| 4C-14 | Block No. | `block_no = 3AH` (F2) | request starts with `ENQ "3A" "F8"`; a response with another block → `FrameMismatchError` |
| 4C-15 | Garbage before STX | `00 FF` + `V-4C1-02` | parses (warning logged) |
| 4C-16 | Wrong F5 count | `V-4C5-02` with count `12 00` → `13 00` (SUM recomputed: `"0C"` → `"0D"`) | `LengthMismatchError` |
| 4C-17 | Wrong F5 response ID | `V-4C5-02` with `FF FF` → `FF FE` (SUM recomputed) | `FrameMismatchError` |
| 4C-18 | F5 error | `V-4C5-05` | `McPlcError(7151H)` |
| 3C-01 | F1–F4 | encode G1, G4; parse | = `V-3C1-*` … `V-3C4-*` |
| 3C-02 | NAK / QNAK | `V-3C1-05`, `V-3C3-05` | `McPlcError(7151H)` |
| 3C-03 | F5 configured | `format = 5` | `McConfigError` |
| 1C-01 | F1–F4 | encode WR, BR, WW, BW; parse | = `V-1C1-*` … `V-1C4-*` |
| 1C-02 | 2-character NAK | `V-1C1-08` | `McPlcError(06H)` |
| 1C-03 | NN (F3) | `V-1C3-08` | `McPlcError(06H)` |
| 1C-04 | AnA/AnU command | `command_set = ANA`, read D100 × 3 | = `V-1C1-09` (`QR`, `D000100`) |
| 1C-05 | Message wait | `message_wait = 0AH` | the character after the command is `"A"` (CMD-25) |
| 1C-06 | 256 points | BR M0 × 256 | = `V-1C1-11` (points `"00"`) |
| 1C-07 | WR on a bit device | WR X40 × 2 | = `V-1C1-10` |
| 1C-08 | Manual sum check | request data `"BR3M0000"`, station 00, PC FF | SUM = `"C0"` |
| 1C-09 | Complete BT / WT | request data CMD-32, CMD-31 wrapped in F1 | `ENQ "00FF"` + request data + SUM |

### 9.9 L5 — Transport

| ID | Description | Expected |
|---|---|---|
| TRN-01 | TCP: `V-3E-B-02` arrives one byte at a time | parses |
| TRN-02 | TCP: `V-4E-B-02` + `V-4E-B-04` in one read | returns the first frame's result; `remainder()` = the second frame |
| TRN-03 | TCP: no response | `McTimeoutError`; the connection is closed; the next call reconnects |
| TRN-04 | UDP: datagram = first 10 bytes of `V-3E-B-02` | `LengthMismatchError` |
| TRN-05 | UDP: datagram with wrong subheader, then a correct one | the first is ignored, the second's result is returned |
| TRN-06 | Serial: first half of `V-4C1-02`, then silence | `McTimeoutError`; `EOT` sent when `send_eot_on_error = true` |
| TRN-07 | Serial F5: `V-4C5-07` split between the two bytes of `10 10` | parses, [1010H] |
| TRN-08 | Serial F4: LF missing | `McTimeoutError` |
| TRN-09 | Connection closed mid-frame | `McTransportError` |

### 9.10 L6 — Client API

| ID | Description | Input | Expected |
|---|---|---|---|
| API-01 | `read_words` splitting | 3E Binary, D0 × 2000 | 3 requests: (D0, 960), (D960, 960), (D1920, 80); results concatenated in order |
| API-02 | `read_bits` splitting | 3E, M0 × 8000 | Binary: (M0, 7168), (M7168, 832); ASCII: (M0, 3584), (M3584, 3584), (M7168, 832) |
| API-03 | `read_bits` splitting on C24 | 4C F1, M0 × 8000 | (M0, 7904), (M7904, 96) |
| API-04 | Word splitting on a bit device | 1E, `read_words("X0", 200)` | (X0, 128 words), (X800, 72 words) |
| API-05 | Write over the limit, splitting not allowed | 3E, `write_words("D0", 1000 values)`, `split_writes = false` | `PointCountError`, nothing sent |
| API-06 | Write over the limit, splitting allowed | same, `split_writes = true` | 2 requests (960 + 40) |
| API-07 | Random read on 1E/1C | `read_random(["D0"])` | `UnsupportedCommandError` |
| API-08 | dword on 1E/1C | `write_random_words([], [("D0", 1)])` | `UnsupportedCommandError` |
| API-09 | `series = IQR` | read D100 × 3; `read_random` with 100 points | subcommand 0002 (= `V-3E-B-11`); split 96 + 4 |
| API-10 | Zero count | `read_words("D0", 0)`, `write_bits("M0", [])` | `PointCountError` |
| API-11 | 1402 word limit | 3E Q/L, 160 word points; 161 word points | 1 request (160 × 12 = 1920); 161 → split or error per `split_writes` |
| API-12 | A-series target via QnA | `a_series_target = true`, `read_words("X41", 1)` | `InvalidDeviceError` |
| API-13 | Concurrency | 2 threads call at the same time | requests are serialized on the wire, no interleaved bytes |
| API-14 | Helpers | D0 = 0000H, D1 = 3F40H; `"ABCD"` | 0.75; [4241H, 4443H] |

### 9.11 L6 — Integration with a mock PLC

Mock PLC: holds device memory (D, W, M, X, Y, B, TN…), understands all 6 frames, answers per §5, and can be configured to return an error (end code / NAK / QNAK / 5BH) for a given device.

Combination matrix (19 combinations):

| Frame | Variants |
|---|---|
| 3E | Binary, ASCII |
| 4E | Binary, ASCII |
| 1E | Binary, ASCII |
| 4C | F1, F2, F3, F4, F5 |
| 3C | F1, F2, F3, F4 |
| 1C | F1, F2, F3, F4 |

For each combination run:

| ID | Scenario | Expected |
|---|---|---|
| INT-01 | `write_words("D100", [1995H, 1202H, 1130H])` → `read_words("D100", 3)` | the same values are read back |
| INT-02 | `write_bits("M100", [1,1,0,0,1,1,0,0])` → `read_bits("M100", 8)` | read back correctly |
| INT-03 | `write_bits` with an odd count (5) → `read_bits` | correct; checks nibble padding / dummy |
| INT-04 | `write_random_bits([("M50", 0), ("Y2F", 1)])` → `read_bits` of each point | correct |
| INT-05 | `write_random_words([("D0", 0550H), ("W26", 1234H)])` → `read_words` | correct |
| INT-06 | `read_random` (QnA only): words D0, TN0, M100; dword D1500 | correct order and values |
| INT-07 | Chunking: `read_words("D0", 2000)` | the mock receives the expected number of requests; data concatenated correctly |
| INT-08 | Mock returns an error | `McPlcError` with the correct `code` for the frame |
| INT-09 | 4C F5 with many `10H` bytes in the data (write D0..D9 = 1010H) | DLE stuffing correct in both directions |

---

## 10. Notes, PDF misprints and open questions

### 10.1 Open questions — to verify on a real PLC

| # | Issue | Handling in v1 |
|---|---|---|
| Q1 | Format 3: the response without data (`STX P QACK ETX`) and the error response (`STX P QNAK err ETX`) are printed **without** a sum check in the PDF (checked against the image of PDF page 33, printed page 31), while the response with data has one. | Implement as printed. Add the setting `f3_short_response_has_sum` (default `false`) so it can be changed quickly if the hardware behaves differently. |
| Q2 | Format 2: the manual does not state that the Block No. in the response always equals the request's Block No. | Check that they match (SHOULD), with an option to disable. |
| Q3 | 1E ASCII bit write (02H) with an odd count: the manual mentions the dummy only for **reads** (`part5:2565`), not for writes. | Send exactly N characters, no padding. |
| Q4 | Binary bit write with an odd count (QnA, 1E): low nibble of the last byte. | Set to 0 per `part3:504`. |
| Q5 | 3E/4E: the manual does not promise that the access route in a normal response always equals the request's. | `check_route = false` by default. |
| Q6 | Error code meanings are not in the three md files. | Return the raw code; add a lookup table later. |
| Q7 | The 1E limit table (§4.4) exists only in PDF Appendix 5 (printed page 470) and has **not** gone through the verification process of the md files; the QnA and 1C limits also appear on the command pages of the verified md files and match them. | Keep the limits in a configuration file; review the 1E values when needed. |
| Q8 | Special relays M9000–M9255 in word units: the manual says "(9000 + multiple of 16) can be specified" (`part5:410`, `part5:2400`) but does not say whether an address that is a multiple of 16 but not 9000 + 16k (e.g. M9008) is also accepted. | Accept only 9000 + 16k in this range (§3.5, DEV-12). |

### 10.2 PDF misprints relevant to this scope

Already marked in the md files with `> **Note:**` (full lists: `verification/PDF_misprint_notes.md`, `verification/part5/PDF_misprint_notes.md`):

| Location | Content | Value used |
|---|---|---|
| `part5:372` | The 1C timer device example prints "T S" but the bytes are `54 4E` | `TN` |
| `part5:2756` | The 1E 02H ASCII example prints characters "4 4", "0 3" above bytes `34 44`, `30 43` | Per the bytes: `4D`, `0C` |
| `part3:1387`, `part3:1410` | The 0403 example says "4 double word access" | 3 (per the request data) |
| `part3:1404` | Field labels in the 0403 binary response | Per the bit diagrams |
| `part3:583` | "Number of device points" label in the binary double word example | It is the head device |
| `part3:1444` | The 1402 word request diagram labels the last write data of the word part "Write data (nth point)" | Write data of the mth word-access point |
| `part2:105` | Format 2 prints "ACX" | ACK (06H) |

Found while writing this document (not in the verification lists; phase 2 scope): the ASCII example of the 1E register-monitor command (`part5:3026-3027`) prints device B2C as `"422E0000002C"`, while the device code table (`part5:2383`) and the binary example on the same page (`2C 00 00 00 20 42`) give code `4220`. Use `4220`.

### 10.3 Implementation notes

- Universal model QCPUs whose serial number starts with 10101 or earlier cannot use 1C/1E; use 2C/3C/4C or 3E/4E (`part5:327-330`, `part5:2336-2341`).
- When an A-series computer link module is in the multidrop connection, only ASCII (Formats 1–4) can be used (`part5:77-81`).
- The first access to an ACPU through E71 needs a monitoring timer within the recommended range (`part5:85-92`).
- With several network modules on the connected station: 1C/1E have no network No.; set "Valid Module During Other Station Access" with the engineering tool (`part2:892-900`).
- `format`, `sum_check` and ASCII/Binary MUST match the module settings on the PLC; a mismatch usually shows up as a timeout or NAK rather than a clear message.

---

## Appendix A — Golden vectors (complete frames)

**Common parameters:**

| Frame | Parameters |
|---|---|
| 3E / 4E | network 00, PC FF, I/O 03FF, station 00, monitoring timer 0010H; 4E serial No. 1234H |
| 1E | PC FF, monitoring timer 000AH |
| 4C | station 00, network 00, PC FF, I/O 03FF, module station 00, self-station 00; sum check on; F2 block No. 00 |
| 3C | station 00, network 00, PC FF, self-station 00; sum check on; F2 block No. 00 |
| 1C | station 00, PC FF, message wait 0, ACPU commands; sum check on; F2 block No. 00 |
| Subcommand | Q/L (unless iQ-R is stated) |

**Scenarios:**

| Code | Scenario |
|---|---|
| G1 | Read words D100 × 3 → 1995H, 1202H, 1130H |
| G2 | Read bits M100 × 8 → 0,0,0,1,0,0,1,1 |
| G3 | Write words D100 × 3 = 1995H, 1202H, 1130H |
| G4 | Write bits M100 × 8 = 1,1,0,0,1,1,0,0 |
| G6 | Read bits M100 × 5 → 1,0,1,0,1 (odd count) |
| Error | 3E/4E: end code C051H; 4C/3C: error code 7151H; 1C: error code 06H; 1E: 5BH + 10H and 50H |

Each vector: the `#` line holds the ID, description and length; the next line holds the hex bytes; ASCII frames have an extra text line (control codes written as `<STX>`). These vectors were generated by the reference encoder that reproduced the manual's vectors 37/37 (§9.4); `V-4C5-M1` is the manual's own example (`part2:397-399`).

### A.1 Frame 3E — Binary

```text
# V-3E-B-01  request G1: 0401/0000 read words D100 × 3  (21 bytes)
50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00

# V-3E-B-02  response G1: 1995H, 1202H, 1130H  (17 bytes)
D0 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11

# V-3E-B-03  request G2: 0401/0001 read bits M100 × 8  (21 bytes)
50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 64 00 00 90 08 00

# V-3E-B-04  response G2: 0,0,0,1,0,0,1,1  (15 bytes)
D0 00 00 FF FF 03 00 06 00 00 00 00 01 00 11

# V-3E-B-05  request G3: 1401/0000 write D100 = 1995H, 1202H, 1130H  (27 bytes)
50 00 00 FF FF 03 00 12 00 10 00 01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11

# V-3E-B-06  write response, success (no data)  (11 bytes)
D0 00 00 FF FF 03 00 02 00 00 00

# V-3E-B-07  request G4: 1401/0001 write M100 = 1,1,0,0,1,1,0,0  (25 bytes)
50 00 00 FF FF 03 00 10 00 10 00 01 14 01 00 64 00 00 90 08 00 11 00 11 00

# V-3E-B-08  request G6: 0401/0001 read bits M100 × 5  (21 bytes)
50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 64 00 00 90 05 00

# V-3E-B-09  response G6: 1,0,1,0,1  (14 bytes)
D0 00 00 FF FF 03 00 05 00 00 00 10 10 10

# V-3E-B-10  error response to G1: end code C051H + error information  (20 bytes)
D0 00 00 FF FF 03 00 0B 00 51 C0 00 FF FF 03 00 01 04 00 00

# V-3E-B-11  request G1 with iQ-R subcommand (0401/0002)  (23 bytes)
50 00 00 FF FF 03 00 0E 00 10 00 01 04 02 00 64 00 00 00 A8 00 03 00

# V-3E-B-12  request G2 with iQ-R subcommand (0401/0003)  (23 bytes)
50 00 00 FF FF 03 00 0E 00 10 00 01 04 03 00 64 00 00 00 90 00 08 00

# V-3E-B-13  request 0403 from CMD-11 / CMD-35  (45 bytes)
50 00 00 FF FF 03 00 24 00 10 00 03 04 00 00 04 03 00 00 00 A8 00 00 00 C2 64 00 00 90 20 00 00 9C DC 05 00 A8 60 01 00 9D 57 04 00 90

```

### A.2 Frame 3E — ASCII

```text
# V-3E-A-01  request G1: 0401/0000 read words D100 × 3  (42 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33
500000FF03FF000018001004010000D*0001000003

# V-3E-A-02  response G1: 1995H, 1202H, 1130H  (34 bytes)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 30 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30
D00000FF03FF0000100000199512021130

# V-3E-A-03  request G2: 0401/0001 read bits M100 × 8  (42 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38
500000FF03FF000018001004010001M*0001000008

# V-3E-A-04  response G2: 0,0,0,1,0,0,1,1  (30 bytes)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 43 30 30 30 30 30 30 30 31 30 30 31 31
D00000FF03FF00000C000000010011

# V-3E-A-05  request G3: 1401/0000 write D100 = 1995H, 1202H, 1130H  (54 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 32 34 30 30 31 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30
500000FF03FF000024001014010000D*0001000003199512021130

# V-3E-A-06  write response, success (no data)  (22 bytes)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 30 30 30
D00000FF03FF0000040000

# V-3E-A-07  request G4: 1401/0001 write M100 = 1,1,0,0,1,1,0,0  (50 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 32 30 30 30 31 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30
500000FF03FF000020001014010001M*000100000811001100

# V-3E-A-08  request G6: 0401/0001 read bits M100 × 5  (42 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 35
500000FF03FF000018001004010001M*0001000005

# V-3E-A-09  response G6: 1,0,1,0,1  (27 bytes)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 39 30 30 30 30 31 30 31 30 31
D00000FF03FF000009000010101

# V-3E-A-10  error response to G1: end code C051H + error information  (40 bytes)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 31 30 30 46 46 30 33 46 46 30 30 30 34 30 31 30 30 30 30
D00000FF03FF000016C05100FF03FF0004010000

# V-3E-A-11  request G1 with iQ-R subcommand (0401/0002)  (46 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 43 30 30 31 30 30 34 30 31 30 30 30 32 44 2A 2A 2A 30 30 30 30 30 31 30 30 30 30 30 33
500000FF03FF00001C001004010002D***000001000003

# V-3E-A-12  request G2 with iQ-R subcommand (0401/0003)  (46 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 43 30 30 31 30 30 34 30 31 30 30 30 33 4D 2A 2A 2A 30 30 30 30 30 31 30 30 30 30 30 38
500000FF03FF00001C001004010003M***000001000008

# V-3E-A-13  request 0403 from CMD-11 / CMD-35  (90 bytes)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 34 38 30 30 31 30 30 34 30 33 30 30 30 30 30 34 30 33 44 2A 30 30 30 30 30 30 54 4E 30 30 30 30 30 30 4D 2A 30 30 30 31 30 30 58 2A 30 30 30 30 32 30 44 2A 30 30 31 35 30 30 59 2A 30 30 30 31 36 30 4D 2A 30 30 31 31 31 31
500000FF03FF0000480010040300000403D*000000TN000000M*000100X*000020D*001500Y*000160M*001111

```

### A.3 Frame 4E — Binary (serial No. 1234H)

```text
# V-4E-B-01  request G1  (25 bytes)
54 00 34 12 00 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00

# V-4E-B-02  response G1  (21 bytes)
D4 00 34 12 00 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11

# V-4E-B-03  request G3  (31 bytes)
54 00 34 12 00 00 00 FF FF 03 00 12 00 10 00 01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11

# V-4E-B-04  response G3 (no data)  (15 bytes)
D4 00 34 12 00 00 00 FF FF 03 00 02 00 00 00

# V-4E-B-05  error response C051H to G1  (24 bytes)
D4 00 34 12 00 00 00 FF FF 03 00 0B 00 51 C0 00 FF FF 03 00 01 04 00 00

```

### A.4 Frame 4E — ASCII (serial No. 1234H)

```text
# V-4E-A-01  request G1  (50 bytes)
35 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33
54001234000000FF03FF000018001004010000D*0001000003

# V-4E-A-02  response G1  (42 bytes)
44 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 30 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30
D4001234000000FF03FF0000100000199512021130

# V-4E-A-03  request G3  (62 bytes)
35 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 32 34 30 30 31 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30
54001234000000FF03FF000024001014010000D*0001000003199512021130

# V-4E-A-04  response G3 (no data)  (30 bytes)
44 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 30 30 30
D4001234000000FF03FF0000040000

# V-4E-A-05  error response C051H to G1  (48 bytes)
44 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 31 30 30 46 46 30 33 46 46 30 30 30 34 30 31 30 30 30 30
D4001234000000FF03FF000016C05100FF03FF0004010000

```

### A.5 Frame 1E — Binary

```text
# V-1E-B-01  request G1: 01H read words D100 × 3  (12 bytes)
01 FF 0A 00 64 00 00 00 20 44 03 00

# V-1E-B-02  response G1  (8 bytes)
81 00 95 19 02 12 30 11

# V-1E-B-03  request G2: 00H read bits M100 × 8  (12 bytes)
00 FF 0A 00 64 00 00 00 20 4D 08 00

# V-1E-B-04  response G2  (6 bytes)
80 00 00 01 00 11

# V-1E-B-05  request G3: 03H write D100 × 3  (18 bytes)
03 FF 0A 00 64 00 00 00 20 44 03 00 95 19 02 12 30 11

# V-1E-B-06  response G3  (2 bytes)
83 00

# V-1E-B-07  request G4: 02H write M100 × 8  (16 bytes)
02 FF 0A 00 64 00 00 00 20 4D 08 00 11 00 11 00

# V-1E-B-08  response G4  (2 bytes)
82 00

# V-1E-B-09  request G6: 00H read bits M100 × 5  (12 bytes)
00 FF 0A 00 64 00 00 00 20 4D 05 00

# V-1E-B-10  response G6 (last nibble = 0)  (5 bytes)
80 00 10 10 10

# V-1E-B-11  error response: end code 5BH + abnormal code 10H  (3 bytes)
81 5B 10

# V-1E-B-12  error response: end code 50H (no abnormal code)  (2 bytes)
81 50

# V-1E-B-13  request 01H read D0 × 256 (points = 00)  (12 bytes)
01 FF 0A 00 00 00 00 00 20 44 00 00

```

### A.6 Frame 1E — ASCII

```text
# V-1E-A-01  request G1: 01H read words D100 × 3  (24 bytes)
30 31 46 46 30 30 30 41 34 34 32 30 30 30 30 30 30 30 36 34 30 33 30 30
01FF000A4420000000640300

# V-1E-A-02  response G1  (16 bytes)
38 31 30 30 31 39 39 35 31 32 30 32 31 31 33 30
8100199512021130

# V-1E-A-03  request G2: 00H read bits M100 × 8  (24 bytes)
30 30 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 38 30 30
00FF000A4D20000000640800

# V-1E-A-04  response G2  (12 bytes)
38 30 30 30 30 30 30 31 30 30 31 31
800000010011

# V-1E-A-05  request G3: 03H write D100 × 3  (36 bytes)
30 33 46 46 30 30 30 41 34 34 32 30 30 30 30 30 30 30 36 34 30 33 30 30 31 39 39 35 31 32 30 32 31 31 33 30
03FF000A4420000000640300199512021130

# V-1E-A-06  response G3  (4 bytes)
38 33 30 30
8300

# V-1E-A-07  request G4: 02H write M100 × 8  (32 bytes)
30 32 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 38 30 30 31 31 30 30 31 31 30 30
02FF000A4D2000000064080011001100

# V-1E-A-08  response G4  (4 bytes)
38 32 30 30
8200

# V-1E-A-09  request G6: 00H read bits M100 × 5  (24 bytes)
30 30 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 35 30 30
00FF000A4D20000000640500

# V-1E-A-10  response G6 (last character is dummy)  (10 bytes)
38 30 30 30 31 30 31 30 31 30
8000101010

# V-1E-A-11  error response: end code 5BH + abnormal code 10H  (6 bytes)
38 31 35 42 31 30
815B10

# V-1E-A-12  error response: end code 50H (no abnormal code)  (4 bytes)
38 31 35 30
8150

# V-1E-A-13  request 01H read D0 × 256 (points = 00)  (24 bytes)
30 31 46 46 30 30 30 41 34 34 32 30 30 30 30 30 30 30 30 30 30 30 30 30
01FF000A4420000000000000

```

### A.7 Frame 4C — Format 1

```text
# V-4C1-01  request G1  (39 bytes)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 35 30
<ENQ>F80000FF03FF000004010000D*000100000350

# V-4C1-02  response G1  (32 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 44 45
<STX>F80000FF03FF0000199512021130<ETX>DE

# V-4C1-03  request G3  (51 bytes)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 42 33
<ENQ>F80000FF03FF000014010000D*0001000003199512021130B3

# V-4C1-04  response G3 (no data)  (17 bytes)
06 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30
<ACK>F80000FF03FF0000

# V-4C1-05  error response, error code 7151H  (21 bytes)
15 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 37 31 35 31
<NAK>F80000FF03FF00007151

# V-4C1-06  request G1 with sum check disabled  (37 bytes)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33
<ENQ>F80000FF03FF000004010000D*0001000003

```

### A.8 Frame 4C — Format 2

```text
# V-4C2-01  request G1  (41 bytes)
05 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 42 30
<ENQ>00F80000FF03FF000004010000D*0001000003B0

# V-4C2-02  response G1  (34 bytes)
02 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 33 45
<STX>00F80000FF03FF0000199512021130<ETX>3E

# V-4C2-03  request G3  (53 bytes)
05 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 31 33
<ENQ>00F80000FF03FF000014010000D*000100000319951202113013

# V-4C2-04  response G3 (no data)  (19 bytes)
06 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30
<ACK>00F80000FF03FF0000

# V-4C2-05  error response, error code 7151H  (23 bytes)
15 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 37 31 35 31
<NAK>00F80000FF03FF00007151

```

### A.9 Frame 4C — Format 3

```text
# V-4C3-01  request G1  (40 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 03 35 33
<STX>F80000FF03FF000004010000D*0001000003<ETX>53

# V-4C3-02  response G1  (36 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 51 41 43 4B 31 39 39 35 31 32 30 32 31 31 33 30 03 46 45
<STX>F80000FF03FF0000QACK199512021130<ETX>FE

# V-4C3-03  request G3  (52 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 03 42 36
<STX>F80000FF03FF000014010000D*0001000003199512021130<ETX>B6

# V-4C3-04  response G3 (no data)  (22 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 51 41 43 4B 03
<STX>F80000FF03FF0000QACK<ETX>

# V-4C3-05  error response, error code 7151H  (26 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 51 4E 41 4B 37 31 35 31 03
<STX>F80000FF03FF0000QNAK7151<ETX>

```

### A.10 Frame 4C — Format 4

```text
# V-4C4-01  request G1  (41 bytes)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 35 30 0D 0A
<ENQ>F80000FF03FF000004010000D*000100000350<CR><LF>

# V-4C4-02  response G1  (34 bytes)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 44 45 0D 0A
<STX>F80000FF03FF0000199512021130<ETX>DE<CR><LF>

# V-4C4-03  request G3  (53 bytes)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 42 33 0D 0A
<ENQ>F80000FF03FF000014010000D*0001000003199512021130B3<CR><LF>

# V-4C4-04  response G3 (no data)  (19 bytes)
06 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 0D 0A
<ACK>F80000FF03FF0000<CR><LF>

# V-4C4-05  error response, error code 7151H  (23 bytes)
15 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 37 31 35 31 0D 0A
<NAK>F80000FF03FF00007151<CR><LF>

```

### A.11 Frame 4C — Format 5 (Binary)

```text
# V-4C5-01  request G1  (26 bytes)
10 02 12 00 F8 00 00 FF FF 03 00 00 01 04 00 00 64 00 00 A8 03 00 10 03 31 46

# V-4C5-02  response G1  (26 bytes)
10 02 12 00 F8 00 00 FF FF 03 00 00 FF FF 00 00 95 19 02 12 30 11 10 03 30 43

# V-4C5-03  request G3  (32 bytes)
10 02 18 00 F8 00 00 FF FF 03 00 00 01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11 10 03 33 38

# V-4C5-04  response G3 (no data)  (20 bytes)
10 02 0C 00 F8 00 00 FF FF 03 00 00 FF FF 00 00 10 03 30 33

# V-4C5-05  error response, error code 7151H  (20 bytes)
10 02 0C 00 F8 00 00 FF FF 03 00 00 FF FF 51 71 10 03 43 35

# V-4C5-06  request read D16 × 1 (device number 10H → additional code)  (27 bytes)
10 02 12 00 F8 00 00 FF FF 03 00 00 01 04 00 00 10 10 00 00 A8 01 00 10 03 43 39

# V-4C5-07  response D16 = 1010H (2 additional codes)  (24 bytes)
10 02 0E 00 F8 00 00 FF FF 03 00 00 FF FF 00 00 10 10 10 10 10 03 32 35

# V-4C5-M1  manual example: station 05, network 07, PC 03, I/O 0004H, module station 01; 0401/0001 X40 × 5  (26 bytes)
10 02 12 00 F8 05 07 03 04 00 01 00 01 04 01 00 40 00 00 9C 05 00 10 03 30 35

```

### A.12 Frame 3C — Format 1

```text
# V-3C1-01  request G1  (33 bytes)
05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 30 32
<ENQ>F90000FF0004010000D*000100000302

# V-3C1-02  response G1  (26 bytes)
02 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 39 30
<STX>F90000FF00199512021130<ETX>90

# V-3C1-03  request G4  (41 bytes)
05 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 39 36
<ENQ>F90000FF0014010001M*00010000081100110096

# V-3C1-04  response G4 (no data)  (11 bytes)
06 46 39 30 30 30 30 46 46 30 30
<ACK>F90000FF00

# V-3C1-05  error response, error code 7151H  (15 bytes)
15 46 39 30 30 30 30 46 46 30 30 37 31 35 31
<NAK>F90000FF007151

```

### A.13 Frame 3C — Format 2

```text
# V-3C2-01  request G1  (35 bytes)
05 30 30 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 36 32
<ENQ>00F90000FF0004010000D*000100000362

# V-3C2-02  response G1  (28 bytes)
02 30 30 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 46 30
<STX>00F90000FF00199512021130<ETX>F0

# V-3C2-03  request G4  (43 bytes)
05 30 30 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 46 36
<ENQ>00F90000FF0014010001M*000100000811001100F6

# V-3C2-04  response G4 (no data)  (13 bytes)
06 30 30 46 39 30 30 30 30 46 46 30 30
<ACK>00F90000FF00

# V-3C2-05  error response, error code 7151H  (17 bytes)
15 30 30 46 39 30 30 30 30 46 46 30 30 37 31 35 31
<NAK>00F90000FF007151

```

### A.14 Frame 3C — Format 3

```text
# V-3C3-01  request G1  (34 bytes)
02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 03 30 35
<STX>F90000FF0004010000D*0001000003<ETX>05

# V-3C3-02  response G1  (30 bytes)
02 46 39 30 30 30 30 46 46 30 30 51 41 43 4B 31 39 39 35 31 32 30 32 31 31 33 30 03 42 30
<STX>F90000FF00QACK199512021130<ETX>B0

# V-3C3-03  request G4  (42 bytes)
02 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 03 39 39
<STX>F90000FF0014010001M*000100000811001100<ETX>99

# V-3C3-04  response G4 (no data)  (16 bytes)
02 46 39 30 30 30 30 46 46 30 30 51 41 43 4B 03
<STX>F90000FF00QACK<ETX>

# V-3C3-05  error response, error code 7151H  (20 bytes)
02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 37 31 35 31 03
<STX>F90000FF00QNAK7151<ETX>

```

### A.15 Frame 3C — Format 4

```text
# V-3C4-01  request G1  (35 bytes)
05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 30 32 0D 0A
<ENQ>F90000FF0004010000D*000100000302<CR><LF>

# V-3C4-02  response G1  (28 bytes)
02 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 39 30 0D 0A
<STX>F90000FF00199512021130<ETX>90<CR><LF>

# V-3C4-03  request G4  (43 bytes)
05 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 39 36 0D 0A
<ENQ>F90000FF0014010001M*00010000081100110096<CR><LF>

# V-3C4-04  response G4 (no data)  (13 bytes)
06 46 39 30 30 30 30 46 46 30 30 0D 0A
<ACK>F90000FF00<CR><LF>

# V-3C4-05  error response, error code 7151H  (17 bytes)
15 46 39 30 30 30 30 46 46 30 30 37 31 35 31 0D 0A
<NAK>F90000FF007151<CR><LF>

```

### A.16 Frame 1C — Format 1

```text
# V-1C1-01  request WR D100 × 3  (17 bytes)
05 30 30 46 46 57 52 30 44 30 31 30 30 30 33 32 44
<ENQ>00FFWR0D0100032D

# V-1C1-02  response WR: 1995H, 1202H, 1130H  (20 bytes)
02 30 30 46 46 31 39 39 35 31 32 30 32 31 31 33 30 03 35 31
<STX>00FF199512021130<ETX>51

# V-1C1-03  request BR M100 × 8  (17 bytes)
05 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 32 36
<ENQ>00FFBR0M01000826

# V-1C1-04  response BR: 0,0,0,1,0,0,1,1  (16 bytes)
02 30 30 46 46 30 30 30 31 30 30 31 31 03 37 32
<STX>00FF00010011<ETX>72

# V-1C1-05  request WW D100 = 1995H, 1202H, 1130H  (29 bytes)
05 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 39 34
<ENQ>00FFWW0D01000319951202113094

# V-1C1-06  request BW M100 = 1,1,0,0,1,1,0,0  (25 bytes)
05 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 41 46
<ENQ>00FFBW0M01000811001100AF

# V-1C1-07  write response, success (no data)  (5 bytes)
06 30 30 46 46
<ACK>00FF

# V-1C1-08  error response, error code 06H  (7 bytes)
15 30 30 46 46 30 36
<NAK>00FF06

# V-1C1-09  request QR D100 × 3 (AnA/AnU command)  (19 bytes)
05 30 30 46 46 51 52 30 44 30 30 30 31 30 30 30 33 38 37
<ENQ>00FFQR0D0001000387

# V-1C1-10  request WR X40 × 2 word (bit device in word units)  (17 bytes)
05 30 30 46 46 57 52 30 58 30 30 34 30 30 32 34 33
<ENQ>00FFWR0X00400243

# V-1C1-11  request BR M0 × 256 (points = "00")  (17 bytes)
05 30 30 46 46 42 52 30 4D 30 30 30 30 30 30 31 44
<ENQ>00FFBR0M0000001D

```

### A.17 Frame 1C — Format 2

```text
# V-1C2-01  request WR D100 × 3  (19 bytes)
05 30 30 30 30 46 46 57 52 30 44 30 31 30 30 30 33 38 44
<ENQ>0000FFWR0D0100038D

# V-1C2-02  response WR: 1995H, 1202H, 1130H  (22 bytes)
02 30 30 30 30 46 46 31 39 39 35 31 32 30 32 31 31 33 30 03 42 31
<STX>0000FF199512021130<ETX>B1

# V-1C2-03  request BR M100 × 8  (19 bytes)
05 30 30 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 38 36
<ENQ>0000FFBR0M01000886

# V-1C2-04  response BR: 0,0,0,1,0,0,1,1  (18 bytes)
02 30 30 30 30 46 46 30 30 30 31 30 30 31 31 03 44 32
<STX>0000FF00010011<ETX>D2

# V-1C2-05  request WW D100 = 1995H, 1202H, 1130H  (31 bytes)
05 30 30 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 46 34
<ENQ>0000FFWW0D010003199512021130F4

# V-1C2-06  request BW M100 = 1,1,0,0,1,1,0,0  (27 bytes)
05 30 30 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 30 46
<ENQ>0000FFBW0M010008110011000F

# V-1C2-07  write response, success (no data)  (7 bytes)
06 30 30 30 30 46 46
<ACK>0000FF

# V-1C2-08  error response, error code 06H  (9 bytes)
15 30 30 30 30 46 46 30 36
<NAK>0000FF06

```

### A.18 Frame 1C — Format 3

```text
# V-1C3-01  request WR D100 × 3  (18 bytes)
02 30 30 46 46 57 52 30 44 30 31 30 30 30 33 03 33 30
<STX>00FFWR0D010003<ETX>30

# V-1C3-02  response WR: 1995H, 1202H, 1130H  (22 bytes)
02 30 30 46 46 47 47 31 39 39 35 31 32 30 32 31 31 33 30 03 44 46
<STX>00FFGG199512021130<ETX>DF

# V-1C3-03  request BR M100 × 8  (18 bytes)
02 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 03 32 39
<STX>00FFBR0M010008<ETX>29

# V-1C3-04  response BR: 0,0,0,1,0,0,1,1  (18 bytes)
02 30 30 46 46 47 47 30 30 30 31 30 30 31 31 03 30 30
<STX>00FFGG00010011<ETX>00

# V-1C3-05  request WW D100 = 1995H, 1202H, 1130H  (30 bytes)
02 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 03 39 37
<STX>00FFWW0D010003199512021130<ETX>97

# V-1C3-06  request BW M100 = 1,1,0,0,1,1,0,0  (26 bytes)
02 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 03 42 32
<STX>00FFBW0M01000811001100<ETX>B2

# V-1C3-07  write response, success (no data)  (8 bytes)
02 30 30 46 46 47 47 03
<STX>00FFGG<ETX>

# V-1C3-08  error response, error code 06H  (10 bytes)
02 30 30 46 46 4E 4E 30 36 03
<STX>00FFNN06<ETX>

```

### A.19 Frame 1C — Format 4

```text
# V-1C4-01  request WR D100 × 3  (19 bytes)
05 30 30 46 46 57 52 30 44 30 31 30 30 30 33 32 44 0D 0A
<ENQ>00FFWR0D0100032D<CR><LF>

# V-1C4-02  response WR: 1995H, 1202H, 1130H  (22 bytes)
02 30 30 46 46 31 39 39 35 31 32 30 32 31 31 33 30 03 35 31 0D 0A
<STX>00FF199512021130<ETX>51<CR><LF>

# V-1C4-03  request BR M100 × 8  (19 bytes)
05 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 32 36 0D 0A
<ENQ>00FFBR0M01000826<CR><LF>

# V-1C4-04  response BR: 0,0,0,1,0,0,1,1  (18 bytes)
02 30 30 46 46 30 30 30 31 30 30 31 31 03 37 32 0D 0A
<STX>00FF00010011<ETX>72<CR><LF>

# V-1C4-05  request WW D100 = 1995H, 1202H, 1130H  (31 bytes)
05 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 39 34 0D 0A
<ENQ>00FFWW0D01000319951202113094<CR><LF>

# V-1C4-06  request BW M100 = 1,1,0,0,1,1,0,0  (27 bytes)
05 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 41 46 0D 0A
<ENQ>00FFBW0M01000811001100AF<CR><LF>

# V-1C4-07  write response, success (no data)  (7 bytes)
06 30 30 46 46 0D 0A

# V-1C4-08  error response, error code 06H  (9 bytes)
15 30 30 46 46 30 36 0D 0A
<NAK>00FF06<CR><LF>

```

---

## Appendix B — Source traceability

| Section | Source |
|---|---|
| §1.1 Frame comparison | `part2:24-55`, `part2:454-490`, `part2:806-820`, `part5:29-41` |
| §2.1 Numeric fields, byte order | `part2:465-478`, `part2:580-593`, `part3:400-436` |
| §2.2–2.4 Bit/word/dword data | `part3:481-583`, `part5:2542-2544` + command examples |
| §2.5 Sum check | `part2:351-412` |
| §2.6 DLE | `part2:266-319` |
| §2.7 Control codes, EOT/CL | `part2:219-289` |
| §3.2 Device codes | `part3:291-353`, `part5:377-402`, `part5:2374-2392` |
| §3.3 Device encoding | `part3:187-289`, `part5:332-375`, `part5:2343-2372` |
| §3.5 Restrictions | `part3:355-398`, `part5:301-330`, `part5:2316-2341` |
| §4.1 QnA commands | `part3:62-85`, `part3:875-1597`, `part3:621-637` |
| §4.2 1E commands | `part5:2272-2967` |
| §4.3 1C commands | `part5:149-289`, `part5:433-911` |
| §4.4 Limits | the "Number of device points" tables in `part3`/`part5`; PDF Appendix 5 (printed pages 466, 469, 470) |
| §5.1–5.2 3E/4E | `part2:492-653`, `part2:756-784`, `part2:822-1012` |
| §5.3 1E | `part5:2029-2270`, `part2:786-804` |
| §5.4 4C | `part2:57-213`, `part2:329-446`, `part2:665-693`, `part2:822-1039` |
| §5.5 3C | `part2:695-714` |
| §5.6 1C | `part2:735-754`, `part5:100-245` |
