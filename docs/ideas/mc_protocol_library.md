# Thư viện MC Protocol (`mc`) — kết quả refine ý tưởng

- **Ngày:** 2026-09-25
- **Đầu vào:** `docs/intent/mc_protocol_library.md` (ý định đã xác nhận), `docs/mc_reference/mc-protocol-frame-spec.vi.md` (đặc tả), `reference_source/device/plc/` (module cũ)
- **Trạng thái:** hướng C **đã được tác giả xác nhận** ngày 2026-09-25, bảy câu hỏi mở đã chốt tại mục 8. Mục 9 (performance) bổ sung cùng ngày, định nghĩa là độ phức tạp thời gian và bộ nhớ của core (mục 9.3).
- **Đã được spec thay thế ở các điểm sau** (phỏng vấn 2026-09-25, quyết định 8–11 trong `docs/intent/mc_protocol_library.md`): đầu ra của `Session` ở mục 4.3 (`ValuesChanged` theo response, `Snapshot` theo loại device, `LinkFault` chỉ báo; EOT do `Session` tự gửi) và cách dựng mock PLC ở biến thể #6 (lai). Khi mâu thuẫn, `docs/spec/SPEC-core-session.md`, `SPEC-mock-plc.md`, `SPEC-qt-device.md` là bản đúng.

---

## 1. Problem Statement

> **Làm sao để** một module MC protocol đi vào bất kỳ project nào, có git hay không, **cập nhật ở một nơi**, app không phải sửa header, **và** người không dùng Qt vẫn dùng được phần lõi?

Căng thẳng cốt lõi: app cũ cần polling engine đầy đủ (vòng đọc, retry, diff, signal), nhưng "người không dùng Qt vẫn dùng được" đòi hỏi phần giá trị nhất của thư viện **không được** nằm trong lớp Qt.

---

## 2. Các biến thể đã xét (Phase 1)

| # | Lăng kính | Biến thể | Điều đáng giữ lại |
|---|---|---|---|
| 1 | Đơn giản 10x | **Port sạch:** chép ba trục Frame / Transport / Context của module cũ vào một folder, cắt `IDevice`, `PlcDevice`, `IRequest`, thay logger bằng sink, giữ `QByteArray`. | Nhanh nhất. Nhưng core vẫn Qt, vẫn chỉ X/Y/M/D, context vẫn `Q_GADGET`. Không đạt "core thuần std". Sửa sau là phá API, đúng thứ ta muốn tránh. |
| 2 | Chuyên gia | **Năm lớp theo spec §8:** Client / Command / FieldCodec / Frame / Transport, đủ 3E, 4E, 1E, 4C, 3C, 1C, random access. | Kiến trúc lớp Command và Codec là đúng: một bộ command QnA dùng cho 4 frame, không có nhánh ASCII/Binary trong command. Nhưng phạm vi quá rộng cho v1. |
| 3 | Ý tưởng gốc | **Hai tầng:** `mc_core` (codec, bảng device, giới hạn, chunker, gom vùng, value store, convert, log sink) + `mc_device` (Qt transport, polling engine, queue, signal). | Đúng ranh giới tác giả muốn. Điểm yếu: polling engine nằm ở Qt, người không dùng Qt phải viết lại vòng polling, và engine chỉ test được khi có socket. |
| 4 | Đảo ngược | **Sans-I/O: engine nằm trong core.** Máy trạng thái polling nhận đầu vào (byte nhận được, `tick(now)`, request, subscribe) và cho ra đầu ra (byte cần gửi, sự kiện). McDevice chỉ là adapter nối QTcpSocket, QSerialPort, QTimer vào nó. | AGENTS.md của app cũ tự thừa nhận: *"nothing in this repository has ever executed a McProtocolDevice method"* vì device tự tạo transport, không có điểm tiêm. Sans-I/O sửa đúng chỗ đó. |
| 5 | Bỏ ràng buộc | **Core header-only** để project không git chỉ cần chép `include/`. | Không lấy toàn bộ: bảng device và frame parser trong header làm compile chậm và lộ nội bộ. Lấy một phần: bề mặt public gọn, nội bộ nằm ở `src/`. |
| 6 | Kết hợp | **Mock PLC dựng từ chính codec:** server test dùng decoder của core để hiểu request và encoder để trả lời, có ngay cho cả 4 frame. | Cho phép test McDevice qua loopback TCP không cần PLC, và thành công cụ "PLC ảo" để demo, giống `VirtualPlcDevice` bên app cũ. |
| 7 | 10x | Pipelining nhiều request qua serial number của 4E, nhiều PLC, UDP. | Không làm v1. Nhưng cấu trúc header frame phải **chừa chỗ** cho serial number để v2 thêm mà không đổi API. |

