# PART 2 MESSAGE FORMATS

This part explains the message format of MC protocol.

- 4 MESSAGES OF SERIAL COMMUNICATION MODULE
- 5 MESSAGES OF Ethernet INTERFACE MODULE
- 6 ACCESS ROUTE SETTINGS

---

## Reading Notes

- **Source:** Mc-protocol.pdf, Part 2 (PDF pages 29–60, printed pages 27–58). The text was compared page by page with the PDF (page images and text layer) in two passes.
- **Page references:** "Page N Title" cross-references keep the page numbers printed in the PDF (PDF page index = printed page number + 2).
- **Diagrams:** message-format diagrams and examples are transcribed as text, with fields in the order they appear in the PDF. The system-configuration and topology figures in 6.2 (PDF pages 52–58) are not reproduced; only the accessible-range figures of 6.1 have one-line descriptions.
- **Notes:** a `> **Note:**` block marks a place where the PDF itself has an evident misprint or is internally inconsistent.

---

## 4 MESSAGES OF SERIAL COMMUNICATION MODULE

This section explains the specifications of the messages of MC protocol and access range when connecting with serial communication from an external device.

### 4.1 Types and Purposes of Messages

The messages of MC protocol can be classified as shown in the following table depending on the supported device and its intended purpose.

**Formats and codes**

There are five formats for the message that can be used for serial communication module.

| Setting value | Format | Code of communication data | Remarks | Reference |
|---|---|---|---|---|
| 1 | Format 1 | ASCII code | — | Page 29 Format 1 |
| 2 | Format 2 | ASCII code | Format with block number appended | Page 30 Format 2 |
| 3 | Format 3 | ASCII code | Format enclosed with STX and ETX | Page 31 Format 3 |
| 4 | Format 4 | ASCII code | Format with CR and LF appended at the end | Page 32 Format 4 |
| 5 | Format 5 | Binary code | Can be used by 4C frame. | Page 33 Format 5 |

Set the format with the communication protocol setting of Engineering tool.

> Communication using binary code shorten the communication time since the amount of communication data is reduced by approximately half as compared to the one using ASCII code.

**Frame**

This section explains the types and purposes of the frames (data communication messages) used by the external device to access the supported devices using MC protocol.

The frames for serial communication modules are as follows:

| Frame | Features and purposes | Compatible message format | Format |
|---|---|---|---|
| 4C frame | Accessible from external devices with the maximum access range. | Dedicated protocols for MELSEC-QnA series serial communication modules (QnA extension frame). | Formats 1 to 5 |
| 3C frame | These message formats are simplified compared to the 4C frame. Data communication software for MELSEC-QnA series programmable controllers can be used. | Dedicated protocols for MELSEC-QnA series serial communication modules (QnA frame). | Formats 1 to 4 |
| 2C frame | These message formats are simplified compared to the 4C frame. Data communication software for MELSEC-QnA series programmable controllers can be used. | Dedicated protocols for MELSEC-QnA series serial communication modules (QnA simplified frame). | Formats 1 to 4 |
| 1C frame | These frames have the same message structures as when accessing the CPU module using an MELSEC-A series computer link module. Data communication software for MELSEC-A series programmable controllers can be used. | Dedicated protocols for MELSEC-A series computer link modules | Formats 1 to 4 |

### 4.2 Message Formats of Each Protocol

This section explains the message format and setting data per each format.

#### Format 1

**Message format**

- ■Request message:
  `ENQ(05H) [Control code] → Frame ID No. → Access route → Request data → Sum check code`
  *(Sum check range: Frame ID No. to Request data)*
- ■Response message (Normal completion: Response data):
  `STX(02H) [Control code] → Frame ID No. → Access route → Response data → ETX(03H) [Control code] → Sum check code`
  *(Sum check range: Frame ID No. to ETX)*
- ■Response message (Normal completion: No response data):
  `ACK(06H) [Control code] → Frame ID No. → Access route`
- ■Response message (Abnormal completion):
  `NAK(15H) [Control code] → Frame ID No. → Access route → Error code`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Control code (ENQ, STX, ACK, NAK, ETX) | A code is defined for control. | Page 34 Control code |
| Frame ID No. | Specify the frame to be used. | Page 36 Frame ID No. |
| Access route | Specify the access route. | Page 45 ACCESS ROUTE SETTINGS |
| Request data | Set the command that indicates the request content. Refer to "Request data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Response data | Store the read data for the command. Refer to "Response data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Sum check code | The value of the lower one byte (8 bits) of the additional result regarding the data in the sum check target range as a binary data. | Page 36 Sum check code |
| Error code | Error code indicates the content of occurred error. | Page 38 Error code |

#### Format 2

**Message format**

- ■Request message:
  `ENQ(05H) [Control code] → Block No. → Frame ID No. → Access route → Request data → Sum check code`
  *(Sum check range: Block No. to Request data)*
- ■Response message (Normal completion: Response data):
  `STX(02H) [Control code] → Block No. → Frame ID No. → Access route → Response data → ETX(03H) [Control code] → Sum check code`
  *(Sum check range: Block No. to ETX)*
- ■Response message (Normal completion: No response data):
  `ACK(06H) [Control code] → Block No. → Frame ID No. → Access route`
- ■Response message (Abnormal completion):
  `NAK(15H) [Control code] → Block No. → Frame ID No. → Access route → Error code`

> **Note:** In the PDF diagram of the Format 2 "Response message (Normal completion: No response data)", the control code is printed as "ACX" (06H); this is an evident typo for ACK (06H).

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Control code (ENQ, STX, ACK, NAK, ETX) | A code is defined for control. | Page 34 Control code |
| Block number | This can set arbitrarily in the range of '00H' to 'FFH'. It is used for data defragmentation. | Page 36 Block number |
| Frame ID No. | Specify the frame to be used. | Page 36 Frame ID No. |
| Access route | Specify the access route. | Page 45 ACCESS ROUTE SETTINGS |
| Request data | Set the command that indicates the request content. Refer to "Request data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Response data | Store the read data for the command. Refer to "Response data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Sum check code | The value of the lower one byte (8 bits) of the additional result regarding the data in the sum check target range as a binary data. | Page 36 Sum check code |
| Error code | Error code indicates the content of occurred error. | Page 38 Error code |

