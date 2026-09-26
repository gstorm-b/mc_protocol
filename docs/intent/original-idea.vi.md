> **Bản gốc lưu trữ, không phải spec.** Ý tưởng thô ban đầu, viết trước buổi phỏng vấn ngày 2026-09-25, giữ lại để truy nguồn. Nội dung đã được thay bằng `docs/intent/mc_protocol_library.md`, `docs/ideas/mc_protocol_library.md` và các spec trong `docs/spec/`; khi khác nhau, các tài liệu đó là bản đúng.
>
> Riêng ví dụ "read words trên device M trả về 1 byte tương đương 8 devices M" ở dưới đã bị thay bởi payload contract trong `docs/spec/SPEC-core-protocol.md`: đọc word luôn trả 2 byte cho mỗi word (16 điểm với bit device).

Mục tiêu:
    - Thiết kế một thư viện có thể tái sử dụng cho đa dạng project.
    - Mục tiêu thư viện tạo ra hai class chính:
        - McProtocol: xử lí thông tin truyền thông MC Protocol viết thuần thư viện tiêu chuẩn của C++.
        - McDevice: hỗ trợ transport, cùng với McProtocol cho phép truyền 
        thông với các PLC của mitbusishi dùng MC protocol. Sử dụng QT 
        framework, hỗ trợ version 6 trở lên. Hỗ trợ build với cmake lẫn qmake.
Liên quan codebase:
    - Sử dụng namespace **mc**.
    - Thiết kế được api của McProtocol và McDevice để khi có update thư viện 
    thì người dùng không tốn quá nhiều công sức, hoặc không cần thay đổi 
    codebase của họ.
    - Các frame sẽ hỗ trợ là 1E, 3E, 1C, 3C, có thể sẽ cân nhắc update thêm 4E và 4C ở các phiên bản tiếp theo.
    - Transport interface hỗ trợ: Serial COM Port, Ethernet TCP/IP.
    - McProtocol là đối tượng xử lí logic protocol chỉ dùng thuần thư viện tiêu chuẩn vì một số trường hợp người dùng sẽ muốn tự viết một class Mc device của riêng họ hoặc không dùng qt framework.
    - McDevice dùng transport của Qt framework.
    - Có các class quản lí memory, thuần thư viện tiêu chuẩn. Ví dụ như store memory device,
    optimize polling query request list.
    - McProtocol trả về dữ liệu plc memory device sẽ được trả về qua một vector byte, các tổ chức sẽ phụ thuộc vào dạng command request tới plc, chứ không phụ thuộc vào dạng device type trong request. Command liên quan tới bit thì mỗi byte trả về đại diện cho 1 bit, có arg optional cho phép trả về dạng 1 byte chứa 8 bit devices. Command liên quan đến word thì 2 bytes đại diện cho 1 word. Ví dụ nếu yêu cầu read words nhưng đối tượng device type là M thì trả về 1 bytes tương đương 8 devices M.
    - Có tests chặt chẽ cho từng dạng frame.
    - Có tests với data thực tế thu được cho các dạng frame. 
- Các widget qt mở rộng của thư viện:
    - Device batch widget, hỗ trợ monnitor, debug modify giá trị của các 
    device (device ở đây là các vùng nhớ X, Y, M,... trên PLC).
    - Transport interface configurtion widget, hỗ trợ setting, config các 
    params của tầng transport để nhúng vào project nhanh chóng.
    - MC Frame setting widget, hỗ trợ setting, config các params của
    frame, nhúng vào project nhanh chóng.
