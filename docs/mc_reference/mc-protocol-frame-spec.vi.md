# Đặc tả Frame Protocol — MC Protocol (1E / 3E / 4E / 1C / 3C / 4C)

**Bản tiếng Việt.** Bản chính là bản tiếng Anh: [mc-protocol-frame-spec.md](mc-protocol-frame-spec.md). Hai bản có cùng số mục, ID test case và ID vector; nếu nội dung khác nhau, bản tiếng Anh được ưu tiên.

> - **Mục đích:** đặc tả để implement module giao tiếp PLC Mitsubishi qua MC protocol (lớp frame + lệnh đọc/ghi bit, word) và viết test.
> - **Nguồn:** `part2_message_formats.md`, `part3_commands.md`, `part5_a_series.md` (bản chuyển từ `Mc-protocol.pdf`, đã được verify — xem `verification/`). Giới hạn số điểm được đối chiếu với Appendix 5 của PDF (trang in 466–470); giới hạn của 1E chỉ có ở đó, không có trong md.
> - **Phiên bản:** 1.0 — 2026-09-24
> - **Test vector:** mọi chuỗi byte trong tài liệu được sinh và đối chiếu bằng một bộ encoder tham chiếu; 37 vector trích từ manual khớp 37/37.

---

## Mục lục