#### Format 3

**Message format**

- ■Request message:
  `STX(02H) [Control code] → Frame ID No. → Access route → Request data → ETX(03H) [Control code] → Sum check code`
  *(Sum check range: Frame ID No. to ETX)*
- ■Response message (Normal completion: Response data):
  `STX(02H) [Control code] → Frame ID No. → Access route → End code → Response data → ETX(03H) [Control code] → Sum check code`
  *(Sum check range: Frame ID No. to ETX)*
- ■Response message (Normal completion: No response data):
  `STX(02H) [Control code] → Frame ID No. → Access route → End code → ETX(03H) [Control code]`
- ■Response message (Abnormal completion):
  `STX(02H) [Control code] → Frame ID No. → Access route → End code → Error code → ETX(03H) [Control code]`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Control code (STX, ETX) | A code is defined for control. | Page 34 Control code |
| Frame ID No. | Specify the frame to be used. | Page 36 Frame ID No. |
| Access route | Specify the access route. | Page 45 ACCESS ROUTE SETTINGS |
| Request data | Set the command that indicates the request content. Refer to "Request data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Response data | Store the read data for the command. Refer to "Response data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Sum check code | The value of the lower one byte (8 bits) of the additional result regarding the data in the sum check target range as a binary data. | Page 36 Sum check code |
| End code | Indicates that the processing result is a normal completion or abnormal completion. • 4C/3C/2C frame: QACK (normal), QNAK (abnormal) • 1C frame: GG (normal completion), NN (abnormal completion) | Page 38 End code |
| Error code | Error code indicates the content of occurred error. | Page 38 Error code |

#### Format 4

**Message format**

- ■Request message:
  `ENQ(05H) [Control code] → Frame ID No. → Access route → Request data → Sum check code → CR(0DH) → LF(0AH) [Control code]`
  *(Sum check range: Frame ID No. to Request data)*
- ■Response message (Normal completion: Response data):
  `STX(02H) [Control code] → Frame ID No. → Access route → Response data → ETX(03H) [Control code] → Sum check code → CR(0DH) → LF(0AH) [Control code]`
  *(Sum check range: Frame ID No. to ETX)*
- ■Response message (Normal completion: No response data):
  `ACK(06H) [Control code] → Frame ID No. → Access route → CR(0DH) → LF(0AH) [Control code]`
- ■Response message (Abnormal completion):
  `NAK(15H) [Control code] → Frame ID No. → Access route → Error code → CR(0DH) → LF(0AH) [Control code]`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Control code (ENQ, STX, ACK, NAK, ETX, CR, LF) | A code is defined for control. | Page 34 Control code |
| Frame ID No. | Specify the frame to be used. | Page 36 Frame ID No. |
| Access route | Specify the access route. | Page 45 ACCESS ROUTE SETTINGS |
| Request data | Set the command that indicates the request content. Refer to "Request data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Response data | Store the read data for the command. Refer to "Response data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Sum check code | The value of the lower one byte (8 bits) of the additional result regarding the data in the sum check target range as a binary data. | Page 36 Sum check code |
| Error code | Error code indicates the content of occurred error. | Page 38 Error code |

#### Format 5

**Message format**

- ■Request message:
  `DLE(10H)+STX(02H) [Control code] → Number of data bytes → Frame ID No.(F8H) → Access route → Request data → DLE(10H)+ETX(03H) [Control code] → Sum check code`
  *(Specify the number of bytes in this range: Frame ID No. to Request data. Sum check range: Number of data bytes to Request data.)*
- ■Response message (Normal completion: Response data):
  `DLE(10H)+STX(02H) [Control code] → Number of data bytes → Frame ID No.(F8H) → Access route → Response ID code(FFFFH) → Normal completion code(0000H) → Response data → DLE(10H)+ETX(03H) [Control code] → Sum check code`
  *(Specify the number of bytes in this range: Frame ID No. to Response data. Sum check range: Number of data bytes to Response data.)*
- ■Response message (Normal completion: No response data):
  `DLE(10H)+STX(02H) [Control code] → Number of data bytes → Frame ID No.(F8H) → Access route → Response ID code(FFFFH) → Normal completion code(0000H) → DLE(10H)+ETX(03H) [Control code] → Sum check code`
  *(Specify the number of bytes in this range: Frame ID No. to Normal completion code. Sum check range: Number of data bytes to Normal completion code.)*
- ■Response message (Abnormal completion):
  `DLE(10H)+STX(02H) [Control code] → Number of data bytes → Frame ID No.(F8H) → Access route → Response ID code(FFFFH) → Error codes → DLE(10H)+ETX(03H) [Control code] → Sum check code`
  *(Specify the number of bytes in this range: Frame ID No. to Error codes. Sum check range: Number of data bytes to Error codes.)*

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Control code (DLE, STX, ETX) | A code is defined for control. | Page 34 Control code |
| Number of data bytes | A number of bytes from a frame ID No. to control code (DLE, ETX). | Page 35 Number of data bytes |
| Frame ID No. | Specify the frame to be used. | Page 36 Frame ID No. |
| Access route | Specify the access route. | Page 45 ACCESS ROUTE SETTINGS |
| Request data | Set the command that indicates the request content. Refer to "Request data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Response data | Store the read data for the command. Refer to "Response data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Sum check code | The value of the lower one byte (8 bits) of the additional result regarding the data in the sum check target range as a binary data. | Page 36 Sum check code |
| Response ID code | This indicates a response message. The 2-byte numerical value, 'FFFFH' is stored. | — |
| Normal completion code | This indicates the processing is completed normally. The 2-byte value, '0000H' is stored. | — |
| Error code | Error code indicates the content of occurred error. | Page 38 Error code |

### 4.3 Details of Setting Data

This section explains how to specify the common data items and their content in each message.

#### Control code

Control code is a data that has special meaning (such as head data of a message) for C24 transmission control.