---

## 3. Ba hướng và stress-test (Phase 2)

| | **A. Port sạch** (#1) | **B. Hai tầng** (#3 + #6) | **C. Sans-I/O** (#4 + #3 + #6) |
|---|---|---|---|
| **Giá trị** | Trung bình. Hết copy-paste cho app Qt, nhưng core vẫn Qt. | Cao cho app Qt. Người không Qt chỉ có codec. | Cao nhất. Toàn bộ hành vi (polling, retry, timeout, reconnect) là std, test bằng byte và tick. McDevice còn khoảng vài trăm dòng glue. |
| **Khả thi** | Cao, vài ngày. | Trung bình. | Trung bình. Máy trạng thái **đã có sẵn** trong `McProtocolDevice` (`polling_query`, `request_handle`, `response_handle`, `retry_request_handle`), việc chính là trích ra và đưa thời gian vào làm đầu vào. |
| **Khác biệt** | Thấp. | Trung bình. | Cao. Đây là thứ làm thư viện thực sự tái sử dụng ngoài Qt. |
| **Chỗ khó nhất** | Không có. | Test engine cần socket hoặc fake `QIODevice`. | Mô hình hóa timeout bằng `tick(now)`, định nghĩa hàng đợi sự kiện đầu ra, và EOT khi lỗi serial. |
| **Painkiller hay vitamin** | Vitamin. | Painkiller cho app Qt. | Painkiller cho mọi consumer. |

**Ma trận quyết định:** C ở ô "giá trị cao, khả thi trung bình" → làm. B là phương án lùi nếu C tỏ ra gượng ép giữa chừng, và việc lùi **không đổi API public** của `McProtocol`, chỉ chuyển engine từ core sang McDevice. A loại vì khóa cứng X/Y/M/D và `QByteArray` vào API, sửa sau là phá.

---

## 4. Hướng đề xuất: C, với cách chia target của B

### 4.1 Sơ đồ lớp

```text
App (Qt hoặc không Qt)
  │ subscribe / submit / tick / bytesIn                  events / bytesOut
  ▼
mc::Session ─────────── engine sans-I/O: queue, polling, retry, diff, heartbeat
  │
mc::McProtocol ──────── encode(Request) → bytes ; Parser::feed(bytes) → payload
  │
Frame{3E,1E,3C,1C} ─── Command{QnA,A1E,A1C} ─── Codec{Ascii,Binary} ─── DeviceTable
                                                                    ┃ mc_core (std C++17)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
mc::McDevice (QObject) ─ QTimer → tick, QTcpSocket/QSerialPort ↔ bytes, outputs → signals
                                                                    ┃ mc_device (Qt 6)
```

### 4.2 Hai class chính theo đúng tên tác giả đặt

**`mc::McProtocol`** (core, không trạng thái phiên): được cấu hình bằng `FrameConfig` (loại frame, data code, access route, monitoring timer, series, sum check, format). Hai việc:

- `encode(const Request&) → Expected<Bytes>`: một request, một frame.
- `parser(const Request&) → Parser`: parser tăng dần, `feed(bytes)` trả `NeedMore | Done | Error`, có `remainder()` cho TCP dính frame, `payload()` là `vector<uint8_t>` đã chuẩn hóa theo command.

**`mc::McDevice`** (Qt): sở hữu một `Session`, một `Transport`, một `QTimer`. Slot: `connectToPlc()`, `disconnectFromPlc()`, `subscribe()`, `submit()`. Signal: `valuesChanged`, `requestFinished(id, result)`, `connectionChanged`, `pollCycleDone`. Không tạo thread bên trong; app `moveToThread` nếu cần, giống cách `PlcRunner` làm bên app cũ.

### 4.3 Các class std đi kèm core

| Class | Vai trò | Nguồn gốc bên app cũ |
|---|---|---|
| `DeviceTable`, `parseDevice()` | Bảng device theo spec §3.2 dưới dạng **dữ liệu**, đủ QnA/1E/1C, không chỉ X/Y/M/D | `mc_define.h` (chỉ 4 device, if/else) |
| `Limits`, `chunk()` | Bảng giới hạn §4.4 theo frame, code, device kind, series; chia request | `MCRequest` hard-code 64/128 |
| `RangeSet`, `ValueStore`, `diff()` | Đăng ký vùng, gom liền nhau, lưu giá trị, diff ra danh sách thay đổi, cho **mọi** symbol device | `McDeviceRange`, `McDeviceMap`, `mc_device_map_diff.h` |
| `Session` | Engine sans-I/O: một request in-flight, polling round-robin, retry, timeout, heartbeat tùy chọn; đầu vào `bytesIn`, `tick(now)`, `submit`, `linkUp/linkDown`; đầu ra `send(bytes)`, `RequestDone`, `ValuesChanged`, `LinkFault{reconnect \| sendEOT}` | máy trạng thái trong `McProtocolDevice` |
| `convert` | words ↔ int16/int32/uint32/float32/float64/string, đóng gói bit | `memory_utils.h`, `MCRequest::buildWriteData_*` |
| `LogSink` | interface `(level, category, message)`, có null sink | `LOG_DEV_*` macro |
| `Error` | `{category: Config \| Encode \| Transport \| Protocol \| Plc, code, text}` theo spec §7 | `FrameReturnCode` + `m_last_error` |

### 4.4 Cấu trúc thư mục

```text
mc_protocol/
├── CMakeLists.txt               # target mc::core (không Qt), mc::device (Qt 6); option MC_BUILD_DEVICE/TESTS/EXAMPLES
├── mc_protocol.pri              # qmake: include mc_core.pri + mc_device.pri
├── include/mc/                  # BỀ MẶT PUBLIC DUY NHẤT, semver
│   ├── version.h
│   ├── core/
│   │   ├── device.h             # Device, DeviceType, parseDevice()
│   │   ├── request.h            # Request {op, device, count, values}
│   │   ├── result.h             # Result, Error, Expected<T>
│   │   ├── frame_config.h       # FrameConfig cho 3E/1E/3C/1C (chừa chỗ serial 4E, format 5)
│   │   ├── protocol.h           # McProtocol, Parser
│   │   ├── limits.h             # Limits, chunk()
│   │   ├── value_store.h        # RangeSet, ValueStore, diff()
│   │   ├── session.h            # Session, SessionConfig, Output
│   │   ├── convert.h
│   │   └── log.h                # LogSink
│   └── device/
│       ├── mc_device.h          # McDevice (QObject)
│       ├── mc_device_config.h   # struct thuần + toJson/fromJson
│       ├── tcp_transport.h
│       └── serial_transport.h
├── src/core/                    # PRIVATE: codec/, command/, frame/, session/, tables/
├── src/device/
├── tests/
│   ├── vectors/*.json           # golden vector từ spec Phụ lục A và bảng CMD-xx
│   ├── core/                    # L1–L4 + Session với fake clock và fake byte
│   └── device/                  # mock PLC dựng từ codec, loopback TCP; serial qua cặp COM ảo, skip nếu thiếu
├── examples/                    # console poller (Qt), non-Qt loop mẫu
└── docs/                        # spec, intent, ideas, ADR
```

Quy tắc đóng gói: mọi include trong thư viện là `mc/...` tương đối với `include/`. Không file nào trong `include/mc/` được include từ `src/`. `src/` được include `include/`.

### 4.5 Phác thảo cách dùng

App Qt:

```cpp
mc::FrameConfig fc = mc::FrameConfig::frame3E();            // binary, route mặc định 00/FF/03FF/00
mc::McDevice dev(fc, std::make_unique<mc::TcpTransport>("192.168.0.10", 5000));
dev.setLogSink(myLogSink);
dev.subscribe("M2000", 64);
dev.subscribe("D2000", 64);
connect(&dev, &mc::McDevice::valuesChanged, this, &App::onPlcValues);
connect(&dev, &mc::McDevice::requestFinished, this, &App::onPlcRequest);
dev.connectToPlc();
uint64_t id = dev.submit(mc::Request::writeWords("D100", mc::convert::fromFloat32(1.5f)));
```

Người không dùng Qt:

```cpp
mc::Session s(fc, sessionCfg);
s.subscribe(mc::parseDevice("D2000"), 64);
s.linkUp();
for (;;) {
    s.bytesIn(mySocket.recv());
    s.tick(nowMs());
    for (auto& out : s.drainOutputs()) {
        if (out.kind == mc::Output::Send)          mySocket.send(out.bytes);
        else if (out.kind == mc::Output::Values)   handle(out.changes);
        else if (out.kind == mc::Output::LinkFault) reconnect();
    }
}
```

Đọc 4 word rồi đổi sang float64, đúng ví dụ tác giả nêu:

```cpp
auto payload = result.payload;                       // 8 byte, 2 byte mỗi word, LE
double v = mc::convert::toFloat64(payload, /*offsetWord=*/0);
```

---

## 5. Giả định cần kiểm chứng

### Phải đúng, sai là chết ý tưởng

- [ ] **Polling engine diễn tả được không cần thread hay blocking**, thời gian đi vào bằng `tick(now)`. *Cách thử:* viết `Session` **trước tiên** với fake clock và fake byte, port lại đủ retry, timeout, reconnect của `McProtocolDevice`. Nếu gượng, lùi về hướng B, API `McProtocol` không đổi.
- [ ] **API public đóng băng được ở v1.0 trước khi project đầu tiên dùng.** Nếu không, lời hứa "update không sửa app" vỡ và quay về copy-paste. *Cách thử:* example app và project tiêu thụ đầu tiên viết trên `include/mc/`; chưa gắn tag 1.0 chừng nào chúng chưa compile nguyên vẹn qua hai vòng sửa nội bộ.
- [ ] **Frame 3E binary ra đúng byte như module cũ trên PLC thật.** *Cách thử:* so byte với golden vector, chạy lại `tests/mc_frame_test` của app cũ trên codec mới, rồi chạy trên chính PLC iQ-L mà app cũ đang nói chuyện.

### Nên đúng, sai thì đổi cách làm

- [ ] 37 golden vector trong spec đúng (spec ghi 37/37 khớp manual). 1C và 3C **chưa được kiểm trên dây** theo ghi chú của app cũ. *Cách thử:* bench tool với cổng C24; chưa có thì README ghi rõ "serial: wire-unverified".
- [ ] Hợp đồng payload chuẩn hóa (bit → 1 byte một điểm, word → 2 byte LE) đủ cho mọi consumer. *Cách thử:* port toàn bộ chỗ dùng của app cũ (map M/D, ghi float) sang helper `convert`.
- [ ] Hai hệ build giữ đồng bộ được. *Cách thử:* một test nhỏ đọc `.pri` và `CMakeLists.txt`, khẳng định danh sách source giống nhau; chạy trong CI.

### Có thể đúng, kiểm sau

- [ ] Consumer sớm cần subcommand iQ-R và random access 0403/1402. Rẻ khi lớp Command là dữ liệu.
- [ ] Chỗ chừa cho serial number 4E sẽ được dùng cho pipelining ở v2.

### Pre-mortem: điều gì giết dự án sau 12 tháng

| Rủi ro | Phòng ngừa |
|---|---|
| API đổi sau 1.0, consumer ngừng update, quay về copy-paste | Semver; `include/mc/` là bề mặt public duy nhất; không hứa ABI, chỉ build từ source |
| Phình phạm vi (4E, 4C, UDP, widget) trước khi core ổn | Danh sách "không làm" bên dưới |
| Engine viết trong Qt trước "cho nhanh", rồi không bao giờ trích ra | `Session` trong core là **milestone đầu tiên**, McDevice làm sau |
| Serial sai trên dây mà không có phần cứng kiểm | Chấp nhận ở v1, dán nhãn rõ |

---

## 6. Phạm vi MVP (v1.0)

**Một việc làm tốt:** app Qt đăng ký vùng nhớ, McDevice polling và báo thay đổi, ghi lẻ có correlation id, qua 3E TCP, với toàn bộ engine test được không cần PLC.

**`mc_core`**

- Device table đủ theo §3.2; `parseDevice()` với quy tắc so khớp dài nhất trước và cơ số theo device.
- Codec Ascii và Binary.
- Command: QnA 0401/1401 bit và word; A1E 00H–03H; A1C BR/WR/BW/WW.
- Frame: 3E, 1E, 3C, 1C (format 1 và 4, sum check bật/tắt); parser tăng dần có `remainder()`.
- `McProtocol` facade; `Limits` + `chunk()`; `RangeSet` + `ValueStore` + `diff()`; `Session`; `convert`; `LogSink`; `Error`.
- Chính sách: đọc bit > 8 điểm trên X/Y/M được đổi thành đọc word rồi giải nén, như app cũ, đặt trong `Session` và có thể tắt.

**`mc_device`**

- `TcpTransport`, `SerialTransport` bọc `QIODevice`, hoàn toàn signal.
- `McDevice` như §4.2; `McDeviceConfig` là struct thuần kèm `toJson/fromJson`.

**Test và ví dụ**

- Golden vector JSON; test L1–L4 theo spec §9; test `Session` với fake clock.
- Mock PLC trên codec, test McDevice qua loopback TCP.
- Example: console poller Qt; vòng lặp non-Qt mẫu.

**Build**

- CMake: `mc::core`, `mc::device`, option tắt device/tests/examples.
- qmake: `mc_core.pri`, `mc_device.pri`, `mc_protocol.pri`.
- `mc/version.h`, `CHANGELOG.md`, tag `v1.x.y`.

**Thứ tự làm:** (1) core codec + golden vector cho 3E → (2) `Session` với fake clock → (3) McDevice + mock PLC loopback → (4) chạy PLC thật 3E → (5) 1E, 3C, 1C trên codec đã có khung → (6) đóng băng API, tag 1.0.

---

## 7. Không làm ở v1 và lý do

- **Ba widget Qt** → v2, target riêng `mc_widgets` link vào `mc_device`. Widget kéo theme và quy ước UI của từng app vào thư viện, làm chậm phần lõi.
- **4E, 4C** → v2. `FrameConfig` chừa chỗ cho serial number và format 5 để thêm là cộng vào, không sửa.
- **UDP** → v2. `Session` xem transport là dòng byte; biên datagram là một adapter nhỏ sau này.
- **Build sẵn, cài đặt, ABI** → chỉ build từ source. Hứa ABI là đóng băng nội bộ quá sớm.
- **Thread bên trong thư viện** → app sở hữu thread, như `PlcRunner` bên app cũ.
- **Random access 0403/1402, monitor, block read** → v1.1 trở đi khi lớp Command đã chứng minh. Xem câu hỏi mở.
- **Bảng ý nghĩa mã lỗi PLC** → trả mã thô; ý nghĩa nằm ở manual từng module, có thể thêm như dữ liệu sau.
- **`Q_GADGET`, `Q_PROPERTY` trong config** → thuộc widget. Config core là struct thuần.

---

## 8. Câu hỏi mở cần chốt trước khi viết spec

1. **Báo lỗi ở API public của core:** struct `Error` và `Expected<T>` hay exception? *Đề xuất:* không exception qua ranh giới public; core dùng `Expected`, McDevice chuyển thành signal.
*Quyết định:* không exception như đề xuất.
2. **Random access 0403/1402** vào v1 hay v1.1? *Đề xuất:* v1.1, trừ khi project sắp tới cần.
*Quyết định:* v1.1.
3. **Ngôn ngữ tài liệu commit trong repo:** tiếng Việt, tiếng Anh, hay song ngữ như spec? App cũ quy định tiếng Anh.
*Quyết định:* dùng tiếng anh.
4. **Tên engine:** `mc::Session` hay tên khác? `McProtocol` giữ đúng tên tác giả đặt cho codec facade.
*Quyết định:* hãy dùng `mc::Session`
5. **Chuẩn C++:** C++17 (Qt 6 tối thiểu). Có consumer nào kẹt C++14 không?
*Quyết định:* không có.
6. **Heartbeat "comm active M device"** của app cũ: đưa vào `Session` như tính năng tùy chọn, mặc định tắt, hay để app tự làm? *Đề xuất:* tùy chọn trong `Session`.
*Quyết định:* Là tính năng optinal. Mặc định tắt.
7. **Sau timeout với 3E/1E qua TCP**, spec §6.1 yêu cầu đóng và mở lại kết nối. `Session` phát `LinkFault{reconnect}` và McDevice tự reconnect, hay chỉ báo và để app quyết? *Đề xuất:* McDevice tự reconnect với backoff, có thể tắt.
*Quyết định:* Chỉ báo, reconnect sẽ cho app quyết.

---

## 9. Performance ở core (bổ sung 2026-09-25, chờ chốt định nghĩa)

Tác giả thêm mục tiêu: **tối ưu performance ở core nhất có thể**. Mục này ghi lại cách hiểu đề xuất và các đòn bẩy, để chốt chỉ số đo trước khi viết spec.

### 9.1 Chi phí thật nằm ở đâu

Một chu kỳ polling gồm: encode request (core, cỡ micro giây) → gửi qua mạng → PLC xử lý → nhận về (round trip, cỡ mili giây) → parse và diff (core, micro giây). Round trip lớn hơn công việc của core khoảng ba bậc. Vì vậy **đòn bẩy lớn nhất là số round trip mỗi chu kỳ**, đòn bẩy thứ hai mới là chi phí CPU và cấp phát của core.

### 9.2 Đòn bẩy đề xuất, theo thứ tự tác động

| # | Đòn bẩy | Nơi áp dụng | Ghi chú |
|---|---|---|---|
| 1 | **Ít round trip nhất:** gom vùng đăng ký tới sát giới hạn điểm của frame (3E: 960 word); **gộp hai vùng cách nhau khe nhỏ** thành một request, đọc thừa vài word thay vì tốn thêm một round trip (`maxGap` cấu hình được); đọc bit device theo word (16 điểm một word). | `RangeSet`, `Limits`, `Session` | App cũ chỉ gom vùng liền kề tuyệt đối, chưa gộp qua khe. v1.1: random read 0403 cho device rải rác. |
| 2 | **Không cấp phát ở trạng thái ổn định:** frame đọc của vòng polling **được encode một lần lúc subscribe** và dùng lại mọi chu kỳ (byte request đọc không đổi); một receive buffer dùng lại; parser trả `span` nhìn vào buffer thay vì copy; `Output` dùng bộ nhớ cấp trước. | `Session`, `Parser` | Chỉ đường ghi lẻ mới encode tại chỗ. |
| 3 | **`ValueStore` là mảng liên tục** theo từng vùng, không phải `std::map<int, T>`; diff là so sánh tuần tự O(n), thân thiện cache. | `ValueStore`, `diff()` | App cũ dùng `std::map` cho cả M và D. |
| 4 | **Không virtual trong đường nóng:** frame và codec chọn lúc cấu hình, bên trong dispatch bằng `switch` trên enum hoặc template; API public giữ non-template. | `McProtocol`, `src/core/frame`, `src/core/codec` | |
| 5 | **Bảng tra là `constexpr`:** bảng device và bảng giới hạn là mảng `constexpr` đánh chỉ số theo enum; `parseDevice()` từ chuỗi chỉ chạy lúc subscribe hoặc submit, không chạy mỗi chu kỳ. | `DeviceTable`, `Limits` | |
| 6 | **Log rẻ khi tắt:** kiểm tra level trước khi dựng chuỗi; không format trên đường nóng. | `LogSink` | |
| 7 | **Đo trước khi tối ưu:** benchmark trong `tests/bench/` cho encode, parse, và trọn chu kỳ với N vùng; số đo là con số theo dõi được, không phải cảm giác. | `tests/bench/` | |

### 9.3 Chỉ số: độ phức tạp thời gian và bộ nhớ (cách hiểu đã chốt 2026-09-25)

Tác giả xác định performance ở core là **time complexity và memory complexity** của từng thao tác. Không đặt con số cho chu kỳ polling vì nó phụ thuộc transport.

Ký hiệu: n = số điểm trong một request, L = độ dài frame theo byte, k = số byte đưa vào một lần `feed`, R = số vùng đã đăng ký, P = tổng số điểm đã đăng ký, C = số chunk (request) của một chu kỳ.

| Thao tác | Thời gian | Bộ nhớ | Ghi chú |
|---|---|---|---|
| `parseDevice(string)` | O(len) | 0 cấp phát | Bảng symbol vài chục mục, so khớp dài nhất trước bằng bảng tĩnh. Chỉ chạy lúc subscribe hoặc submit. |
| `McProtocol::encode` | O(n) | 0 cấp phát khi ghi vào buffer do caller cấp | Không tra chuỗi; device code lấy từ bảng `constexpr` theo enum. |
| `Parser::feed` | O(k) mỗi lần, O(L) tổng | O(1) trạng thái, không copy buffer | Tăng dần, không quét lại từ đầu; payload trả `span` nhìn vào buffer nhận. |
| `RangeSet::subscribe` | O(log R) | O(R) | Khoảng đã sắp xếp; gom và gộp qua khe làm **một lần** khi subscribe, không làm mỗi chu kỳ. |
| `chunk()` | O(C) | O(C) | Chạy một lần lúc subscribe; kết quả là danh sách frame đã encode sẵn. |
| `ValueStore` tra theo device | O(log R) tìm vùng + O(1) chỉ số | O(P), liên tục | Không `std::map<int, T>`; mỗi vùng một mảng liền. |
| `ValueStore` cập nhật từ payload | O(n) | 0 cấp phát | Sao chép tuần tự vào mảng của vùng. |
| `diff()` | O(n) mỗi response | 0 cấp phát ở trạng thái ổn định | So sánh tuần tự; danh sách thay đổi ghi vào bộ nhớ cấp trước, chỉ lớn lên khi số thay đổi vượt dung lượng đã giữ. |
| `Session::tick` | O(1) amortized | O(1) | Không quét toàn bộ vùng mỗi tick; chỉ so với mốc thời gian của request đang chờ và của chu kỳ. |
| `Session::bytesIn` | O(k) | 0 cấp phát | Đẩy thẳng vào parser của request đang chờ. |
| `Session::drainOutputs` | O(số output) | Dùng lại vùng chứa | |
| **Một chu kỳ polling, tổng** | **O(P)** | **0 cấp phát heap** | Số `Output::Send` mỗi chu kỳ bằng C, tối thiểu theo cấu trúc sau khi gộp. |
| Bảng device, bảng giới hạn | O(1) theo enum | `constexpr`, không heap | |

Bộ nhớ thường trực của `Session`: O(P) cho giá trị; O(tổng byte frame đã encode sẵn) cho các request polling; một receive buffer dung lượng bằng response dài nhất của frame đang dùng; hàng đợi request lẻ có giới hạn cấu hình được.

**Cách kiểm:**

- Test đếm cấp phát bằng allocator hook cho một chu kỳ ở trạng thái ổn định, kỳ vọng 0.
- Review thiết kế theo bảng trên cho mọi thay đổi chạm vào core; một thao tác vượt độ phức tạp đã ghi phải có lý do trong PR.
- Benchmark trong `tests/bench/` chỉ để theo dõi hồi quy, không đặt ngưỡng tuyệt đối.

### 9.4 Điều không đánh đổi vì performance

- API public không thành template và không lộ kiểu nội bộ.
- Không bỏ kiểm tra tham số ở `encode()`; kiểm tra một lần lúc subscribe hoặc submit, không kiểm tra lại mỗi chu kỳ.
- Không dùng thread trong core.