- [0. Giới thiệu](#0-giới-thiệu)
- [1. Tổng quan các frame](#1-tổng-quan-các-frame)
- [2. Quy tắc mã hóa chung](#2-quy-tắc-mã-hóa-chung)
- [3. Mô hình device](#3-mô-hình-device)
- [4. Lớp command (request data / response data)](#4-lớp-command-request-data--response-data)
- [5. Lớp frame](#5-lớp-frame)
- [6. Transport và thuật toán nhận frame](#6-transport-và-thuật-toán-nhận-frame)
- [7. Mô hình lỗi](#7-mô-hình-lỗi)
- [8. Kiến trúc module và API đề xuất](#8-kiến-trúc-module-và-api-đề-xuất)
- [9. Kế hoạch test và test case](#9-kế-hoạch-test-và-test-case)
- [10. Lưu ý, lỗi in trong PDF và câu hỏi mở](#10-lưu-ý-lỗi-in-trong-pdf-và-câu-hỏi-mở)
- [Phụ lục A — Golden vectors (frame đầy đủ)](#phụ-lục-a--golden-vectors-frame-đầy-đủ)
- [Phụ lục B — Truy xuất nguồn](#phụ-lục-b--truy-xuất-nguồn)

---

## 0. Giới thiệu

### 0.1 Phạm vi

**Trong phạm vi (v1):**

| Frame | Đường truyền | Kiểu mã | Lệnh cần implement |
|---|---|---|---|
| 3E | Ethernet (TCP/UDP) | ASCII, Binary | 0401, 1401 (word/bit), 0403, 1402 (word/bit) |
| 4E | Ethernet (TCP/UDP) | ASCII, Binary | như 3E |
| 1E | Ethernet (TCP/UDP) | ASCII, Binary | 00H, 01H, 02H, 03H, 04H, 05H |
| 4C | Serial (C24) | Format 1–4 (ASCII), Format 5 (Binary) | như 3E |
| 3C | Serial (C24) | Format 1–4 (ASCII) | như 3E |
| 1C | Serial (C24) | Format 1–4 (ASCII) | BR/JR, WR/QR, BW/JW, WW/QW, BT/JT, WT/QT |

**Ngoài phạm vi v1** (thiết kế cần chừa điểm mở rộng, xem §4.5):

- 2C frame; label access (041A…); buffer memory (0613, 0601…); remote control, file control, lệnh riêng của C24 (1610, 1612, 1615, 1618, 0630…).
- Device extension specification (subcommand `008□`, `00C0`: link direct device `J□\□`, module access device `U□\G`) và monitor condition (subcommand `0040`).
- Batch read/write multiple blocks (0406/1406), monitor (0801/0802, 1E 06H–09H, 1C BM/WM/MB/MN…) — **giai đoạn 2**.
- Extended file register (1C ER/EW/NR/NW…, 1E 17H–3CH), special function module buffer (TR/TW, 0EH/0FH), loopback.
- Device double word của iQ-R (LTN, LSTN, LCN, LZ…) — giai đoạn 2.

### 0.2 Quy ước

| Ký hiệu | Ý nghĩa |
|---|---|
| `1AH`, `1A` trong bảng byte | Giá trị hex |
| `"5000"` | Chuỗi ký tự ASCII (mỗi ký tự là 1 byte: `35 30 30 30`) |
| **ASCII hex N** | Chuyển số sang hex, chữ HOA, đệm `0` bên trái cho đủ N ký tự, gửi chữ số cao trước (manual: *"send it from the upper digits"*) |
| **LE N** | Số nguyên N byte, little-endian (manual: *"send from the lower byte (L: bits 0 to 7)"*) |
| **MUST / SHOULD / MAY** | Bắt buộc / Khuyến nghị / Tuỳ chọn |
| `part3:1086` | `part3_commands.md` dòng 1086 (tương tự `part2:`, `part5:`) |
| Q/L, iQ-R | Hai loại subcommand của lệnh QnA: cho MELSEC-Q/L series (0000/0001) và cho MELSEC iQ-R series (0002/0003) |
| request data | Phần lệnh: command + subcommand + tham số (giống nhau giữa các frame cùng họ lệnh) |
| frame | Request data được bọc thêm header, access route, độ dài, checksum, control code |

### 0.3 Thuật ngữ

| Thuật ngữ | Giải thích |
|---|---|
| E71 | Module Ethernet (3E/4E/1E). C24: module serial (4C/3C/1C) |
| Access route | Các trường chỉ định trạm đích (network No., PC No., station No., …) |
| End code | Mã kết quả trong response (0 = bình thường) |
| Sum check code | Checksum 8 bit của frame serial |
| Additional code | Byte `10H` chèn thêm trước mỗi `10H` trong dữ liệu Format 5 (DLE stuffing) |
| Monitoring timer | Thời gian chờ xử lý (đơn vị 250 ms) trong 3E/4E/1E |
| Message wait | Thời gian trễ trước khi C24 gửi response (đơn vị 10 ms) trong 1C |

---

## 1. Tổng quan các frame

### 1.1 Bảng so sánh

| Thuộc tính | 3E | 4E | 1E | 4C | 3C | 1C |
|---|---|---|---|---|---|---|
| Đường truyền | TCP/UDP | TCP/UDP | TCP/UDP | Serial | Serial | Serial |
| Kiểu mã | ASCII / Binary | ASCII / Binary | ASCII / Binary | F1–F4 ASCII, F5 Binary | F1–F4 ASCII | F1–F4 ASCII |
| Đầu request | Subheader `5000` | Subheader `5400` + serial + `0000` | Subheader = mã lệnh (`00`…`05`) | Control code + Frame ID `F8` | Control code + Frame ID `F9` | Control code (không có Frame ID) |
| Access route | Network, PC, Req. dest. I/O, Req. dest. station | như 3E | PC No. | Station, Network, PC, Req. dest. I/O, Req. dest. station, Self-station | Station, Network, PC, Self-station | Station, PC |
| Trường độ dài | Request/Response data length | như 3E | Không có | F5: Number of data bytes; F1–F4: không có | Không có | Không có |
| Timer | Monitoring timer | Monitoring timer | ACPU monitoring timer | — | — | Message wait |
| Checksum | — | — | — | Sum check (tuỳ cấu hình PLC) | Sum check (tuỳ cấu hình PLC) | Sum check (tuỳ cấu hình PLC) |
| Kết quả | End code 2 byte + Error information | như 3E | End code 1 byte (+ Abnormal code khi `5BH`) | ACK/NAK (F1,F2,F4), QACK/QNAK (F3), `FFFF`+`0000`/error (F5) | như 4C (không có F5) | ACK/NAK (F1,F2,F4), GG/NN (F3) |
| Họ lệnh | QnA (0401…) | QnA | A-series (00H…) | QnA | QnA | A-series (BR…) |

Nguồn: `part2:24-55`, `part2:454-490`, `part2:657-820`, `part5:29-41`.

**Nhận xét quan trọng cho thiết kế:**

1. **3E, 4E, 3C, 4C dùng chung request data của họ lệnh QnA.** Một bộ encoder/decoder command QnA dùng được cho 4 frame, chỉ khác lớp bọc frame. 4C Format 5 dùng request data binary giống hệt 3E binary; 4C/3C Format 1–4 dùng request data ASCII giống hệt 3E ASCII.
2. **1E và 1C có họ lệnh riêng (tương thích A-series)**, cách mã hóa device và dữ liệu khác QnA.
3. Chỉ 3E/4E và 4C Format 5 có trường độ dài trong response (4C F5 vẫn cần DLE ETX để xác định biên). 1E phải **tự tính độ dài response** từ request; các frame serial Format 1–4 dùng **control code** (STX/ETX, ACK/NAK, CR LF) để xác định biên.

### 1.2 Phân lớp

```text
┌────────────────────────────────────────────────────────────────┐
│ Client API   read_bits / read_words / write_bits / write_words │  kiểm tra tham số, chia nhỏ (chunking),
│              read_random / write_random_bits / _words           │  kiểm tra năng lực của frame
├────────────────────────────────────────────────────────────────┤
│ Command      QnaCommand | A1eCommand | A1cCommand               │  tạo request data, giải mã response data
├────────────────────────────────────────────────────────────────┤
│ Field codec  AsciiCodec | BinaryCodec                           │  u8/u16/u32, device, bits, words, dwords
├────────────────────────────────────────────────────────────────┤
│ Frame        Frame3E | Frame4E | Frame1E | Frame4C | Frame3C | Frame1C │  header, route, length, checksum,
│                                                                │  DLE, control code, parse response
├────────────────────────────────────────────────────────────────┤
│ Transport    TcpTransport | UdpTransport | SerialTransport      │  chỉ vận chuyển byte + timeout
└────────────────────────────────────────────────────────────────┘
```

Nguyên tắc:

- Lớp **Command** và **Frame** MUST là code thuần (không I/O): `encode(...) -> bytes`, `parser.feed(bytes)`. Nhờ vậy toàn bộ protocol test được bằng vector, không cần PLC.
- Lớp **Transport** không biết gì về protocol, chỉ đọc/ghi byte và báo timeout.
- Lớp **Client** chứa logic nghiệp vụ: chọn lệnh theo frame, chia request khi vượt giới hạn, ghép kết quả.

---

## 2. Quy tắc mã hóa chung

### 2.1 Trường số

| Kích thước logic | ASCII | Binary | Ví dụ giá trị 0018H |
|---|---|---|---|
| u8 (1 byte) | ASCII hex 2 | 1 byte | — |
| u16 (2 byte) | ASCII hex 4 | LE 2 | ASCII `"0018"` = `30 30 31 38`; Binary `18 00` |
| u32 (4 byte) | ASCII hex 8 | LE 4 | — |

Nguồn: `part2:465-478`, `part2:588-593`.

- Encoder MUST dùng chữ HOA (`A`–`F`) (`part3:406`, `part5:2527`). Decoder SHOULD chấp nhận cả chữ thường.
- Encoder MUST báo lỗi khi giá trị vượt độ rộng trường (ví dụ u8 = 256), trừ các trường "số điểm" 1 byte của 1E/1C, nơi 256 được mã hóa thành `00` (xem §2.1.1).

#### 2.1.1 Ngoại lệ — các trường KHÔNG theo quy tắc chung (MUST đọc kỹ)

| # | Trường | Quy tắc | Nguồn |
|---|---|---|---|
| E1 | Subheader 3E/4E | Chuỗi byte cố định, **không** phải u16: request `50 00` / `"5000"`, response `D0 00` / `"D000"`; 4E: `54 00` / `"5400"`, `D4 00` / `"D400"`. Sau serial No. của 4E là `00 00` / `"0000"` | `part2:541-578` |
| E2 | Device code QnA | ASCII là **mã chữ** (`"D*"`, `"TN"`, `"D***"`), Binary là **mã số** (`A8H`, `00A8H`). Hai bảng khác nhau | `part3:210-239` |
| E3 | Device number QnA (ASCII) | 6 chữ số (Q/L) hoặc 8 chữ số (iQ-R) **theo cơ số của device**: thập phân cho D, M, T…; hex cho X, Y, B, W… | `part3:241-281` |
| E4 | Device number 1E (ASCII) | Luôn 8 chữ số **hex** (D1234 → `"000004D2"`) | `part5:2354-2372` |
| E5 | Device number 1C | Theo cơ số của device; 4 chữ số (T/C: 3) cho lệnh ACPU, 6 chữ số (T/C: 5) cho lệnh AnA/AnU | `part5:341-375` |
| E6 | Sum check code | Luôn là 2 ký tự ASCII hex (kể cả Format 5 binary), gửi chữ số cao trước | `part2:403-412` |
| E7 | Message wait (1C) | 1 ký tự ASCII hex (`0`–`F`, đơn vị 10 ms) | `part5:187-203` |
| E8 | Số điểm của 1E / 1C | u8; 256 điểm mã hóa là `00` | `part5:414-427`, `part5:2517-2540` |
| E9 | Error code 1C | 2 ký tự ASCII hex (4C/3C: 4 ký tự; Format 5: LE 2) | `part5:237-245`, `part2:437-442` |

### 2.2 Dữ liệu bit

| Frame / kiểu mã | Ghi (request) | Đọc (response) |
|---|---|---|
| QnA ASCII (3E/4E ASCII, 4C/3C F1–F4), 1C | 1 ký tự / điểm: `'1'` (31H) = ON, `'0'` (30H) = OFF | như bên ghi |
| QnA Binary (3E/4E binary, 4C F5), 1E Binary | 4 bit / điểm; điểm đầu tiên ở **nibble cao**; số điểm lẻ → nibble thấp của byte cuối = 0 | như bên ghi |
| 1E ASCII | 1 ký tự / điểm | 1 ký tự / điểm; **số điểm lẻ → PLC thêm 1 ký tự dummy `'0'`** ở cuối |

Ví dụ M10..M14 = ON, OFF, ON, OFF, ON: ASCII `"10101"` = `31 30 31 30 31`; Binary (E71) `10 10 10` (`part3:506-510`).
Ví dụ M100..M107 = 0,0,0,1,0,0,1,1: Binary `00 01 00 11` (`part3:1089`).

```text
pack_bits(bits):                    # binary
    for i in 0, 2, 4, ...:
        hi = bits[i]; lo = bits[i+1] if i+1 < len(bits) else 0
        out.append((hi << 4) | lo)  # mỗi nibble chỉ là 0 hoặc 1
unpack_bits(buf, n):
    for i in 0..n-1: bit = (buf[i/2] >> (4 if i even else 0)) & 0x0F  → MUST là 0 hoặc 1, khác → lỗi protocol
```

### 2.3 Dữ liệu word và double word

| | ASCII | Binary |
|---|---|---|
| Word (16 bit) | ASCII hex 4 / word | LE 2 / word |
| Double word (32 bit) | ASCII hex 8 / dword | LE 4 / dword |

- D350 = 56ABH, D351 = 170FH, đọc 2 word: ASCII `"56AB170F"`; Binary `AB 56 0F 17` (`part3:534-558`).
- Đọc D350 dạng double word: ASCII `"170F56AB"`; Binary `AB 56 0F 17` → giá trị 170F56ABH (word thấp = D350) (`part3:573-581`).

### 2.4 Bit device đọc/ghi theo word

Một word chứa 16 điểm liên tiếp: **bit i (bit 0 = LSB) ↔ device (head + i)**. Double word chứa 32 điểm theo cùng quy tắc.

Ví dụ đọc M100, 2 word, response `1234H`, `0002H` → M102, M104, M105, M109, M112 = ON (từ `1234H`), M117 = ON (bit 1 của `0002H`) (`part3:980-989`).

```text
words_to_bits(words):  [ (w >> i) & 1  for w in words for i in 0..15 ]
bits_to_words(bits):   len(bits) MUST chia hết cho 16; word k = Σ bits[16k+i] << i
```

### 2.5 Sum check code

**Thuật toán:** cộng tất cả byte trong vùng tính (sum check range), lấy 8 bit thấp, mã hóa ASCII hex 2 (`part2:375-412`).

```text
sumcheck(data) = ascii_hex2( sum(data) & 0xFF )
```

| Ví dụ (từ manual) | Vùng tính | Tổng | Sum check |
|---|---|---|---|
| 1C Format 1: `00FFBR3M0000` | `30 30 46 46 42 52 33 4D 30 30 30 30` | 2C0H | `"C0"` = `43 30` |
| 4C Format 5 (xem vector `V-4C5-M1`) | `12 00 F8 05 … 05 00` (bỏ additional code) | 205H | `"05"` = `30 35` |

**Vùng tính theo format** (`part2:57-213`, `part2:365-373`):

| Format | Request | Response có dữ liệu (STX … ETX) |
|---|---|---|
| F1 | Sau ENQ → hết request data | Sau STX → **gồm** ETX |
| F2 | Từ Block No. → hết request data | Từ Block No. → gồm ETX |
| F3 | Sau STX → **gồm** ETX | Sau STX → gồm ETX |
| F4 | Như F1 (CR LF nằm sau sum check, không tính) | Như F1, CR LF sau sum check |
| F5 | Từ Number of data bytes → hết request data (không gồm DLE ETX, **không tính additional code**) | Tương tự |

- Response ACK/NAK của F1, F2, F4: **không có** sum check.
- Response F3 không dữ liệu (`QACK`/`GG` + ETX) và response lỗi F3 (`QNAK`/`NN` + error code + ETX): theo PDF **không có** sum check (xem §10, mục Q1).
- Khi PLC cấu hình sum check = "None": request không gửi sum check, response không có sum check (`part2:357-363`). Cấu hình này MUST là tham số của frame codec.

### 2.6 Additional code (DLE stuffing) — Format 5

Nguồn: `part2:266-319`.

```text
raw     = number_of_data_bytes(LE 2) + frame_id(F8) + access_route(7) + request_data
count   = len(frame_id + access_route + request_data)       # KHÔNG tính additional code
sum     = sumcheck(raw)                                     # KHÔNG tính additional code
wire    = 10 02 + stuff(raw) + 10 03 + sum
stuff(x): thay mỗi byte 10H bằng 10H 10H (áp dụng cả cho Number of data bytes)
```

Giải mã (receiver): sau `10 02`, đọc tuần tự; gặp `10 10` → 1 byte `10H`; gặp `10 03` → kết thúc thân frame, đọc tiếp 2 ký tự sum check (nếu bật); gặp `10` + byte khác → lỗi protocol.

### 2.7 Control code

| Ký hiệu | Mã | Dùng ở |
|---|---|---|
| STX | 02H | F1–F5 |
| ETX | 03H | F1–F5 |
| EOT | 04H | Khởi tạo lại trình tự truyền (F1–F4) |
| ENQ | 05H | Đầu request F1, F2, F4 |
| ACK | 06H | Response bình thường không dữ liệu (F1, F2, F4) |
| LF | 0AH | Cuối frame F4 |
| CL | 0CH | Khởi tạo lại trình tự truyền (F1–F4) |
| CR | 0DH | Cuối frame F4 |
| DLE | 10H | F5 |
| NAK | 15H | Response lỗi (F1, F2, F4) |

EOT / CL (`part2:239-264`): gửi `EOT` (F1–F3) hoặc `EOT CR LF` (F4) để huỷ request đang xử lý và đưa C24 về trạng thái chờ lệnh. C24 **không** gửi response cho EOT/CL. Với F5 phải dùng lệnh 1615 (ngoài phạm vi v1).

---

## 3. Mô hình device

### 3.1 Mô hình dữ liệu

```text
enum DeviceKind { BIT, WORD, DWORD }
enum Radix      { DEC, HEX }

record DeviceType {
    symbol        : string          # "D", "X", "TN", "SM" ...
    kind          : DeviceKind
    radix         : Radix           # cơ số khi viết số hiệu device (D100 thập phân, X1F hex)
    qna_ascii_ql  : string?         # "D*"    (2 ký tự)   null = không hỗ trợ
    qna_bin_ql    : u8?             # 0xA8
    qna_ascii_iqr : string?         # "D***"  (4 ký tự)
    qna_bin_iqr   : u16?            # 0x00A8
    e1_code       : u16?            # 0x4420  (1E)
    c1_code       : string?         # "D"     (1C)
}

record Device { type: DeviceType, number: u32 }
```

### 3.2 Bảng device code

Nguồn: QnA `part3:291-340`; 1E `part5:2374-2392`; 1C `part5:377-402`.

| Symbol | Tên | Kiểu | Cơ số | QnA ASCII Q/L | QnA Bin Q/L | QnA ASCII iQ-R | QnA Bin iQ-R | 1E code | 1C code |
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

¹ 1E/1C truy cập special relay/register qua M9000–M9255 (→ SM1000–SM1255) và D9000–D9255 (→ SD1000–SD1255) (`part5:320-325`, `part5:2329-2334`).
² 1E không có mã riêng cho L, S: "For L and S, perform accessing by specifying 'M'" (`part5:2448`). Library MAY chấp nhận L/S và mã hóa bằng `4D20`, giữ nguyên số hiệu.

Các device long của iQ-R (LTS/LTC/LTN, LSTS/LSTC/LSTN, LCS/LCC/LCN, LZ; chỉ LTN, LSTN, LCN, LZ là double word, còn lại là bit device) nằm ngoài phạm vi v1; các ràng buộc riêng của chúng ở `part3:365-386`, `part3:945-969`.

- Ký tự `*` trong device code ASCII của QnA có thể thay bằng dấu cách (20H) (`part3:225`). Encoder MUST dùng `*`.
- Chữ số `0` đứng đầu device number ASCII có thể thay bằng dấu cách (`part3:254`, `part5:381`). Encoder MUST dùng `0`.

### 3.3 Mã hóa một device theo từng họ frame

| Họ | Thứ tự trường | Kích thước |
|---|---|---|
| QnA ASCII Q/L | code (2) → number (6, theo cơ số device) | 8 ký tự |
| QnA ASCII iQ-R | code (4) → number (8, theo cơ số device) | 12 ký tự |
| QnA Binary Q/L | number (LE 3) → code (1) | 4 byte |
| QnA Binary iQ-R | number (LE 4) → code (LE 2) | 6 byte |
| 1E ASCII | code (ASCII hex 4) → number (ASCII hex 8) | 12 ký tự |
| 1E Binary | number (LE 4) → code (LE 2) | 6 byte |
| 1C lệnh ACPU (BR, WR…) | code (1) → number (4); T/C: code (2) → number (3) | 5 ký tự |
| 1C lệnh AnA/AnU (JR, QR…) | code (1) → number (6); T/C: code (2) → number (5) | 7 ký tự |

Ví dụ (đã kiểm chứng):

| Device | QnA ASCII Q/L | QnA Bin Q/L | QnA ASCII iQ-R | QnA Bin iQ-R | 1E ASCII | 1E Bin | 1C ACPU | 1C AnA/AnU |
|---|---|---|---|---|---|---|---|---|
| D100 | `D*000100` | `64 00 00 A8` | `D***00000100` | `64 00 00 00 A8 00` | `442000000064` | `64 00 00 00 20 44` | `D0100` | `D000100` |
| X1F | `X*00001F` | `1F 00 00 9C` | `X***0000001F` | `1F 00 00 00 9C 00` | `58200000001F` | `1F 00 00 00 20 58` | `X001F` | `X00001F` |
| TN10 | `TN000010` | `0A 00 00 C2` | `TN**00000010` | `0A 00 00 00 C2 00` | `544E0000000A` | `0A 00 00 00 4E 54` | `TN010` | `TN00010` |
| M1234 | `M*001234` | `D2 04 00 90` | `M***00001234` | `D2 04 00 00 90 00` | `4D20000004D2` | `D2 04 00 00 20 4D` | `M1234` | `M001234` |
| M9000 | `M*009000` | `28 23 00 90` | `M***00009000` | `28 23 00 00 90 00` | `4D2000002328` | `28 23 00 00 20 4D` | `M9000` | `M009000` |

### 3.4 Parse chuỗi địa chỉ device

`parse_device("D100") -> Device(D, 100)`

1. Symbol không phân biệt hoa/thường; so khớp **dài nhất trước** trên danh sách symbol (ví dụ `STS` trước `S`, `SM`/`SD`/`SB`/`SW` trước `S`, `DX`/`DY` trước `D`, `ZR` trước `Z`, `CS`/`CC`/`CN` và `TS`/`TC`/`TN` là symbol 2 ký tự).
2. Phần số được parse theo cơ số của device: device HEX chấp nhận `[0-9A-Fa-f]+`, device DEC chấp nhận `[0-9]+`. Ký tự sai cơ số (ví dụ `M1F`) → `InvalidDeviceError`.
3. `T10`, `C10` (không nói rõ contact/coil/current value) → `InvalidDeviceError` (MUST dùng `TN`/`TS`/`TC`, `CN`/`CS`/`CC`). Library MAY cung cấp alias có cấu hình, mặc định tắt.
4. Kiểm tra độ rộng theo họ frame khi **mã hóa** (không phải khi parse):

| Họ | Giới hạn số hiệu |
|---|---|
| QnA Q/L | ASCII 6 chữ số (DEC ≤ 999999, HEX ≤ FFFFFF), Binary ≤ FFFFFFH |
| QnA iQ-R | ASCII 8 chữ số, Binary ≤ FFFFFFFFH |
| 1E | ≤ FFFFFFFFH |
| 1C ACPU | 4 chữ số (T/C: 3) |
| 1C AnA/AnU | 6 chữ số (T/C: 5) |

5. Library **không** kiểm tra dải device thực tế của CPU (phụ thuộc tham số PLC); PLC sẽ trả lỗi. Bảng dải device của A-series (`part5:377-402`, `part5:2402-2515`) MAY dùng cho validation tuỳ chọn.

### 3.5 Ràng buộc truy cập

| Quy tắc | Áp dụng | Nguồn |
|---|---|---|
| Lệnh đơn vị bit dùng cho bit device (library MUST từ chối word device) | Mọi frame | `part3:1578` (1402 bit), `part5:253` (1C), `part5:2548` (1E) |
| Truy cập bit device theo word: head number MUST là bội của 16 | 1C (WR/WW/WT), 1E (01/03/05), lệnh QnA khi đích là A-series | `part5:408-410`, `part5:542-543`, `part5:2398-2400`, `part3:943`, `part3:1155`, `part3:1491` |
| Special relay M9000–M9255 truy cập theo word (1C WR/WW/WT, 1E 01H/03H/05H): head number MUST là 9000 + bội của 16 (trong vùng này quy tắc này thay cho quy tắc bội của 16, nên M9008 bị từ chối dù 9008 = 16 × 563; xem §10 Q8) | 1C, 1E | `part5:410`, `part5:2400` |
| Không dùng được trong 0401: LTS, LTC, LSTS, LSTC, LZ. Trong 1401 (word và bit): thêm LTN, LSTN; 1401 bit còn cấm LCN | QnA | `part3:923-927`, `part3:1135-1139`, `part3:1236-1241` |
| Không dùng được trong 0403: LTS, LTC, LSTS, LSTC, LCS, LCC | QnA | `part3:1373-1377` |
| Không truy cập được từ 1C/1E khi đích không phải ACPU: device bổ sung, L, S (1C: L/S → M), R (1C: chỉ R của QnACPU) | 1C, 1E | `part5:305-318`, `part5:2318-2327` |

Với đích Q/L/iQ-R qua lệnh QnA, head number của bit device đọc theo word **không** bắt buộc là bội của 16. Library SHOULD có tuỳ chọn `a_series_target = true` để bật kiểm tra này cho QnA frame.

---

## 4. Lớp command (request data / response data)

Mỗi command là một object thuần, biết:

- `encode(codec) -> bytes`: tạo request data.
- `has_response_data`: lệnh đọc = true, lệnh ghi = false.
- `response_size(codec) -> int`: kích thước response data mong đợi (dùng để kiểm tra, và bắt buộc cho 1E vì 1E không có trường độ dài).
- `decode(codec, payload) -> result`: giải mã response data; MUST báo `LengthMismatchError` nếu kích thước khác `response_size`.

### 4.1 Họ lệnh QnA (dùng cho 3E, 4E, 3C, 4C)

Nguồn: `part3:62-85`, `part3:875-1597`.

| Thao tác | Command | Sub Q/L | Sub iQ-R | Response data |
|---|---|---|---|---|
| Batch read, word units | 0401 | 0000 | 0002 | N word |
| Batch read, bit units | 0401 | 0001 | 0003 | N điểm bit |
| Batch write, word units | 1401 | 0000 | 0002 | không có |
| Batch write, bit units | 1401 | 0001 | 0003 | không có |
| Random read, word units | 0403 | 0000 | 0002 | m word + n dword |
| Random write, word units (test) | 1402 | 0000 | 0002 | không có |
| Random write, bit units (test) | 1402 | 0001 | 0003 | không có |

- Command và subcommand là u16: ASCII `"0401"`, Binary `01 04`.
- Đích là iQ-R/iQ-L vẫn truy cập được các device tương đương Q/L bằng subcommand Q/L (`part3:85`). **Mặc định MUST dùng subcommand Q/L**; chỉ dùng subcommand iQ-R khi cấu hình `series = IQR` (cần cho số hiệu device > 6 chữ số hoặc device chỉ có ở iQ-R). Lưu ý giới hạn điểm của lệnh random giảm một nửa với subcommand iQ-R.

#### 4.1.1 Batch read — 0401

| # | Trường | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"0401"` | `01 04` |
| 2 | Subcommand | `"0000"` word / `"0001"` bit (iQ-R: `"0002"` / `"0003"`) | `00 00` / `01 00` (iQ-R: `02 00` / `03 00`) |
| 3 | Head device | §3.3 (8 hoặc 12 ký tự) | §3.3 (4 hoặc 6 byte) |
| 4 | Number of device points | u16 → 4 ký tự | u16 → LE 2 |

Response data:

| Loại | ASCII | Binary |
|---|---|---|
| Word units (N word) | 4N ký tự | 2N byte |
| Bit units (N điểm) | N ký tự | ⌈N/2⌉ byte |

Ví dụ từ manual:

- Đọc word TN100..TN102 = 1234H, 0002H, 1DEFH. Binary request `01 04 00 00 64 00 00 C2 03 00`, response `34 12 02 00 EF 1D`; ASCII request `"04010000TN0001000003"`, response `"123400021DEF"` (`part3:991-1008`).
- Đọc bit M100..M107. Binary request `01 04 01 00 64 00 00 90 08 00`, response `00 01 00 11`; ASCII request `"04010001M*0001000008"`, response `"00010011"` (`part3:1073-1089`).

#### 4.1.2 Batch write — 1401

| # | Trường | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"1401"` | `01 14` |
| 2 | Subcommand | `"0000"` word / `"0001"` bit (iQ-R `"0002"`/`"0003"`) | `00 00` / `01 00` (iQ-R `02 00`/`03 00`) |
| 3 | Head device | §3.3 | §3.3 |
| 4 | Number of device points | 4 ký tự | LE 2 |
| 5 | Write data | word: 4 ký tự/word; bit: 1 ký tự/điểm | word: LE 2/word; bit: nibble-packed |

Response data: không có.

Ví dụ từ manual: ghi D100..D102 = 1995H, 1202H, 1130H → Binary `01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11`; ghi M100..M107 = 1,1,0,0,1,1,0,0 → Binary `01 14 01 00 64 00 00 90 08 00 11 00 11 00` (`part3:1177-1190`, `part3:1260-1273`).

#### 4.1.3 Random read — 0403

| # | Trường | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"0403"` | `03 04` |
| 2 | Subcommand | `"0000"` (iQ-R `"0002"`) | `00 00` (iQ-R `02 00`) |
| 3 | Number of word access points (m) | u8 → 2 ký tự | 1 byte |
| 4 | Number of double word access points (n) | u8 → 2 ký tự | 1 byte |
| 5 | m device truy cập word | §3.3 | §3.3 |
| 6 | n device truy cập double word | §3.3 | §3.3 |

Response data: m word rồi n dword → ASCII 4m + 8n ký tự; Binary 2m + 4n byte.

- Bit device trong nhóm word đọc 16 điểm; trong nhóm dword đọc 32 điểm (`part3:1356-1361`).
- m = 0 hoặc n = 0 thì bỏ trống nhóm tương ứng; m + n ≥ 1.
- Với subcommand Q/L, file register ZR của High Performance model QCPU tính là 2 điểm mỗi điểm truy cập (`part3:1363-1365`); Client MUST tính mỗi điểm truy cập ZR là 2 khi `high_performance_qcpu = true` (§8.3, §8.5).
- Subcommand có monitor condition (`0040`) nằm ngoài phạm vi v1.
- Không dùng được với đích A-series (`part3:1354`).
- Ví dụ đầy đủ trong manual: `part3:1381-1402` (vector `CMD-11` ở §9).

#### 4.1.4 Random write, word units (test) — 1402 / 0000

| # | Trường | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"1402"` | `02 14` |
| 2 | Subcommand | `"0000"` (iQ-R `"0002"`) | `00 00` (iQ-R `02 00`) |
| 3 | m (word access points) | 2 ký tự | 1 byte |
| 4 | n (double word access points) | 2 ký tự | 1 byte |
| 5 | m × [device, write data u16] | device + 4 ký tự | device + LE 2 |
| 6 | n × [device, write data u32] | device + 8 ký tự | device + LE 4 |

Response data: không có. Ví dụ: `part3:1501-1517`.

#### 4.1.5 Random write, bit units (test) — 1402 / 0001

| # | Trường | ASCII | Binary |
|---|---|---|---|
| 1 | Command | `"1402"` | `02 14` |
| 2 | Subcommand | `"0001"` (iQ-R `"0003"`) | `01 00` (iQ-R `03 00`) |
| 3 | Number of bit access points (n) | 2 ký tự | 1 byte |
| 4 | n × [device, set/reset] | Set/reset Q/L: `"01"`/`"00"`; iQ-R: `"0001"`/`"0000"` | Q/L: `01`/`00`; iQ-R: `01 00`/`00 00` |

Response data: không có. Nguồn set/reset: `part3:621-637`. Ví dụ M50 OFF, Y2F ON: Binary `02 14 01 00 02 32 00 00 90 00 2F 00 00 9D 01` (`part3:1586-1597`).

### 4.2 Họ lệnh 1E

Nguồn: `part5:2272-2967`. Với 1E, **mã lệnh nằm ở subheader** của frame (§5.3); request data bắt đầu ngay sau monitoring timer. Subheader của response = mã lệnh OR 80H.

| Lệnh | Thao tác | Request data | Response data |
|---|---|---|---|
| 00H | Batch read, bit units | head device → points (u8) → `00` | N điểm bit |
| 01H | Batch read, word units | head device → points (u8) → `00` | N word |
| 02H | Batch write, bit units | head device → points (u8) → `00` → write data | không có |
| 03H | Batch write, word units | head device → points (u8) → `00` → write data | không có |
| 04H | Test (random write), bit units | n (u8) → `00` → n × [device → ON/OFF (u8: `00`/`01`)] | không có |
| 05H | Test (random write), word units | n (u8) → `00` → n × [device → write data (u16)] | không có |

- Device mã hóa theo §3.3 (1E): ASCII `code(4 hex) + number(8 hex)`; Binary `number(LE 4) + code(LE 2)`.
- Points / n là u8, 256 → `00` (`part5:2517-2522`). Trường `00` (fixed value) là u8: ASCII `"00"`, Binary `00`. Trong ASCII, "n + fixed value" hiện ra thành `"0300"` với n = 3 (`part5:2890`).
- ON/OFF của lệnh 04H là u8: ASCII `"01"`/`"00"`, Binary `01`/`00` (`part5:2870-2877`).
- Dữ liệu bit/word theo §2.2, §2.3. Đọc bit ASCII với N lẻ → response có N + 1 ký tự, ký tự cuối là dummy (`part5:2564-2565`).
- Bit device trong lệnh 01H/03H/05H: head number MUST là bội của 16 (`part5:2650`, `part5:2794`, `part5:2924`).

Kích thước response data:

| Lệnh | ASCII | Binary |
|---|---|---|
| 00H (N điểm) | N + (N mod 2) ký tự | ⌈N/2⌉ byte |
| 01H (N word) | 4N ký tự | 2N byte |
| 02H–05H | 0 | 0 |

Ví dụ từ manual (đã kiểm chứng): 00H M100 × 12 điểm: ASCII `"4D20000000640C00"`, Binary `64 00 00 00 20 4D 0C 00` (`part5:2588-2620`); 01H Y40 × 2 word: response ASCII `"829D553E"`, Binary `9D 82 3E 55` (`part5:2662-2695`).

### 4.3 Họ lệnh 1C

Nguồn: `part5:149-185`, `part5:247-289`, `part5:433-911`. Request data = **command (2 ký tự) + message wait (1 ký tự) + character area**.

| Thao tác | ACPU | AnA/AnU | Character area của request | Character area của response |
|---|---|---|---|---|
| Batch read, bit | `BR` | `JR` | head device → points (2 ký tự) | N ký tự |
| Batch read, word | `WR` | `QR` | head device → points (số word, 2 ký tự) | 4N ký tự |
| Batch write, bit | `BW` | `JW` | head device → points → N ký tự | không có |
| Batch write, word | `WW` | `QW` | head device → points → 4N ký tự | không có |
| Test, bit | `BT` | `JT` | n (2 ký tự) → n × [device → `'0'`/`'1'` (1 ký tự)] | không có |
| Test, word | `WT` | `QT` | n (2 ký tự) → n × [device → 4 ký tự] | không có |

- Toàn bộ là ASCII (1C không có binary).
- Message wait: `0`–`F`, đơn vị 10 ms (0–150 ms), ví dụ 100 ms → `"A"` (`part5:187-203`).
- Device: §3.3 (1C). Lệnh ACPU (`BR`…) dùng được cho mọi ACPU; lệnh AnA/AnU (`JR`…) chỉ dùng cho AnA/AnUCPU (`part5:277-289`). Cấu hình `command_set = ACPU | ANA`, mặc định `ACPU`.
- Points u8, 256 → `"00"` (`part5:414-427`).
- Set/reset của BT: `'0'` = OFF, `'1'` = ON, 1 ký tự (`part5:809-812`) — khác với 04H của 1E (2 ký tự).
- WR/WW/WT với bit device: head number MUST là bội của 16.

Ví dụ từ manual: `BR` X40 × 5, message wait 100 ms: `"BRAX004005"`, response `"01101"` (`part5:473-496`); `WW` D0 × 2 = 1234H, ACD7H: `"WW0D0000021234ACD7"` (`part5:745-759`).

### 4.4 Giới hạn số điểm mỗi lần truyền

Nguồn: bảng của từng lệnh trong `part3` và Appendix 5 của PDF (trang in 466, 469, 470).

**Họ QnA (3E/4E/3C/4C)** — cột "iQ-R/Q/L" là đích MELSEC iQ-R, iQ-L, Q, L; "QnA" là đích QnA hoặc trạm khác qua mạng QnA; "A" là đích A-series:

| Lệnh | Điều kiện | iQ-R/Q/L | QnA | A |
|---|---|---|---|---|
| 0401 word | word device | 960 điểm | 480 | 64 |
| 0401 word | bit device | 960 word (15360 điểm) | 480 word | 32 word |
| 0401 bit | C24 (4C/3C) | 7904 điểm | 3952 | 256 |
| 0401 bit | E71 ASCII | 3584 điểm | 1792 | 256 |
| 0401 bit | E71 Binary | 7168 điểm | 3584 | 256 |
| 1401 word | word device | 960 điểm | 480 | 64 |
| 1401 word | bit device | 960 word | 480 word | 10 word (160 điểm) |
| 1401 bit | C24 / E71 ASCII / E71 Binary | 7904 / 3584 / 7168 | 3952 / 1792 / 3584 | 160 |
| 0403 | m + n, sub 0000 | 192 | 96 | — |
| 0403 | m + n, sub 0002 | 96 | — | — |
| 1402 word | m×12 + n×14, sub 0000 | ≤ 1920 | ≤ 960 | m ≤ 10 |
| 1402 word | m×12 + n×14, sub 0002 | ≤ 960 | — | — |
| 1402 bit | n, sub 0001 | 188 | 94 | 20 |
| 1402 bit | n, sub 0003 | 94 | — | — |

**Họ 1E:**

| Lệnh | Điều kiện | Tối đa |
|---|---|---|
| 00H | — | 256 điểm |
| 01H | bit device | 128 word (2048 điểm) |
| 01H | word device | 256 điểm |
| 02H | — | 256 điểm |
| 03H | bit device | 40 word (640 điểm) |
| 03H | word device | 256 điểm |
| 04H | — | 80 điểm |
| 05H | bit device / word device | 40 word / 40 điểm |

**Họ 1C:**

| Lệnh | Điều kiện | Tối đa |
|---|---|---|
| BR/JR | — | 256 điểm |
| WR/QR | bit device / word device | 32 word (512 điểm) / 64 điểm |
| BW/JW | — | 160 điểm |
| WW/QW | bit device / word device | 10 word (160 điểm) / 64 điểm |
| BT/JT | — | 20 điểm |
| WT/QT | bit device / word device | 10 word / 10 điểm |

Quy tắc implement:

- Command encoder MUST kiểm tra `1 ≤ N ≤ max` với **max tuyệt đối theo từng trường** và báo `PointCountError`: Number of device points của 0401/1401 là u16 (≤ 65535); m, n của 0403/1402 và Number of bit access points của 1402 là u8 (≤ 255, `part3:419-436`); số điểm của 1E/1C là u8 với 256 mã hóa thành `00` (≤ 256).
- 0403 với ZR của High Performance model QCPU (subcommand Q/L): mỗi điểm tính × 2 (`part3:1363-1365`).
- Lớp Client MUST dùng bảng giới hạn trên (theo frame, kiểu mã, loại device, `series`, `target_family`) để chia request (§8.5). Bảng giới hạn MUST là dữ liệu cấu hình được (không hard-code rải rác), vì đích thực tế có thể giới hạn chặt hơn.

### 4.5 Điểm mở rộng cho giai đoạn 2

Thiết kế Command MUST cho phép thêm lệnh mà không sửa lớp Frame. Các lệnh dự kiến:

| Lệnh | Frame | Ghi chú | Nguồn |
|---|---|---|---|
| 0406 / 1406 batch read/write multiple blocks | QnA | Khối word + khối bit; tối đa 120 khối (subcommand 0000), 60 khối (0002, 008□) | `part3:1599-1858` |
| 0801 / 0802 register monitor / monitor | QnA | Request data của 0801 giống 0403 | `part3:1860-1954` |
| 06H/07H/08H/09H monitor | 1E | Lệnh 08H đọc bit, số điểm lẻ có dummy | `part5:2969-3108` |
| BM/JM, WM/QM, MB/MJ, MN/MQ | 1C | Đăng ký rồi đọc | `part5:913-1140` |
| Subcommand 008□ / 00C0 (device extension, monitor condition) | QnA | Layout device khác (Appendix 1 của PDF, trang in 438 = trang PDF 440; chưa có trong md) | `part3:201-208` |

---

## 5. Lớp frame

Mỗi frame codec cung cấp:

```text
interface FrameCodec {
    encode_request(cmd: Command) -> bytes
    new_response_parser(cmd: Command) -> ResponseParser   # parser tăng dần, biết lệnh vừa gửi
}
interface ResponseParser {
    feed(chunk: bytes) -> NEED_MORE | DONE
    remainder() -> bytes          # byte thừa sau frame (TCP có thể dính 2 frame)
    result() -> payload           # response data; hoặc raise McPlcError / McProtocolError
}
```

### 5.1 Frame 3E

Nguồn: `part2:492-653`, `part2:756-784`, `part2:806-1012`.

**Request:**

| # | Trường | ASCII | Binary | Mặc định |
|---|---|---|---|---|
| 1 | Subheader | `"5000"` | `50 00` | cố định |
| 2 | Network No. | u8 → 2 ký tự | 1 byte | `00` |
| 3 | PC No. | u8 → 2 ký tự | 1 byte | `FF` |
| 4 | Request destination module I/O No. | u16 → 4 ký tự | LE 2 | `03FF` |
| 5 | Request destination module station No. | u8 → 2 ký tự | 1 byte | `00` |
| 6 | Request data length | u16 → 4 ký tự | LE 2 | = độ dài (7) + (8) |
| 7 | Monitoring timer | u16 → 4 ký tự | LE 2 | `0010` (4 s) |
| 8 | Request data | §4.1 | §4.1 | |

- Request data length tính theo **byte trên đường truyền** (ASCII: số ký tự) của trường 7 + 8. Ví dụ đọc D100 × 3: binary 2 + 10 = 12 = `0C 00`; ASCII 4 + 20 = 24 = `"0018"`.
- Header (1–6) dài cố định: Binary 9 byte, ASCII 18 ký tự.

**Response:**

| # | Trường | ASCII | Binary |
|---|---|---|---|
| 1 | Subheader | `"D000"` | `D0 00` |
| 2–5 | Access route (Network, PC, I/O, station) | 10 ký tự | 5 byte |
| 6 | Response data length | 4 ký tự | LE 2 |
| 7 | End code | u16 → 4 ký tự | LE 2 |
| 8a | Response data (khi end code = 0) | §4.1 | §4.1 |
| 8b | Error information (khi end code ≠ 0): access route (5 byte / 10 ký tự) + command (u16) + subcommand (u16) | 18 ký tự | 9 byte |

- Response data length = độ dài (7) + (8a) hoặc (7) + (8b). Response ghi không dữ liệu: length = 2 (`02 00`) / 4 (`"0004"`).
- Error information: access route là của **trạm trả lỗi**, có thể khác request (`part2:646-653`).

**Thuật toán parse (TCP):**

```text
1. Đọc đủ H byte (Binary 9, ASCII 18).
2. subheader ≠ D0 00 / "D000"            → FrameMismatchError
3. (tuỳ chọn, mặc định tắt) route ≠ route của request → FrameMismatchError
4. L = response data length; đọc đủ L byte.
5. L < kích thước end code (2 / 4)       → LengthMismatchError
6. end = end code
   end == 0 : payload = phần còn lại; kiểm tra len(payload) == cmd.response_size(codec) → trả payload
   end != 0 : parse error information nếu đủ độ dài (9 byte / 18 ký tự), raise McPlcError(end, error_info)
```

**Monitoring timer** (`part2:595-616`): `0000` = chờ vô hạn; `0001`–`FFFF` × 250 ms. Khuyến nghị: trạm kết nối `0001`–`0028` (0,25–10 s), trạm khác `0002`–`00F0` (0,5–60 s). Timeout đọc của client SHOULD lớn hơn `timer × 250 ms` + biên (ví dụ + 1 s); khi `timer = 0` client MUST dùng timeout riêng.

**Access route — các giá trị hay dùng:**

| Đích | Network | PC | I/O | Station | Nguồn |
|---|---|---|---|---|---|
| Trạm kết nối (host) | `00` | `FF` | `03FF` | `00` | `part2:867-869`, `part2:936-938` |
| Trạm khác (network n, station m) | `01`–`EF` | `01`–`78` | `03FF` | `00` | `part2:879-891` |
| Control/master station chỉ định / hiện hành | `01`–`EF` | `7D` / `7E` | `03FF` | `00` | `part2:886-887` |
| Theo "Valid Module During Other Station Access" | `FE` | … | … | … | `part2:892-900` |
| Trạm multidrop nối vào C24 của trạm kết nối | `00` | `FF` | I/O đầu của C24 ÷ 16 | `00`–`1F` | `part2:875-877`, `part2:952-961` |
| Trạm multidrop qua mạng | network của trạm relay | station của trạm relay | I/O đầu của C24 ÷ 16 | `00`–`1F` | `part2:902-908`, `part2:963-972` |
| Multiple CPU No.1–4 | `00` | `FF` | `03E0`–`03E3` | `00` | `part2:974-984` |
| Redundant: control / standby / system A / system B | `00` | `FF` | `03D0` / `03D1` / `03D2` / `03D3` | `00` | `part2:985-988` |

Ví dụ: network 2, station 3 → Binary `02 03`, ASCII `"0203"` (`part2:918`); multiple CPU No.2 → Binary `E1 03`, ASCII `"03E1"` (`part2:1012`).

Vector: `V-3E-B-*`, `V-3E-A-*` (Phụ lục A).

### 5.2 Frame 4E

Nguồn: `part2:545-562`. Giống 3E, chỉ khác subheader:

| | ASCII (12 ký tự) | Binary (6 byte) |
|---|---|---|
| Request | `"5400"` + serial (4 ký tự hex) + `"0000"` | `54 00` + serial (LE 2) + `00 00` |
| Response | `"D400"` + serial + `"0000"` | `D4 00` + serial + `00 00` |

Ví dụ serial 1234H: ASCII `"540012340000"`, Binary `54 00 34 12 00 00` (`part2:556-562`).

- Header response: Binary 13 byte, ASCII 26 ký tự.
- Serial No. `0000`–`FFFF` do client quản lý (`part2:549-551`): tăng 1 sau mỗi request, quay vòng `FFFF → 0000`.
- Response có serial khác request đang chờ → là response muộn của request cũ (đã timeout): parser MUST bỏ qua frame đó và đọc tiếp đến hết deadline. Đây là lợi thế chính của 4E so với 3E.
- (Giai đoạn 2) Có thể gửi nhiều request đồng thời và ghép response theo serial.

Vector: `V-4E-B-*`, `V-4E-A-*`.

### 5.3 Frame 1E

Nguồn: `part5:2029-2270`.

**Request:**

| # | Trường | ASCII | Binary | Mặc định |
|---|---|---|---|---|
| 1 | Subheader (= mã lệnh 00H–05H) | 2 ký tự | 1 byte | theo lệnh |
| 2 | PC No. | 2 ký tự | 1 byte | `FF` (host); trạm khác `01`–`40` |
| 3 | ACPU monitoring timer | 4 ký tự | LE 2 | `000A` (2,5 s) |
| 4 | Request data | §4.2 | §4.2 | |

**Response:**

| # | Trường | ASCII | Binary | Có khi |
|---|---|---|---|---|
| 1 | Subheader (= mã lệnh OR 80H) | 2 ký tự | 1 byte | luôn |
| 2 | End code | 2 ký tự | 1 byte | luôn; `00` = bình thường |
| 3 | Abnormal code | 2 ký tự | 1 byte | chỉ khi end code = `5B` |
| 4 | Response data | §4.2 | §4.2 | chỉ khi end code = `00` và là lệnh đọc |

Ví dụ lỗi: end code `5B` + abnormal code `10` (PC No. error) → ASCII `"5B10"`, Binary `5B 10`; end code `10` → ASCII `"10"`, Binary `10` (`part5:2247-2270`).

**Thuật toán parse (TCP)** — 1E không có trường độ dài:

```text
1. Đọc 2 byte (Binary) / 4 ký tự (ASCII): subheader + end code.
2. subheader ≠ (cmd | 0x80)              → FrameMismatchError
3. end == 0x00 : đọc đúng cmd.response_size(codec) byte (0 với lệnh ghi) → trả payload
   end == 0x5B : đọc thêm 1 byte / 2 ký tự abnormal code → raise McPlcError(end, abnormal)
   khác        : raise McPlcError(end)   # KHÔNG đọc thêm byte nào
```

Monitoring timer: ý nghĩa và khuyến nghị giống 3E; lần đầu truy cập ACPU/QnACPU cần thời gian chờ nhận dạng CPU, MUST đặt timer trong dải khuyến nghị (`part5:83-92`, `part5:2195-2196`).

Vector: `V-1E-B-*`, `V-1E-A-*`.

### 5.4 Frame 4C

Nguồn: `part2:57-446`, `part2:665-693`.

**Access route 4C** (thứ tự cố định):

| Trường | ASCII | Binary (F5) | Mặc định (host) |
|---|---|---|---|
| Station No. | 2 ký tự | 1 byte | `00` (multidrop: `00`–`1F`; global: `FF`) |
| Network No. | 2 ký tự | 1 byte | `00` |
| PC No. | 2 ký tự | 1 byte | `FF` |
| Request destination module I/O No. | 4 ký tự | LE 2 | `03FF` |
| Request destination module station No. | 2 ký tự | 1 byte | `00` |
| Self-station No. | 2 ký tự | 1 byte | `00` (m:n multidrop: `00`–`1F`) |

Host: ASCII `"0000FF03FF0000"`, Binary `00 00 FF FF 03 00 00` (`part2:673-680`). Frame ID No.: ASCII `"F8"` (`46 38`), Binary `F8` (`part2:329-349`).

**Format 1–4 (ASCII)** — ký hiệu: `P` = Frame ID + access route (ASCII), `RD` = request data ASCII (§4.1), `SUM` = sum check (2 ký tự, chỉ khi bật), `BLK` = Block No. (2 ký tự, `00`–`FF`):

| Format | Request | Response có dữ liệu | Response không dữ liệu | Response lỗi |
|---|---|---|---|---|
| F1 | `ENQ P RD SUM` | `STX P data ETX SUM` | `ACK P` | `NAK P err4` |
| F2 | `ENQ BLK P RD SUM` | `STX BLK P data ETX SUM` | `ACK BLK P` | `NAK BLK P err4` |
| F3 | `STX P RD ETX SUM` | `STX P "QACK" data ETX SUM` | `STX P "QACK" ETX` | `STX P "QNAK" err4 ETX` |
| F4 | `ENQ P RD SUM CR LF` | `STX P data ETX SUM CR LF` | `ACK P CR LF` | `NAK P err4 CR LF` |

`err4` = error code 4 ký tự ASCII hex (`part2:437-442`). Vùng tính sum check: §2.5.

**Format 5 (Binary)** (`part2:181-213`, `part2:290-319`):

| Loại | Cấu trúc (trước khi DLE stuffing) |
|---|---|
| Request | `10 02` · count(LE 2) · `F8` · route(7) · request data binary · `10 03` · SUM |
| Response có dữ liệu | `10 02` · count · `F8` · route(7) · `FF FF` · `00 00` · data · `10 03` · SUM |
| Response không dữ liệu | `10 02` · count · `F8` · route(7) · `FF FF` · `00 00` · `10 03` · SUM |
| Response lỗi | `10 02` · count · `F8` · route(7) · `FF FF` · error code (LE 2) · `10 03` · SUM |

- `count` = số byte từ Frame ID đến hết data (không tính additional code).
- `FF FF` = Response ID code; `00 00` = normal completion code.
- DLE stuffing áp dụng từ count đến hết data (§2.6).

**Kiểm tra khi parse (4C, 3C, 1C):**

| Kiểm tra | Mức | Lỗi |
|---|---|---|
| Frame ID (`F8`/`F9`) khớp | MUST | FrameMismatchError |
| Station No. (và toàn bộ route) khớp request | SHOULD (`check_route`, bật mặc định; quan trọng với multidrop) | FrameMismatchError |
| Block No. khớp request (F2) | SHOULD (`check_block_no`; giả định PLC trả lại Block No. của request — xác minh trên phần cứng) | FrameMismatchError |
| Sum check đúng | MUST (khi bật) | SumCheckError |
| End code (F3) là `QACK`/`QNAK` (`GG`/`NN` với 1C) | MUST | FrameMismatchError |
| F5: Response ID code = `FFFF`, count khớp số byte thực | MUST | FrameMismatchError / LengthMismatchError |
| Kích thước data = `cmd.response_size` | MUST | LengthMismatchError |

Vector: `V-4C1-*` … `V-4C5-*`.

### 5.5 Frame 3C

Nguồn: `part2:695-714`. Giống 4C Format 1–4 (không có Format 5), khác:

- Frame ID `"F9"` (`46 39`).
- Access route chỉ có 4 trường: Station No. (2) → Network No. (2) → PC No. (2) → Self-station No. (2). Host: `"0000FF00"`.
- Error code 4 ký tự; end code F3 là `QACK`/`QNAK`.

Vector: `V-3C1-*` … `V-3C4-*`.

### 5.6 Frame 1C

Nguồn: `part2:57-179`, `part2:735-754`, `part5:100-245`.

- Không có Frame ID. Access route: Station No. (2) → PC No. (2). Host: `"00FF"`.
- Request data = command (2) + message wait (1) + character area (§4.3).
- Error code **2 ký tự**; end code F3 là `"GG"` (bình thường) / `"NN"` (lỗi) (`part2:414-423`).
- Chỉ có Format 1–4. Cấu trúc như bảng 4C F1–F4 với `P` = `"00FF"` và `err2` thay cho `err4`.

Ví dụ sum check của manual (Format 1): `ENQ "00" "FF" "BR" "3" "M0000" "C0"` (`part2:381-390`).

Vector: `V-1C1-*` … `V-1C4-*`.

---

## 6. Transport và thuật toán nhận frame

### 6.1 TCP

- Dòng byte không có biên: MUST đọc lặp đến đủ số byte cần (`read_exact`), xử lý đọc từng mẩu và nhiều frame dính nhau (dùng `remainder()`).
- Một request đang chờ trên mỗi kết nối (3E, 1E). 4E cho phép bỏ qua response cũ theo serial.
- Sau timeout với 3E/1E: MUST đóng và mở lại kết nối trước request tiếp theo (response muộn có thể đến sau và bị hiểu nhầm là response của request mới).

### 6.2 UDP

- Mỗi datagram là một frame. Với 3E/4E, MUST kiểm tra `H + L == len(datagram)`; lệch → `LengthMismatchError`.
- Datagram không khớp (subheader/serial sai) SHOULD bị bỏ qua và tiếp tục chờ đến deadline.
- Client MUST có timeout; lệnh ghi không tự gửi lại (§7.3).

### 6.3 Serial — nhận frame ASCII (F1–F4)

Trạng thái của parser (biết trước: frame type, format, sum check bật/tắt, lệnh có response data hay không):

```text
state START:
    c = next byte
    F1/F2/F4: c ∈ {STX, ACK, NAK}; F3: c == STX; byte khác → bỏ qua (SHOULD log) hoặc FrameMismatchError
state STX-BODY (F1, F2, F4):
    đọc đến ETX (dữ liệu ASCII không bao giờ chứa 03H)
    nếu sum check bật: đọc 2 ký tự SUM, kiểm tra
    F4: đọc CR LF
state ACK (F1, F2, F4):
    đọc đủ len(BLK) + len(P)            # 1C: 4, 3C: 10, 4C: 16 (+2 nếu F2)
    F4: đọc CR LF
state NAK (F1, F2, F4):
    đọc đủ len(BLK) + len(P) + len(err) # err: 1C = 2, 3C/4C = 4
    F4: đọc CR LF
state F3-BODY:
    đọc đến ETX; tách P, end code (4C/3C: 4 ký tự; 1C: 2 ký tự)
    end code = QACK/GG và có data → nếu sum check bật: đọc 2 ký tự SUM
    end code = QACK/GG không data, hoặc QNAK/NN → xong (theo PDF không có SUM, xem §10 Q1)
```

Timeout: timeout tổng cho cả response + (khuyến nghị) timeout giữa hai ký tự. Khi timeout hoặc lỗi protocol ở F1–F4, client SHOULD gửi `EOT` (hoặc `EOT CR LF` với F4) để C24 về trạng thái chờ lệnh trước request tiếp theo (`part2:239-264`); có cấu hình bật/tắt.

### 6.4 Serial — nhận frame Format 5

```text
chờ 10 02 (bỏ qua byte rác trước đó)
đọc và un-stuff đến khi gặp 10 03
nếu sum check bật: đọc 2 ký tự SUM, tính trên phần đã un-stuff
kiểm tra count == len(phần sau count)
```

Parser MUST xử lý trường hợp cặp `10 10` hoặc `10 03` bị tách giữa hai lần đọc.

---

## 7. Mô hình lỗi

### 7.1 Phân cấp exception

```text
McError
├── McConfigError                  cấu hình frame/client sai, phát hiện khi khởi tạo (§8.3)
├── McEncodeError                  lỗi tham số, phát hiện TRƯỚC khi gửi
│   ├── InvalidDeviceError         symbol/số hiệu sai, sai cơ số, vượt độ rộng trường, sai căn 16
│   ├── PointCountError            N = 0 hoặc vượt giới hạn
│   └── UnsupportedCommandError    thao tác frame không hỗ trợ (vd. random read với 1E/1C)
├── McTransportError               lỗi kết nối / I/O
│   └── McTimeoutError
├── McProtocolError                response sai định dạng
│   ├── FrameMismatchError         subheader, frame ID, serial, route, block No., end code F3 sai
│   ├── LengthMismatchError        độ dài không khớp
│   └── SumCheckError
└── McPlcError                     PLC trả lỗi
        frame, code (end code / error code), abnormal_code (1E), error_info (3E/4E), raw_response
```

### 7.2 Ánh xạ lỗi PLC

| Frame | Điều kiện | Exception | Trường |
|---|---|---|---|
| 3E/4E | end code ≠ 0000H | McPlcError | `code` = end code (u16), `error_info` = {route, command, subcommand} |
| 1E | end code ≠ 00H | McPlcError | `code` = end code (u8), `abnormal_code` khi end code = 5BH |
| 4C/3C F1, F2, F4 | NAK | McPlcError | `code` = error code (4 ký tự hex → u16) |
| 4C/3C F3 | `QNAK` | McPlcError | như trên |
| 4C F5 | Response ID `FFFF` + mã ≠ `0000` | McPlcError | `code` = u16 (LE) |
| 1C F1, F2, F4 | NAK | McPlcError | `code` = error code (2 ký tự hex → u8) |
| 1C F3 | `NN` | McPlcError | như trên |

Ý nghĩa của từng mã lỗi không có trong 3 file md (manual trỏ sang user's manual của từng module). Library trả mã số thô; bảng tra mã lỗi MAY bổ sung sau như dữ liệu riêng.

### 7.3 Chính sách thử lại

- Lệnh đọc MAY thử lại (cấu hình, mặc định 0 lần).
- Lệnh ghi MUST NOT tự thử lại trừ khi người dùng bật rõ ràng.
- Sau timeout: TCP 3E/1E → reconnect (§6.1); Serial F1–F4 → gửi EOT (§6.3).

---

## 8. Kiến trúc module và API đề xuất

Tài liệu không ràng buộc ngôn ngữ; tên dưới đây chỉ là gợi ý.

### 8.1 Cấu trúc thư mục

```text
mcprotocol/
├── core/
│   ├── constants        control code, subheader, frame ID
│   ├── hexascii         ASCII hex encode/decode
│   ├── checksum         sumcheck()
│   ├── dle              stuff() / unstuff() / DleReader
│   └── bits             pack/unpack bit, words<->bits
├── device/
│   ├── device_types     bảng §3.2 (dữ liệu, không phải if/else)
│   ├── device           Device, parse_device()
│   └── encode           qna_device(), e1_device(), c1_device()
├── codec/
│   └── field_codec      FieldCodec, AsciiCodec, BinaryCodec
├── command/
│   ├── qna              0401, 1401, 0403, 1402
│   ├── a1e              00H–05H
│   ├── a1c              BR/JR, WR/QR, BW/JW, WW/QW, BT/JT, WT/QT
│   └── limits           bảng §4.4 (dữ liệu)
├── frame/
│   ├── frame3e, frame4e, frame1e
│   ├── frame4c, frame3c, frame1c
│   └── serial_parser    parser tăng dần F1–F5 dùng chung cho 4C/3C/1C
├── transport/
│   ├── tcp, udp, serial
├── client/
│   └── client           McClient + chunking
└── errors
tests/
├── vectors/             golden vectors dạng dữ liệu (JSON/YAML) — Phụ lục A + bảng CMD-xx
├── unit/                primitives, device, command, frame
├── transport/           fake socket / fake serial
└── integration/         mock PLC server
```

### 8.2 Interface chính

```text
interface FieldCodec {
    kind : ASCII | BINARY
    u8(v) -> bytes ; u16(v) -> bytes ; u32(v) -> bytes        # §2.1
    fixed(binary: bytes, ascii: string) -> bytes              # subheader cố định (§2.1.1 E1)
    bits(values) -> bytes ; words(values) -> bytes ; dwords(values) -> bytes
    size_u8() ; size_u16() ; size_bits(n) ; size_words(n) ; size_dwords(n)
    reader(buf) -> FieldReader    # u8(), u16(), u32(), bits(n), words(n), dwords(n), remaining()
}

interface Command {
    family            : QNA | A1E | A1C
    code_1e           : u8        # chỉ với A1E (đi vào subheader)
    has_response_data : bool
    encode(codec) -> bytes                    # request data
    response_size(codec) -> int               # kích thước response data khi thành công
    decode(codec, payload) -> Result
}

interface Transport {
    open() ; close()
    send(data: bytes)
    receive(max: int, deadline) -> bytes      # trả về ≥ 1 byte hoặc raise McTimeoutError
}
```

- `AsciiCodec` và `BinaryCodec` là hai implementation duy nhất cần cho QnA và 1E. Command QnA/1E chỉ viết **một lần** và gọi codec; không có nhánh ASCII/Binary trong code command, trừ mã hóa device (§3.3).
- Command 1C luôn dùng `AsciiCodec` + `c1_device()`.
- Frame 1E đọc `cmd.code_1e` để đặt subheader; các frame khác đặt command vào request data.

### 8.3 Cấu hình frame

| Frame | Tham số | Mặc định |
|---|---|---|
| 3E | `code` (ASCII/BINARY), `network`, `pc`, `io`, `station`, `monitoring_timer`, `series` (QL/IQR), `check_route` | BINARY, 00, FF, 03FF, 00, 0010H, QL, false |
| 4E | như 3E + `serial_start` | 0 |
| 1E | `code`, `pc`, `monitoring_timer` | BINARY, FF, 000AH |
| 4C | `format` (1–5), `station`, `network`, `pc`, `io`, `module_station`, `self_station`, `sum_check`, `block_no` (F2), `series`, `send_eot_on_error` | 1, 00, 00, FF, 03FF, 00, 00, true, 00, QL, true |
| 3C | `format` (1–4), `station`, `network`, `pc`, `self_station`, `sum_check`, `block_no`, `series`, `send_eot_on_error` | 1, 00, 00, FF, 00, true, 00, QL, true |
| 1C | `format` (1–4), `station`, `pc`, `message_wait` (0–F), `sum_check`, `block_no`, `command_set` (ACPU/ANA), `send_eot_on_error` | 1, 00, FF, 0, true, 00, ACPU, true |
| Serial (4C/3C/1C) | `check_route` (§5.4), `check_block_no` (F2, §10 Q2), `f3_short_response_has_sum` (§10 Q1) | true, true, false |
| Chung | `timeout`, `read_retries` (§7.3), `target_family` (IQR_Q_L / QNA / A — chọn bảng giới hạn), `high_performance_qcpu` (ZR trong 0403 tính × 2, §4.1.3), `split_writes`, `a_series_target` | tự động (xem dưới), 0, IQR_Q_L, false, false, false |

- `timeout` mặc định: 3E/4E/1E dùng `monitoring_timer × 250 ms + 1 s` (5 s với 0010H, 3,5 s với 000AH); frame serial dùng 3 s. Nếu đặt `timeout` thủ công cho 3E/4E/1E thì SHOULD luôn lớn hơn `monitoring_timer × 250 ms` + biên (§5.1); khi `monitoring_timer = 0` thì MUST đặt `timeout` thủ công.
- `format`, `sum_check`, kiểu mã phải **khớp tham số của module trên PLC** (cài đặt bằng engineering tool, `part2:40`, `part2:360`); library không tự dò.
- Giá trị ngoài dải (vd. `format = 5` với 3C, `message_wait = 16`) → `McConfigError` khi khởi tạo.

### 8.4 Client API và bảng ánh xạ lệnh

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

`device` nhận chuỗi (`"D100"`) hoặc `Device`.

| API | 3E / 4E / 3C / 4C | 1E | 1C (ACPU / AnA) |
|---|---|---|---|
| `read_bits` | 0401 / 0001 (iQ-R 0003) | 00H | BR / JR |
| `read_words` | 0401 / 0000 (iQ-R 0002) | 01H | WR / QR |
| `write_bits` | 1401 / 0001 (iQ-R 0003) | 02H | BW / JW |
| `write_words` | 1401 / 0000 (iQ-R 0002) | 03H | WW / QW |
| `read_random` | 0403 / 0000 (iQ-R 0002) | ✗ `UnsupportedCommandError` | ✗ |
| `write_random_bits` | 1402 / 0001 (iQ-R 0003) | 04H | BT / JT |
| `write_random_words` | 1402 / 0000 (iQ-R 0002) | 05H (chỉ word) | WT / QT (chỉ word) |

`read_words` trên bit device trả word thô (bit i = device head + i); dùng `words_to_bits` nếu cần.

### 8.5 Chia request (chunking)

```text
max = limits.lookup(frame, code, operation, device.kind, series, target_family)
step_per_unit = 16 nếu (đọc/ghi word trên bit device) ngược lại 1
for off in 0, max, 2*max, ... < count:
    n    = min(max, count - off)
    head = device.number + off * step_per_unit
    gửi lệnh (head, n); nối kết quả
```

- Đọc: luôn cho phép chia.
- Ghi: chia **không nguyên tử** (một phần có thể đã ghi khi request sau lỗi). Mặc định `split_writes = false` → vượt giới hạn thì raise `PointCountError` và không gửi gì; bật `true` để cho phép chia.
- Random: 0403 chia theo `m + n ≤ max`, trong đó mỗi điểm ZR tính là 2 khi `high_performance_qcpu = true` và dùng subcommand Q/L (§4.1.3); 1402 word chia theo `m×12 + n×14 ≤ max`; giữ đúng thứ tự kết quả.
- `head` sau khi cộng MUST vẫn nằm trong độ rộng trường của device (§3.4).

### 8.6 Đồng thời, log

- Mỗi `McClient` giữ một khoá: tại mỗi thời điểm chỉ có một request trên đường truyền.
- Log mức debug: hex dump TX/RX; với frame ASCII in thêm dạng text, control code hiển thị `<STX>`, `<ETX>`, `<ENQ>`, `<ACK>`, `<NAK>`, `<CR>`, `<LF>`, `<DLE>`.

### 8.7 Tiện ích chuyển đổi dữ liệu (tầng ứng dụng)

| Tiện ích | Quy tắc | Ví dụ | Nguồn |
|---|---|---|---|
| `words_to_u32(lo, hi)` | word thấp ở device số nhỏ | D350 = 56ABH, D351 = 170FH → 170F56ABH | `part3:573` |
| `words_to_float32(lo, hi)` | IEEE-754, word thấp trước | D0 = 0000H, D1 = 3F40H → 0.75 | `part3:590` |
| `string_to_words("ABCD")` | ký tự đầu ở byte thấp của word | D0 = 4241H, D1 = 4443H, (D2 = 0000H nếu cần NULL) | `part3:595-615` |
| `to_int16(u16)` | bù 2 | FFFFH → −1 | — |

---

## 9. Kế hoạch test và test case

### 9.1 Các mức test

| Mức | Mục tiêu | Cách làm |
|---|---|---|
| L1 Primitive | hex, LE, sum check, DLE, bit/word | table-driven + property-based |
| L2 Device | parse, mã hóa theo 8 họ, ràng buộc | table-driven |
| L3 Command | request data khớp manual; decode response data | vector `CMD-xx`, `CMDD-xx` |
| L4 Frame | frame đầy đủ; parse bình thường/lỗi/sai định dạng | vector Phụ lục A + vector lỗi dẫn xuất |
| L5 Transport | chia mẩu, dính frame, timeout, datagram lạ | fake socket / fake serial |
| L6 Integration | client ↔ mock PLC cho mọi tổ hợp frame × code × format | mock server in-process |
| L7 HIL (tuỳ chọn) | PLC thật | chạy tay hoặc nightly |

Nguyên tắc:

- Golden vector MUST lưu thành file dữ liệu (`tests/vectors/*.json|yaml`) với cấu trúc `{id, frame, code, format, options, command, request_hex, response_hex, expected}` để dùng lại cho mọi ngôn ngữ và cho mock server.
- Mock PLC SHOULD được viết từ bảng trong tài liệu này, không import encoder của client (tránh cùng một lỗi ở hai phía); golden vector là mốc chung cho cả hai.
- Mục tiêu coverage: 100% nhánh của `core/`, `device/`, `command/`, `frame/`.

### 9.2 L1 — Primitive

| ID | Mô tả | Input | Kỳ vọng |
|---|---|---|---|
| PRIM-01 | u16 | 0018H | ASCII `"0018"`; Binary `18 00` |
| PRIM-02 | Chữ hoa | u16 ABCDH, ASCII | `"ABCD"` (không phải `"abcd"`) |
| PRIM-03 | u32 | 12345678H | `"12345678"`; `78 56 34 12` |
| PRIM-04 | Tràn trường | u8(100H), u16(10000H) | `McEncodeError` |
| PRIM-05 | Decode chữ thường | `"abcd"` | ABCDH (SHOULD) |
| PRIM-06 | Decode ký tự không phải hex | `"12G4"` | `McProtocolError` |
| PRIM-07 | Sum check 1C (manual) | `"00FFBR3M0000"` | `"C0"` (`part2:383-390`) |
| PRIM-08 | Sum check 4C F5 (manual) | `12 00 F8 05 07 03 04 00 01 00 01 04 01 00 40 00 00 9C 05 00` | `"05"` (`part2:392-401`) |
| PRIM-09 | Sum check tràn nhiều vòng | 300 byte `FF` | tổng 12AD4H → `"D4"` |
| PRIM-10 | DLE stuff | `01 10 02 10 10` | `01 10 10 02 10 10 10 10` |
| PRIM-11 | DLE unstuff lỗi | `10 41` trong thân frame | `McProtocolError` |
| PRIM-12 | Pack bit | [0,0,0,1,0,0,1,1]; [1,0,1,0,1] | `00 01 00 11`; `10 10 10` |
| PRIM-13 | Unpack nibble không hợp lệ | `21`, n = 2 | `McProtocolError` |
| PRIM-14 | Bit ASCII | `"10101"`; `"1021"` | [1,0,1,0,1]; `McProtocolError` |
| PRIM-15 | words → bits | [1234H, 0002H] | chỉ số ON: 2, 4, 5, 9, 12, 17 |
| PRIM-16 | dword | 170F56ABH | `"170F56AB"`; `AB 56 0F 17` |
| PRIM-17 | Property: roundtrip | u8/u16/u32/bits/words/dwords ngẫu nhiên, cả 2 codec | `decode(encode(x)) == x` |
| PRIM-18 | Property: DLE | chuỗi byte ngẫu nhiên | `unstuff(stuff(x)) == x`; `stuff(x)` không có `10` đơn lẻ |

### 9.3 L2 — Device

| ID | Mô tả | Input | Kỳ vọng |
|---|---|---|---|
| DEV-01 | Parse cơ bản | `"D100"`, `"d100"` | (D, 100) |
| DEV-02 | Parse hex | `"X1F"`, `"x1f"` | (X, 1FH) |
| DEV-03 | Sai cơ số | `"M1F"`, `"X1G"` | `InvalidDeviceError` |
| DEV-04 | Khớp dài nhất | `"SM400"`, `"SD10"`, `"SB1F"`, `"SW10"`, `"DX10"`, `"ZR100"`, `"STS5"`, `"S5"`, `"TN10"` | SM 400; SD 10; SB 1FH; SW 10H; DX 10H; ZR 100H; STS 5; S 5; TN 10 |
| DEV-05 | Symbol mơ hồ | `"T10"`, `"C5"` | `InvalidDeviceError` |
| DEV-06 | Chuỗi hỏng | `""`, `"D"`, `"100"`, `"Q10"` | `InvalidDeviceError` |
| DEV-07 | Bảng mã hóa §3.3 | D100, X1F, TN10, M1234, M9000 × 8 họ | đúng từng ô của bảng |
| DEV-08 | Tràn độ rộng QnA | D1000000 (ASCII Q/L); X1000000 (Binary Q/L) | `InvalidDeviceError`; iQ-R: `"D***01000000"` hợp lệ |
| DEV-09 | Tràn độ rộng 1C | ACPU: M10000, TN1000; AnA: TN1000 | lỗi; lỗi; `"TN01000"` |
| DEV-10 | Device không hỗ trợ theo họ | 1E: SM0, SD0, ZR0; 1C: V0; QnA Q/L: RD0 | `InvalidDeviceError` |
| DEV-11 | L/S với 1E | L100 | code `4D20`, number 100 (khi bật alias) |
| DEV-12 | Căn 16 (1E/1C word-unit bit device) | X40, X41, M9000, M9008, M9016 | OK, lỗi, OK, lỗi, OK |
| DEV-13 | Lệnh bit với word device | `read_bits("D0", 1)` | `InvalidDeviceError` |

### 9.4 L3 — Command: request data khớp manual

Các vector sau lấy nguyên văn từ manual (cột "Nguồn") và đã được bộ encoder tham chiếu tạo lại đúng 37/37. Test MUST so sánh byte-by-byte.

| ID | Lệnh / tham số | Mã | Byte kỳ vọng | Nguồn |
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
| CMD-34 | 4C Format 5 frame đầy đủ (= V-4C5-M1) | Bin | `10 02 12 00 F8 05 07 03 04 00 01 00 01 04 01 00 40 00 00 9C 05 00 10 03 30 35` | `part2:397-399` |
| CMD-35 | 0403 (như CMD-11) | ASCII | `"040300000403D*000000TN000000M*000100X*000020D*001500Y*000160M*001111"` | `part3:1390-1391` |
| CMD-36 | 1402 word (như CMD-12) | ASCII | `"140200000403D*0000000550D*0000010575M*0001000540X*0000200583D*00150004391202Y*00016023752607M*00111104250475"` | `part3:1511-1512` |
| CMD-37 | 1E 05H (như CMD-24) | ASCII | `"03005920000000807B295720000000261234434E000000120050"` | `part5:2953-2954` |

### 9.5 L3 — Command: decode response data

| ID | Lệnh | Payload | Kỳ vọng | Nguồn |
|---|---|---|---|---|
| CMDD-01 | 0401 word, Binary, N = 3 | `34 12 02 00 EF 1D` | [1234H, 0002H, 1DEFH] | `part3:1008` |
| CMDD-02 | 0401 word, ASCII, N = 3 | `"123400021DEF"` | [1234H, 0002H, 1DEFH] | `part3:1001` |
| CMDD-03 | 0401 word trên M100, Binary, N = 2 | `34 12 02 00` | [1234H, 0002H] | `part3:988` |
| CMDD-04 | 0401 bit, ASCII, N = 8 | `"00010011"` | [0,0,0,1,0,0,1,1] | `part3:1082` |
| CMDD-05 | 0401 bit, Binary, N = 8 | `00 01 00 11` | [0,0,0,1,0,0,1,1] | `part3:1089` |
| CMDD-06 | 0401 bit, Binary, N = 5 | `10 10 10` | [1,0,1,0,1] | `part3:510` |
| CMDD-07 | 0403, Binary, m = 4, n = 3 | `95 19 02 12 30 20 49 48 4E 4F 54 4C AF B9 DE C3 B7 BC DD BA` | word [1995H, 1202H, 2030H, 4849H]; dword [4C544F4EH, C3DEB9AFH, BADDBCB7H] | `part3:1400-1402` |
| CMDD-08 | 0403, ASCII, m = 4, n = 3 | `"19951202203048494C544F4EC3DEB9AFBADDBCB7"` | như CMDD-07 | `part3:1393-1395` |
| CMDD-09 | 1E 00H, ASCII, N = 12 | `"101010101010"` | [1,0,1,0,1,0,1,0,1,0,1,0] | `part5:2604-2605` |
| CMDD-10 | 1E 00H, Binary, N = 12 | `10 10 10 10 10 10` | như CMDD-09 | `part5:2617-2619` |
| CMDD-11 | 1E 01H, ASCII, N = 2 (Y40) | `"829D553E"` | [829DH, 553EH] | `part5:2678-2681` |
| CMDD-12 | 1E 01H, Binary, N = 2 | `9D 82 3E 55` | [829DH, 553EH] | `part5:2692-2695` |
| CMDD-13 | 1E 00H, ASCII, N = 3 (lẻ) | `"1010"` | [1,0,1] (bỏ dummy); payload `"101"` → `LengthMismatchError` | `part5:2564-2565` |
| CMDD-14 | 1C BR, N = 5 | `"01101"` | [0,1,1,0,1] | `part5:493-496` |
| CMDD-15 | 1C WR bit device X40, N = 2 | `"1234ABCD"` | [1234H, ABCDH] | `part5:565-569` |
| CMDD-16 | 1C WR TN123, N = 2 | `"7BC91234"` | [7BC9H, 1234H] | `part5:591-595` |
| CMDD-17 | Sai độ dài | 0401 word Binary N = 3, payload 5 byte | `LengthMismatchError` | — |
| CMDD-18 | Nibble sai | 0401 bit Binary N = 2, payload `21` | `McProtocolError` | — |

### 9.6 L4 — Frame 3E / 4E

| ID | Mô tả | Input | Kỳ vọng |
|---|---|---|---|
| 3E-01 | Encode Binary | G1, G2, G3, G4, G6, G1/G2 iQ-R | = `V-3E-B-01`, `-03`, `-05`, `-07`, `-08`, `-11`, `-12` |
| 3E-02 | Encode ASCII | như trên | = `V-3E-A-01`, `-03`, `-05`, `-07`, `-08`, `-11`, `-12` |
| 3E-03 | Trường độ dài | mọi vector 3E/4E | request: length = số byte (ký tự) của timer + request data; response: length = số byte (ký tự) của end code + response data / error information |
| 3E-04 | Parse đọc word | `V-3E-B-02`, `V-3E-A-02` (lệnh G1) | [1995H, 1202H, 1130H] |
| 3E-05 | Parse đọc bit | `V-3E-B-04`, `-09`; `V-3E-A-04`, `-09` | [0,0,0,1,0,0,1,1]; [1,0,1,0,1] |
| 3E-06 | Parse ghi | `V-3E-B-06`, `V-3E-A-06` | thành công, không dữ liệu |
| 3E-07 | Parse lỗi | `V-3E-B-10`, `V-3E-A-10` | `McPlcError(code = C051H, error_info = {net 00, pc FF, io 03FF, st 00, cmd 0401, sub 0000})` |
| 3E-08 | Subheader sai | `V-3E-B-02` đổi 2 byte đầu thành `D4 00` | `FrameMismatchError` |
| 3E-09 | Length < end code | `D0 00 00 FF FF 03 00 01 00 00` | `LengthMismatchError` |
| 3E-10 | Payload sai kích thước | `V-3E-B-02` với length `06 00` và bỏ 2 byte cuối | `LengthMismatchError` |
| 3E-11 | Lỗi không có error info | `D0 00 00 FF FF 03 00 02 00 51 C0` | `McPlcError(C051H)`, `error_info = null` |
| 3E-12 | Trạm khác | network 02, PC 03 | route Binary `02 03 FF 03 00`; ASCII `"0203"` + `"03FF00"` |
| 3E-13 | Multiple CPU No.2 | io = 03E1H | Binary `E1 03`; ASCII `"03E1"` |
| 3E-14 | Random read đầy đủ | lệnh 0403 của CMD-11 bọc 3E Binary | = `V-3E-B-13` |
| 4E-01 | Encode | G1, G3 (serial 1234H) | = `V-4E-B-01`, `-03`; `V-4E-A-01`, `-03` |
| 4E-02 | Parse | `V-4E-B-02`, `-04`, `-05` và bản ASCII | như 3E-04, 3E-06, 3E-07 |
| 4E-03 | Serial quay vòng | 3 request liên tiếp từ FFFEH | FFFEH, FFFFH, 0000H |
| 4E-04 | Bỏ qua response cũ | nạp response serial 1233H rồi 1234H cho request 1234H | bỏ frame 1233H, trả kết quả của 1234H |
| 4E-05 | Header 3E khi đang dùng 4E | `V-3E-B-02` | `FrameMismatchError` |

### 9.7 L4 — Frame 1E

| ID | Mô tả | Input | Kỳ vọng |
|---|---|---|---|
| 1E-01 | Encode Binary | G1, G2, G3, G4, G6 | = `V-1E-B-01`, `-03`, `-05`, `-07`, `-09` |
| 1E-02 | Encode ASCII | như trên | = `V-1E-A-01`, `-03`, `-05`, `-07`, `-09` |
| 1E-03 | Parse đọc | `V-1E-B-02`, `-04`, `-10`; bản ASCII | [1995H, 1202H, 1130H]; [0,0,0,1,0,0,1,1]; [1,0,1,0,1] |
| 1E-04 | Parse ghi | `V-1E-B-06`, `-08` | thành công |
| 1E-05 | Lỗi 5BH + abnormal | `V-1E-B-11` (`81 5B 10`), `V-1E-A-11` | `McPlcError(5BH, abnormal = 10H)`; parser tiêu thụ đúng 3 byte / 6 ký tự |
| 1E-06 | Lỗi không abnormal | `V-1E-B-12` (`81 50`) | `McPlcError(50H)`; parser xong sau 2 byte, **không chờ thêm** |
| 1E-07 | Subheader sai | `80 00` cho lệnh 01H | `FrameMismatchError` |
| 1E-08 | 256 điểm | đọc D0 × 256 | = `V-1E-B-13` (points = `00`) |
| 1E-09 | 257 điểm | đọc D0 × 257 | `PointCountError` |
| 1E-10 | Dummy khi đọc bit lẻ (ASCII) | `V-1E-A-10` với N = 5 | [1,0,1,0,1] |
| 1E-11 | Đệm nibble khi ghi bit lẻ (Binary) | ghi M100 = [1,1,1] | write data `11 10` |
| 1E-12 | Lệnh 04H/05H đầy đủ | request data CMD-22, CMD-24 (Binary); CMD-23, CMD-37 (ASCII) bọc header 1E | Binary: header `04 FF 0A 00` / `05 FF 0A 00` + request data; ASCII: header `"04FF000A"` / `"05FF000A"` + request data |
| 1E-13 | PC No. trạm khác | pc = 03H | Binary `03`; ASCII `"03"` |
| 1E-14 | Căn 16 | lệnh 01H với X41 | `InvalidDeviceError` |

### 9.8 L4 — Frame 4C / 3C / 1C

| ID | Mô tả | Input | Kỳ vọng |
|---|---|---|---|
| 4C-01 | F1 | encode G1, G3; parse response | = `V-4C1-01..05` |
| 4C-02 | F2 (block 00) | như trên | = `V-4C2-01..05` |
| 4C-03 | F3 | như trên | = `V-4C3-01..05` |
| 4C-04 | F4 | như trên | = `V-4C4-01..05` |
| 4C-05 | F5 | như trên | = `V-4C5-01..05` |
| 4C-06 | F5 ví dụ manual | station 05, network 07, PC 03, I/O 0004H, module station 01; 0401/0001 X40 × 5 | = `V-4C5-M1` |
| 4C-07 | DLE trong request | đọc D16 × 1 | = `V-4C5-06` (count 12H, device number `10 00 00` gửi thành `10 10 00 00`) |
| 4C-08 | DLE trong response | `V-4C5-07` | [1010H] |
| 4C-09 | Sai sum check | `V-4C1-02` đổi `"DE"` thành `"DF"` | `SumCheckError` |
| 4C-10 | Tắt sum check | `sum_check = false` | request = `V-4C1-06`; response không SUM parse được |
| 4C-11 | NAK | `V-4C1-05` | `McPlcError(7151H)` |
| 4C-12 | QNAK (F3) | `V-4C3-05` | `McPlcError(7151H)` |
| 4C-13 | Station No. khác | `V-4C1-02` với station `"01"` (sửa lại SUM) | `FrameMismatchError` |
| 4C-14 | Block No. | `block_no = 3AH` (F2) | request bắt đầu `ENQ "3A" "F8"`; response khác block → `FrameMismatchError` |
| 4C-15 | Byte rác trước STX | `00 FF` + `V-4C1-02` | parse thành công (log cảnh báo) |
| 4C-16 | F5 count sai | `V-4C5-02` đổi count `12 00` → `13 00` (sửa SUM: `"0C"` → `"0D"`) | `LengthMismatchError` |
| 4C-17 | F5 Response ID sai | `V-4C5-02` đổi `FF FF` → `FF FE` (sửa SUM) | `FrameMismatchError` |
| 4C-18 | F5 lỗi | `V-4C5-05` | `McPlcError(7151H)` |
| 3C-01 | F1–F4 | encode G1, G4; parse | = `V-3C1-*` … `V-3C4-*` |
| 3C-02 | NAK / QNAK | `V-3C1-05`, `V-3C3-05` | `McPlcError(7151H)` |
| 3C-03 | Cấu hình F5 | `format = 5` | `McConfigError` |
| 1C-01 | F1–F4 | encode WR, BR, WW, BW; parse | = `V-1C1-*` … `V-1C4-*` |
| 1C-02 | NAK 2 ký tự | `V-1C1-08` | `McPlcError(06H)` |
| 1C-03 | NN (F3) | `V-1C3-08` | `McPlcError(06H)` |
| 1C-04 | Lệnh AnA/AnU | `command_set = ANA`, đọc D100 × 3 | = `V-1C1-09` (`QR`, `D000100`) |
| 1C-05 | Message wait | `message_wait = 0AH` | ký tự sau command là `"A"` (CMD-25) |
| 1C-06 | 256 điểm | BR M0 × 256 | = `V-1C1-11` (points `"00"`) |
| 1C-07 | WR bit device | WR X40 × 2 | = `V-1C1-10` |
| 1C-08 | Sum check manual | request data `"BR3M0000"`, station 00, PC FF | SUM = `"C0"` |
| 1C-09 | BT / WT đầy đủ | request data CMD-32, CMD-31 bọc F1 | `ENQ "00FF"` + request data + SUM |

### 9.9 L5 — Transport

| ID | Mô tả | Kỳ vọng |
|---|---|---|
| TRN-01 | TCP: `V-3E-B-02` đến từng byte một | parse thành công |
| TRN-02 | TCP: `V-4E-B-02` + `V-4E-B-04` trong một lần đọc | trả kết quả frame đầu; `remainder()` = frame sau |
| TRN-03 | TCP: không có response | `McTimeoutError`; kết nối bị đóng; lần gọi sau mở lại kết nối |
| TRN-04 | UDP: datagram = 10 byte đầu của `V-3E-B-02` | `LengthMismatchError` |
| TRN-05 | UDP: datagram subheader sai rồi datagram đúng | bỏ qua datagram đầu, trả kết quả datagram sau |
| TRN-06 | Serial: nửa đầu `V-4C1-02` rồi im lặng | `McTimeoutError`; gửi `EOT` khi `send_eot_on_error = true` |
| TRN-07 | Serial F5: `V-4C5-07` tách giữa hai byte `10 10` | parse thành công, [1010H] |
| TRN-08 | Serial F4: thiếu LF | `McTimeoutError` |
| TRN-09 | Kết nối bị đóng giữa frame | `McTransportError` |

### 9.10 L6 — Client API

| ID | Mô tả | Input | Kỳ vọng |
|---|---|---|---|
| API-01 | Chia `read_words` | 3E Binary, D0 × 2000 | 3 request: (D0, 960), (D960, 960), (D1920, 80); kết quả nối đúng thứ tự |
| API-02 | Chia `read_bits` | 3E, M0 × 8000 | Binary: (M0, 7168), (M7168, 832); ASCII: (M0, 3584), (M3584, 3584), (M7168, 832) |
| API-03 | Chia `read_bits` C24 | 4C F1, M0 × 8000 | (M0, 7904), (M7904, 96) |
| API-04 | Chia word trên bit device | 1E, `read_words("X0", 200)` | (X0, 128 word), (X800, 72 word) |
| API-05 | Ghi vượt giới hạn, không cho chia | 3E, `write_words("D0", 1000 giá trị)`, `split_writes = false` | `PointCountError`, không gửi byte nào |
| API-06 | Ghi vượt giới hạn, cho chia | như trên, `split_writes = true` | 2 request (960 + 40) |
| API-07 | Random read trên 1E/1C | `read_random(["D0"])` | `UnsupportedCommandError` |
| API-08 | dword trên 1E/1C | `write_random_words([], [("D0", 1)])` | `UnsupportedCommandError` |
| API-09 | `series = IQR` | đọc D100 × 3; `read_random` 100 điểm | subcommand 0002 (= `V-3E-B-11`); chia 96 + 4 |
| API-10 | Số lượng 0 | `read_words("D0", 0)`, `write_bits("M0", [])` | `PointCountError` |
| API-11 | Giới hạn 1402 word | 3E Q/L, 160 word point; 161 word point | 1 request (160 × 12 = 1920); 161 → chia hoặc lỗi theo `split_writes` |
| API-12 | Đích A-series qua QnA | `a_series_target = true`, `read_words("X41", 1)` | `InvalidDeviceError` |
| API-13 | Đồng thời | 2 luồng gọi cùng lúc | request tuần tự trên đường truyền, không xen kẽ byte |
| API-14 | Tiện ích | D0 = 0000H, D1 = 3F40H; `"ABCD"` | 0.75; [4241H, 4443H] |

### 9.11 L6 — Integration với mock PLC

Mock PLC: giữ bộ nhớ device (D, W, M, X, Y, B, TN…), hiểu đủ 6 frame, trả response theo §5; có thể cấu hình để trả lỗi (end code / NAK / QNAK / 5BH) cho một device chỉ định.

Ma trận tổ hợp (19 tổ hợp):

| Frame | Biến thể |
|---|---|
| 3E | Binary, ASCII |
| 4E | Binary, ASCII |
| 1E | Binary, ASCII |
| 4C | F1, F2, F3, F4, F5 |
| 3C | F1, F2, F3, F4 |
| 1C | F1, F2, F3, F4 |

Với mỗi tổ hợp chạy:

| ID | Kịch bản | Kỳ vọng |
|---|---|---|
| INT-01 | `write_words("D100", [1995H, 1202H, 1130H])` → `read_words("D100", 3)` | đọc lại đúng giá trị |
| INT-02 | `write_bits("M100", [1,1,0,0,1,1,0,0])` → `read_bits("M100", 8)` | đọc lại đúng |
| INT-03 | `write_bits` với số điểm lẻ (5) → `read_bits` | đúng; kiểm tra đệm nibble / dummy |
| INT-04 | `write_random_bits([("M50", 0), ("Y2F", 1)])` → `read_bits` từng điểm | đúng |
| INT-05 | `write_random_words([("D0", 0550H), ("W26", 1234H)])` → `read_words` | đúng |
| INT-06 | `read_random` (chỉ QnA): word D0, TN0, M100; dword D1500 | đúng thứ tự, đúng giá trị |
| INT-07 | Chunking: `read_words("D0", 2000)` | mock nhận đúng số request, dữ liệu nối đúng |
| INT-08 | Mock trả lỗi | `McPlcError` với `code` đúng theo frame |
| INT-09 | 4C F5 với dữ liệu chứa nhiều `10H` (ghi D0..D9 = 1010H) | DLE stuffing hai chiều đúng |

---

## 10. Lưu ý, lỗi in trong PDF và câu hỏi mở

### 10.1 Câu hỏi mở — cần xác minh trên PLC thật

| # | Vấn đề | Cách xử lý trong v1 |
|---|---|---|
| Q1 | Format 3: response không dữ liệu (`STX P QACK ETX`) và response lỗi (`STX P QNAK err ETX`) được PDF in **không có** sum check (đã đối chiếu ảnh trang PDF 33, trang in 31), trong khi response có dữ liệu thì có. | Implement đúng như PDF. Thêm cấu hình `f3_short_response_has_sum` (mặc định `false`) để đổi nhanh nếu phần cứng khác. |
| Q2 | Format 2: manual không nói rõ Block No. trong response có luôn bằng Block No. của request. | Kiểm tra khớp (SHOULD) nhưng cho phép tắt. |
| Q3 | 1E ASCII ghi bit (02H) với số điểm lẻ: manual chỉ nói dummy cho **đọc** (`part5:2565`), không nói cho ghi. | Gửi đúng N ký tự, không đệm. |
| Q4 | Ghi bit Binary với số điểm lẻ (QnA, 1E): nibble thấp của byte cuối. | Đặt 0 theo quy tắc `part3:504`. |
| Q5 | 3E/4E: manual không cam kết access route trong response bình thường luôn giống request. | `check_route = false` mặc định. |
| Q6 | Ý nghĩa mã lỗi không có trong 3 file md. | Trả mã số thô; bảng tra bổ sung sau. |
| Q7 | Bảng giới hạn 1E (§4.4) chỉ có trong Appendix 5 của PDF (trang in 470), **chưa** qua quy trình verify như các file md; giới hạn QnA và 1C còn có trên trang lệnh của các file md đã verify và khớp với chúng. | Để giới hạn trong file cấu hình; rà lại giá trị 1E khi cần. |
| Q8 | Special relay M9000–M9255 theo word: manual ghi "(9000 + multiple of 16) can be specified" (`part5:410`, `part5:2400`) nhưng không nói địa chỉ là bội của 16 mà không phải 9000 + 16k (vd. M9008) có được chấp nhận không. | Chỉ chấp nhận 9000 + 16k trong vùng này (§3.5, DEV-12). |

### 10.2 Lỗi in của PDF liên quan tới phạm vi này

Đã được đánh dấu trong md bằng `> **Note:**` (danh sách đầy đủ: `verification/PDF_misprint_notes.md`, `verification/part5/PDF_misprint_notes.md`):

| Vị trí | Nội dung | Giá trị dùng |
|---|---|---|
| `part5:372` | Ví dụ device timer 1C in "T S" nhưng byte là `54 4E` | `TN` |
| `part5:2756` | Ví dụ 1E 02H ASCII in ký tự "4 4", "0 3" trên byte `34 44`, `30 43` | Theo byte: `4D`, `0C` |
| `part3:1387`, `part3:1410` | Ví dụ 0403 ghi "4 double word access" | 3 (theo request data) |
| `part3:1404` | Nhãn trường trong response binary 0403 | Theo sơ đồ bit |
| `part3:583` | Nhãn "Number of device points" trong ví dụ double word binary | Đó là head device |
| `part3:1444` | Sơ đồ request 1402 word ghi nhãn write data cuối của phần word là "Write data (nth point)" | Write data của điểm word thứ m |
| `part2:105` | Format 2 in "ACX" | ACK (06H) |

Phát hiện thêm khi viết tài liệu này (chưa có trong danh sách verify, thuộc phạm vi giai đoạn 2): ví dụ ASCII của lệnh 1E đăng ký monitor (`part5:3026-3027`) in device B2C là `"422E0000002C"`, trong khi bảng device code (`part5:2383`) và ví dụ binary cùng trang (`2C 00 00 00 20 42`) cho mã `4220`. Dùng `4220`.

### 10.3 Lưu ý khi triển khai

- Universal model QCPU có 5 chữ số đầu serial ≤ 10101 không dùng được 1C/1E, phải dùng 2C/3C/4C hoặc 3E/4E (`part5:327-330`, `part5:2336-2341`).
- Khi có computer link module (A-series) trong multidrop, chỉ dùng được ASCII (Format 1–4) (`part5:77-81`).
- Truy cập ACPU lần đầu qua E71 cần monitoring timer trong dải khuyến nghị (`part5:85-92`).
- Mạng có nhiều module mạng trên trạm kết nối: 1C/1E không có network No.; cần đặt "Valid Module During Other Station Access" bằng engineering tool (`part2:892-900`).
- `format`, `sum_check`, ASCII/Binary MUST khớp cài đặt của module trên PLC; sai cài đặt thường thể hiện bằng timeout hoặc NAK chứ không có thông báo rõ.

---

## Phụ lục A — Golden vectors (frame đầy đủ)

**Tham số chung:**

| Frame | Tham số |
|---|---|
| 3E / 4E | network 00, PC FF, I/O 03FF, station 00, monitoring timer 0010H; 4E serial No. 1234H |
| 1E | PC FF, monitoring timer 000AH |
| 4C | station 00, network 00, PC FF, I/O 03FF, module station 00, self-station 00; sum check bật; F2 block No. 00 |
| 3C | station 00, network 00, PC FF, self-station 00; sum check bật; F2 block No. 00 |
| 1C | station 00, PC FF, message wait 0, lệnh ACPU; sum check bật; F2 block No. 00 |
| Subcommand | Q/L (trừ khi ghi iQ-R) |

**Kịch bản:**

| Mã | Kịch bản |
|---|---|
| G1 | Đọc word D100 × 3 → 1995H, 1202H, 1130H |
| G2 | Đọc bit M100 × 8 → 0,0,0,1,0,0,1,1 |
| G3 | Ghi word D100 × 3 = 1995H, 1202H, 1130H |
| G4 | Ghi bit M100 × 8 = 1,1,0,0,1,1,0,0 |
| G6 | Đọc bit M100 × 5 → 1,0,1,0,1 (số điểm lẻ) |
| Lỗi | 3E/4E: end code C051H; 4C/3C: error code 7151H; 1C: error code 06H; 1E: 5BH + 10H và 50H |

Mỗi vector: dòng `#` là ID + mô tả + độ dài; dòng tiếp theo là byte hex; với frame ASCII có thêm dòng text (control code viết dạng `<STX>`). Các vector này được sinh bằng bộ encoder tham chiếu, cùng bộ đã tái tạo đúng 37/37 vector của manual (§9.4); riêng `V-4C5-M1` chính là ví dụ trong manual (`part2:397-399`).

### A.1 Frame 3E — Binary

```text
# V-3E-B-01  request G1: 0401/0000 đọc word D100 × 3  (21 byte)
50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00

# V-3E-B-02  response G1: 1995H, 1202H, 1130H  (17 byte)
D0 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11

# V-3E-B-03  request G2: 0401/0001 đọc bit M100 × 8  (21 byte)
50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 64 00 00 90 08 00

# V-3E-B-04  response G2: 0,0,0,1,0,0,1,1  (15 byte)
D0 00 00 FF FF 03 00 06 00 00 00 00 01 00 11

# V-3E-B-05  request G3: 1401/0000 ghi D100 = 1995H, 1202H, 1130H  (27 byte)
50 00 00 FF FF 03 00 12 00 10 00 01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11

# V-3E-B-06  response ghi thành công (không dữ liệu)  (11 byte)
D0 00 00 FF FF 03 00 02 00 00 00

# V-3E-B-07  request G4: 1401/0001 ghi M100 = 1,1,0,0,1,1,0,0  (25 byte)
50 00 00 FF FF 03 00 10 00 10 00 01 14 01 00 64 00 00 90 08 00 11 00 11 00

# V-3E-B-08  request G6: 0401/0001 đọc bit M100 × 5  (21 byte)
50 00 00 FF FF 03 00 0C 00 10 00 01 04 01 00 64 00 00 90 05 00

# V-3E-B-09  response G6: 1,0,1,0,1  (14 byte)
D0 00 00 FF FF 03 00 05 00 00 00 10 10 10

# V-3E-B-10  response lỗi cho G1: end code C051H + error information  (20 byte)
D0 00 00 FF FF 03 00 0B 00 51 C0 00 FF FF 03 00 01 04 00 00

# V-3E-B-11  request G1 với subcommand iQ-R (0401/0002)  (23 byte)
50 00 00 FF FF 03 00 0E 00 10 00 01 04 02 00 64 00 00 00 A8 00 03 00

# V-3E-B-12  request G2 với subcommand iQ-R (0401/0003)  (23 byte)
50 00 00 FF FF 03 00 0E 00 10 00 01 04 03 00 64 00 00 00 90 00 08 00

# V-3E-B-13  request 0403 của CMD-11 / CMD-35  (45 byte)
50 00 00 FF FF 03 00 24 00 10 00 03 04 00 00 04 03 00 00 00 A8 00 00 00 C2 64 00 00 90 20 00 00 9C DC 05 00 A8 60 01 00 9D 57 04 00 90

```

### A.2 Frame 3E — ASCII

```text
# V-3E-A-01  request G1: 0401/0000 đọc word D100 × 3  (42 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33
500000FF03FF000018001004010000D*0001000003

# V-3E-A-02  response G1: 1995H, 1202H, 1130H  (34 byte)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 30 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30
D00000FF03FF0000100000199512021130

# V-3E-A-03  request G2: 0401/0001 đọc bit M100 × 8  (42 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38
500000FF03FF000018001004010001M*0001000008

# V-3E-A-04  response G2: 0,0,0,1,0,0,1,1  (30 byte)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 43 30 30 30 30 30 30 30 31 30 30 31 31
D00000FF03FF00000C000000010011

# V-3E-A-05  request G3: 1401/0000 ghi D100 = 1995H, 1202H, 1130H  (54 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 32 34 30 30 31 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30
500000FF03FF000024001014010000D*0001000003199512021130

# V-3E-A-06  response ghi thành công (không dữ liệu)  (22 byte)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 30 30 30
D00000FF03FF0000040000

# V-3E-A-07  request G4: 1401/0001 ghi M100 = 1,1,0,0,1,1,0,0  (50 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 32 30 30 30 31 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30
500000FF03FF000020001014010001M*000100000811001100

# V-3E-A-08  request G6: 0401/0001 đọc bit M100 × 5  (42 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 35
500000FF03FF000018001004010001M*0001000005

# V-3E-A-09  response G6: 1,0,1,0,1  (27 byte)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 39 30 30 30 30 31 30 31 30 31
D00000FF03FF000009000010101

# V-3E-A-10  response lỗi cho G1: end code C051H + error information  (40 byte)
44 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 31 30 30 46 46 30 33 46 46 30 30 30 34 30 31 30 30 30 30
D00000FF03FF000016C05100FF03FF0004010000

# V-3E-A-11  request G1 với subcommand iQ-R (0401/0002)  (46 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 43 30 30 31 30 30 34 30 31 30 30 30 32 44 2A 2A 2A 30 30 30 30 30 31 30 30 30 30 30 33
500000FF03FF00001C001004010002D***000001000003

# V-3E-A-12  request G2 với subcommand iQ-R (0401/0003)  (46 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 43 30 30 31 30 30 34 30 31 30 30 30 33 4D 2A 2A 2A 30 30 30 30 30 31 30 30 30 30 30 38
500000FF03FF00001C001004010003M***000001000008

# V-3E-A-13  request 0403 của CMD-11 / CMD-35  (90 byte)
35 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 34 38 30 30 31 30 30 34 30 33 30 30 30 30 30 34 30 33 44 2A 30 30 30 30 30 30 54 4E 30 30 30 30 30 30 4D 2A 30 30 30 31 30 30 58 2A 30 30 30 30 32 30 44 2A 30 30 31 35 30 30 59 2A 30 30 30 31 36 30 4D 2A 30 30 31 31 31 31
500000FF03FF0000480010040300000403D*000000TN000000M*000100X*000020D*001500Y*000160M*001111

```

### A.3 Frame 4E — Binary (serial No. 1234H)

```text
# V-4E-B-01  request G1  (25 byte)
54 00 34 12 00 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00

# V-4E-B-02  response G1  (21 byte)
D4 00 34 12 00 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11

# V-4E-B-03  request G3  (31 byte)
54 00 34 12 00 00 00 FF FF 03 00 12 00 10 00 01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11

# V-4E-B-04  response G3 (không dữ liệu)  (15 byte)
D4 00 34 12 00 00 00 FF FF 03 00 02 00 00 00

# V-4E-B-05  response lỗi C051H cho G1  (24 byte)
D4 00 34 12 00 00 00 FF FF 03 00 0B 00 51 C0 00 FF FF 03 00 01 04 00 00

```

### A.4 Frame 4E — ASCII (serial No. 1234H)

```text
# V-4E-A-01  request G1  (50 byte)
35 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 38 30 30 31 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33
54001234000000FF03FF000018001004010000D*0001000003

# V-4E-A-02  response G1  (42 byte)
44 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 30 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30
D4001234000000FF03FF0000100000199512021130

# V-4E-A-03  request G3  (62 byte)
35 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 32 34 30 30 31 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30
54001234000000FF03FF000024001014010000D*0001000003199512021130

# V-4E-A-04  response G3 (không dữ liệu)  (30 byte)
44 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 30 30 30
D4001234000000FF03FF0000040000

# V-4E-A-05  response lỗi C051H cho G1  (48 byte)
44 34 30 30 31 32 33 34 30 30 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 36 43 30 35 31 30 30 46 46 30 33 46 46 30 30 30 34 30 31 30 30 30 30
D4001234000000FF03FF000016C05100FF03FF0004010000

```

### A.5 Frame 1E — Binary

```text
# V-1E-B-01  request G1: 01H đọc word D100 × 3  (12 byte)
01 FF 0A 00 64 00 00 00 20 44 03 00

# V-1E-B-02  response G1  (8 byte)
81 00 95 19 02 12 30 11

# V-1E-B-03  request G2: 00H đọc bit M100 × 8  (12 byte)
00 FF 0A 00 64 00 00 00 20 4D 08 00

# V-1E-B-04  response G2  (6 byte)
80 00 00 01 00 11

# V-1E-B-05  request G3: 03H ghi D100 × 3  (18 byte)
03 FF 0A 00 64 00 00 00 20 44 03 00 95 19 02 12 30 11

# V-1E-B-06  response G3  (2 byte)
83 00

# V-1E-B-07  request G4: 02H ghi M100 × 8  (16 byte)
02 FF 0A 00 64 00 00 00 20 4D 08 00 11 00 11 00

# V-1E-B-08  response G4  (2 byte)
82 00

# V-1E-B-09  request G6: 00H đọc bit M100 × 5  (12 byte)
00 FF 0A 00 64 00 00 00 20 4D 05 00

# V-1E-B-10  response G6 (nibble cuối = 0)  (5 byte)
80 00 10 10 10

# V-1E-B-11  response lỗi: end code 5BH + abnormal code 10H  (3 byte)
81 5B 10

# V-1E-B-12  response lỗi: end code 50H (không có abnormal code)  (2 byte)
81 50

# V-1E-B-13  request 01H đọc D0 × 256 (points = 00)  (12 byte)
01 FF 0A 00 00 00 00 00 20 44 00 00

```

### A.6 Frame 1E — ASCII

```text
# V-1E-A-01  request G1: 01H đọc word D100 × 3  (24 byte)
30 31 46 46 30 30 30 41 34 34 32 30 30 30 30 30 30 30 36 34 30 33 30 30
01FF000A4420000000640300

# V-1E-A-02  response G1  (16 byte)
38 31 30 30 31 39 39 35 31 32 30 32 31 31 33 30
8100199512021130

# V-1E-A-03  request G2: 00H đọc bit M100 × 8  (24 byte)
30 30 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 38 30 30
00FF000A4D20000000640800

# V-1E-A-04  response G2  (12 byte)
38 30 30 30 30 30 30 31 30 30 31 31
800000010011

# V-1E-A-05  request G3: 03H ghi D100 × 3  (36 byte)
30 33 46 46 30 30 30 41 34 34 32 30 30 30 30 30 30 30 36 34 30 33 30 30 31 39 39 35 31 32 30 32 31 31 33 30
03FF000A4420000000640300199512021130

# V-1E-A-06  response G3  (4 byte)
38 33 30 30
8300

# V-1E-A-07  request G4: 02H ghi M100 × 8  (32 byte)
30 32 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 38 30 30 31 31 30 30 31 31 30 30
02FF000A4D2000000064080011001100

# V-1E-A-08  response G4  (4 byte)
38 32 30 30
8200

# V-1E-A-09  request G6: 00H đọc bit M100 × 5  (24 byte)
30 30 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 35 30 30
00FF000A4D20000000640500

# V-1E-A-10  response G6 (ký tự cuối là dummy)  (10 byte)
38 30 30 30 31 30 31 30 31 30
8000101010

# V-1E-A-11  response lỗi: end code 5BH + abnormal code 10H  (6 byte)
38 31 35 42 31 30
815B10

# V-1E-A-12  response lỗi: end code 50H (không có abnormal code)  (4 byte)
38 31 35 30
8150

# V-1E-A-13  request 01H đọc D0 × 256 (points = 00)  (24 byte)
30 31 46 46 30 30 30 41 34 34 32 30 30 30 30 30 30 30 30 30 30 30 30 30
01FF000A4420000000000000

```

### A.7 Frame 4C — Format 1

```text
# V-4C1-01  request G1  (39 byte)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 35 30
<ENQ>F80000FF03FF000004010000D*000100000350

# V-4C1-02  response G1  (32 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 44 45
<STX>F80000FF03FF0000199512021130<ETX>DE

# V-4C1-03  request G3  (51 byte)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 42 33
<ENQ>F80000FF03FF000014010000D*0001000003199512021130B3

# V-4C1-04  response G3 (không dữ liệu)  (17 byte)
06 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30
<ACK>F80000FF03FF0000

# V-4C1-05  response lỗi, error code 7151H  (21 byte)
15 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 37 31 35 31
<NAK>F80000FF03FF00007151

# V-4C1-06  request G1 khi tắt sum check  (37 byte)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33
<ENQ>F80000FF03FF000004010000D*0001000003

```

### A.8 Frame 4C — Format 2

```text
# V-4C2-01  request G1  (41 byte)
05 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 42 30
<ENQ>00F80000FF03FF000004010000D*0001000003B0

# V-4C2-02  response G1  (34 byte)
02 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 33 45
<STX>00F80000FF03FF0000199512021130<ETX>3E

# V-4C2-03  request G3  (53 byte)
05 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 31 33
<ENQ>00F80000FF03FF000014010000D*000100000319951202113013

# V-4C2-04  response G3 (không dữ liệu)  (19 byte)
06 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30
<ACK>00F80000FF03FF0000

# V-4C2-05  response lỗi, error code 7151H  (23 byte)
15 30 30 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 37 31 35 31
<NAK>00F80000FF03FF00007151

```

### A.9 Frame 4C — Format 3

```text
# V-4C3-01  request G1  (40 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 03 35 33
<STX>F80000FF03FF000004010000D*0001000003<ETX>53

# V-4C3-02  response G1  (36 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 51 41 43 4B 31 39 39 35 31 32 30 32 31 31 33 30 03 46 45
<STX>F80000FF03FF0000QACK199512021130<ETX>FE

# V-4C3-03  request G3  (52 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 03 42 36
<STX>F80000FF03FF000014010000D*0001000003199512021130<ETX>B6

# V-4C3-04  response G3 (không dữ liệu)  (22 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 51 41 43 4B 03
<STX>F80000FF03FF0000QACK<ETX>

# V-4C3-05  response lỗi, error code 7151H  (26 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 51 4E 41 4B 37 31 35 31 03
<STX>F80000FF03FF0000QNAK7151<ETX>

```

### A.10 Frame 4C — Format 4

```text
# V-4C4-01  request G1  (41 byte)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 35 30 0D 0A
<ENQ>F80000FF03FF000004010000D*000100000350<CR><LF>

# V-4C4-02  response G1  (34 byte)
02 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 44 45 0D 0A
<STX>F80000FF03FF0000199512021130<ETX>DE<CR><LF>

# V-4C4-03  request G3  (53 byte)
05 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 31 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 42 33 0D 0A
<ENQ>F80000FF03FF000014010000D*0001000003199512021130B3<CR><LF>

# V-4C4-04  response G3 (không dữ liệu)  (19 byte)
06 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 0D 0A
<ACK>F80000FF03FF0000<CR><LF>

# V-4C4-05  response lỗi, error code 7151H  (23 byte)
15 46 38 30 30 30 30 46 46 30 33 46 46 30 30 30 30 37 31 35 31 0D 0A
<NAK>F80000FF03FF00007151<CR><LF>

```

### A.11 Frame 4C — Format 5 (Binary)

```text
# V-4C5-01  request G1  (26 byte)
10 02 12 00 F8 00 00 FF FF 03 00 00 01 04 00 00 64 00 00 A8 03 00 10 03 31 46

# V-4C5-02  response G1  (26 byte)
10 02 12 00 F8 00 00 FF FF 03 00 00 FF FF 00 00 95 19 02 12 30 11 10 03 30 43

# V-4C5-03  request G3  (32 byte)
10 02 18 00 F8 00 00 FF FF 03 00 00 01 14 00 00 64 00 00 A8 03 00 95 19 02 12 30 11 10 03 33 38

# V-4C5-04  response G3 (không dữ liệu)  (20 byte)
10 02 0C 00 F8 00 00 FF FF 03 00 00 FF FF 00 00 10 03 30 33

# V-4C5-05  response lỗi, error code 7151H  (20 byte)
10 02 0C 00 F8 00 00 FF FF 03 00 00 FF FF 51 71 10 03 43 35

# V-4C5-06  request đọc D16 × 1 (device number 10H → additional code)  (27 byte)
10 02 12 00 F8 00 00 FF FF 03 00 00 01 04 00 00 10 10 00 00 A8 01 00 10 03 43 39

# V-4C5-07  response D16 = 1010H (2 additional code)  (24 byte)
10 02 0E 00 F8 00 00 FF FF 03 00 00 FF FF 00 00 10 10 10 10 10 03 32 35

# V-4C5-M1  ví dụ manual: station 05, network 07, PC 03, I/O 0004H, module station 01; 0401/0001 X40 × 5  (26 byte)
10 02 12 00 F8 05 07 03 04 00 01 00 01 04 01 00 40 00 00 9C 05 00 10 03 30 35

```

### A.12 Frame 3C — Format 1

```text
# V-3C1-01  request G1  (33 byte)
05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 30 32
<ENQ>F90000FF0004010000D*000100000302

# V-3C1-02  response G1  (26 byte)
02 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 39 30
<STX>F90000FF00199512021130<ETX>90

# V-3C1-03  request G4  (41 byte)
05 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 39 36
<ENQ>F90000FF0014010001M*00010000081100110096

# V-3C1-04  response G4 (không dữ liệu)  (11 byte)
06 46 39 30 30 30 30 46 46 30 30
<ACK>F90000FF00

# V-3C1-05  response lỗi, error code 7151H  (15 byte)
15 46 39 30 30 30 30 46 46 30 30 37 31 35 31
<NAK>F90000FF007151

```

### A.13 Frame 3C — Format 2

```text
# V-3C2-01  request G1  (35 byte)
05 30 30 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 36 32
<ENQ>00F90000FF0004010000D*000100000362

# V-3C2-02  response G1  (28 byte)
02 30 30 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 46 30
<STX>00F90000FF00199512021130<ETX>F0

# V-3C2-03  request G4  (43 byte)
05 30 30 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 46 36
<ENQ>00F90000FF0014010001M*000100000811001100F6

# V-3C2-04  response G4 (không dữ liệu)  (13 byte)
06 30 30 46 39 30 30 30 30 46 46 30 30
<ACK>00F90000FF00

# V-3C2-05  response lỗi, error code 7151H  (17 byte)
15 30 30 46 39 30 30 30 30 46 46 30 30 37 31 35 31
<NAK>00F90000FF007151

```

### A.14 Frame 3C — Format 3

```text
# V-3C3-01  request G1  (34 byte)
02 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 03 30 35
<STX>F90000FF0004010000D*0001000003<ETX>05

# V-3C3-02  response G1  (30 byte)
02 46 39 30 30 30 30 46 46 30 30 51 41 43 4B 31 39 39 35 31 32 30 32 31 31 33 30 03 42 30
<STX>F90000FF00QACK199512021130<ETX>B0

# V-3C3-03  request G4  (42 byte)
02 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 03 39 39
<STX>F90000FF0014010001M*000100000811001100<ETX>99

# V-3C3-04  response G4 (không dữ liệu)  (16 byte)
02 46 39 30 30 30 30 46 46 30 30 51 41 43 4B 03
<STX>F90000FF00QACK<ETX>

# V-3C3-05  response lỗi, error code 7151H  (20 byte)
02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 37 31 35 31 03
<STX>F90000FF00QNAK7151<ETX>

```

### A.15 Frame 3C — Format 4

```text
# V-3C4-01  request G1  (35 byte)
05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 33 30 32 0D 0A
<ENQ>F90000FF0004010000D*000100000302<CR><LF>

# V-3C4-02  response G1  (28 byte)
02 46 39 30 30 30 30 46 46 30 30 31 39 39 35 31 32 30 32 31 31 33 30 03 39 30 0D 0A
<STX>F90000FF00199512021130<ETX>90<CR><LF>

# V-3C4-03  request G4  (43 byte)
05 46 39 30 30 30 30 46 46 30 30 31 34 30 31 30 30 30 31 4D 2A 30 30 30 31 30 30 30 30 30 38 31 31 30 30 31 31 30 30 39 36 0D 0A
<ENQ>F90000FF0014010001M*00010000081100110096<CR><LF>

# V-3C4-04  response G4 (không dữ liệu)  (13 byte)
06 46 39 30 30 30 30 46 46 30 30 0D 0A
<ACK>F90000FF00<CR><LF>

# V-3C4-05  response lỗi, error code 7151H  (17 byte)
15 46 39 30 30 30 30 46 46 30 30 37 31 35 31 0D 0A
<NAK>F90000FF007151<CR><LF>

```

### A.16 Frame 1C — Format 1

```text
# V-1C1-01  request WR D100 × 3  (17 byte)
05 30 30 46 46 57 52 30 44 30 31 30 30 30 33 32 44
<ENQ>00FFWR0D0100032D

# V-1C1-02  response WR: 1995H, 1202H, 1130H  (20 byte)
02 30 30 46 46 31 39 39 35 31 32 30 32 31 31 33 30 03 35 31
<STX>00FF199512021130<ETX>51

# V-1C1-03  request BR M100 × 8  (17 byte)
05 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 32 36
<ENQ>00FFBR0M01000826

# V-1C1-04  response BR: 0,0,0,1,0,0,1,1  (16 byte)
02 30 30 46 46 30 30 30 31 30 30 31 31 03 37 32
<STX>00FF00010011<ETX>72

# V-1C1-05  request WW D100 = 1995H, 1202H, 1130H  (29 byte)
05 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 39 34
<ENQ>00FFWW0D01000319951202113094

# V-1C1-06  request BW M100 = 1,1,0,0,1,1,0,0  (25 byte)
05 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 41 46
<ENQ>00FFBW0M01000811001100AF

# V-1C1-07  response ghi thành công (không dữ liệu)  (5 byte)
06 30 30 46 46
<ACK>00FF

# V-1C1-08  response lỗi, error code 06H  (7 byte)
15 30 30 46 46 30 36
<NAK>00FF06

# V-1C1-09  request QR D100 × 3 (lệnh AnA/AnU)  (19 byte)
05 30 30 46 46 51 52 30 44 30 30 30 31 30 30 30 33 38 37
<ENQ>00FFQR0D0001000387

# V-1C1-10  request WR X40 × 2 word (bit device theo word)  (17 byte)
05 30 30 46 46 57 52 30 58 30 30 34 30 30 32 34 33
<ENQ>00FFWR0X00400243

# V-1C1-11  request BR M0 × 256 (points = "00")  (17 byte)
05 30 30 46 46 42 52 30 4D 30 30 30 30 30 30 31 44
<ENQ>00FFBR0M0000001D

```

### A.17 Frame 1C — Format 2

```text
# V-1C2-01  request WR D100 × 3  (19 byte)
05 30 30 30 30 46 46 57 52 30 44 30 31 30 30 30 33 38 44
<ENQ>0000FFWR0D0100038D

# V-1C2-02  response WR: 1995H, 1202H, 1130H  (22 byte)
02 30 30 30 30 46 46 31 39 39 35 31 32 30 32 31 31 33 30 03 42 31
<STX>0000FF199512021130<ETX>B1

# V-1C2-03  request BR M100 × 8  (19 byte)
05 30 30 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 38 36
<ENQ>0000FFBR0M01000886

# V-1C2-04  response BR: 0,0,0,1,0,0,1,1  (18 byte)
02 30 30 30 30 46 46 30 30 30 31 30 30 31 31 03 44 32
<STX>0000FF00010011<ETX>D2

# V-1C2-05  request WW D100 = 1995H, 1202H, 1130H  (31 byte)
05 30 30 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 46 34
<ENQ>0000FFWW0D010003199512021130F4

# V-1C2-06  request BW M100 = 1,1,0,0,1,1,0,0  (27 byte)
05 30 30 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 30 46
<ENQ>0000FFBW0M010008110011000F

# V-1C2-07  response ghi thành công (không dữ liệu)  (7 byte)
06 30 30 30 30 46 46
<ACK>0000FF

# V-1C2-08  response lỗi, error code 06H  (9 byte)
15 30 30 30 30 46 46 30 36
<NAK>0000FF06

```

### A.18 Frame 1C — Format 3

```text
# V-1C3-01  request WR D100 × 3  (18 byte)
02 30 30 46 46 57 52 30 44 30 31 30 30 30 33 03 33 30
<STX>00FFWR0D010003<ETX>30

# V-1C3-02  response WR: 1995H, 1202H, 1130H  (22 byte)
02 30 30 46 46 47 47 31 39 39 35 31 32 30 32 31 31 33 30 03 44 46
<STX>00FFGG199512021130<ETX>DF

# V-1C3-03  request BR M100 × 8  (18 byte)
02 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 03 32 39
<STX>00FFBR0M010008<ETX>29

# V-1C3-04  response BR: 0,0,0,1,0,0,1,1  (18 byte)
02 30 30 46 46 47 47 30 30 30 31 30 30 31 31 03 30 30
<STX>00FFGG00010011<ETX>00

# V-1C3-05  request WW D100 = 1995H, 1202H, 1130H  (30 byte)
02 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 03 39 37
<STX>00FFWW0D010003199512021130<ETX>97

# V-1C3-06  request BW M100 = 1,1,0,0,1,1,0,0  (26 byte)
02 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 03 42 32
<STX>00FFBW0M01000811001100<ETX>B2

# V-1C3-07  response ghi thành công (không dữ liệu)  (8 byte)
02 30 30 46 46 47 47 03
<STX>00FFGG<ETX>

# V-1C3-08  response lỗi, error code 06H  (10 byte)
02 30 30 46 46 4E 4E 30 36 03
<STX>00FFNN06<ETX>

```

### A.19 Frame 1C — Format 4

```text
# V-1C4-01  request WR D100 × 3  (19 byte)
05 30 30 46 46 57 52 30 44 30 31 30 30 30 33 32 44 0D 0A
<ENQ>00FFWR0D0100032D<CR><LF>

# V-1C4-02  response WR: 1995H, 1202H, 1130H  (22 byte)
02 30 30 46 46 31 39 39 35 31 32 30 32 31 31 33 30 03 35 31 0D 0A
<STX>00FF199512021130<ETX>51<CR><LF>

# V-1C4-03  request BR M100 × 8  (19 byte)
05 30 30 46 46 42 52 30 4D 30 31 30 30 30 38 32 36 0D 0A
<ENQ>00FFBR0M01000826<CR><LF>

# V-1C4-04  response BR: 0,0,0,1,0,0,1,1  (18 byte)
02 30 30 46 46 30 30 30 31 30 30 31 31 03 37 32 0D 0A
<STX>00FF00010011<ETX>72<CR><LF>

# V-1C4-05  request WW D100 = 1995H, 1202H, 1130H  (31 byte)
05 30 30 46 46 57 57 30 44 30 31 30 30 30 33 31 39 39 35 31 32 30 32 31 31 33 30 39 34 0D 0A
<ENQ>00FFWW0D01000319951202113094<CR><LF>

# V-1C4-06  request BW M100 = 1,1,0,0,1,1,0,0  (27 byte)
05 30 30 46 46 42 57 30 4D 30 31 30 30 30 38 31 31 30 30 31 31 30 30 41 46 0D 0A
<ENQ>00FFBW0M01000811001100AF<CR><LF>

# V-1C4-07  response ghi thành công (không dữ liệu)  (7 byte)
06 30 30 46 46 0D 0A

# V-1C4-08  response lỗi, error code 06H  (9 byte)
15 30 30 46 46 30 36 0D 0A
<NAK>00FF06<CR><LF>

```

---

## Phụ lục B — Truy xuất nguồn

| Mục tài liệu | Nguồn |
|---|---|
| §1.1 So sánh frame | `part2:24-55`, `part2:454-490`, `part2:806-820`, `part5:29-41` |
| §2.1 Trường số, thứ tự byte | `part2:465-478`, `part2:580-593`, `part3:400-436` |
| §2.2–2.4 Dữ liệu bit/word/dword | `part3:481-583`, `part5:2542-2544` + ví dụ lệnh |
| §2.5 Sum check | `part2:351-412` |
| §2.6 DLE | `part2:266-319` |
| §2.7 Control code, EOT/CL | `part2:219-289` |
| §3.2 Device code | `part3:291-353`, `part5:377-402`, `part5:2374-2392` |
| §3.3 Mã hóa device | `part3:187-289`, `part5:332-375`, `part5:2343-2372` |
| §3.5 Ràng buộc | `part3:355-398`, `part5:301-330`, `part5:2316-2341` |
| §4.1 Lệnh QnA | `part3:62-85`, `part3:875-1597`, `part3:621-637` |
| §4.2 Lệnh 1E | `part5:2272-2967` |
| §4.3 Lệnh 1C | `part5:149-289`, `part5:433-911` |
| §4.4 Giới hạn | các bảng "Number of device points" trong `part3`/`part5`; PDF Appendix 5 (trang in 466, 469, 470) |
| §5.1–5.2 3E/4E | `part2:492-653`, `part2:756-784`, `part2:822-1012` |
| §5.3 1E | `part5:2029-2270`, `part2:786-804` |
| §5.4 4C | `part2:57-213`, `part2:329-446`, `part2:665-693`, `part2:822-1039` |
| §5.5 3C | `part2:695-714` |
| §5.6 1C | `part2:735-754`, `part5:100-245` |