**Control code used in a message (format 1 to format 4) in ASCII code**

The control code used for a message in ASCII code (format 1 to format 4) is shown in the following table.

| Symbol name | Description | Code (hexadecimal) |
|---|---|---|
| STX | Start of Text | 02H |
| ETX | End of Text | 03H |
| EOT | End of Transmission | 04H |
| ENQ | Enquiry | 05H |
| ACK | Acknowledge | 06H |
| LF | Line Feed | 0AH |
| CL | Clear | 0CH |
| CR | Carriage Return | 0DH |
| NAK | Negative Acknowledge | 15H |

**■EOT(04H), CL(0CH)**

EOT and CL are codes for initializing the transmission sequence for data communications in ASCII code using the MC protocol and for placing C24 into wait state to receive commands from an external device.

The transmission sequence is initialized with the command (command code: 1615) when binary code (format 5) is used.

When performing the following at an external device, send the EOT/CL to the C24 depending on the format used.

- Canceling a read/write request by command previously sent. (If a write request is issued, the write request cannot be canceled when the data has already written to the CPU module.)
- Placing C24 into the wait state to receive commands before commands are sent.
- Placing C24 into the state where it has been started up when data communication cannot be performed normally.

The message structure when sending EOT, CL is shown below.
Only the following data is sent. The station No. and PC No. are not required.

| Format | EOT | CL |
|---|---|---|
| Format 1 to format 3 | EOT(04H) | CL(0CH) |
| Format 4 | EOT(04H) + CR(0DH) + LF(0AH) | CL(0CH) + CR(0DH) + LF(0AH) |

When C24 receives EOT or CL, it proceeds as follows.

- C24 terminates any read/write processing performed to the CPU module upon request from the external device. In this case, C24 does not send a response message to the command previously received.
- C24 initializes the transmission sequence of the MC protocol from which the EOT/CL is received on the interface side and placing C24 into wait state to receive commands from an external device.
- C24 does not send a response message to the EOT or CL reception. (It does not send anything to external devices.)
- When it receives EOT or CL while the on-demand function (data transmission function from the CPU module to external devices) is being performed, C24 terminates to transmit the on-demand data to external devices. (Page 279 On-demand function)

**Control code used in a message (format 5) in binary code**

The control code used for a message in binary code (format 5) is shown in the table below.

| Symbol name | Description | Code (hexadecimal) |
|---|---|---|
| STX | Start of Text | 02H |
| ETX | End of Text | 03H |
| DLE | Data Link Escape | 10H |

**■Additional code (10H)**

The additional code is added to distinguish the data when the control code DLE (10H) is the same as the setting data in the frame 5.

When '10H' is included in the data from "Number of data bytes" and "Request data" in the request message, the additional code '10H' is added in front of the data.

When '10H' is included in the data from "Number of data bytes" and "Response data" in the response message, the additional code '10H' is added.

('10H' is transmitted as '10H' + '10H'.)

> Calculate the following value except for the additional code.
> - Number of data bytes (setting item of format 5)
> - Sum check code

#### Number of data bytes

A number of data bytes indicates the total number of bytes from the frame ID No. to control code.

**Range**

Calculate the data in the range from frame ID No. before DLE (10H) except for the additional code. (Page 35 Additional code (10H))

`DLE(10H)+STX(02H) [Control code] → Number of data bytes → Frame ID No.(F8H) → ... → DLE(10H)+ETX(03H) [Control code] → Sum check code`
*(Specify the number of bytes in this range: Frame ID No. to the part shown as "...", i.e. up to just before DLE(10H)+ETX(03H).)*

**Setting method**

Set the data in binary code (format 5) at data communication.
Send 2-byte numerical value from the lower byte (L: bits 0 to 7).

**Ex.**

Response message (Normal completion: Response data)

- Frame ID No.: 1 byte
- Access route: 7 byte
- Response ID code, normal completion code: 4 bytes
- Response data: 2 bytes + additional code (10H) 1 byte

Number of data bytes = 1 + 7 + 4 + 2 = 14 (0EH)

Example byte sequence: `DLE(10H) STX(02H) 0E00H [Number of data bytes] F8H [Frame ID No.] [Access route, 7 bytes] FFFFH [Response ID code] 0000H [Normal completion code] 001010H [Response data] DLE(10H) ETX(03H) [Sum check code]`

*(Specify the number of bytes in this range: Frame ID No. to Response data.)*

#### Block number

Block number is an arbitrary number defined by an external device and used for data defragmentation.

Block number converts data to 2-digit (hexadecimal) ASCII code within the range of '00H' to 'FFH' and sent them from the upper digits.

C24 only checks if the block number is specified within the correct range. It does not check whether the block numbers are sent in order.

#### Frame ID No.

Specify the frame to be used.

| Type | Setting value |
|---|---|
| 4C frame | F8 |
| 3C frame | F9 |
| 2C frame | FB |
| 1C frame | — (Not required) |

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value.

**Ex.** For 4C frame (F8)

| | ASCII code | Binary code |
|---|---|---|
| F8 | 46H 38H | F8H |

#### Sum check code

Set the sum check code when performing sum check.

For sum check code, set the value to be calculated from the data with the range of sum check for error detection.

**Sum check**

Sum check is a function for detecting error when data changes while data transmission.
Set the sum check existence by Engineering tool.

- ■When sum check code is set to "Exist": Attach a sum check code to the request message. C24 checks the sum check code. The sum check code is added to the response message.
- ■When sum check code is set to "None": The sum check code is not required for the request message. C24 does not check the sum check code. The sum check code is not added to the response message.

#### Sum check range

The sum check range of each message format is as follows:

| Format | Message structure | Reference |
|---|---|---|
| Format 1 to format 3 | `Control code` → `- - -` → `Sum check code` (Sum check range: the part `- - -` between Control code and Sum check code) | Page 29 Format 1 / Page 30 Format 2 / Page 31 Format 3 |
| Format 4 | `Control code` → `- - -` → `Sum check code` → `Control code (CR 0DH, LF 0AH)` (Sum check range: the part `- - -` between the first Control code and Sum check code) | Page 32 Format 4 |
| Format 5 | `Control code` → `- - -` → `Control code (DLE 10H, ETX 03H)` → `Sum check code` (Sum check range: the part `- - -` between the first Control code and Control code (DLE, ETX)) | Page 33 Format 5 |

