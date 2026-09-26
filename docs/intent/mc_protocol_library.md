# Ý định: Thư viện MC Protocol (`mc`)

- **Trạng thái:** đã xác nhận qua phỏng vấn ngày 2026-09-25. Hướng thiết kế C và bảy quyết định mở đã chốt cùng ngày (xem mục "Quyết định đã chốt").
- **Nguồn:** `docs/intent/original-idea.vi.md` (ý tưởng thô, bản gốc lưu trữ; trước đây là `docs/spec-vi.md`), `docs/mc_reference/` (đặc tả protocol), `reference_source/device/plc/` (module đang dùng trong app vision)
- **Bước tiếp theo:** 7 spec trong `docs/spec/` đã được duyệt ngày 2026-09-26 (xem bảng "Module specs" của `CAPABILITY-MAP.md`); tiếp theo là `/plan`

## Kết quả mong muốn

Một thư viện MC protocol **tự chứa trong một thư mục**, namespace `mc`, gồm hai phần:

- **Core** thuần C++ tiêu chuẩn, không I/O, không Qt.
- **McDevice** dùng Qt 6, cung cấp transport TCP và serial cùng vòng polling.

Build được bằng cả CMake lẫn qmake.

## Người dùng

- Chính tác giả và các project của tác giả, cả project có git lẫn không có git.
- Người tự viết device không dùng Qt vẫn dùng được phần core.

## Vì sao bây giờ

Mỗi project đang giữ một bản copy đã sửa tay của module cũ. Muốn đem sang project khác phải chỉnh cả bộ header nền của app cũ đi kèm. Sửa một chỗ không lan sang chỗ khác, nên mỗi lần update là một lần khổ.

## Thành công là khi

- Project mới kéo thư viện vào bằng git submodule (`add_subdirectory` hoặc `include(.pri)`), hoặc chép nguyên folder đè lên, **không phải sửa header nào của project**.
- Nâng version thư viện thì app không cần đổi code, hoặc đổi rất ít.
- Core test được hoàn toàn bằng golden vector, không cần PLC.

## Ràng buộc chính

- **Core:** một request vào một frame ra; buffer vào payload ra kèm trạng thái "cần thêm byte". Payload là `vector<byte>` chuẩn hóa theo command (bit: 1 byte mỗi điểm, tùy chọn đóng gói 8 điểm một byte; word: 2 byte mỗi word). Chuyển kiểu (int16, int32, float32, float64, string) là API riêng, người dùng tự gọi.
- **Class std đi kèm core:** chia nhỏ request theo bảng giới hạn, gom vùng nhớ liền nhau thành ít request nhất, lưu giá trị device đã đọc.
- **McDevice:** event-driven, non-blocking. App đăng ký vùng nhớ, McDevice tự polling theo chu kỳ và phát signal khi giá trị đổi. App có thể đẩy request lẻ (chủ yếu ghi) và nhận kết quả qua signal có correlation id. Không có hàm blocking kiểu `readWords()` trả về ngay.
- **Log:** thư viện có log sink để app cắm hệ thống log của họ vào.
- **Frame v1:** 1E, 3E, 1C, 3C. Transport v1: Ethernet TCP/IP, Serial COM.
- **Đóng gói:** thư mục thư viện không include gì ra ngoài chính nó. Include path phía app cố định dạng `mc/...`.

## Mục tiêu bổ sung (thêm 2026-09-25)

- **Tối ưu performance ở core nhất có thể**, hiểu là **độ phức tạp thời gian và bộ nhớ** của từng thao tác trong core. Không đặt con số cho chu kỳ polling vì phụ thuộc transport. Mục tiêu theo thao tác và cách kiểm nằm ở `docs/ideas/mc_protocol_library.md` mục 9.3; tóm tắt: một chu kỳ polling là O(P) thời gian và 0 cấp phát heap, tra bảng O(1), parser tăng dần O(k), value store là mảng liên tục O(P).

## Quyết định đã chốt (2026-09-25)

| # | Câu hỏi | Quyết định |
|---|---|---|
| 0 | Hướng thiết kế | **C**: engine sans-I/O `mc::Session` nằm trong core, `McDevice` là adapter Qt. B là phương án lùi. |
| 1 | Báo lỗi ở API public của core | Không exception. Core dùng `Error` và `Expected<T>`, McDevice chuyển thành signal. |
| 2 | Random access 0403/1402 | v1.1. |
| 3 | Ngôn ngữ tài liệu commit trong repo | Tiếng Anh. |
| 4 | Tên engine | `mc::Session`. `McProtocol` là codec facade. |
| 5 | Chuẩn C++ | C++17. Không consumer nào kẹt C++14. |
| 6 | Heartbeat "comm active M device" | Tính năng tùy chọn trong `Session`, mặc định tắt. |
| 7 | Sau timeout TCP với 3E/1E | `Session` chỉ báo `LinkFault`. Reconnect do app quyết, thư viện không tự reconnect. |

## Quyết định bổ sung (phỏng vấn 2026-09-25, cho `core-session`, `mock-plc`, `qt-device`)

| # | Câu hỏi | Quyết định |
|---|---|---|
| 8 | "Vùng memory" của snapshot | Mỗi **loại device** (M, X, Y, D, …). Snapshot của một loại chứa mọi điểm đã đăng ký của loại đó. |
| 9 | Công bố giá trị | Snapshot mỗi loại ở cuối mỗi vòng. Vòng 1 sau **mỗi** `linkUp`: không phát value changed, mọi snapshot hoãn tới khi xong vòng 1. Từ vòng 2: value changed phát theo từng response có giá trị đổi. Điểm đọc lần đầu (vùng mới, vùng từng lỗi) chỉ lập mốc, không phát. Vùng lỗi là invalid, không zero-fill, không chặn vùng khác. |
| 10 | Subscribe khi đang polling | Động; áp dụng ở ranh giới vòng kế. |
| 11 | Mock PLC | Lai: dùng chung primitive của core (bảng device, hexascii, sum check, field codec); chiều server tự viết từ spec, kiểm bằng golden vector theo chiều ngược. |

Chi tiết nằm ở `docs/spec/SPEC-core-session.md`, `SPEC-mock-plc.md`, `SPEC-qt-device.md`.

## Quyết định bổ sung (phỏng vấn 2026-09-26, test với PLC thật)

| # | Câu hỏi | Quyết định |
|---|---|---|
| 12 | Test với data thật | Thêm module `hil-capture`: sau khi McProtocol và McDevice xong, chạy danh mục lệnh theo từng frame trên PLC thật (Q, iQ-F FX5, F FX3; đủ 1E, 3E, 1C, 3C, ASCII và Binary), lưu request/response thành `.vec` để replay không cần phần cứng. Không test iQ-R, L, A series trên máy thật. |
| 13 | An toàn | PLC trên bàn test; chủ dự án chỉ định vùng nháp; tool từ chối chạy nếu có lệnh ghi ra ngoài vùng nháp. |
| 14 | PLC thật khác spec | Ghi vào `docs/hil/FINDINGS.md`, chủ dự án duyệt từng trường hợp; golden vector từ manual không sửa. |
| 15 | Benchmark thời gian | Đo time-to-first-byte, receive time, round-trip time cho từng loại request; chỉ để tham khảo, không pass/fail. |

Chi tiết: `docs/spec/SPEC-hil-capture.md`, `docs/hil/COMMAND-CATALOGUE.md`.

## Ngoài phạm vi v1

- Ba widget Qt (device batch monitor, transport config, frame setting).
- Frame 4E, 4C.
- UDP.
- Build sẵn thành thư viện cài đặt được (`find_package`), cam kết ABI.