**Calculation of a sum check code**

For sum check code, set the numerical values of the lower 1 byte (8 bits) of the added result (sum) as binary data within the sum check range.

Calculate sum check code except for the additional code. (Page 35 Additional code (10H))

**Ex.**

In the following case of 1C frame format 1, the sum check code will be 'C0'.

- Formula: 30H + 30H + 46H + 46H + 42H + 52H + 33H + 4DH + 30H + 30H + 30H + 30H = 2C0H
- Sum check code: 'C0' (ASCII code 43H, 30H)

Message example: `ENQ(05H) | Station No. '0' '0' (30H 30H) | PC No. 'F' 'F' (46H 46H) | Command 'B' 'R' (42H 52H) | Message wait '3' (33H) | Character area 'M' '0' '0' '0' '0' (4DH 30H 30H 30H 30H) | Sum check code 'C' '0' (43H 30H)`

*(Sum check range: Station No. to Character area.)*

In the following case of 4C frame format 5, the sum check code will be '05'.

- Formula: 12H + 00H + F8H + 05H + 07H + 03H + 04H + 00H + 01H + 00H + 01H + 04H + 01H + 00H + 40H + 00H + 00H + 9CH + 05H + 00H = 205H
- Sum check code: '05' (ASCII code 30H, 35H)

Byte sequence example: `DLE(10H) STX(02H) 12H 00H F8H 05H 07H 03H 04H 00H 01H 00H 01H 04H 01H 00H 40H 00H 00H 9CH 05H 00H DLE(10H) ETX(03H) 30H 35H`

Field mapping: `DLE(10H) | STX(02H) | Number of data bytes: 12H(L) 00H(H) | Frame ID No.: F8H | Station No.: 05H | Network No.: 07H | PC No.: 03H | Request destination module I/O No.: 04H(L) 00H(H) | Request destination module Station No.: 01H | Self-station No.: 00H | Command: 01H(L) 04H(H) | Subcommand: 01H(L) 00H(H) | Head device: 40H(L) 00H 00H(H) | Device code: 9CH | Number of device points: 05H(L) 00H(H) | DLE(10H) | ETX(03H) | Sum check code: 30H(H) 35H(L)`

*(Sum check range (excluding additional codes): Number of data bytes to Number of device points, i.e. 12H 00H ... 05H 00H.)*

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: The same as the data communication in ASCII code, use the numerical value converted to the 2 digit ASCII code (hexadecimal). Send 2-byte numerical value from the upper byte (H: bits 8 to 15).

**Ex.** Sum check code: '05' (ASCII code 30H, 35H)

| | ASCII code, binary code |
|---|---|
| 05 | 30H 35H |

#### End code

Indicates that the processing result is a normal completion or abnormal completion.

The following fixed value is stored.

| Processing result | 4C frame, 3C frame, 2C frame | 1C frame |
|---|---|---|
| Normal completion | QACK (ASCII: Q 51H, A 41H, C 43H, K 48H) | GG (ASCII: G 47H, G 47H) |
| Abnormal completion | QNAK (ASCII: Q 51H, N 4EH, A 41H, K 48H) | NN (ASCII: N 4EH, N 4EH) |

#### Error code

Error code indicates the content of occurred error.

If more than one error occurs at the same time, the error code detected first is returned.

For the content of error code and its corrective action, refer to the user's manual of the module used.

- Q Corresponding Serial Communication Module User's Manual (Basic)
- MELSEC-L Serial Communication Module User's Manual (Basic)
- MELSEC iQ-R Serial Communication Module User's Manual(Application)

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical value from the lower byte (L: bits 0 to 7).

**Ex.** When error code 7151H is returned: `7 1 5 1` → ASCII `37H 31H 35H 31H`; Binary `51H 71H`

For the error code of 1C frame, refer to the following section.

Page 348 Error code

---

## 5 MESSAGES OF Ethernet INTERFACE MODULE

This section explains the specifications of the messages and access range of MC protocol when connecting with Ethernet communication from an external device.

### 5.1 Types and Purposes of Messages

The messages of MC protocol can be classified as shown in the following table depending on the supported device and its intended purpose.

**Code**

ASCII code and binary code are available.
Set the operation settings with Engineering tool.

> Communication using binary code shorten the communication time since the amount of communication data is reduced by approximately half as compared to the one using ASCII code.

**Data storage order**

The data size and the storing order of values for data in each item vary between ASCII code and binary code.

- ■Data communication in ASCII code: Data is stored in order from the upper byte to the lower byte.
- ■Data communication in binary code: Data is stored in order from the lower byte to the upper byte.

**Ex.** Subheader of 4E frame request message (serial No. is '1234')

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | 5400 → 35H 34H 30H 30H | 54H 00H |
| Serial number | 1234 → 31H 32H 33H 34H | 34H 12H |
| Fixed value | 0000 → 30H 30H 30H 30H | 00H 00H |

**Frame**

This section explains the types and purposes of the frames (data communication messages) used by the external device to access the supported devices using MC protocol.

The frames for Ethernet interface modules are as follows:

| Frame | Features and purposes | Compatible message format | Correspondence code |
|---|---|---|---|
| 4E frame | A message format that a "Serial No." (arbitrary number for message identification) is added to 3E frame. By appending a "Serial No.", the send source can be identified when multiple request messages have been sent. | Message formats for SLMP | ASCII code, Binary code |
| 3E frame | These frames have the same message structures as when accessing the CPU module using MELSEC-QnA series Ethernet interface module. Data communication software for MELSEC-QnA series programmable controllers can be used. | Message formats for SLMP / Message formats for MELSEC-QnA series Ethernet interface modules | ASCII code, Binary code |
| 1E frame | These frames have the same message structures as when accessing the CPU module using an MELSEC-A series Ethernet interface module. Data communication software for MELSEC-A series programmable controllers can be used. | Message formats for MELSEC-A series Ethernet interface modules | ASCII code, Binary code |

### 5.2 Message Format

This section explains the message format and setting data for 4E frame and 3E frames.
For the message format for 1E frame, refer to the following section.

Page 391 Message Format

**Message format**

- ■Request message:
  `Header → Subheader → Access route → Request data length → Monitoring timer → Request data`
  *(Specify the number of bytes from the Monitoring timer to the Request data in "Request data length".)*
- ■Response message (Normal completion: Response data):
  `Header → Subheader → Access route → Response data length → End code → Response data`
  *(Specify the number of bytes from the End code to the Response data in "Response data length".)*
- ■Response message (Normal completion: No response data):
  `Header → Subheader → Access route → Response data length → End code`
  *(Response data length specifies the number of bytes of the end code.)*
- ■Response message (Abnormal completion):
  `Header → Subheader → Access route → Response data length → End code → Error information`
  *(Specify the number of bytes from the End code to the Error information in "Response data length".)*

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Header | A header of Ethernet. Normally, it is added automatically. | Page 42 Header |
| Subheader | The value to be set according to type of message is defined. • 4E frame: Set a serial No. • 3E frame: Fixed value (Request message '5000', Response message 'D000') | Page 42 Subheader |
| Access route | Specify the access route. | Page 45 ACCESS ROUTE SETTINGS |
| Request data length | Specify the data length from the monitoring timer to the request data. | Page 43 Request data length and response data length |
| Monitoring timer | Set the wait time up to the completion of reading and writing processing. | Page 43 Monitoring timer |
| Request data | For the request data, set the command that indicates the request content. Refer to "Request data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| Response data length | The data length from an end code to a response data (at normal completion) or an error information (at abnormal completion) is stored. | Page 43 Request data length and response data length |
| Response data | For the response data, store the read data for the command at normal completion. Refer to "Response data" rows of each command. | Page 60 COMMANDS AND FUNCTIONS |
| End code | The command processing result is stored. | Page 44 End code |
| Error information | Store the information of a station on which an error occurred and information of a command. | Page 44 Error information |

### 5.3 Details of Setting Data

This section explains how to specify the common data items and their content in each message.
For the setting data with 1E frame, refer to the following section.
Page 391 Details of Setting Data

#### Header

A header for TCP/IP and UDP/IP. A header of a request message is added on the external device side and sent. Normally, it is added automatically by an external device. A header for a response message is set automatically by E71.

#### Subheader

The value to be set according to type of message is defined.

**Setting method for 4E frame**

Set the fixed value (request message: '5400', response message: 'D400') and a serial No. (0000H to FFFFH).

A serial No. is an arbitrary number that is added on the external device side for message recognition. When a request message is sent with a serial No. added, the same serial No. is added to the response message. Use a serial No. when transmitting more than one request messages from an external device to the same supported device.

> Serial No. added on the external device side must be managed at the external device side.

- ■Data communication in ASCII code: It's 12 bytes in total. A fixed value is set by 4-digit ASCII code. For the serial No., convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits. The 4-byte '0' (30H) is inserted after the serial No.
- ■Data communication in binary code: It's 6 bytes in total. A fixed value is set in 2 bytes. For the serial No., send 2-byte numerical value from the lower byte (L: bits 0 to 7). The 2-byte '0' (00H) is inserted after the serial No.

**Ex.** Request message (serial No. '1234')

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | 5400 → 35H 34H 30H 30H | 54H 00H |
| Serial number | 1234 → 31H 32H 33H 34H | 34H 12H |
| Free | 0000 → 30H 30H 30H 30H | 00H 00H |

**Setting method for 3E frame**

Set the fixed value (request message: '5000', response message: 'D000').

**Request message**

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | 5000 → 35H 30H 30H 30H | 50H 00H |

**Response message**

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | D000 → 44H 30H 30H 30H | D0H 00H |

#### Request data length and response data length

For the request data length, specify the data length from the monitoring timer to the request data.

For the response data length, the data length from an end code to a response data (at normal completion) or an error information (at abnormal completion) is stored.

**Setting method**

Specify the data length in hexadecimal. (Unit: byte)

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical value from the lower byte (L: bits 0 to 7).

**Ex.** The data length is 24 bytes: `0018H` → ASCII `30H 30H 31H 38H`; Binary `18H 00H`

#### Monitoring timer

Set the wait time up to the completion of reading and writing processing.

Set the wait time from when E71 on the connection station requests processing to the access target to when the response is returned.

- 0000H (0): Wait infinitely (Waits until a processing is completed.)
- 0001H to FFFFH (1 to 65535): Waiting time (unit: 250 ms)

To perform normal data communication, using the timer within the setting range in the table below is recommended depending on the communication destination.

| Access target | Monitoring timer |
|---|---|
| Connected station (host station) | 0001H to 0028H (0.25 s to 10 s) |
| Other station | 0002H to 00F0H (0.5s to 60s) |

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical value from the lower byte (L: bits 0 to 7).

**Ex.** When specifying 10H (16 × 250 ms = 4 seconds) for the monitoring timer: `0010H` → ASCII `30H 30H 31H 30H`; Binary `10H 00H`

#### End code

The command processing result is stored.

At normal completion, '0' is stored.
At abnormal completion, an error code of the access target is stored.

Error code indicates the content of occurred error.
If more than one error occurs at the same time, the error code detected first is returned.

For the content of error code and its corrective action, refer to the user's manual of the module used.

- QCPU User's Manual (Hardware Design, Maintenance and Inspection)
- MELSEC-L CPU Module User's Manual (Hardware Design, Maintenance and Inspection)
- Q Corresponding Ethernet Interface Module User's Manual (Basic)
- MELSEC-L Ethernet Interface Module User's Manual (Basic)
- MELSEC iQ-R Serial Communication Module User's Manual(Application)

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical value from the lower byte (L: bits 0 to 7).

**Ex.**

- Normal completion: `0000H` → ASCII `30H 30H 30H 30H`; Binary `00H 00H`
- Error code C051H: `C051` → ASCII `43H 30H 35H 31H`; Binary `51H C0H`

#### Error information

Store the information of a station on which an error occurred and information of a command.

- Access route: The information of a station which sent an error response is stored. It may differ from the contents of a request message.
- Command, subcommand: The command and the subcommand when an error occurred are stored.

Field order: `Access route → Command → Subcommand`

---

## 6 ACCESS ROUTE SETTINGS

This chapter explains the accessible range of each frame of MC protocol and data to specify the access target.

### 6.1 Accessible Ranges and Setting Data for Each Frame

The accessible range of each frame and the data items to set an access route are as shown below.

#### 4C frame

**Accessible range of 4C frame**

The following ranges can be accessed. (External device → Connected station (Host station) → Multidrop connection → Network No.1 → (Relay station) → Network No.n → Multidrop connection → Accessible target station.)

4C frame is supported by multiple CPU system. (Page 462 Compatibility with Multiple CPU Systems)

**Message format** (Setting example for accessing connected station (host station))

- ■Data communication in ASCII code (Format 1 to Format 4):
  `Station No.(00) → Network No.(00) → PC No.(FF) → Request destination module I/O No.(03FF) → Request destination module station No.(00) → Self-station No.(00)`
  ASCII: `30H 30H | 30H 30H | 46H 46H | 30H 33H 46H 46H | 30H 30H | 30H 30H`
- ■Data communication in binary code (Format 5):
  `00H | 00H | FFH | FFH 03H | 00H | 00H`
  (Station No. → Network No. → PC No. → Request destination module I/O No. → Request destination module station No. → Self-station No.)

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Station No. | Specify the station to be connected from an external device. | Page 50 Station No. |
| Network No. | Specify the network No. of an access target. | Page 52 Network No., PC No. |
| PC No. | Specify the network module station No. of an access target. | Page 52 Network No., PC No. |
| Request destination module I/O No. | • When accessing a multidrop connection station via network, specify the start input/output number of a multidrop connection source module. • Specify the CPU module of the multiple CPU system and redundant system. | Page 55 Request destination module I/O No., request destination module station No. |
| Request destination module station No. | When accessing a multidrop connection station via network, specify the station No. of an access target module. | Page 55 Request destination module I/O No., request destination module station No. |
| Self-station No. | At the time of m:n multidrop connection, specify the station No. of a request source external device. | Page 58 Self-station No. |

#### 3C frame

**Accessible range of 3C frame**

The following ranges can be accessed. (External device → Connected station (Host station) → Multidrop connection → Network No.1 → (Relay station) → Network No.n → Accessible target station.)

**Message format** (Setting example for accessing connected station (host station))

`Station No.(00) → Network No.(00) → PC No.(FF) → Self-station No.(00)` — ASCII: `30H 30H | 30H 30H | 46H 46H | 30H 30H`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Station No. | Specify the station to be connected from an external device. | Page 50 Station No. |
| Network No. | Specify the network No. of an access target. | Page 52 Network No., PC No. |
| PC No. | Specify the network module station No. of an access target. | Page 52 Network No., PC No. |
| Self-station No. | At the time of m:n multidrop connection, specify the station No. of a request source external device. | Page 58 Self-station No. |

#### 2C frame

**Accessible range of 2C frame**

The following ranges can be accessed. (External device → Connected station (Host station) → Multidrop connection → Accessible target station.)

**Message format** (Setting example for accessing connected station (host station))

`Station No.(00) → Self-station No.(00)` — ASCII: `30H 30H | 30H 30H`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Station No. | Specify the station to be connected from an external device. | Page 50 Station No. |
| Self-station No. | At the time of m:n multidrop connection, specify the station No. of a request source external device. | Page 58 Self-station No. |

#### 1C frame

**Accessible range of 1C frame**

The following ranges can be accessed. (External device → Connected station (Host station) → Multidrop connection → Network → Accessible target station.)

When accessing a device, only the applicable device range for MELSEC-A series module can be accessed. (Page 352 Accessible device range)

**Message format** (Setting example for accessing connected station (host station))

`Station No.(00) → PC No.(FF)` — ASCII: `30H 30H | 46H 46H`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Station No. | Specify the station to be connected from an external device. | Page 50 Station No. |
| PC No. | Specify the network module station No. of an access target. | Page 52 Network No., PC No. |

#### 4E frame, 3E frame

**Accessible range of 4E frame, 3E frame**

The following ranges can be accessed. (External device → Connected station (Host station) → Network No.1 → (Relay station) → Network No.n → Multidrop connection → Accessible target station.)

4C frame is supported by multiple CPU system. (Page 462 Compatibility with Multiple CPU Systems)

> **Note:** The PDF prints "4C frame" in this sentence, although it appears in the 4E frame, 3E frame section.

**Message format** (Setting example for accessing connected station (host station))

- ■Data communication in ASCII code:
  `Network No.(00) → PC No.(FF) → Request destination module I/O No.(03FF) → Request destination module station No.(00)`
  ASCII: `30H 30H | 46H 46H | 30H 33H 46H 46H | 30H 30H`
- ■Data communication in binary code:
  `00H | FFH | FFH 03H | 00H`
  (Network No. → PC No. → Request destination module I/O No. → Request destination module station No.)

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Network No. | Specify the network No. of an access target. | Page 52 Network No., PC No. |
| PC No. | Specify the network module station No. of an access target. | Page 52 Network No., PC No. |
| Request destination module I/O No. | • When accessing a multidrop connection station, specify the start input/output number of a multidrop connection source module. • Specify the CPU module of the multiple CPU system and redundant system. | Page 55 Request destination module I/O No., request destination module station No. |
| Request destination module station No. | When accessing a multidrop connection station, specify the station No. of an access target module. | Page 55 Request destination module I/O No., request destination module station No. |

#### 1E Frame

**Accessible range of 1E frame**

The following ranges can be accessed. (External device → Connected station (Host station) → Network → Accessible target station.)

When accessing a device, only the applicable device range for MELSEC-A series module can be accessed. (Page 399 Accessible device range)

**Message format** (Setting example for accessing connected station (host station))

`PC No.(FF)` — ASCII: `46H 46H`; Binary: `FFH`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| PC No. | Specify the network module station No. of an access target. | Page 52 Network No., PC No. |

### 6.2 Details of Setting Data

This section explains the content and specification method of the data items to set the access route.

○: Necessary, —: Unnecessary

| Item | Frames for C24 | | | | Frames for E71 | | | Reference |
|---|---|---|---|---|---|---|---|---|
| | 4C | 3C | 2C | 1C | 4E | 3E | 1E | |
| Station No. | ○ | ○ | ○ | ○ | — | — | — | Page 50 Station No. |
| Network No. | ○ | ○ | — | — | ○ | ○ | — | Page 52 Network No., PC No. |
| PC No. | ○ | ○ | — | ○ | ○ | ○ | ○ | Page 52 Network No., PC No. |
| Request destination module I/O No. | ○ | — | — | — | ○ | ○ | — | Page 55 Request destination module I/O No., request destination module station No. |
| Request destination module station No. | ○ | — | — | — | ○ | ○ | — | Page 55 Request destination module I/O No., request destination module station No. |
| Self-station No. | ○ | ○ | ○ | — | — | — | — | Page 58 Self-station No. |

#### Station No.

Specify the station accessed from an external device.

**Accessing the connected station (host station)**

Specify '0' when accessing the connected station (host station).

**Accessing multidrop connection station**

Specify the station No. of an access target station from '0' to '31' (00H to 1FH) when connecting with the multidrop connection.

**■When accessing all stations connected with the multidrop connection with the global function**

Specify 'FF' (FFH) when turning ON/OFF the global signal to all station connected with the multidrop connection using the global function. By specifying 0 to 31 (00H to 1FH), X1A/X1B turns ON only on the specified station, and does not turn ON on the other stations. (Page 254 Global Function)

**Accessing other stations via network**

Specify the station No. from 0 to 31 (00H to 1FH) of a station that relays multidrop connection and network when accessing other stations via network.

Specify '0' when accessing other stations via network without the multidrop connection.

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value.

**Ex.**

- When the station No. setting for C24 to be accessed is '5': ASCII `30H 35H` (05); Binary `05H`
- When accessing all station connected with the multidrop connection using the global function: ASCII `46H 46H` (FF); Binary `FFH`

> The station No. of the serial communication module can be checked by using the following parameters of Engineering tool.
> - GX Works2: "Station Number Setting" in "Switch Setting"
> - GX Works3: "Station Number Settings" in "Module Parameter"

#### Network No., PC No.

Specify the network No. and station No. that are set with the parameters for the access target network module.

Specify a fixed value when accessing the connection station.

> Specify the network No. with the value shown below.
> Specifying improper value may result in no response returned.

**Accessing the connected station (host station)**

Specify '0' for the network No., and 'FF' for the PC No.

**■When using the on-demand function**

Specify '0' for the network No., and 'FE' for the PC No. (Page 279 On-demand function)

**Accessing multidrop connection station**

Specify '0' for the network No., and 'FF' for the PC No.

**Accessing other stations via network**

Specify the network No. and station No. of an access target.

| Access target | Network No. | PC No. |
|---|---|---|
| Other station of which station No. is set | 01H to EFH (1 to 239). Stations with network No.240 to 255 are not accessible. | 01H to 78H (1 to 120) |
| Specified control station/master station*1 | 01H to EFH (1 to 239). Stations with network No.240 to 255 are not accessible. | 7DH |
| Current control station/Master station*2 | 01H to EFH (1 to 239). Stations with network No.240 to 255 are not accessible. | 7EH |

*1 Access the station set as a control station/master station by parameters.
*2 Access the station which is operating as a control station/master station.

**■When accessing with the "Valid Module During Other Station Access" setting**

1C frame and 1E frame do not have the setting of network No.

When specifying the network of access target is required because more than one network module is mounted on the connection station, set the "Valid Module During Other Station Access" with Engineering tool.

When accessing in accordance with the setting of "Valid Module During Other Station Access" using a frame with the network No. set is desired, specify 'FEH' (254) to the network No.

When accessing other station via C24/E71 mounted on MELSECNET/H remote I/O station, the access to the other station specified with the PC No. of MELSECNET/H remote I/O station is available by specifying 'FEH' to the network No.

**Accessing multidrop connection station via network**

Specify the network No. and station No. of a station relaying the network routed through and multidrop connection station.

| Access target | Network No. | PC No. |
|---|---|---|
| Multidrop connection station via network | 01H to EFH (1 to 239). Stations with network No.240 to 255 are not accessible. | 01H to 78H (1 to 120) |

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value.

**Ex.**

- Accessing connected station (host station) or multidrop connection station: Network No. `00`, PC No. `FF` → ASCII `30H 30H 46H 46H`; Binary `00H FFH`
- When accessing other station of which network No. is '2' and station No. is '3': Network No. `02`, PC No. `03` → ASCII `30H 32H 30H 33H`; Binary `02H 03H`

> The network No. and station No. of the network module can be checked by using the following parameters of Engineering tool.
> - GX Works2: "Network Parameter"
> - GX Works3: "Module Parameter"
>
> The network No. and the station No. of the network module are normally set in decimal. However, the network No. and the PC No. are set in hexadecimal.

#### Request destination module I/O No., request destination module station No.

Specify these numbers when an access target is as shown below.

- Multidrop connection station
- CPU module on multiple CPU system
- CPU module on redundant system, CC-Link IE Field Network remote head module

Specify the fixed value when the access target is other than those listed above.

| Request destination module I/O No. | Request destination module station No. |
|---|---|
| 03FFH | 00H |

**Accessing multidrop connection station**

When connecting to an access target with a direct multidrop connection, it can be accessed by specifying a station No. (Page 50 Station No.).

For the request destination module I/O No. and the request destination module station No., specify the fixed value.

| Request destination module I/O No. | Request destination module station No. |
|---|---|
| 03FFH | 00H |

*Figure, labels: External device; Multidrop connection; legend (hatched circle): Access target station.*

**■For 4E frame and 3E frame**

When accessing a multidrop connection station with the frames (4E frame, 3E frame) for Ethernet interface module, specify the start input/output number of a multidrop connection source module (relay station) and the station No. of an access target module.

| Access target | Request destination module I/O No. | Request destination module station No. |
|---|---|---|
| MELSEC iQ-R series module | 0000H to 02FFH: Values obtained by dividing the start input/output number by 16 | 00H to 1FH (0 to 31): Station No. |
| MELSEC-Q/L series module | 0000H to 01FFH: Values obtained by dividing the start input/output number by 16 | 00H to 1FH (0 to 31): Station No. |

*Figure, labels: External device; Connected station (Host station); E71; C24; (Relay station); Multidrop connection; legend (gray box): Station specified to the request destination module I/O number; legend (hatched circle): Access target station (Station specified to the request destination module station No.).*

**Accessing multidrop connection station via network**

Specify the start input/output number of a multidrop connection source module (relay station) and the station No. of an access target.

| Access target | Request destination module I/O No. | Request destination module station No. |
|---|---|---|
| MELSEC iQ-R series module | 0000H to 02FFH: Values obtained by dividing the start input/output number by 16 | 00H to 1FH (0 to 31): Station No. |
| MELSEC-Q/L series module | 0000H to 01FFH: Values obtained by dividing the start input/output number by 16 | 00H to 1FH (0 to 31): Station No. |

*Figure, labels: External device; Connected station (Host station); Network No.n; (Relay station); Multidrop connection; legend (gray circle): Station specified to the request destination module I/O number; legend (hatched circle): Access target station (Station specified to the request destination module station No.).*

**Accessing multiple CPU system, redundant system**

Specify the access target with the request destination module I/O No. Specify the fixed value (00H) for the station No.

| Access target | | Request destination module I/O No. | Request destination module station No. |
|---|---|---|---|
| Multiple CPU system | Control CPU | 03FFH | 00H |
| Multiple CPU system | Non-control CPU — Multiple CPU No.1 | 03E0H | 00H |
| Multiple CPU system | Non-control CPU — Multiple CPU No.2 | 03E1H | 00H |
| Multiple CPU system | Non-control CPU — Multiple CPU No.3 | 03E2H | 00H |
| Multiple CPU system | Non-control CPU — Multiple CPU No.4 | 03E3H | 00H |
| Redundant system | CPU module — Control system*1 | 03D0H | 00H |
| Redundant system | CPU module — Standby system*1 | 03D1H | 00H |
| Redundant system | CPU module — System A | 03D2H | 00H |
| Redundant system | CPU module — System B | 03D3H | 00H |
| Redundant system | CC-Link IE Field Network remote head module — Remote head No.1 | 03E0H | 00H |
| Redundant system | CC-Link IE Field Network remote head module — Remote head No.2 | 03E1H | 00H |
| Redundant system | CC-Link IE Field Network remote head module — Control system*1 | 03D0H | 00H |
| Redundant system | CC-Link IE Field Network remote head module — Standby system*1 | 03D1H | 00H |

*1 When executing a command that manages files, specify the I/O number other than that of the control system (03D0H) and standby system (03D1H). Otherwise, the access target is changed and the files cannot be read/written.

**Setting method**

For the request destination module I/O No., specify the value obtained by dividing the start input/output number assigned to the module by 16 in 4 digits (hexadecimal).

- ■Data communication in ASCII code: For the request destination module I/O No., convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits. For the request destination module station No., convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: For the request destination module I/O No., the 2-byte value is sent from the lower byte (L: bit 0 to 7). For the request destination module station No., the 1-byte value is sent.

**Ex.**

- Accessing the connected station (host station): Request destination module I/O No. `03FF`, Request destination module station No. `00` → ASCII `30H 33H 46H 46H | 30H 30H`; Binary `FFH 03H | 00H`
- Accessing multidrop connection station via network (Start input/output number: 0080H (input/output signal: 0080H to 009FH); Request destination module I/O No.: 0008H; Station No.: 5): Request destination module I/O No. `0008`, Request destination module station No. `05` → ASCII `30H 30H 30H 38H | 30H 35H`; Binary `08H 00H | 05H`

> The station No. of the serial communication module can be checked by using the following parameters of Engineering tool.
> - GX Works2: "Station Number Setting" in "Switch Setting"
> - GX Works3: "Station Number Settings" in "Module Parameter"

- When accessing the non-control CPU (multiple CPU No.2) on multiple CPU system: Request destination module I/O No. `03E1`, Request destination module station No. `00` → ASCII `30H 33H 45H 31H | 30H 30H`; Binary `E1H 03H | 00H`

#### Self-station No.

Specify this when more than one external device (m stations) and more than one C24s (n stations) are connected with the multidrop connection.

Specify the fixed value (00H) for any cases other than multidrop connection in a m:n basis.

**When external devices are connected with the m:n multidrop connection**

Specify the station No. of request source external device, 0 to 31 (00H to 1FH).

For the station No. (m stations) of external devices, the value which is not set to C24 (n stations) of the multidrop connection is used. (The total of 'm' and 'n' is up to 32 stations.)

- Station No. of a request source external device: Specify it to the self-station No.
- Station No. of the connected station C24: Specify it to the station No. (Page 50 Station No.)

*Figure (m:n multidrop connection), labels from left to right: External device: Station No. n; C24: Station No.0; Request source external device: Station number specified for the Self-station No.; C24: Station number specified for the Station No.; Connected station; External device: Station No. (n + m - 1) < 32; C24: Station No.n-1.*

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value.

**Ex.**

- For connection other than m:n multidrop connection: `00` → ASCII `30H 30H`; Binary `00H`
- For accessing with the m:n multidrop connection (Station No. assigned to a request source external device: 31 (1FH)): `1F` → ASCII `31H 46H`; Binary `1FH`
