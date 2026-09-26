# PART 5 COMPATIBILITY WITH A SERIES

This part explains the specifications when using MELSEC-A series devices.

- 16 MELSEC-A SERIES SUPPORTED SPECIFICATIONS
- 17 COMMUNICATING USING 1C FRAMES
- 18 COMMUNICATING USING 1E FRAMES

---

## Reading Notes

- **Source:** Mc-protocol.pdf, Part 5 (PDF pages 343–439, printed pages 341–437). The text was compared page by page with the PDF (page images and text layer) in two independent passes.
- **Page references:** "Page N Title" cross-references keep the page numbers printed in the PDF (PDF page index = printed page number + 2).
- **Diagrams:** message-format diagrams and communication examples are transcribed as text. `Field(value) → Field(value)` gives the fields left to right, where the value is the character(s) printed in the cell; `(unlabeled cell)(value)` is a cell that has no printed label; `Bytes:` is the row of hex bytes printed under the cells, left to right. Lines such as `Devices:`, `Bit pattern:`, `Expanded bits:`, `Labels under ...` and `Bit expansion of ...` reproduce sub-figures printed under a diagram (device names or bit rows) and contain only what the PDF prints.
- **Figures:** a `*Figure: ...*` line lists only the labels and legend text of a pictorial figure.
- **Boxes:** `> **Point**`, `> **Restriction**` etc. are the boxes printed in the PDF.
- **Notes:** a `> **Note:**` block marks a place where the PDF itself has an evident misprint or is internally inconsistent. It states what the PDF prints and which value is used here. Where the correct value cannot be determined from the PDF, the text follows the PDF as printed.

---

## 16 MELSEC-A SERIES SUPPORTED SPECIFICATIONS

This chapter explains the specifications of the messages of MC protocol and access ranges when using the MELSEC-A series devices as follows:

- When accessing system including MELSEC-A series modules
- When utilizing the software for data communication created for MELSEC-A series programmable controller.

### 16.1 Frames and Commands that can be Used

When accessing MELSEC-A series modules, all frames of MC protocol can be used.
However, the commands that can be used have some restrictions. (Page 471 Accessible Modules for Each Command)

**A compatible frame**

The following frames have compatibility with the message protocol and the message format for MELSEC-A series.

| Frame | Compatible message format | Accessible range |
|---|---|---|
| 1C frame | Dedicated protocols for MELSEC-A series computer link modules | Page 47 Accessible range of 1C frame |
| 1E frame | Message formats for MELSEC-A series Ethernet interface modules | Page 49 Accessible range of 1E frame |

### 16.2 Accessible modules

The following MELSEC-A series modules can be accessed within the access range.

**Accessible modules to other stations**

The following modules can be accessed.

| Type | Model name |
|---|---|
| CPU module | A1NCPU, A2NCPU, A2NCPU-S1, A3NCPU, A2ACPU, A2ACPU-S1, A3ACPU, A2UCPU, A2UCPU-S1, A3UCPU, A4UCPU<br>A1SCPU, A1SJCPU(-S3), A1SHCPU, A1SJHCPU, A2SCPU, A2SHCPU, A2USCPU, A2USCPU-S1, A2USHCPU-S1<br>A0J2HCPU |
| CPU module | Q02CPU-A, Q02HCPU-A, Q06HCPU-A |
| CPU module | A2CCPUC24 and A2CCPUC24-PRF (when connected to an external device by multidrop connection.) |
| MELSECNET/10 remote I/O | AJ72LP25 (G), AJ72BR15 |
| Special function module | Refer to the following section.<br>Page 385 Accessible modules |

**Modules that can be relayed between networks**

For the modules that can be relayed between networks when accessing MELSEC-A series module, refer to the following table.

■MELSEC-A series module

| Network | Model name |
|---|---|
| MELSECNET/10 | AJ71LP21 (G), AJ71BR11, A1SJ71LP21, A1SJ71BR11 |

■Module other than MELSEC-A series

Page 286 Modules that can be relayed between networks

### 16.3 Considerations

The following shows the considerations when using MELSEC-A series devices.

**Considerations when connecting C24**

■When a computer link module is included in multidrop connection

Access in ASCII code (format 1 to format 4). A binary code (format 5) cannot be used. (Including the access to the connected station)

**Considerations when connecting E71**

■Setting range of monitoring timer

When accessing ACPU for the first time, the wait time for CPU monitoring timer is required before receiving a response message because QnACPU identifies the CPU type. Be sure to set a value within the setting range shown below.

| Access target | Monitoring timer |
|---|---|
| Connected station (host station) | 1H to 28H (0.25 s to 10 s) |
| Other station | 2H to F0H (0.5 s to 60 s) |

## 17 COMMUNICATING USING 1C FRAMES

This chapter explains the functions when accessing using 1C frame and their message format.
1C frame is compatible with the communication function of the dedicated protocols supported by A series computer link modules.
Only the commands for 1C frame explained in this chapter can be used for 1C frame.

### 17.1 Message Format

This section explains the message format when communicating data using 1C frame.

**Message format**

■Request message

`Data by the format → Station No. → PC No. → Command → Message wait → Character area → Data by the format`

*Figure: Data by the format; Access route (over Station No., PC No.); Request data (over Command, Message wait, Character area); Data by the format; Station No.; PC No.; Character area; Command; Message wait*

■Response message (Normal completion: Response data)

`Data by the format → Station No. → PC No. → Data by the format → Character area → Data by the format`

*Figure: Data by the format; Access route (over Station No., PC No.); Data by the format; Response data (over Character area); Data by the format; Station No.; PC No.; Character area*

■Response message (Normal completion: No response data)

`Data by the format → Station No. → PC No. → Data by the format`

*Figure: Data by the format; Access route (over Station No., PC No.); Data by the format; Station No.; PC No.*

■Response message (Abnormal completion)

`Data by the format → Station No. → PC No. → Data by the format → Error code → Data by the format`

*Figure: Data by the format; Access route (over Station No., PC No.); Data by the format (over the cell after PC No. and the cell after Error code); Station No.; PC No.; Error code*

**Setting data**

Set the following items.

| Item | Item | Description | Reference |
|---|---|---|---|
| Data by format | Data by format | The message formats differ depending on the set format (Format 1 to Format 4). | Page 29 Message Formats of Each Protocol |
| Access route | Station No. | Specify the station to be connected from an external device. | Page 50 Station No. |
| Access route | PC No. | Specify the network module station No. of an access target. | Page 52 Network No., PC No. |
| Request data | Command | Specify the function to request such as read or write. | Page 346 Command |
| Request data | Message wait | A data to generate a delay time for response transmission.<br>Specify the wait time within the range of 0 to 150 ms in 10 ms units. | Page 347 Message wait |
| Request data | Character area | A data that instructs the CPU module to execute a request specified by command. The content of the character areas differs depending on the command. | Page 347 Character area |
| Response data | Character area | A data that C24 returns to a request specified by a command. The content of the character areas differs depending on the command. | Page 347 Character area |
| Error code | Error code | Error code indicates the content of occurred error. | Page 348 Error code |

### 17.2 Details of Setting Data

This section explains how to specify the common data items and their content in each message.

#### Command

Set the command type. (Page 349 Command and Function Lists for 1C Frame)
The setting values for each command are as follows.

| Function | ACPU common command: Symbol | ACPU common command: ASCII code | AnA/AnUCPU common command: Symbol | AnA/AnUCPU common command: ASCII code | Reference |
|---|---|---|---|---|---|
| Device memory read and write | BR | 42H, 52H | JR | 4AH, 52H | Page 354 Batch read (bit units) (command: BR, JR) |
| Device memory read and write | WR | 57H, 52H | QR | 51H, 52H | Page 356 Batch read (word units) (command: WR, QR) |
| Device memory read and write | BW | 42H, 57H | JW | 4AH, 57H | Page 358 Batch write (bit units) (command: BW, JW) |
| Device memory read and write | WW | 57H, 57H | QW | 51H, 57H | Page 360 Batch write (word units) (command: WW, QW) |
| Device memory read and write | BT | 42H, 54H | JT | 4AH, 54H | Page 362 Test (random write) (bit units) (command: BT, JT) |
| Device memory read and write | WT | 57H, 54H | QT | 51H, 54H | Page 364 Test (random write) (word units) (command: WT, QT) |
| Device memory read and write | BM | 42H, 4DH | JM | 4AH, 4DH | Page 367 Register monitor data (bit units) (command: BM, JM) |
| Device memory read and write | WM | 57H, 4DH | QM | 51H, 4DH | Page 368 Register monitor data (word units) (command: WM, QM) |
| Device memory read and write | MB | 4DH, 42H | MJ | 4DH, 4AH | Page 369 Monitor (bit units) (command: MB, MJ) |
| Device memory read and write | MN | 4DH, 4EH | MQ | 4DH, 51H | Page 370 Monitor (word units) (command: MN, MQ) |
| Read and write extended file register | ER | 45H. 52H | — | — | Page 375 Batch read (command: ER) |
| Read and write extended file register | EW | 45H, 57H | — | — | Page 376 Batch write (command: EW) |
| Read and write extended file register | — | — | NR | 4EH, 52H | Page 381 Direct read (command: NR) |
| Read and write extended file register | — | — | NW | 4EH, 57H | Page 382 Direct write (command: NW) |
| Read and write extended file register | ET | 45H, 54H | — | — | Page 377 Test (random write) (command: ET) |
| Read and write extended file register | EM | 45H, 4DH | — | — | Page 379 Register monitor data (command: EM) |
| Read and write extended file register | ME | 4DH, 45H | — | — | Page 380 Monitor (command: ME) |
| Read/write buffer memory of special function module | TR | 54H, 52H | — | — | Page 386 Batch read (command: TR) |
| Read/write buffer memory of special function module | TW | 54H, 57H | — | — | Page 388 Batch write (command: TW) |
| Loopback test | TT | 54H, 54H | — | — | Page 389 Loopback test (Command: TT) |

**Setting method**

Use the commands by converting to 2-digit (hexadecimal) ASCII codes.

**Ex.**
Device memory batch read (BR) in bit unit

`(unlabeled cell)(B) → (unlabeled cell)(R)`
Bytes: `42H 52H`

#### Message wait

Message wait is a data to generate a delay time for response transmission.
Some external devices may take time to become receiving status after sending a command.
Specify the minimum wait time to send the result after C24 is received a command from an external device. Specify the wait time in accordance with the specifications of the external device.

**Setting method**

Specify the wait time within the range of 0 to 150 ms in 10 ms units.
Convert 0H to FH (0 to 15) to 1-digit (hexadecimal) ASCII codes regarding 10 ms as 1H.

**Ex.**
When the message wait time is 100 ms
If the following value is set to message wait in request message, after passing 100 ms or more, transmission of a response message will be started.

`(unlabeled cell)(A)`
Bytes: `41H`

#### Character area

The content of the character areas differs depending on the command.
The character area of request data is equivalent to the character A area and the character C area of the dedicated protocols for A series computer link module. The character are of response data is equivalent to the character B area of a dedicated protocols.

- Character area A: A data that C24 instructs the CPU module to perform the read request specified by command.
- Character area B: A data that C24 returns to a request specified by a command.
- Character area C: A data that C24 instructs the CPU module to perform the write request specified by command.

**When reading data (Response data)**

The following shows the image when the response data (character B area of the dedicated protocol) is included in the response message.
(The head of the message data in the figure is a control code of format 1. Page 29 Message Formats of Each Protocol)

*Figure: External device; Command; Message wait; Response message that can be abbreviated (Normal completion); Request message; ENQ; Character area A; ACK; Transmission order; Response message (Normal completion : The response data exist.); STX; Character area B; or; Error code; Response message (Abnormal completion); NAK; Supported devices*

**When writing data (No response data)**

The following shows the image when the response data is not included in the response message.
(The head of the message data in the figure is a control code of format 1. Page 29 Message Formats of Each Protocol)

*Figure: External device; Command; Message wait; Request message; ENQ; Character area C; Transmission order; Response message (Normal completion : The response data does not exist.); ACK; or; Error code; Response message (Abnormal completion); NAK; Supported devices*

#### Error code

Error code indicates the content of occurred error.
If more than one error occurs at the same time, the error code detected first is returned.
For the content of error code and its corrective action, refer to the user's manual of the module used.
MELSEC iQ-R Serial Communication Module User's Manual(Application)
Q Corresponding Serial Communication Module User's Manual (Basic)
MELSEC-L Serial Communication Module User's Manual (Basic)

**Setting method**

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

**Ex.**
For error code 05H

`(unlabeled cell)(0) → (unlabeled cell)(5)`
Bytes: `30H 35H`

### 17.3 Command and Function Lists for 1C Frame

Use the following commands for data communication using 1C frame.

| Function | Function | Function | ACPU common command | AnA/AnUCPU common command | Description |
|---|---|---|---|---|---|
| Device memory*1 | Batch read | Bit units | BR | JR | Reads bit devices (X, Y, M, etc.) in 1-point units. |
| Device memory*1 | Batch read | Word units | WR | QR | Reads bit devices (X, Y, M, etc.) in 16-point units. Reads word devices (D, T, C, etc.) in 1-point units. |
| Device memory*1 | Batch write | Bit units | BW | JW | Writes bit devices (X, Y, M, etc.) in 1-point units. |
| Device memory*1 | Batch write | Word units | WW | QW | Writes bit devices (X, Y, M, etc.) in 16-point units. Writes word devices (D, T, C, etc.) in 1-point units. |
| Device memory*1 | Test (random write) | Bit units | BT | JT | Set/reset devices and device numbers to bit devices (X, Y, M, etc.) by specifying them randomly in 1 point unit. |
| Device memory*1 | Test (random write) | Word units | WT | QT | Set/reset devices and device numbers to bit devices (X, Y, M, etc.) by specifying them randomly in 16 point units. Write devices and device numbers to word devices (D, T, C, etc.) by specifying them randomly in 1 point units. |
| Device memory*1 | Register monitor data*2 | Bit units | BM | JM | Registers bit devices (X, Y, M, etc.) to be monitored in 1-point units |
| Device memory*1 | Register monitor data*2 | Word units | WM | QM | Registers bit devices (X, Y, M, etc.) to be monitored in 16-point units. Registers word devices (D, T, C, etc.) to be monitored in 1-point units. |
| Device memory*1 | Monitor | Bit units | MB | MJ | Monitors the devices registered by monitor data registration. |
| Device memory*1 | Monitor | Word units | MN | MQ | Monitors the devices registered by monitor data registration. |
| Extended file register | Batch read | Batch read | ER | — | Reads extended file register (R) in 1-point units. |
| Extended file register | Batch write | Batch write | EW | — | Writes extended file register (R) in 1-point units. |
| Extended file register | Direct read | Word units | — | NR | Read data in 1 point units by specifying the consecutive device number regardless of the block number of extended file register. |
| Extended file register | Direct write | Word units | — | NW | Write data in 1 point units by specifying the consecutive device number regardless of the block number of extended file register. |
| Extended file register | Test (Random write) | Test (Random write) | ET | — | Write block numbers and device numbers to extended file register (R) by specifying them randomly in 1 point units. |
| Extended file register | Register monitor data*2 | Register monitor data*2 | EM | — | Register extended file register (R) in 1-point units. |
| Extended file register | Monitor | Word units | ME | — | Monitor the extended file register (R) registered by monitor data registration. |
| Special function module | Batch read | Batch read | TR | — | Reads data in the buffer memory of a special function module. |
| Special function module | Batch write | Batch write | TW | — | Writes data to the buffer memory of a special function module. |
| Loopback test | Loopback test | Loopback test | TT | — | Send (returns) characters received from an external device back to the external device unchanged. |

*1 Use the dedicated commands for extended registers to read/write extended file registers from/to ACPU.
*2 The devices for the five types of commands (BM, JM, WM, QM, EM) for registering monitor data can be registered simultaneously in C24 for each interface.

**ACPU common command, AnA/AnUCPU common command**

ACPU common command is a communication function issued by MC protocol. The command is accessible for ACPU.
AnA/AnUCPU common command is a command for AnACPU and AnUCPU. The command cannot be executed for other than AnA/AnUCPU.
○: Executable, △: Executable (with restrictions), ×: Not executable

| Type | ACPU other than AnA/AnUCPU | AnA/AnUCPU | Module other than ACPU |
|---|---|---|---|
| ACPU common command | ○ | ○ | △ |
| AnA/AnUCPU common command | × | ○ | △ |

When accessing modules other than ACPU, there is a restriction for the accessible device range.
Page 350 Considerations when accessing devices other than ACPU module

### 17.4 Device Memory Read and Write

This section explains the specification content and examples of the control procedure when reading from/writing to the device memory are as shown below.
For the message formats other than request data and response data, refer to the following sections.
Page 344 Message Format, Page 346 Details of Setting Data

> **Point**
> To read and write the extended file register, use the commands dedicated to the extended file register.
> Page 371 Read and Write Extended File Register

#### Considerations

The considerations when reading/writing device memory using the commands described in this section.

**Considerations when accessing devices other than ACPU module**

■Accessible devices

Only the devices with the same names that exist in ACPU can be accessed within the device range of AnACPU.
Page 352 Accessible device range

The following devices cannot be accessed from the external devices:

- Added devices
- Latch relay (L) and step relay (S)*1
- File register (R) of QnACPU

*1 Even when the latch relay (L) or step relay (S) is specified, the internal relay (M) can be accessed.

■Special relays and special registers

Special relays and special registers can be accessed within the following range.

- Access SM1000 to SM1255 by specifying M9000 to M9255.
- Access SD1000 to SD1255 by specifying D9000 to D9255.

■Universal model QCPU

Use the Universal model QCPU with a serial number whose first five digits are '10102' or later.
As for the serial number whose first five digits are '10101' or earlier, access using 2C/3C/4C frame.

#### Data to be specified in command

**Device codes, device numbers**

The settings of each device when reading/writing device memory can be performed using device code and device number as shown in the following figure.
Specify the device to be accessed by a device code and a device number.
The setting data size differ between ACPU common commands and AnA/AnUCPU commands.
The setting data size differ when the device type is timer or counter.

| Device type | ACPU common command | AnA/AnUCPU common command |
|---|---|---|
| Other than timer and counter | *Figure: Device code (1 digit), Device number (4 digits)* | *Figure: Device code (1 digit), Device number (6 digits)* |
| Timer, counter | *Figure: Device code (2 digits), Device number (3 digits)* | *Figure: Device code (2 digits), Device number (5 digits)* |

■ACPU common commands

- Device code: Convert the device name to 1-digit ASCII code (2-digits for timer or counter), and send it from the upper digits.
- Device number: Convert the numerical value to 4-digit ASCII code (3-digits for timer or counter), and send it from the upper digits.

■AnA/AnUCPU common commands

- Device code: Convert the device name to 1-digit ASCII code (2-digits for timer or counter), and send it from the upper digits.
- Device number: Convert the numerical value to 6-digit ASCII code (5-digits for timer and counter), and send it from the upper digits.

**Ex.**
Current value of input (X) 40 and timer (T) 10

Input (X) 40

ACPU common command: `Device code(X) → Device number(0040)`
Bytes: `58H 30H 30H 34H 30H`

AnA/AnUCPU common command: `Device code(X) → Device number(000040)`
Bytes: `58H 30H 30H 30H 30H 34H 30H`

Current value of timer (T) 10

ACPU common command: `Device code(TN) → Device number(010)`
Bytes: `54H 4EH 30H 31H 30H`

> **Note:** In the PDF the device code cell of the timer example is printed "T S" (ACPU and AnA/AnUCPU) while the bytes printed under it are 54H 4EH, which is "TN" (the current value code in the device table). The bytes and the "Current value of timer" label indicate TN.

AnA/AnUCPU common command: `Device code(TN) → Device number(00010)`
Bytes: `54H 4EH 30H 30H 30H 31H 30H`

**Accessible device range**

The following table shows the devices and device number range that can be specified when accessing the device memory.
Access the CPU module within the range of device number that can be used by commands and the range of the device number that can be used in the access target CPU. (Page 350 Considerations when accessing devices other than ACPU module)
□: Specify 0 (30H) or space (20H)

| Device name | Device name (sub) | Symbol | Type | Representation | Device code | Device number: ACPU common command | Device number: AnA/AnUCPU common command |
|---|---|---|---|---|---|---|---|
| Input | Input | X | Bit | Hexadecimal | X | □□□0 to □7FF | □□□□□0 to □□1FFF |
| Output | Output | Y | Bit | Hexadecimal | Y | □□□0 to □7FF | □□□□□0 to □□1FFF |
| Internal relay | Internal relay | M | Bit | Decimal | M | □□□0 to 2047 | □□□□□0 to □□8191 |
| Latch relay | Latch relay | L | Bit | Decimal | L | □□□0 to 2047 | □□□□□0 to □□8191 |
| Step relay | Step relay | S | Bit | Decimal | S | □□□0 to 2047 | □□□□□0 to □□8191 |
| Annunciator | Annunciator | F | Bit | Decimal | F | □□□0 to □255 | □□□□□0 to □□2047 |
| Link relay | Link relay | B | Bit | Hexadecimal | B | □□□0 to □3FF | □□□□□0 to □□1FFF |
| Timer | Current value | T | Word | Decimal | TN | □□0 to 255 | □□□□0 to □2047 |
| Timer | Contact | T | Bit | Decimal | TS | □□0 to 255 | □□□□0 to □2047 |
| Timer | Coil | T | Bit | Decimal | TC | □□0 to 255 | □□□□0 to □2047 |
| Counter | Current value | C | Word | Decimal | CN | □□0 to 255 | □□□□0 to □1023 |
| Counter | Contact | C | Bit | Decimal | CS | □□0 to 255 | □□□□0 to □1023 |
| Counter | Coil | C | Bit | Decimal | CC | □□0 to 255 | □□□□0 to □1023 |
| Data register | Data register | D | Word | Decimal | D | □□□0 to 1023 | □□□□□0 to □□8191 |
| Link register | Link register | W | Word | Hexadecimal | W | □□□0 to □3FF | □□□□□0 to □□1FFF |
| File register | File register | R | Word | Decimal | R | □□□0 to 8191 | □□□□□0 to □□8191 |
| Special relay | Special relay | M | Bit | Decimal | M | 9000 to 9255 | □□9000 to □□9255 |
| Special register | Special register | D | Word | Decimal | D | 9000 to 9255 | □□9000 to □□9255 |

> **Restriction**
> - Do not write data to the devices which cannot be written in the range of the special relays (M9000 to M9255) and special registers (D9000 to D9255). For details on the special relays and special registers, refer to manual of ACPU to be accessed.
> - The range of M, L, and S devices can be specified for MELSEC-A series CPU module, however, if the range of M is specified by L or S or vice versa, they are processed equivalently.

> **Point**
> - For word unit specification, the head device number of bit device must be specified in multiple of 16.
> - For special relay M9000 or later, (9000 + multiple of 16) can be specified.

**Number of device points**

Specify the number of device points to be read or written.
Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points in one command within the device points that can be processed in one communication.
Page 466 Number of Processing per One Communication
Specify '00' for 256 points.

**Ex.**
5 points, 20 points, 256 points

| Number of device points | ASCII code |
|---|---|
| 5 points | `0`(30H) `5`(35H) |
| 20 points | `1`(31H) `4`(34H) |
| 256 points | `0`(30H) `0`(30H) |

**Read data, write data**

The data storage method is the same as reading/writing data with device access of 4C/3C/2C frame. (Page 72 Read data, write data)

#### Batch read (bit units) (command: BR, JR)

Reads bit devices (X, Y, M, etc.) in batch.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command → Message wait → Head device → Number of device points`

■Response data

The value of read device is stored in bit units. (Page 72 Read data, write data)

**Data specified by request data**

■Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `Command(BR)`, Bytes: `42H 52H` | `Command(JR)`, Bytes: `4AH 52H` |

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device

Specify the head device. (Page 351 Device codes, device numbers)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 256 (for 256 points, specify '00H')
- Head device No. + Number of device points - 1 ≤ Maximum device No.

**Communication example**

Read data in bit units under the following conditions.

- Message wait: 100 ms
- Head device: X040
- Number of device points: 5 points

(Request data)

■When using BR (ACPU common command)

Request data: `Command(BR) → Message wait(A) → Head device(X0040) → Number of device points(05)`
Bytes: `42H 52H 41H 58H 30H 30H 34H 30H 30H 35H`

■When using JR (AnA/AnUCPU common command)

Request data: `Command(JR) → Message wait(A) → Head device(X000040) → Number of device points(05)`
Bytes: `4AH 52H 41H 58H 30H 30H 30H 30H 34H 30H 30H 35H`

(Response data)

Response data: `Data read(0 → 1 → 1 → 0 → 1)` with (X40) (X41) (X42) (X43) (X44) printed under the cells
Bytes: `30H 31H 31H 30H 31H`

#### Batch read (word units) (command: WR, QR)

Reads bit devices (X, Y, M, etc.) in 16-point units.
Reads word devices (D, T, C, etc.) in 1-point units

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command → Message wait → Head device → Number of device points (Number of words)`

■Response data

The value of read device is stored in word units. (Page 72 Read data, write data)

**Data specified by request data**

■Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `W R` | `Q R` |
| `57H 52H` | `51H 52H` |

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device

Specify the head device. (Page 351 Device codes, device numbers)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- For bit device: 1 ≤ Number of device points ≤ 32
- For bit device: Head device No. + Number of device points ×16 - 1 ≤ Maximum device No.
- For word device: 1 ≤ Number of device points ≤ 64
- For word device: Head device No. + Number of device points - 1 ≤ Maximum device No.

> **Point**
> - When specifying bit devices, set the head device No. in multiples of 16 (0, 16, ... in decimal notation).

**Communication example (Reading bit device memory)**

Read bit devices in 16-point units under the following conditions.

- Message wait: 0 ms
- Head device: X040
- Number of device points: 32 points (2 words)

(Request data)

■When using WR (ACPU common command)

Request data: `Command(WR) → Message wait(0) → Head device(X0040) → Number of device points (Number of words)(02)`
Bytes: `57H 52H 30H 58H 30H 30H 34H 30H 30H 32H`

■When using QR (AnA/AnUCPU common command)

Request data: `Command(QR) → Message wait(0) → Head device(X000040) → Number of device points (Number of words)(02)`
Bytes: `51H 52H 30H 58H 30H 30H 30H 30H 34H 30H 30H 32H`

(Response data)

Data read: `0001 (Hexadecimal : 1) → 0010 (Hexadecimal : 2) → 0011 (Hexadecimal : 3) → 0100 (Hexadecimal : 4) → 1010 (Hexadecimal : A) → 1011 (Hexadecimal : B) → 1100 (Hexadecimal : C) → 1101 (Hexadecimal : D)`
Bytes: `31H 32H 33H 34H 41H 42H 43H 44H`
Devices: `(X4F) to (X4C) → (X4B) to (X48) → (X47) to (X44) → (X43) to (X40) → (X5F) to (X5C) → (X5B) to (X58) → (X57) to (X54) → (X53) to (X50)`

**Communication example (Reading word device memory)**

Read word devices in 1-point units under the following conditions.

- Message wait: 0 ms
- Head device: Current value of T123
- Number of device points: 2 points (2 words)

(Request data)

■When using WR (ACPU common command)

Request data: `Command(WR) → Message wait(0) → Head device(TN123) → Number of device points (Number of words)(02)`
Bytes: `57H 52H 30H 54H 4EH 31H 32H 33H 30H 32H`

■When using QR (AnA/AnUCPU common command)

Request data: `Command(QR) → Message wait(0) → Head device(TN00123) → Number of device points (Number of words)(02)`
Bytes: `51H 52H 30H 54H 4EH 30H 30H 31H 32H 33H 30H 32H`

(Response data)

Data read: `7 → B → C → 9 → 1 → 2 → 3 → 4`
Bytes: `37H 42H 43H 39H 31H 32H 33H 34H`
Devices: `(T123) → (T124)`

#### Batch write (bit units) (command: BW, JW)

Writes bit devices (X, Y, M, etc.) in batch.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

ACPU common command

`Command → Message wait → Head device → Number of device points → Write data for the number of device points`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `B W` | `J W` |
| `42H 57H` | `4AH 57H` |

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device

Specify the head device. (Page 351 Device codes, device numbers)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 160
- Head device No. + Number of device points - 1 ≤ Maximum device No.

■Write data for the number of device points

Store the data to be written in batch. (Page 353 Read data, write data)

**Communication example**

Write data in bit units in batch under the following conditions.

- Message wait: 0 ms
- Head device: M903
- Number of device points: 5 points

(Request data)

■When using BW (ACPU common command)

Request data: `Command(BW) → Message wait(0) → Head device(M0903) → Number of device points (Number of words)(05) → Write data for the number of device points(0 → 1 → 1 → 0 → 1)`
Bytes: `42H 57H 30H 4DH 30H 39H 30H 33H 30H 35H 30H 31H 31H 30H 31H`
Devices: `(M903) (M904) (M905) (M906) (M907)`

■When using JW (AnA/AnUCPU common command)

Request data: `Command(JW) → Message wait(0) → Head device(M000903) → Number of device points (Number of words)(05) → Write data for the number of device points(0 → 1 → 1 → 0 → 1)`
Bytes: `4AH 57H 30H 4DH 30H 30H 30H 39H 30H 33H 30H 35H 30H 31H 31H 30H 31H`
Devices: `(M903) (M904) (M905) (M906) (M907)`

#### Batch write (word units) (command: WW, QW)

Write data to bit devices (X, Y, M, etc.) in 16-point units.
Write data to word devices (D, T, C, etc.) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command → Message wait → Head device → Number of device points → Write data for the number of device points`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `W W` | `Q W` |
| `57H 57H` | `51H 57H` |

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device

Specify the head device. (Page 351 Device codes, device numbers)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- For bit device: 1 ≤ Number of device points ≤ 10
- For bit device: Head device No. + Number of device points ×16 - 1 ≤ Maximum device No.
- For word device: 1 ≤ Number of device points ≤ 64
- For word device: Head device No. + Number of device points - 1 ≤ Maximum device No.

■Write data for the number of device points

Store 4-digit data per one device point. (Page 353 Read data, write data)

> **Point**
> When specifying bit devices, set the head device No. in multiples of 16 (0, 16, ... in decimal notation).

**Communication example (Writing to word bit memory)**

Write data to bit devices in 16-point units under the following conditions.

- Message wait: 0 ms
- Head device: M640
- Number of device points: 32 points (2 words)

(Request data)

■When using WW (ACPU common command)

Request data: `Command(WW) → Message wait(0) → Head device(M0640) → Number of device points (Number of words)(02) → Write data for the number of device points(2347AB96)`
Bytes: `57H 57H 30H 4DH 30H 36H 34H 30H 30H 32H 32H 33H 34H 37H 41H 42H 39H 36H`
Devices: `(M671) to (M656)`

Bit pattern: `0 → 0 → 1 → 0 → 0 → 0 → 1 → 1 → 0 → 1 → 0 → 0 → 0 → 1 → 1 → 1`
Devices: `(M655) to (M640)`

■When using QW (AnA/AnUCPU common command)

Request data: `Command(QW) → Message wait(0) → Head device(M000640) → Number of device points (Number of words)(02) → Write data for the number of device points(2347AB96)`
Bytes: `51H 57H 30H 4DH 30H 30H 30H 36H 34H 30H 30H 32H 32H 33H 34H 37H 41H 42H 39H 36H`
Devices: `(M671) to (M656)`

Bit pattern: `0 → 0 → 1 → 0 → 0 → 0 → 1 → 1 → 0 → 1 → 0 → 0 → 0 → 1 → 1 → 1`
Devices: `(M655) to (M640)`

**Communication example (Writing to word device memory)**

Write data to word devices in 1-point units under the following conditions.

- Message wait: 0 ms
- Head device: D0
- Number of device points: 2 points (2 words)

(Request data)

■When using WW (ACPU common command)

Request data: `Command(WW) → Message wait(0) → Head device(D0000) → Number of device points (Number of words)(02) → Write data for the number of device points(1234ACD7)`
Bytes: `57H 57H 30H 44H 30H 30H 30H 30H 30H 32H 31H 32H 33H 34H 41H 43H 44H 37H`
Devices: `(D0) (D1)`

■When using QW (AnA/AnUCPU common command)

Request data: `Command(QW) → Message wait(0) → Head device(D000000) → Number of device points (Number of words)(02) → Write data for the number of device points(1234ACD7)`
Bytes: `51H 57H 30H 44H 30H 30H 30H 30H 30H 30H 30H 32H 31H 32H 33H 34H 41H 43H 44H 37H`
Devices: `(D0) (D1)`

#### Test (random write) (bit units) (command: BT, JT)

Set/reset devices and device numbers to bit devices (X, Y, M, etc.) by specifying them randomly in 1 point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command → Message wait → Number of device points (n points) → Device (first point) → Set/reset (first point) → ... → Device (nth point) → Set/reset (nth point)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `B T` | `J T` |
| `42H 54H` | `4AH 54H` |

■Message wait

Specify the delayed time of the response transmission.
Page 347 Message wait

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 20

■Device

Specify the device to test.
Page 351 Device codes, device numbers

■Set/Reset

- 0 (30H): Reset (OFF)
- 1 (31H): Set (ON)

**Communication example**

Perform the test in bit units under the following conditions.

- Message wait: 0 ms
- Number of device points: 3 points
- Device: Turn ON M50, turn OFF B31A, and turn ON Y02F

(Request data)

■When using BT (ACPU common command)

Request data: `Command(BT) → Message wait(0) → Number of device points (Number of words)(03) → Device(M0050) → Set/reset(1) → Device(B031A) → Set/reset(0) → Device(Y002F) → Set/reset(1)`
Bytes: `42H 54H 30H 30H 33H 4DH 30H 30H 35H 30H 31H 42H 30H 33H 31H 41H 30H 59H 30H 30H 32H 46H 31H`

■When using JT (AnA/AnUCPU common command)

Request data: `Command(JT) → Message wait(0) → Number of device points (Number of words)(03) → Device(M000050) → Set/reset(1) → Device(B00031A) → Set/reset(0) → Device(Y00002F) → Set/reset(1)`
Bytes: `4AH 54H 30H 30H 33H 4DH 30H 30H 30H 30H 35H 30H 31H 42H 30H 30H 30H 33H 31H 41H 30H 59H 30H 30H 30H 30H 32H 46H 31H`

#### Test (random write) (word units) (command: WT, QT)

Set/reset devices and device numbers to bit devices (X, Y, M, etc.) by specifying them randomly in 16 point units.
Write devices and device numbers to word devices (D, T, C, etc.) by specifying them randomly in 1 point units.
A mixture of word devices and bit devices (16 bit units) can be specified.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command → Message wait → Number of device points (n points) → Device (first point) → Write data (first point) → ... → Device (nth point) → Write data (nth point)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `WT` (`57H 54H`) | `QT` (`51H 54H`) |

■Message wait

Specify the delayed time of the response transmission.
Page 347 Message wait

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 10 (for bit device : 10 (16 points are designated as 1))

■Device

Specify the device to test.
Page 351 Device codes, device numbers

■Write data

Store 4-digit data per one device point.
Page 353 Read data, write data

> **Point**
> When specifying bit devices, set the head device No. in multiples of 16 (0, 16, ... in decimal notation).

**Communication example**

Write data with mixture specification of word devices and bit devices (16-point unit) under the following conditions.

- Message wait: 0 ms
- Number of device points: 3 points (3 words)
- Device: Set 1234H to D500, BCA9H from Y100 to Y10F, and 64H to current values of C100

(Request data)

■When using WT (ACPU common command)

Request data: `Command(WT) → Message wait(0) → Number of device points(03) → Device(D0500) → Write data(1234) → Device(Y0100) → Write data(BCA9) → Device(CN100) → Write data(0064)`
Bytes: `57H 54H 30H 30H 33H 44H 30H 35H 30H 30H 31H 32H 33H 34H 59H 30H 31H 30H 30H 42H 43H 41H 39H 43H 4EH 31H 30H 30H 30H 30H 36H 34H`

Bits: `1 0 1 1 1 1 0 0 1 0 1 0 1 0 0 1`
(Y10F) to (Y108): `1 0 1 1 1 1 0 0`
(Y107) to (Y100): `1 0 1 0 1 0 0 1`

■When using QT (AnA/AnUCPU common command)

Request data: `Command(QT) → Message wait(0) → Number of device points(03) → Device(D000500) → Write data(1234) → Device(Y000100) → Write data(BCA9) → Device(CN00100) → Write data(0064)`
Bytes: `51H 54H 30H 30H 33H 44H 30H 30H 30H 35H 30H 30H 31H 32H 33H 34H 59H 30H 30H 30H 31H 30H 30H 42H 43H 41H 39H 43H 4EH 30H 30H 31H 30H 30H 30H 30H 36H 34H`

Bits: `1 0 1 1 1 1 0 0 1 0 1 0 1 0 0 1`
(Y10F) to (Y108): `1 0 1 1 1 1 0 0`
(Y107) to (Y100): `1 0 1 0 1 0 0 1`

#### Monitor (Command: BM, JM, WM, QM, MB, MJ, MN, MQ)

The monitor data registration function registers the devices and numbers to be monitored from an external device to C24.
The monitor function reads the data of the registered devices from the CPU module and processes it in the external device.
When the batch read (BR/WR/JR/QR) is performed, the read device numbers will be consecutive, however, by using this function, devices can be monitored by specifying the device numbers randomly
The following example shows the control procedure for monitoring and registering name and number of the devices to be monitored to the C24.

**Monitoring procedure**

1. Process the monitor data registration (Edit of commands for registration and transmission of device specification.)
   - ACPU common commands: BM, WM
   - AnA/AnUCPU common commands: JM, QM
2. Perform read process. (Execution of command for monitoring.)
   - ACPU common commands: MB, MN
   - AnA/AnUCPU common commands: MJ, MQ
3. Process the data. (Screen display, etc.)
4. If do not change the devices to be monitored, return to step 2, and repeat the process.

> **Point**
> - When monitoring data as the procedure shown above, the monitor data registration is required. If monitoring data without registering the data, a protocol error occurs.
> - The content of registered monitor data are deleted when C24 is rebooted.
> - The devices can be registered for each command for bit units (BM or JM), word units (WM or QM), or the extended file register (EM) in C24. (Monitoring extended file registerPage 378 Monitor (command: EM, ME))
> - When registering device memory of the CPU module as a monitor data from more than one C24s on the same station, the recently registered device memory will be available since the registration data is overwritten.

**Register monitor data (bit units) (command: BM, JM)**

Set the bit devices (X, Y, M, etc.) to be monitored in 1-point units.
Monitor the device memory registered in bit units using the following command.
Page 369 Monitor (bit units) (command: MB, MJ)

■Request data

`Command → Message wait → Number of device points (n points) → Device (first point) → ... → Device (nth point)`

- Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `B M` (`42H 4DH`) | `J M` (`4AH 4DH`) |

- Message wait: Specify the delayed time of the response transmission. (Page 347 Message wait)
- Number of device points: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- Device: Specify the devices to be monitored. (Page 351 Device codes, device numbers)

> **Point**
> Specify the number of device points within the following range:
> 1 ≤ Number of device points ≤ 40
> When using the BM command and accessing ACPU other than AnA/AnU, device X (input) has two processing points per point.

■Response data

There is no response data for this command.

■Communication example (Monitor data registration in bit units)

Perform monitor data registration of the bit device under the following conditions.

- Message wait: 0 ms
- Number of device points: 3 points (3 bits)
- Device: Contacts of X40, Y060, and T123.

(Request data)

- When using BM (ACPU common command)

Request data: `Command(BM) → Message wait(0) → Number of device points(03) → Device(X0040) → Device(Y0060) → Device(TS123)`
Bytes: `42H 4DH 30H 30H 33H 58H 30H 30H 34H 30H 59H 30H 30H 36H 30H 54H 53H 31H 32H 33H`

> **Note:** In the PDF the byte row under Device(X0040) prints `58H 30H 35H 34H 30H`; the third byte is printed 35H under the character "0" (ASCII 30H). The md shows 30H.

- When using JM (AnA/AnUCPU common command)

Request data: `Command(JM) → Message wait(0) → Number of device points(03) → Device(X000040) → Device(Y000060) → Device(TS00123)`
Bytes: `4AH 4DH 30H 30H 33H 58H 30H 30H 30H 30H 34H 30H 59H 30H 30H 30H 30H 36H 30H 54H 53H 30H 30H 31H 32H 33H`

**Register monitor data (word units) (command: WM, QM)**

Register the bit devices (X, Y, M, etc.) to be monitored in 16-point units.
Register the word devices (D, T, C, etc.) to be monitored in 1-point units.
A mixture of word devices and bit devices (16 bit units) can be specified.
Monitor the device memory registered in word units using the following command.
Page 370 Monitor (word units) (command: MN, MQ)

■Request data

`Command → Message wait → Number of device points (n points) → Device (first point) → ... → Device (nth point)`

- Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `W M` (`57H 4DH`) | `Q M` (`51H 4DH`) |

- Message wait: Specify the delayed time of the response transmission. (Page 347 Message wait)
- Number of device points: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- Device: Specify the devices to be monitored. (Page 351 Device codes, device numbers)

> **Point**
> Specify the number of device points within the following range:
> 1 ≤ Number of device points ≤ 20
> When using the WM command and accessing ACPU other than AnA/AnU, device X (input) has two processing points per point.

■Response data

There is no response data for this command.

■Communication example (Monitor data registration in word units)

Register the monitor data in a word unit under the following conditions.

- Message wait: 0 ms
- Number of device points: 4 points (4 words)
- Device: Current value of D15, W11E, and T123, and Y060 to Y06F

(Request data)

- When using WM (ACPU common command)

Request data: `Command(WM) → Message wait(0) → Number of device points(04) → Device(D0015) → Device(W011E) → Device(TN123) → Device(Y0060)`
Bytes: `57H 4DH 30H 30H 34H 44H 30H 30H 31H 35H 57H 30H 31H 31H 45H 54H 4EH 31H 32H 33H 59H 30H 30H 36H 30H`

- When using QM (AnA/AnUCPU common command)

Request data: `Command(QM) → Message wait(0) → Number of device points(04) → Device(D000015) → Device(W00011E) → Device(TN00123) → Device(Y000060)`
Bytes: `51H 4DH 30H 30H 34H 44H 30H 30H 30H 30H 31H 35H 57H 30H 30H 30H 31H 31H 45H 54H 4EH 30H 30H 31H 32H 33H 59H 30H 30H 30H 30H 36H 30H`

**Monitor (bit units) (command: MB, MJ)**

Monitor the registered bit devices (X, Y, M, etc.).

> **Point**
> - The bit device memory registered with BM command is monitored using MB command.
> - The bit device memory registered with JM command is monitored using MJ command.

■Request data

`Command → Message wait`

- Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `M B` (`4DH 42H`) | `M J` (`4DH 4AH`) |

- Message wait: Specify the delayed time of the response transmission. (Page 347 Message wait)

■Response data

`Monitoring result ( For the number of device points )`

The value of read device is stored in bit units. (Page 72 Read data, write data)

■Communication example

Monitor bit devices specified with monitor data registration under the following conditions.

- Message wait: 0 ms
- Registered devices for monitoring: 3 points (3 bits) of contacts for X040, Y060 and T123.

(Request data)

- When using MB (ACPU common command)

Request data: `Command(MB) → Message wait(0)`
Bytes: `4DH 42H 30H`

- When using MJ (AnA/AnUCPU common command)

Request data: `Command(MJ) → Message wait(0)`
Bytes: `4DH 4AH 30H`

(Response data)

Response data: `Monitoring result ( For the number of device points )(1 → 0 → 1)`
Bytes: `31H 30H 31H`
Labels under bytes: `(X040) (Y060) (Contact of T123)`

**Monitor (word units) (command: MN, MQ)**

Monitor the registered bit devices (X, Y, M, etc.) in 16-point units.
Monitor the registered word device (D, T, C, etc.) in 1-point unit.

> **Point**
> - The bit device memory registered with WM command is monitored using MN command.
> - The bit device memory registered with QM command is monitored using MQ command.

■Request data

`Command → Message wait`

- Command

| ACPU common | AnA/AnUCPU common |
|---|---|
| `M N` (`4DH 4EH`) | `M Q` (`4DH 51H`) |

- Message wait: Specify the delayed time of the response transmission. (Page 347 Message wait)

■Response data

`Monitoring result ( For the number of device points )`

The value of read device is stored in word units. (Page 72 Read data, write data)

■Communication example

Monitor bit devices or word devices specified with monitor data registration under the following conditions.

- Message wait: 0 ms
- Registered devices for monitoring: Current values of D15, W11E, T123, and 4 points (4 words) of Y060 to Y06F.

(Request data)

- When using MN (ACPU common command)

Request data: `Command(MN) → Message wait(0)`
Bytes: `4DH 4EH 30H`

- When using MQ (AnA/AnUCPU common command)

Request data: `Command(MQ) → Message wait(0)`
Bytes: `4DH 51H 30H`

(Response data)

Response data: `Monitoring result ( For the number of device points )(1234 (Decimal : 4660) → 0050 (Decimal : 80) → 0064 (Decimal : 100) → 0000 → 0111 → 0110 → 0100)`
Bytes: `31H 32H 33H 34H 30H 30H 35H 30H 30H 30H 36H 34H 30H 37H 36H 34H`
Labels under bytes: `(D15) (W11E) (Current value of T123) (Y06F) to (Y06C) (Y06B) to (Y068) (Y067) to (Y064) (Y063) to (Y060)`

### 17.5 Read and Write Extended File Register

The extended file register is a memory area that stores required data and operation result for various data processing by using the software package for extended file register 'SW0GHP-UTLPC-FN1' or 'SW0SRX-FNUP' (hereinafter abbreviated to UTLP-FN1 and FNUP) and AnACPU and AnUSCPU extended file register dedicated instructions. The extended file register uses free area of user memory area in CPU module as a file register.
The following example shows the control procedure to read and write extended file register.

#### Considerations for reading and writing extended file register

The following shows the considerations when reading writing extended file register using the commands described in this section.

**Accessible CPU modules**

Only CPU modules that can handle the extended file register can be accessed.
This function cannot be used for CPU modules that cannot handle an extended file register (such as A1N).

**Error detection for block numbers that do not exist**

Depending on the type of memory cassette inserted in the CPU module, an error (character area error '06H') may not be detected even when a read/write operation is performed by specifying the block numbers that do not exist. In this case, the read data is incorrect. If writing data to the CPU module, the user memory of the CPU module will be collapsed.
Check the type of memory cassette and parameter settings before using these functions.

| Memory cassette model name | Block numbers that do not cause character area error (06H) — A0J2H, A2, A3CPU | Block numbers that do not cause character area error (06H) — A2N, A3NCPU | Block numbers that do not cause character area error (06H) — A3H, AnA, AnUCPU |
|---|---|---|---|
| A3NMCA-12 | No.10 to No.11 | No.10 to No.11 | No.10 to No.11 |
| A3NMCA-18 | — | No.10 to No.28 | No.10 to No.28 |
| A3NMCA-24 | — | No.13 to No.20 | No.13 to No.28 |
| A3NMCA-40 | — | — | No.21 to No.28 |
| A3AMCA-96 | — | — | No.21 to No.48*1 |

*1 A3AMCA-96 can be used for A3A, A3U, and A4UCPU.
For details, refer to the manual of UTLP-FN1 or FNUP, or user's manual of the access target CPU module.

**Block numbers of extended file register that can be handled by A2USCPU(S1)**

The block numbers of the extended file register that can be handled by A2USCPU(S1) is as follows.

- A2USCPU: No.1 to 3
- A2USCPU-S1: No.1 to 8, No.10 to 16

**Extended file register of R/L/Q/QnACPU**

The extended file register of R/L/Q/QnACPU cannot be read/written.

#### Specification method for extended file register

The specification method differs depending on the command.

- ACPU common command: Specify with device number and block number.
- AnA/AnUCPU common command: Specify the address from device number 0 of block number 1 as a device number.
(Access with the consecutive device number of extended file register using usable number of blocks × 8192 points.)

*Figure: Device numbers specified with the ACPU common commands: 0 to 8191 Block No.1; 0 to 8191 Block No.2; 1 word*

*Figure: Device numbers specified with the AnA/AnUCPU common commands: 0 to 8191 Area of block No.1; 8192 to 16383 Area of block No.2; 16384; 1 word; Device numbers are automatically assigned in ascending order beginning from the device with block No.1 to the device with block No.256.*

**Range of block number and device number that can be specified**

An extended file register has blocks numbered from '0' to 'n' (the n differs depending on the memory cassette). Block number '0' has a number of points registered with a parameter of the CPU module, while block numbers '1' to 'n' have a register of 8192 points in each block.
However, the range that can be read from or written to the CPU module will be the range specified in the parameter of 0 block.
The range of block numbers and device numbers that can be specified differ depending on the type of memory cassette and the parameter setting in the CPU module. For details, refer to the operating manual of UTLP-FN1 or FNUP, or user's manuals of AnACPU and AnUCPU.

> **Point**
> - The AnA/AnUCPU common commands can be used for reading/writing data to the extended file register of block number 1 to 256. In addition, the commands can be used regardless of the existence of file register parameter.
> - When accessing the file register (R) set by parameter or when accessing it by specifying block number, use ACPU common commands.

**Device number (address) specification using AnA/AnUCPU common commands**

By using AnA/AnU common command function, the extended file register of block number 1 to 256 can be accessed regardless of each block number by specifying the address from device number '0' of the block number '1' as a device number. (Access with the consecutive device number of extended file register using usable number of blocks × 8192 points.)

■Device number calculation method

The calculation formula of the head device number to be specified by AnA/AnUCPU common commands is as follow:
(When specifying the device number 'm' (0 to 8191) of n block (more than 1) from the head of the block.)
Head device number = (n - 1) × 8192 + m

■Range of device number that can be specified

The range of device number that can be specified are as follows.
0 to (number of available blocks × 8192) - 1
A device number is not assigned to a block number which does not exist in the memory cassette. As shown below, the device numbers are automatically assigned by skipping block numbers that do not exist in the memory cassette.

*Figure: Device number; 0 to 8191 Area of block No.1; 8192 to 16383 Area of block No.2; Block No.3 to 9 do not exist; 16384 to 24575 Area of block No.10; 24576 to Area of block No.11*

The following table shows the range of device numbers to be specified when using AnA/AnUCPU common commands for 28 blocks per each block.

| Device number | Position of target block | Device number | Position of target block |
|---|---|---|---|
| 0 to 8191 | First block R0 to R8191 | 114688 to 122879 | 15th block R0 to R8191 |
| 8192 to 16383 | 2nd block R0 to R8191 | 122880 to 131071 | 16th block R0 to R8191 |
| 16384 to 24575 | 3rd block R0 to R8191 | 131072 to 139263 | 17th block R0 to R8191 |
| 24576 to 32767 | 4th block R0 to R8191 | 139264 to 147455 | 18th block R0 to R8191 |
| 32768 to 40959 | 5th block R0 to R8191 | 147456 to 155647 | 19th block R0 to R8191 |
| 40960 to 49151 | 6th block R0 to R8191 | 155648 to 163839 | 20th block R0 to R8191 |
| 49152 to 57343 | 7th block R0 to R8191 | 163840 to 172031 | 21st block R0 to R8191 |
| 57344 to 65535 | 8th block R0 to R8191 | 172032 to 180223 | 22nd block R0 to R8191 |
| 65536 to 73727 | 9th block R0 to R8191 | 180224 to 188415 | 23rd block R0 to R8191 |
| 73728 to 81919 | 10th block R0 to R8191 | 188416 to 196607 | 24th block R0 to R8191 |
| 81920 to 90111 | 11th block R0 to R8191 | 196608 to 204799 | 25th block R0 to R8191 |
| 90112 to 98303 | 12th block R0 to R8191 | 204800 to 212991 | 26th block R0 to R8191 |
| 98304 to 106495 | 13th block R0 to R8191 | 212992 to 221183 | 27th block R0 to R8191 |
| 106496 to 114687 | 14th block R0 to R8191 | 221184 to 229375 | 28th block R0 to R8191 |

#### Data to be specified in command

**Device number**

■ACPU common command

Specify the block number and device number with 7 digits.

- When the block number is less than 2 digits
'Block number (2 digits)' + 'R' + 'Device number (4 digits)'
- When the block number is 3 digits
'Block number (3 digits)' + 'Device number (4 digits)'

Specification example
When the block No. is less than 2 digits: `Block No.(05) → R → Device No.(8190)`
When the block No. is 3 digits: `Block No.(102) → Device No.(8190)`

■AnA/AnUCPU common command

Specify the address from device number 0 of block number 1 in 7 digits.
Page 373 Device number (address) specification using AnA/AnUCPU common commands

Specification example 1: Specifying R10 of block number 1: `0000010`
Specification example 2: Specifying R8 of block number 2: `0008200`; below it `(2-1) × 8192+8` with the labels Block No. (under 2-1) and Block points (under 8192)
"0" in the first digits (for example, the leading three digits indicated in 0008200) can be specified by spaces (20H).

#### Batch read (command: ER)

Read extended file register (R) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(E R: 45H 52H) → Message wait → Head device No. → Number of device points (Number of words)`

■Response data

The value of read device is stored in word units. (Page 72 Read data, write data)

**Data specified by request data**

■Command

ACPU common

`Command(E R)`
Bytes: `45H 52H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device No.

Specify the block number and device number of head device with 7 digits. (Page 374 Device number)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 64
- Head device No. + Number of device points - 1 ≤ Maximum device No.

**Communication example**

Read data of 2 points (R8190 and R8191) of block number 12 under the following conditions.

- Message wait: 0 ms
- Block number: 12
- Head device: R8190
- Number of device points: 2 points (2 words)

(Request data)

Request data: `Command(ER) → Message wait(0) → Head device No.(12R8190) → Number of device points (Number of words)(02)`
Bytes: `45H 52H 30H 31H 32H 52H 38H 31H 39H 30H 30H 32H`

(Response data)

Response data: `Data read(1234 (Decimal : 4660) (R8190 in NO.12) → 7ABC (Decimal : 31420) (R8191 in NO.12))`
Bytes: `31H 32H 33H 34H 37H 41H 42H 43H`

#### Batch write (command: EW)

Write extended file register (R) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(E W: 45H 57H) → Message wait → Head device No. → Number of device points → Write data for the number of device points`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

ACPU common

`Command(E W)`
Bytes: `45H 57H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device No.

Specify the block number and device number of head device with 7 digits. (Page 374 Device number)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 64
- : Head device No. + Number of device points - 1 ≤ Maximum device No.

■Write data for the number of device points

Store the data to be written in batch.

**Communication example**

Write data of 3 points (R7010 to R7012) of block number 5 under the following conditions.

- Message wait: 0 ms
- Block number: 5
- Head device: R7010
- Number of device points: 3 points (3 words)

(Request data)

Request data: `Command(EW) → Message wait(0) → Head device No.(05R7010) → Number of device points (Number of words)(03) → Write data for the number of device points(0123 (Decimal : 291) (R7010 in No.5) → ABC7 (Decimal : 21753) (R7011 in No.5) → 3322 (Decimal : 13090) (R7012 in No.5))`
Bytes: `45H 57H 30H 30H 35H 52H 37H 30H 31H 30H 30H 33H 30H 31H 32H 33H 41H 42H 43H 37H 33H 33H 32H 32H`

> **Note:** In the PDF, the write data ABC7 (bytes 41H 42H 43H 37H) is printed with "(Decimal : 21753)"; the hexadecimal ABC7 and the decimal 21753 do not correspond, and the correct value cannot be determined from the PDF. The PDF is reproduced as printed.

#### Test (random write) (command: ET)

Specify the block number and device number to the extended file register (R) in 1-point units and write them randomly.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(E T: 45H 54H) → Message wait → Number of device points → Device No. → Write data`

*Figure: Data for the number of device points*

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

ACPU common

`Command(E T)`
Bytes: `45H 54H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:

- 1 ≤ Number of device points ≤ 10

■Device number

Specify the block number and device number to be test with 7 digits. (Page 374 Device number)

■Write data

Store 4 characters per one device point.

**Communication example**

Write data of three points (3 words) randomly under the following conditions.

- Message wait: 0 ms
- Number of device points: 3 points (3 words)
- Device: Set R1234H to R1050 of block number 5, 1A1BH to R2121 of block number 7, and 506H to R3210 of block number 10.

(Request data)

Request data: `Command(ET) → Message wait(0) → Number of device points(03) → Device(05R1050) → Write data(1234) → Device(07R2121) → Write data(1A1B) → Device(10R3210) → Write data(0506)`
Bytes: `45H 54H 30H 30H 33H 30H 35H 52H 31H 30H 35H 30H 31H 32H 33H 34H 30H 37H 52H 32H 31H 32H 31H 31H 41H 31H 42H 31H 30H 52H 33H 32H 31H 30H 30H 35H 30H 36H`

#### Monitor (command: EM, ME)

The monitor data registration function registers the devices and numbers to be monitored from an external device to C24.
The monitor function reads the data of the registered devices from the CPU module and processes it in the external device.
When the batch read (ER) or direct read (NR) is performed, the read device numbers will be consecutive, however, by using this function, devices can be monitored by specifying the device numbers randomly.
The following example shows the control procedure for monitoring and registering name and number of the devices to be monitored to the C24.

**Monitoring procedure**

1. Process the monitor data registration (Edit of EM command and transmission of device specification.)
Page 379 Register monitor data (command: EM)
2. Perform read process. (Execution of ME command)
Page 380 Monitor (command: ME)
3. Process the data. (Screen display, etc.)
4. If do not change the devices to be monitored, return to step 2, and repeat the process.

> **Point**
> - When monitoring data as the procedure shown above, the monitor data registration is required. If monitoring data without registering the data, a protocol error occurs.
> - The content of registered monitor data are deleted when C24 is rebooted.
> - Five kinds of monitor data can be registered for the extended file register (EM), device memory in bit units (BM or JM), and word units (WM or QM).
> - When registering device memory of the CPU module as a monitor data from more than one external devices on the same station, the recently registered device memory will be available since the registration data is overwritten. (Monitoring device memory Page 366 Monitor (Command: BM, JM, WM, QM, MB, MJ, MN, MQ))

**Register monitor data (command: EM)**

Register the device number to be monitored in 1-point units.
Monitor the extended file register registered with EM command using the following command.
Page 380 Monitor (command: ME)

■Request data

`EM → Message wait → Number of device points → Device No. ( for the number of device points)`
Bytes: `45H 4DH`

- Command

ACPU common

`EM`
Bytes: `45H 4DH`

- Message wait: Specify the delayed time of the response transmission. (Page 347 Message wait)
- Number of device points: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- Device number: Specify the block number and device number of the device to be monitored with 7 digits. (Page 374 Device number)

> **Point**
> Specify the number of device points within the following range:
> 1 ≤ Number of device points ≤ 20

■Response data

There is no response data for this command.

■Communication example

Perform monitor data registration of 4 points (4 words) under the following conditions.

- Message wait: 0 ms
- Number of device points: 4 points (4 words)
- Device number: Register R1234 of block number 5, R2345 of block number 6, R3055 of block number 15, and R8000 of block number 17.
(When the extended file register of block number 1 to 8 and 10 to 17 exist.)

Request data: `EM → Message wait(0) → Number of device points(04) → Device No.(05R1234) → Device No.(06R2345) → Device No.(15R3055) → Device No.(17R8000)`
Bytes: `45H 4DH 30H 30H 34H 30H 35H 52H 31H 32H 33H 34H 30H 36H 52H 32H 33H 34H 35H 31H 35H 52H 33H 30H 35H 35H 31H 37H 52H 38H 30H 30H 30H`

**Monitor (command: ME)**

Monitor the registered extended file registers.

■Message format

- Request data

`ME → Message wait`
Bytes: `4DH 45H`

- Response data

`Monitoring result ( For the number of device points )`

The value of read device is stored in word units. (Page 72 Read data, write data)

■Data specified by request data

- Command

ACPU common

`ME`
Bytes: `4DH 45H`

- Message wait: Specify the delayed time of the response transmission. (Page 347 Message wait)

■Communication example

Monitor data of 4 points (4 word) specified with monitor data registration under the following conditions.

- Message wait: 0 ms
- Registered devices for monitoring: R1234 of block number 5, R2345 of block number 6, R3055 of block number 15, and 4-point of R8000 of block number 17.

(Request data)

Request data: `ME → Message wait(0)`
Bytes: `4DH 45H 30H`

(Response data)

Response data: `Monitoring result ( For the number of device points )(3501 (Decimal : 13569) → 4F5B (Decimal : 20315) → 0150 (Decimal : 366) → 1C2D (Decimal : 366))`
Bytes: `33H 35H 30H 31H 34H 46H 35H 42H 30H 31H 35H 30H 31H 43H 32H 44H`
Labels under the cells: `(R1234 in No.5) → (R2345 in No.6) → (R3055 in No.15) → (R800 in No.17)`

> **Note:** In the PDF, the decimal value printed under 0150 (30H 31H 35H 30H) and under 1C2D (31H 43H 32H 44H) is "366" for both, and the last label is printed "(R800 in No.17)" while the text above says R8000 of block number 17. The correct values cannot be determined from the PDF; the PDF print is kept.

#### Direct read (command: NR)

Read extended file register in 1-point (1 word) units by specifying the consecutive device number of extended file register.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`NR → Message wait → Head device No. → Number of device points (Number of words)`
Bytes: `4EH 52H`

■Response data

The value of read device is stored in word units. (Page 72 Read data, write data)

**Data specified by request data**

■Command

ACPU common

`NR`
Bytes: `4EH 52H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device No.

Specify the device number of head device with 7 digits. (Page 374 Device number)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:
- 1 ≤ Number of device points ≤ 64
- Head device No. + Number of device points - 1 ≤ Maximum device No.

**Communication example**

Read data of 2 points (R8190 and R8191) of block number 2 under the following conditions.

- Message wait: 0 ms
- Block number: 2
- Head device: R8190
- Number of device points: 2 points (2 words)

(Request data)

Request data: `NR → Message wait(0) → Head device No.(0016382) → Number of device points (Number of words)(02)`
Bytes: `4EH 52H 30H 30H 30H 31H 36H 33H 38H 32H 30H 32H`

(Response data)

Response data: `Data read(1234 (Decimal : 4660) → 7ABC (Decimal : 31420))`
Bytes: `31H 32H 33H 34H 37H 41H 42H 43H`
Labels under the cells: `(R8190 in No.2) → (R8191 in No.2)`

#### Direct write (command: NW)

Write extended file register in 1-point (1 word) units by specifying the consecutive device number of extended file register.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`NW → Message wait → Head device No. → Number of device points → Write data for the number of device points`
Bytes: `4EH 57H`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

ACPU common

`NW`
Bytes: `4EH 57H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Head device No.

Specify the device number of head device with 7 digits. (Page 374 Device number)

■Number of device points

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the number of device points within the following range:
- 1 ≤ Number of device points ≤ 64
- Head device No. + Number of device points - 1 ≤ Maximum device No.

■Write data for the number of device points

Store 4-digit data per one device point.

**Communication example**

Write data of 3 points under the following conditions.

- Message wait: 0 ms
- Write data: Write R8190 and R8191 of block number 12 and RO of block number 13.
(When the extended file register of block number 1 to 8 and 10 to 13 exist.)
- Number of device points: 3 points (3 words)

(Request data)

Request data: `NW → Message wait(0) → Head device No.(0090110) → Number of device points (Number of words)(03) → Write data for the number of device points(0123 (Decimal : 291) → ABC7 (Decimal : 21753) → 3322 (Decimal : 13090))`
Bytes: `4EH 57H 30H 30H 30H 39H 30H 31H 31H 30H 30H 33H 30H 31H 32H 33H 41H 42H 43H 37H 33H 33H 32H 32H`
Labels under the cells: `(R8190 in No.12) → (R8191 in No.12) → (R0 in No.13)`

> **Note:** In the PDF, the decimal value printed under ABC7 (41H 42H 43H 37H) is "21753", which does not correspond to the characters ABC7 printed above it. The correct value cannot be determined from the PDF; the PDF print is kept.

### 17.6 Read and write Buffer Memory of Special Function Module

The following examples the commands that performs data read/write to the buffer memory of MELSEC-A series special function modules.

> **Point**
> This command accesses in byte units regardless of the word/byte specification.

#### Data to be specified in command

This section explains the contents and specification methods for data items which are set in each command related to the access to the special function module buffer memory.
For details of the specification method of address and module number for commands (TR, TW), refer to the following manual.
Computer Link Module (Com. link func./Print. func.) User's Manual

**Start address**

Specify the start address of the buffer memory to be read/written.
Convert the numerical value to 5-digit ASCII code (hexadecimal), and send it from the upper digits.

■Calculation method

Calculate the start address as follows:
Start address = (Buffer memory address ×2) + the arbitrary additional value of a module
For the additional values (buffer memory start address) for each module, refer to the following section.
Page 385 Accessible modules

**Ex.**
When specifying buffer memory address 1H of AD61
(1H×2) + 80H = 82H

**Byte length**

Specify the byte length of the special function module buffer memory data to be read/written.
Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

**Read data, write data**

The read buffer memory value is stored for reading, and the data to be written is stored for writing.
This function reads/writes data in byte unit.
Page 160 Read data, write data

**Special function module No.**

Specify the last input/output signal (I/O address) of the special function module. (Specify the upper 2-digit in 3-digit representation.)
Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Special function module No. which occupies 1 slot

Special function module No. is the upper 2 digits of the input/output signal (I/O address) of the last number represented with 3-digits on the slot where the module is mounted.

*Figure: Power supply module; CPU module; Input, 16 points, 00 to 0F; Output, 32 points, 10 to 2F; Input, 16 points, 30 to 4F; Output, 32 points, 50 to 5F; Special function module, 32 points, 60 to 7F; Input, 16 points, 80 to 8F; Special function module, 32 points, 90 to AF; Output, 32 points, B0 to CF; Module No. "0AH"; Module No. "07H"; Power supply module; Input, 16 points, D0 to DF; Output, 32 points, E0 to FF; Output, 32 points, 100 to 11F; Special function module, 32 points, 120 to 13F; Output, 32 points, 140 to 15F; Module No. "13H"*

■Special function module No. which occupies 2 slots

For a special function module which occupies two slots, the number of occupied points for each slot is fixed for each module.
Special function module No. specified for the operation procedure is the upper 2 digits of the input/output signal (I/O address) of the last number represented with 3-digits of the slot assigned as a special function module on the slot.
For details of the assigned slot for each module, refer to the manual of the modules.

**Ex.**
A module that assign the first part of the slots as empty slots (AD72, A84AD, etc.)

*Figure: (Empty slot), 16 points, 00 to 0F; Special function module, 32 points, 10 to 2F; Module No. "02H"*

**Ex.**
A module that assign the last part of the slots as empty slots (A61LS, etc.)

*Figure: Special function module, 32 points, 00 to 1F; (Empty slot), 16 points, 20 to 2F; Module No. "01H"*

**Ex.**
A module in which both assignment of special function module and I/O assignment exist (A81CPU)

*Figure: Special function module, 64 points, 00 to 3F; Input module, 64 points, 40 to 7F; Module No. "03H"*

#### Accessible modules

The accessible special function modules are as follows.

| Module | Type | Additional values when calculating start address (Buffer memory start address) | Module number when module is mounted on slot 0 |
|---|---|---|---|
| High-speed counter module | AD61 (S1) | 80H | 01H |
| High-speed counter module | A1SD61, A1SD62 (E/D) | 10H | 01H |
| Analog-digital converter module | A616AD | 10H | 01H |
| Analog-digital converter module | A68AD(S2), A68ADN | 80H | 01H |
| Analog-digital converter module | A84AD | 10H | 02H |
| Analog-digital converter module | A1S64AD | 10H | 01H |
| Digital-analog converter module | A616DAI, A616DAV<br>A62DA (S1)<br>A68DAV/DAI<br>A1S62DA | 10H | 01H |
| Temperature-digital converter module | A616TD<br>A68RD3/4<br>A1S62RD3/4 | 10H | 01H |
| PID control module | A81CPU | 200H | 03H |
| Position detection module | A61LS | 80H | 01H |
| Position detection module | A62LS (S5) | 80H | 02H |
| MELSECNET/MINI master module | AJ71PT32 (S3), AJ71T32-S3<br>A1SJ71PT32-S3 | 20H | 01H |
| CC-Link system master/local module | AJ61BT11 | 2000H | 01H |
| CC-Link system master/local module | A1SJ61BT11 | 2000H | 01H |
| Multidrop link module | AJ71C22 (S1) | 1000H | 01H |
| Computer link module | AJ71C24 (S3/S6/S8) | 1000H | 01H |
| Computer link module | AJ71UC24<br>A1SJ71 (U) C24-R2<br>A1SJ71 (U) C24-PRF<br>A1SJ71 (U) C24-R4 | 400H | 01H |
| Intelligent communication module | AD51H (S3), AD51H (S3) | 800H | 02H |
| Terminal interface module | AJ71C21 (S1) | 400H | 01H |
| B/NET interface module | AJ71B62 | 20H | 01H |
| SUMINET interface module | AJ71P41 | 400H | 01H |
| Ethernet interface module | AJ71E71 (S3) | 400H | 01H |
| Ethernet interface module | A1SJ71E71 (S3) | 4000H | 01H |
| External fault diagnosis module | AD51FD (S3) | 280H | 02H |
| Graphic controller module | AD57G (S3) | 280H | 02H |
| Vision sensor module | AS25VS, AS50VS | 100H | 02H |
| Vision sensor module | AS50VS-GN | 80H | 02H |
| Memory card interface module | AD59 (S1) | 1800H | 01H |
| Positioning module | AD70 (D) (S2) | 80H | 01H |
| Positioning module | AD71 (S1/S2/S7) | 200H | 01H |
| Positioning module | AD72<br>A1SD71-S2/S7 | 200H | 02H |
| Positioning module | AD75P1/P2/P3 (S3), AD75M1/M2/M3<br>A1SD75P1/P2/P3 (S3), A1SD75M1/M2/M3 | 800H | 01H |
| Positioning module for 1 axis | A1SD70 | 80H | 01H |
| Analog input/output module | A1S63ADA | 10H | 01H |
| Temperature control module | A1S64TCTT (BW)-S1<br>A1S64TCRT (BW)-S1<br>A1S64TCTRT (BW)<br>A1S62TCTT (BW)-S2<br>A1S62TCRT (BW)-S2 | 20H | 01H |

#### Batch read (command: TR)

Read the buffer memory of a special function module.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(T R) → Message wait → Start address → Byte length → Special function module No.`
Bytes: `54H 52H`

■Response data

The value read from the buffer memory is stored.
2-digit ASCII code data is stored per 1 byte of buffer memory data.

**Data specified by request data**

■Command

ACPU common

Command: `T R`
Bytes: `54H 52H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Start address

Specify the start address of the buffer memory to be read in five digits. (Page 386 Start address)

■Byte length

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the byte length within the following ranges:

- 1 ≤ Byte length ≤ 128

■Special function module No.

Specify with 2 digits. (Page 386 Special function module No.)

> **Point**
> The content of one data may cross 2 or 3 bytes depending on the special function module. For the specifications of byte length, refer to the manual of each module.

**Communication example**

Read data of 4 bytes under the following conditions.

- Message wait: 0 ms
- Start address: 7F0H
- Byte length: 4 bytes
- Special function module No.: 13 (input/output signals are 120H to 13FH)

> **Note:** In the PDF the condition line prints "7FOH" (letter O) as the start address, while the request data diagram shows 7F0 (bytes 37H 46H 30H).

(Request data)

Request data: `Command(T R) → Message wait(0) → Start address(0 0 7 F 0) → Byte length(0 4) → Special function module No.(1 3)`
Bytes: `54H 52H 30H 30H 30H 37H 46H 30H 30H 34H 31H 33H`

(Response data)

Response data: `Data read(1 2 7 8 4 3 6 5)`
Bytes: `31H 32H 37H 38H 34H 33H 36H 35H`

Braces under the data: `7F0H` (first four bytes 1 2 7 8), `7F2H` (last four bytes 4 3 6 5)

| Start address | Buffer memory |
|---|---|
| (7F0H) | 7812H |
| (7F2H) | 6543H |

#### Batch write (command: TW)

Write data to the buffer memory of a special function module.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(T W) → Message wait → Start address → Byte length → Special function module No. → Write data`
Bytes: `54H 57H`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

ACPU common

Command: `T W`
Bytes: `54H 57H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Start address

Specify the start address of the buffer memory to be written in five digits. (Page 386 Start address)

■Byte length

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the byte length within the following ranges:

- 1 ≤ Byte length ≤ 128

■Special function module No.

Specify in two characters. (Page 386 Special function module No.)

■Write data

Store the data written to buffer memory.

> **Point**
> The content of one data may cross 2 or 3 bytes depending on the special function module. For the specifications of byte length, refer to the manual of each module.

**Communication example**

Write data of 4 bytes under the following conditions.

- Message wait: 0 ms
- Start address: 27FAH
- Byte length: 4 bytes
- Special function module No.: 13 (input/output signals are 120H to 13FH)

(Request data)

Request data: `Command(T W) → Message wait(0) → Start address(0 2 7 F A) → Byte length(0 4) → Special function module No.(1 3) → Write data(0 1 C D A B E F)`
Bytes: `54H 57H 30H 30H 32H 37H 46H 41H 30H 34H 31H 33H 30H 31H 43H 44H 41H 42H 45H 46H`

Braces under the write data: `27FAH` (0 1 C D), `27FCH` (A B E F)

| Start address | Buffer memory |
|---|---|
| (27FAH) | CD01H |
| (27FCH) | EFABH |

### 17.7 Loopback Test

A loopback test checks whether the communication function between an external device and C24 operates normally.

#### Loopback test (Command: TT)

Return the characters received from an external device to the external device unchanged.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`(unlabeled cell)(T T) → Message wait → Character length → Loopback data`
Bytes: `54H 54H`

■Response data

`Character length → Loopback data`

**Data specified by request data**

■Command

ACPU common

Command: `T T`
Bytes: `54H 54H`

■Message wait

Specify the delayed time of the response transmission. (Page 347 Message wait)

■Character length

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Specify the character length within the following range:

- 1 ≤ Character length ≤ 254

■Loopback data

Store the loopback data for character length.

**Data stored by response data**

■Character length

The same data as request data is stored.

■Loopback data

The same data as request data is stored.

> **Point**
> Specify 'FF' for PC No.

**Communication example**

Return 5-digit data received from an external device to the external device unchanged under the following conditions.

- Message wait: 0 ms
- Character length: 5 characters
- Loopback data: 'ABCDE'

(Request data)

Request data: `(unlabeled cell)(T T) → Message wait(0) → Character length(0 5) → Loopback data(A B C D E)`
Bytes: `54H 54H 30H 30H 35H 41H 42H 43H 44H 45H`

(Response data)

Response data: `Character length(0 5) → Loopback data(A B C D E)`
Bytes: `30H 35H 41H 42H 43H 44H 45H`

## 18 COMMUNICATING USING 1E FRAMES

This chapter explains the functions when accessing using 1E frame and their message format.
1E frame is compatible with the communication function supported by MELSEC-A series Ethernet interface modules.
Only the commands for 1E frame explained in this chapter can be used for 1E frame.

### 18.1 Message Format

This section explains the message format when communicating data using 1E frame.

**Message format**

■Request message

`Header → Subheader → PC No. → ACPU monitoring timer → Request data`

■Response message (Normal completion: Response data)

`Header → Subheader → End code → Response data`

■Response message (Normal completion: No response data)

`Header → Subheader → End code`

■Response message (Abnormal completion)

`Header → Subheader → End code → Abnormal code`

**Setting data**

Set the following items.

| Item | Description | Reference |
|---|---|---|
| Header | A header of Ethernet. Normally, it is added automatically. | Page 392 Header |
| Subheader | Set the command type. | Page 392 Subheader |
| PC No. | Specify the network module station No. of an access target. | Page 393 PC No. |
| ACPU monitoring timer | Set the wait time up to the completion of reading and writing processing. | Page 394 ACPU monitoring timer |
| Request data | Set the commands that indicates request content. | Page 397 Read and Write Device Memory<br>Page 418 Read and Write Extended File Register<br>Page 432 Read and Write Buffer Memory of Special Function Module |
| Response data | For the response data, store the read data for the command at normal completion. Refer to "Response data" rows of each command. | Page 397 Read and Write Device Memory<br>Page 418 Read and Write Extended File Register<br>Page 432 Read and Write Buffer Memory of Special Function Module |
| End code<br>Abnormal code | The command processing result is stored. | Page 395 End code, Abnormal code |

### 18.2 Details of Setting Data

This section explains how to specify the common data items and their content in each message.

#### Header

A header for TCP/IP and UDP/IP. A header of a request message is added on the external device side and sent. Normally, it is added automatically by an external device. A header for a response message is set automatically by E71.

#### Subheader

Set the command type.
(Page 396 Commands and Function List for 1E Frame)
The setting values for each command are as follows.

| Function | Subheader: Request message | Subheader: Response message | Reference |
|---|---|---|---|
| Read and write device memory | 00H | 80H | Page 403 Batch read in bit units (command: 00) |
| Read and write device memory | 01H | 81H | Page 405 Batch read in word units (command: 01) |
| Read and write device memory | 02H | 82H | Page 407 Batch write in bit units (command: 02) |
| Read and write device memory | 03H | 83H | Page 409 Batch write in word units (command: 03) |
| Read and write device memory | 04H | 84H | Page 410 Test in bit units (random write) (command: 04) |
| Read and write device memory | 05H | 85H | Page 412 Test in word units (random write) (command: 05) |
| Read and write device memory | 06H | 86H | Page 415 Register monitor data(command: 06, 07) |
| Read and write device memory | 07H | 87H | Page 415 Register monitor data(command: 06, 07) |
| Read and write device memory | 08H | 88H | Page 416 Monitor in bit units (command: 08) |
| Read and write device memory | 09H | 89H | Page 417 Monitor in word units (command: 09) |
| Read and write extended file register | 17H | 97H | Page 419 Batch read (command: 17) |
| Read and write extended file register | 18H | 98H | Page 421 Batch write (command: 18) |
| Read and write extended file register | 19H | 99H | Page 423 Test (random write) (command: 19) |
| Read and write extended file register | 1AH | 9AH | Page 426 Register monitor data (command: 1A) |
| Read and write extended file register | 1BH | 9BH | Page 427 Monitoring (command: 1B) |
| Read and write extended file register | 3BH | BBH | Page 428 Direct read (command: 3B) |
| Read and write extended file register | 3CH | BCH | Page 430 Direct write (command: 3C) |
| Read and write buffer memory of special function module | 0EH | 8EH | Page 434 Batch read (command: 0E) |
| Read and write buffer memory of special function module | 0FH | 8FH | Page 436 Batch write (command: 0F) |

**Setting method**

■Data communication in ASCII code

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send a 1-byte numerical value.

**Ex.**
Device memory batch read (word units)

Request message

ASCII code: `(unlabeled cell)(0 0)`
Bytes: `30H 30H`

Binary code: `(unlabeled cell)(00H)`
Bytes: `00H`

Response message

ASCII code: `(unlabeled cell)(8 0)`
Bytes: `38H 30H`

Binary code: `(unlabeled cell)(80H)`
Bytes: `80H`

#### PC No.

Specify the station No. of the access target.

**Accessing the connected station (host station)**

Specify 'FF'.

*Figure: External device; Connected station (Host station); :Access target station*

**Accessing other stations via network**

Specify the network module station No.01H to 40H (1 to 64) of the access target.

*Figure: External device; Connected station (host station); Network; Other station; :Access target station*

**Setting method**

■Data communication in ASCII code

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send a 1-byte numerical value.

**Ex.**
Accessing the connected station (host station)

ASCII code: `(unlabeled cell)(F F)`
Bytes: `46H 46H`

Binary code: `(unlabeled cell)(FFH)`
Bytes: `FFH`

When accessing other station of network station No. '3'.

ASCII code: `(unlabeled cell)(0 3)`
Bytes: `30H 33H`

Binary code: `(unlabeled cell)(03H)`
Bytes: `03H`

> **Point**
> The station No. of the network module can be checked by using the following parameters of Engineering tool.
> - GX Developer and GX Works2: "Network Parameter"
> - GX Works3: "Module Parameter"
>
> The network module station No. is set in decimal. However, the PC No. is set in hexadecimal.
> When specifying the network of the access target is required, set "Valid Module During Other Station Access" with an Engineering tool.

#### ACPU monitoring timer

Set the wait time up to the completion of reading and writing processing.

- 0000H (0): Wait infinitely (Waits until a processing is completed.)
- 0001H to FFFFH (1 to 65535): Waiting time (unit: 250 ms)

To perform normal data communication, using the timer within the setting range in the table below is recommended depending on the communication destination.

| Access target | The recommended value for monitoring timer |
|---|---|
| Connected station (host station) | 1H to 28H (0.25 s to 10 s) |
| Other station | 2H to F0H (0.5 s to 60 s) |

> **Point**
> When accessing QnACPU or ACPU for the first time, the wait time for CPU monitoring timer is required before receiving a response message because QnACPU identifies the CPU type. Be sure to set a value within the range of recommended range.

**Setting method**

■Data communication in ASCII code

Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.**
When specifying '10H' for the monitoring timer

ASCII code: `(unlabeled cell)(0 0 1 0)`
Bytes: `30H 30H 31H 30H`

Binary code: `(unlabeled cell)(10H 00H)`
Bytes: `10H 00H`

#### End code, Abnormal code

The command processing result is stored.

**End code**

At normal completion, '0' is stored.
At abnormal completion, an error code of the access target is stored.
Error code indicates the content of occurred error.
If more than one error occurs at the same time, the error code detected first is returned.
For the content of error code and its corrective action, refer to the user's manual of the module used.
QCPU User's Manual (Hardware Design, Maintenance and Inspection)
MELSEC-L CPU Module User's Manual (Hardware Design, Maintenance and Inspection)
Q Corresponding Ethernet Interface Module User's Manual (Basic)
MELSEC-L Ethernet Interface Module User's Manual (Basic)

**Abnormal code**

When an end code is '5BH', the details of abnormal content are displayed.

**Setting method**

■Data communication in ASCII code

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Use a 1-byte value.

**Ex.**
Normal completion

ASCII code: `(unlabeled cell)(0 0)`
Bytes: `30H 30H`

Binary code: `(unlabeled cell)(00H)`
Bytes: `00H`

When Error code '10H' is returned

ASCII code: `(unlabeled cell)(1 0)`
Bytes: `31H 30H`

Binary code: `(unlabeled cell)(10H)`
Bytes: `10H`

When error code '5BH' and an abnormal code '10H' (PC No. error) are returned,

ASCII code: `(unlabeled cell)(5 B 1 0)`
Bytes: `35H 42H 31H 30H`

Binary code: `(unlabeled cell)(5BH 10H)`
Bytes: `5BH 10H`

### 18.3 Commands and Function List for 1E Frame

Use the following commands for data communication using 1E frame.

| Function | Function | Function | Command | Description |
|---|---|---|---|---|
| Device memory*1 | Batch read | Bit units | 00H | Reads bit devices (X, Y, M, etc.) in 1-point units. |
| Device memory*1 | Batch read | Word units | 01H | Reads bit devices (X, Y, M, etc.) in 16-point units. |
| Device memory*1 | Batch read | Word units | 01H | Reads word devices (D, T, C, etc.) in 1-point units. |
| Device memory*1 | Batch write | Bit units | 02H | Writes bit devices (X, Y, M, etc.) in 1-point units. |
| Device memory*1 | Batch write | Word units | 03H | Writes bit devices (X, Y, M, etc.) in 16-point units. |
| Device memory*1 | Batch write | Word units | 03H | Writes word devices (D, T, C, etc.) in 1-point units. |
| Device memory*1 | Test (random write) | Bit units | 04H | Specify the devices and device numbers of bit devices (X, Y, M, etc.) in 1-point units randomly, and set/reset them. |
| Device memory*1 | Test (random write) | Word units | 05H | Specify the devices and device numbers of bit devices (X, Y, M, etc.) in 16-point units randomly, and set/reset them. |
| Device memory*1 | Test (random write) | Word units | 05H | Specify the devices and device numbers of word devices (D, T, C, etc.) in 1-point units randomly, and write them. |
| Device memory*1 | Register monitor data*2 | Bit units | 06H | Registers bit devices (X, Y, M, etc.) to be monitored in 1-point units |
| Device memory*1 | Register monitor data*2 | Word units | 07H | Registers bit devices (X, Y, M, etc.) to be monitored in 16-point units. |
| Device memory*1 | Register monitor data*2 | Word units | 07H | Registers word devices (D, T, C, etc.) to be monitored in 1-point units. |
| Device memory*1 | Monitoring | Bit units | 08H | Monitors the devices registered by monitor data registration. |
| Device memory*1 | Monitoring | Word units | 09H | Monitors the devices registered by monitor data registration. |
| Extended file register | Batch read | Batch read | 17H | Reads extended file register (R) in 1-point units. |
| Extended file register | Batch write | Batch write | 18H | Writes extended file register (R) in 1-point units. |
| Extended file register | Test (random write) | Test (random write) | 19H | Specify the block numbers and device numbers in 1-point units and write them to the extended file register (R) randomly. |
| Extended file register | Register monitor data*2 | Register monitor data*2 | 1AH | Registers extended file register (R) in 1-point units. |
| Extended file register | Monitoring | Monitoring | 1BH | Monitors extended file register (R) with monitor data registered. |
| Extended file register | Direct read | Direct read | 3BH | Reads extended file register in 1-point units with direct designation. |
| Extended file register | Direct write | Direct write | 3CH | Writes extended file register in 1-point units by direct specification. |
| Special function module | Batch read | Batch read | 0EH | Reads the content in the buffer memory of a special function module. |
| Special function module | Batch write | Batch write | 0FH | Writes data to the buffer memory of a special function module. |

*1 Use the dedicated commands for extended registers when performing extended file registers read/write.
*2 The devices that can be registered to E71 is for 1 command out of the three types of commands (06H, 07H, 1AH).
The specified device recently used by any of the above commands is registered to E71.

### 18.4 Read and Write Device Memory

This section explains the specification content and examples of the request data and the response data when reading and writing device memory.
For the message formats other than request data and response data, refer to the following sections.
Page 391 Message Format, Page 391 Details of Setting Data

> **Point**
> To read and write the extended file register, use the commands dedicated to the extended file register.
> Page 418 Read and Write Extended File Register

#### Considerations

**Considerations when reading/writing data to module other than ACPU module**

■Accessible devices

Only the devices with the same names that exist in ACPU can be accessed within the device range of AnACPU.
The following devices cannot be accessed from the external devices:

- Added devices
- Latch relay (L) and step relay (S)
- File register (R)

■Special relays and special registers

Special relays and special registers can be accessed within the following range.

- Access SM1000 to SM1255 by specifying M9000 to M9255.
- Access SD1000 to SD1255 by specifying D9000 to D9255.

■Universal model QCPU

Use the Universal model QCPU with a serial number whose first five digits are '10102' or later.
When the module with the serial number whose first five digits are '10101' or earlier, access using 3E frame or 4E frame.
When accessing the built-in Ethernet port of the CPU module, refer to the following manual.
QnUCPU User's Manual (Communication via Built-in Ethernet Port)

#### Data to be specified in command

**Device codes and device numbers**

The settings of each device when reading/writing device memory can be performed using device code and device number as shown in the following figure.
Specify the device to be accessed by a device code and a device number.
The data order differs between ASCII code or binary code.

ASCII code: `Device code(4 digits) → Device number(8 digits)`
Binary code: `Device number(4 bytes) → Device code(2 bytes)`

■Data communication in ASCII code

- Device code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- Device number: Convert the numerical value to 8-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

- Device number: Send 4-byte numerical values from the lower byte (L: bits 0 to 7).
- Device code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.**
Data register (D) 1234 (device number is decimal)
Convert a device number to hexadecimal. '1234' (decimal)→'4D2' (hexadecimal)

ASCII code: `(unlabeled cell)(4420) → (unlabeled cell)(000004D2)`
Bytes: `34H 34H 32H 30H 30H 30H 30H 30H 30H 34H 44H 32H`

Binary code: `(unlabeled cell)(D2H 04H 00H 00H) → (unlabeled cell)(20H 44H)`
Bytes: `D2H 04H 00H 00H 20H 44H`

For the values of each device code, refer to the following section.
□: Space

| Device name | Device name | Symbol | Type | Representation | Device code | Device code |
|---|---|---|---|---|---|---|
| Input | Input | X | Bit | Hexadecimal | X□ | 5820H |
| Output | Output | Y | Bit | Hexadecimal | Y□ | 5920H |
| Internal relay (include in latch relay and step relay) | Internal relay (include in latch relay and step relay) | M/L/S | Bit | Decimal | M□ | 4D20H |
| Annunciator | Annunciator | F | Bit | Decimal | F□ | 4620H |
| Link relay | Link relay | B | Bit | Hexadecimal | B□ | 4220H |
| Timer | Current value | T | Word | Decimal | TN | 544EH |
| Timer | Contact | T | Bit | Decimal | TS | 5453H |
| Timer | Coil | T | Bit | Decimal | TC | 5443H |
| Counter | Current value | C | Word | Decimal | CN | 434EH |
| Counter | Contact | C | Bit | Decimal | CS | 4353H |
| Counter | Coil | C | Bit | Decimal | CC | 4343H |
| Data register | Data register | D | Word | Decimal | D□ | 4420H |
| Link register | Link register | W | Word | Hexadecimal | W□ | 5720H |
| File register | File register | R | Word | Decimal | R□ | 5220H |

Access the devices within the range that can be used in the access target CPU.
For the accessible device range, refer to the following section.
Page 399 Accessible device range

> **Point**
> - For word unit specification, the head device number of bit device must be specified in multiple of 16.
> - For special relay M9000 or later, (9000 + multiple of 16) can be specified.

**Accessible device range**

■List of devices (ACPU other than AnU)

Specify the device number within the range of the access target module.
○: Accessible, —: No device

| Device | Device | Device range | Device number | A1S<br>A1SH<br>A1SJ<br>A1SJH<br>A1<br>A1N | A2S<br>A2SH<br>A2<br>A2N<br>A2C<br>A2CJ<br>A0J2H | A2-S1<br>A2N-S1 | A3<br>A3N | A2A | A2A-S1 | A3A |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Input | Input | X0 to X0FF | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Input | Input | X100 to X1FF | 0100H to 01FFH | — | ○ | ○ | ○ | ○ | ○ | ○ |
| Input | Input | X200 to X3FF | 0200H to 03FFH | — | — | ○ | ○ | — | ○ | ○ |
| Input | Input | X400 to X7FF | 0400H to 07FFH | — | — | — | ○ | — | — | ○ |
| Output | Output | Y0 to Y0FF | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Output | Output | Y100 to Y1FF | 0100H to 01FFH | — | ○ | ○ | ○ | ○ | ○ | ○ |
| Output | Output | Y200 to Y3FF | 0200H to 03FFH | — | — | ○ | ○ | — | ○ | ○ |
| Output | Output | Y400 to Y7FF | 0400H to 07FFH | — | — | — | ○ | — | — | ○ |
| Internal relay<br>(Including latch relay and step relay) | Internal relay<br>(Including latch relay and step relay) | M0 to M2047 | 0000H to 07FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Internal relay<br>(Including latch relay and step relay) | Internal relay<br>(Including latch relay and step relay) | M2048 to M8191 | 0800H to 1FFFH | — | — | — | — | ○ | ○ | ○ |
| Internal relay<br>(Including latch relay and step relay) | Internal relay<br>(Including latch relay and step relay) | M9000 to M9255 | 2328H to 2427H | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Annunciator | Annunciator | F0 to F255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Annunciator | Annunciator | F256 to F2047 | 0100H to 07FFH | — | — | — | — | ○ | ○ | ○ |
| Link relay | Link relay | B0 to B3FF | 0000H to 03FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Link relay | Link relay | B400 to BFFF | 0400H to 0FFFH | — | — | — | — | ○ | ○ | ○ |
| Timer | Current value | T0 to T255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Timer | Current value | T256 to T2047 | 0100H to 07FFH | — | — | — | — | ○ | ○ | ○ |
| Timer | Contact | T0 to T255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Timer | Contact | T256 to T2047 | 0100H to 07FFH | — | — | — | — | ○ | ○ | ○ |
| Timer | Coil | T0 to T255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Timer | Coil | T256 to T2047 | 0100H to 07FFH | — | — | — | — | ○ | ○ | ○ |
| Counter | Current value | C0 to C255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Counter | Current value | C256 to C1023 | 0100H to 03FFH | — | — | — | — | ○ | ○ | ○ |
| Counter | Contact | C0 to C255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Counter | Contact | C256 to C1023 | 0100H to 03FFH | — | — | — | — | ○ | ○ | ○ |
| Counter | Coil | C0 to C255 | 0000H to 00FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Counter | Coil | C256 to C1023 | 0100H to 03FFH | — | — | — | — | ○ | ○ | ○ |
| Data register | Data register | D0 to D1023 | 0000H to 03FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Data register | Data register | D1024 to D6143 | 0400H to 17FFH | — | — | — | — | ○ | ○ | ○ |
| Data register | Data register | D9000 to D9255 | 2328H to 2427H | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Link register | Link register | W0 to W3FF | 0000H to 03FFH | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| Link register | Link register | W400 to WFFF | 0400H to 0FFFH | — | — | — | — | ○ | ○ | ○ |
| File register | File register | R0 to R4095 | 0000H to 0FFFH | — | ○ | ○ | ○ | ○ | ○ | ○ |
| File register | File register | R4096 to R8191 | 1000H to 1FFFH | — | — | — | ○ | ○ | ○ | ○ |

> **Restriction**
> - Do not write data to the devices which cannot be written in the range of the special relays (M9000 to M9255) and special registers (D9000 to D9255). For details on the special relays and special registers, refer to manual of ACPU to be accessed.
> - For L and S, perform accessing by specifying 'M' (For example, to access L100, specify M100.)

■List of devices (AnUCPU, QnACPU)

Specify the device within the range of AnACPU.
Page 397 Considerations when reading/writing data to module other than ACPU module
○: Accessible, ×: Not accessible, —: No device

| Device | Device | Device range | Device number | A2US<br>A2U | A2US-S1<br>A2USH-S1<br>A2U-S1 | A3U<br>A4U | Q2A<br>Q2AS<br>Q2ASH | Q2A-S1<br>Q2AS-S1<br>Q2ASH-S1 | Q3A<br>Q4A<br>Q4AR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Input | Input | X0 to X1FF | 0000H to 01FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Input | Input | X200 to X3FF | 0200H to 03FFH | — | ○ | ○ | — | ○ | ○ |
| Input | Input | X400 to X7FF | 0400H to 07FFH | — | — | ○ | — | — | ○ |
| Output | Output | Y0 to Y1FF | 0000H to 01FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Output | Output | Y200 to Y3FF | 0200H to 03FFH | — | ○ | ○ | — | ○ | ○ |
| Output | Output | Y400 to Y7FF | 0400H to 07FFH | — | — | ○ | — | — | ○ |
| Internal relay<br>(Including latch relay and step relay) | Internal relay<br>(Including latch relay and step relay) | M0 to M8191 | 0000H to 1FFFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Internal relay<br>(Including latch relay and step relay) | Internal relay<br>(Including latch relay and step relay) | M9000 to M9255<br>(SM1000 to SM1255) | 2328H to 2427H | ○ | ○ | ○ | ○ | ○ | ○ |
| Annunciator | Annunciator | F0 to F2047 | 0000H to 07FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Link relay | Link relay | B0 to BFFF | 0000H to 0FFFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Timer | Current value | T0 to T2047 | 0000H to 07FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Timer | Contact | T0 to T2047 | 0000H to 07FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Timer | Coil | T0 to T2047 | 0000H to 07FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Counter | Current value | C0 to C1023 | 0000H to 03FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Counter | Contact | C0 to C1023 | 0000H to 03FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Counter | Coil | C0 to C1023 | 0000H to 03FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Data register | Data register | D0 to D6143 | 0000H to 17FFH | ○ | ○ | ○ | ○ | ○ | ○ |
| Data register | Data register | D9000 to D9255<br>(SD1000 to SD1255) | 2328H to 2427H | ○ | ○ | ○ | ○ | ○ | ○ |
| Link register | Link register | W0 to WFFF | 0000H to 0FFFH | ○ | ○ | ○ | ○ | ○ | ○ |
| File register | File register | R0 to R8191 | 0000H to 1FFFH | ○ | ○ | ○ | × | × | × |

> **Restriction**
> - Do not write data to the devices which cannot be written in the range of the special relays (M9000 to M9255) and special registers (D9000 to D9255). For details on the special relays and special registers, refer to the programming manual of ACPU.
> - For L and S, perform accessing by specifying 'M' (For example, to access L100, specify M100.)

■Device list (QCPU, LCPU, safety CPU)

Specify the device within the range of AnACPU.
Page 397 Considerations when reading/writing data to module other than ACPU module
○: Accessible, ×: Not accessible, —: No device

| Device | Device | Device range | Device number | Basic model QCPU<br>Safety CPU | QCPU other than left<br>LCPU |
| --- | --- | --- | --- | --- | --- |
| Input | Input | X0 to X7FF | 0000H to 07FFH | ○ | ○ |
| Output | Output | Y0 to Y7FF | 0000H to 07FFH | ○ | ○ |
| Internal relay | Internal relay | M0 to M8191 | 0000H to 1FFFH | ○ | ○ |
| Internal relay | Internal relay | M9000 to M9255<br>(SM1000 to SM1255) | 2328H to 2427H | — | ○ |
| Annunciator | Annunciator | F0 to F1023 | 0000H to 03FFH | ○ | ○ |
| Annunciator | Annunciator | F1024 to F2047 | 0400H to 07FFH | — | ○ |
| Link relay | Link relay | B0 to B7FF | 0000H to 07FFH | ○ | ○ |
| Link relay | Link relay | B800 to BFFF | 0800H to 0FFFH | — | ○ |
| Timer | Current value | T0 to T511 | 0000H to 01FFH | ○ | ○ |
| Timer | Current value | T512 to T2047 | 0200H to 07FFH | — | ○ |
| Timer | Contact | T0 to T511 | 0000H to 01FFH | ○ | ○ |
| Timer | Contact | T512 to T2047 | 0200H to 07FFH | — | ○ |
| Timer | Coil | T0 to T511 | 0000H to 01FFH | ○ | ○ |
| Timer | Coil | T512 to T2047 | 0200H to 07FFH | — | ○ |
| Counter | Current value | C0 to C511 | 0000H to 01FFH | ○ | ○ |
| Counter | Current value | C512 to C1023 | 0200H to 03FFH | — | ○ |
| Counter | Contact | C0 to C511 | 0000H to 01FFH | ○ | ○ |
| Counter | Contact | C512 to C1023 | 0200H to 03FFH | — | ○ |
| Counter | Coil | C0 to C511 | 0000H to 01FFH | ○ | ○ |
| Counter | Coil | C512 to C1023 | 0200H to 03FFH | — | ○ |
| Data register | Data register | D0 to D6143 | 0000H to 17FFH | ○ | ○ |
| Data register | Data register | D9000 to D9255<br>(SD1000 to SD1255) | 2328H to 2427H | — | ○ |
| Link register | Link register | W0 to W7FF | 0000H to 07FFH | ○ | ○ |
| Link register | Link register | W800 to WFFF | 0800H to 0FFFH | — | ○ |
| File register | File register | R0 or more | 0000H or more | × | × |

**Number of device points**

Specify the number of device points to be read or written.
Specify the number of device points in one command within the device points that can be processed in one communication.
Page 466 Number of Processing per One Communication
Specify '00' for 256 points.

■Data communication in ASCII code

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
Use capitalized code for alphabetical letter.

■Data communication in binary code

Send a 1-byte numerical value.

**Ex.**
5 points, 20 points, 256 points

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 5 points | 0 5<br>30H 35H | 05H |
| 20 points | 1 4<br>31H 34H | 14H |
| 256 points | 0 0<br>30H 30H | 00H |

**Read data, write data**

The read data is stored for reading, and the data to be written is stored for writing. The data order differs between bit units or word units.

#### Batch read in bit units (command: 00)

Read bit devices (X, Y, M, etc.) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.
Specify the command type by subheader. (Page 392 Subheader)

■Request data

`Head device → Number of device points → Fixed value`

■Response data

The data for the number of device points are stored.
(Page 402 Read data, write data)

> **Point**
> For ASCII code, when the number of device points are specified in an odd number, one byte of dummy data (30H) will be added to the response data. For example, if a data for three points are read, data for four points is returned. The last byte will be a dummy data.

**Data specified by request data**

■Head device

Specify the head device number of bit devices to be read. (Page 397 Device codes and device numbers)

■Number of device points

Specify the number of the bit device points to be read.
Specify '00H' when the number of device points is to 256 points. (Page 402 Number of device points)

■Fixed value

Fixed to '0'.

ASCII code: `Fixed value(00)`
Bytes: `30H 30H`

Binary code: `Fixed value(00H)`
Bytes: `00H`

**Communication example**

Read the bit devices in the CPU module with E71 mounted under the following conditions.

- Head device: M100
- Number of device points: 12 points

■Data communication in ASCII code

(Request data)

Request data: `Head device(4D2000000064) → Number of device points(0C) → (unlabeled cell)(00)`
Bytes: `34H 44H 32H 30H 30H 30H 30H 30H 30H 30H 36H 34H 30H 43H 30H 30H`

(Response data)

Response data: `Data for the number of specified device points(1 0 1 0 1 0 1 0 1 0 1 0)`
Bytes: `31H 30H 31H 30H 31H 30H 31H 30H 31H 30H 31H 30H`
(M100) to (M111)

■Data communication in binary code

(Request data)

Request data: `Head device(64H 00H 00H 00H 20H 4DH) → Number of device points(0CH) → (unlabeled cell)(00H)`
Bytes: `64H 00H 00H 00H 20H 4DH 0CH 00H`

(Response data)

Response data: `Data for the number of specified device points(10H 10H 10H 10H 10H 10H)`
Bytes: `10H 10H 10H 10H 10H 10H`
Expanded bits: `1 0 1 0 1 0 1 0 1 0 1 0`
(M100) to (M111)

#### Batch read in word units (command: 01)

Reads bit devices (X, Y, M, etc.) in 16-point units.
Reads word devices (D, T, C, etc.) in 1-point units

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Head device → Number of device points → Fixed value`

■Response data

The data for the number of device points are stored.
The data order differs between ASCII code or binary code. (Page 402 Read data, write data)

**Data specified by request data**

■Head device

Specify the head device of the device to be read. (Page 397 Device codes and device numbers)

■Number of device points

Specify the device number to be read.
Specify '00H' when the number of device points is to 256 points. (Page 402 Number of device points)
When specifying bit devices, set the head device No. in multiples of 16 (0, 16, ... in decimal notation).

■Fixed value

Fixed to '0'.

ASCII code: `Fixed value(00)`
Bytes: `30H 30H`

Binary code: `Fixed value(00H)`
Bytes: `00H`

**Communication example**

Read the bit devices in the CPU module with E71 mounted under the following conditions.

- Head device: Y40
- Number of device points: 32 points (2 bytes)

■Data communication in ASCII code

(Request data)

Request data: `Head device(592000000040) → Number of device points(02) → (unlabeled cell)(00)`
Bytes: `35H 39H 32H 30H 30H 30H 30H 30H 30H 30H 34H 30H 30H 32H 30H 30H`

(Response data)

Response data: `Data for the number of specified device points(8 2 9 D 5 5 3 E)`
Bytes: `38H 32H 39H 44H 35H 35H 33H 45H`
Expanded bits: `82H(1 0 0 0 0 0 1 0) | 9DH(1 0 0 1 1 1 0 1) | 55H(0 1 0 1 0 1 0 1) | 3EH(0 0 1 1 1 1 1 0)`
(Y4F) to (Y48) | (Y47) to (Y40) | (Y5F) to (Y58) | (Y57) to (Y50)

■Data communication in binary code

(Request data)

Request data: `Head device(40H 00H 00H 00H 20H 59H) → Number of device points(02H) → (unlabeled cell)(00H)`
Bytes: `40H 00H 00H 00H 20H 59H 02H 00H`

(Response data)

Response data: `Data for the number of specified device points(9DH 82H 3EH 55H)`
Bytes: `9DH 82H 3EH 55H`
Expanded bits: `9DH(1 0 0 1 1 1 0 1) | 82H(1 0 0 0 0 0 1 0) | 3EH(0 0 1 1 1 1 1 0) | 55H(0 1 0 1 0 1 0 1)`
(Y47) to (Y40) | (Y4F) to (Y48) | (Y57) to (Y50) | (Y5F) to (Y58)

#### Batch write in bit units (command: 02)

Write bit devices (X, Y, M, etc.) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Head device → Number of device points → Fixed value → Write data`

■Response data

There is no response data for this command.

**Data specified by request data**

■Head device

Specify the head device of the bit device to be written. (Page 397 Device codes and device numbers)

■Fixed value

Fixed to '0'.

ASCII code: `Fixed value(00)`
Bytes: `30H 30H`

Binary code: `Fixed value(00H)`
Bytes: `00H`

■Number of device points

Specify the bit device points to be written.
Specify '00H' when the number of device points is to 256 points. (Page 402 Number of device points)

■Write data

Stored data for the number of device points to be written.

- 0 (30H): OFF
- 1 (31H): ON

**Communication example**

Write the bit devices in the CPU module with E71 mounted under the following conditions.

- Head device: M50
- Number of device points: 12 points

■Data communication in ASCII code

(Request data)

Request data: `Head device(4D2000000032) → Number of device points(0C) → (unlabeled cell)(00) → Data for the number of specified device points(1 1 … 0 1)`
Bytes: `34H 44H 32H 30H 30H 30H 30H 30H 30H 30H 33H 32H 30H 43H 30H 30H 31H 31H … 30H 31H`
(M50) to (M61)

> **Note:** In the PDF the characters above the first two bytes of the head device (34H 44H) are printed as "4 4" instead of "4 D", and the characters above the number of device points bytes (30H 43H) are printed as "0 3" instead of "0 C". The corrected characters follow from the bytes 44H and 43H and from the 12 points (0CH) in the binary example.

■Data communication in binary code

(Request data)

Request data: `Head device(32H 00H 00H 00H 20H 4DH) → Number of device points(0CH) → (unlabeled cell)(00H) → Data for the number of specified device points(01H 11H 01H 00H 00H 01H)`
Bytes: `32H 00H 00H 00H 20H 4DH 0CH 00H 01H 11H 01H 00H 00H 01H`
Expanded bits: `0 1 1 1 0 1 0 0 0 0 0 1`
(M50) to (M61)

#### Batch write in word units (command: 03)

Write bit devices (X, Y, M, etc.) in 16-point units.
Write word devices (D, T, C, etc.) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Head device → Number of device points → Fixed value → Write data`

■Response data

There is no response data for this command.

**Data specified by request data**

■Head device

Specify the head device of the device to be written.

■Number of device points

Specify the number of device points to be written.
Specify '00H' when the number of device points is to 256 points.
When specifying bit devices, set the head device No. in multiples of 16 (0, 16, ... in decimal notation).

■Fixed value

Fixed to '0'.

ASCII code: `Fixed value(00)`
Bytes: `30H 30H`

Binary code: `Fixed value(00H)`
Bytes: `00H`

■Write data

Stored data for the number of device points to be written.
The data order differs between ASCII code or binary code. (Page 402 Read data, write data)

**Communication example**

Write the devices in the CPU module with E71 mounted under the following conditions.

- Head device: D100
- Number of device points: 3 points

■Data communication in ASCII code

(Request data)

Request data: `Head device(442000000064) → Number of device points(03) → (unlabeled cell)(00) → Data for the number of specified device points(123498760109)`
Bytes: `34H 34H 32H 30H 30H 30H 30H 30H 30H 30H 36H 34H 30H 33H 30H 30H 31H 32H 33H 34H 39H 38H 37H 36H 30H 31H 30H 39H`
Data per device: `(D100)(1234) | (D101)(9876) | (D102)(0109)`

■Data communication in binary code

(Request data)

Request data: `Head device(64H 00H 00H 00H 20H 44H) → Number of device points(03H) → (unlabeled cell)(00H) → Data for the number of specified device points(34H 12H 76H 98H 09H 01H)`
Bytes: `64H 00H 00H 00H 20H 44H 03H 00H 34H 12H 76H 98H 09H 01H`
Data per device: `(D100)(34H 12H) | (D101)(76H 98H) | (D102)(09H 01H)`

#### Test in bit units (random write) (command: 04)

Specify the devices and device numbers of bit devices (X, Y, M, etc.) in 1-point units randomly, and set/reset them.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

There is no request data for this command.

`Number of device points (n points) → Fixed value → Specified device (first point) → ON/OFF specification (first point) → ... → Specified device (nth point) → ON/OFF specification (nth point)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Number of device points

Specify the number of points of the bit device to be set/reset. (Page 402 Number of device points)

■Fixed value

Fixed to '0'.

| ASCII code | Binary code |
|---|---|
| `0` `0` (`30H` `30H`) | `00H` |

■Specified device

Specify the bit devices to be set/reset. (Page 397 Device codes and device numbers)

■ON/OFF specification

Specify set/reset.

| Processing | ASCII code | Binary code |
|---|---|---|
| Reset | `0` `0` (`30H` `30H`) | `00H` |
| Set | `0` `1` (`30H` `31H`) | `01H` |

**Communication example**

Set/reset the bit devices in CPU module with E71 mounted under the following conditions.

- Number of device points: 3 points
- Data for the number of specified device points: Specify Y94 to ON, M60 to OFF, and B26 to ON.

■Data communication in ASCII code

(Request data)

Request data: `Number of device points(0300) → Specified device (first point)(592000000094) → ON/OFF specification (first point)(01) → Specified device (second point)(4D200000003C) → ON/OFF specification (second point)(00) → Specified device (third point)(422000000026) → ON/OFF specification (third point)(01)`
Bytes: `30H 33H 30H 30H 35H 39H 32H 30H 30H 30H 30H 30H 30H 30H 39H 34H 30H 31H 34H 44H 32H 30H 30H 30H 30H 30H 30H 30H 33H 43H 30H 30H 34H 32H 32H 30H 30H 30H 30H 30H 30H 30H 32H 36H 30H 31H`

Device labels printed under the cells: (Y94), (M60), (B26); printed under the ON/OFF specification cells: (ON), (OFF), (ON).

■Data communication in binary code

(Request data)

Request data: `Number of device points(03H 00H) → Specified device (first point)(Y94: 94H 00H 00H 00H 20H 59H) → ON/OFF specification (first point)(ON: 01H) → Specified device (second point)(M60: 3CH 00H 00H 00H 20H 4DH) → ON/OFF specification (second point)(OFF: 00H) → Specified device (third point)(B26: 26H 00H 00H 00H 20H 42H) → ON/OFF specification (third point)(ON: 01H)`
Bytes: `03H 00H 94H 00H 00H 00H 20H 59H 01H 3CH 00H 00H 00H 20H 4DH 00H 26H 00H 00H 00H 20H 42H 01H`

#### Test in word units (random write) (command: 05)

Specify the devices and device numbers of bit devices (X, Y, M, etc.) in 16-point units randomly, and set/reset them.
Specify the devices and device numbers of word devices (D, T, C, etc.) in 1-point units randomly, and write them.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Number of device points (n points) → Fixed value → Specified device (first point) → ON/OFF specification (first point) → ... → Specified device (nth point) → ON/OFF specification (nth point)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Number of device points

Specify the number of device points to be set/reset. (Page 402 Number of device points)
When specifying bit devices, set the head device No. in multiples of 16 (0, 16, ... in decimal notation).

■Fixed value

Fixed to '0'.

| ASCII code | Binary code |
|---|---|
| `0` `0` (`30H` `30H`) | `00H` |

■Specified device

Specify the device to be set/reset. (Page 397 Device codes and device numbers)

■ON/OFF specification

Store the data to be written.

**Communication example**

Set/reset the devices in the CPU module with E71 mounted under the following condition.

- Number of device points: 3 points
- Data for the number of specified device points: Specify Y80 to 8F to ON/OFF, W26 to '1234H', and the current value of C18 to '50H'.

■Data communication in ASCII code

(Request data)

Request data: `Number of device points(0300) → Specified device (first point)(592000000080) → ON/OFF specification (first point)(7B29) → Specified device (second point)(572000000026) → ON/OFF specification (second point)(1234) → Specified device (third point)(434E00000012) → ON/OFF specification (third point)(0050)`
Bytes: `30H 33H 30H 30H 35H 39H 32H 30H 30H 30H 30H 30H 30H 30H 38H 30H 37H 42H 32H 39H 35H 37H 32H 30H 30H 30H 30H 30H 30H 30H 32H 36H 31H 32H 33H 34H 34H 33H 34H 45H 30H 30H 30H 30H 30H 30H 31H 32H 30H 30H 35H 30H`

Device labels printed under the cells: (Y80), (W26), (Current value of C18); printed under the ON/OFF specification cells: (1234H), (50H).

Bit expansion of the ON/OFF specification (first point) 7B29: `0 1 1 1 1 0 1 1 0 0 1 0 1 0 0 1`, where the first 8 bits are (Y8F) to (Y88) and the last 8 bits are (Y87) to (Y80).

■Data communication in binary code

(Request data)

Request data: `Number of device points(03H 00H) → Specified device (first point)(Y80: 80H 00H 00H 00H 20H 59H) → ON/OFF specification (first point)(29H 7BH) → Specified device (second point)(W26: 26H 00H 00H 00H 20H 57H) → ON/OFF specification (second point)(1234H: 34H 12H) → Specified device (third point)(Current value of C18: 12H 00H 00H 00H 4EH 43H) → ON/OFF specification (third point)(50H: 50H 00H)`
Bytes: `03H 00H 80H 00H 00H 00H 20H 59H 29H 7BH 26H 00H 00H 00H 20H 57H 34H 12H 12H 00H 00H 00H 4EH 43H 50H 00H`

Bit expansion of the ON/OFF specification (first point) 29H 7BH: `0 0 1 0 1 0 0 1 0 1 1 1 1 0 1 1`, where the first 8 bits are (Y87) to (Y80) and the last 8 bits are (Y8F) to (Y88).

#### Monitor device memory (command: 06, 07, 08, 09)

The ON/OFF status or contents of devices in the CPU module can be monitored from an external device by registering the devices and device numbers to be monitored to E71 in advance and executing the monitor command from the external device.
When reading device memory in batch, the read device numbers will be consecutive. However, when reading them using the monitor command, the devices can be monitored randomly by specifying the arbitrary devices and device numbers.
For monitoring the extended file register, refer to the following section.
Page 425 Monitor extended file registers (command: 1A, 1B)

**Procedure for monitoring**

1. Register the devices to be monitored from the external device to E71 by registering monitor data.
2. Execute the read processing by a monitor command.
3. Process the data.
4. If do not change the devices to be monitored, return to step 2, and repeat the process.

> **Point**
> - When monitoring data as the procedure shown above, the monitor data registration is required. If monitoring data without registering the data, a protocol error occurs.
> - The content of registered monitor data is deleted when turning the power OFF or the resetting the CPU module.
> - There are three types of commands for monitor data registration; expansion file register, device memory bit unit, and device memory word unit. The recently registered one command out of three types of commands can be registered to E71.
> - When registering device memory of the CPU module as a monitoring data from more than one external devices on the same station, the recently registered device memory will be available since the registration data is overwritten.

**Register monitor data(command: 06, 07)**

Registers bit devices (X, Y, M, etc.) to be monitored in 1-point units. (Command: 06)
Registers bit devices (X, Y, M, etc.) to be monitored in 16-point units. (Command: 07)
Registers word devices (D, T, C, etc.) to be monitored in 1-point units. (Command: 07)

■Request data

`Number of device points (n points) → Fixed value → Device number (first point) → ... → Device number (nth point)`

- Number of device points: Specify the number of devices to be registered as a monitoring data. (Page 402 Number of device points)
- Fixed value: '0'

| ASCII code | Binary code |
|---|---|
| `0` `0` (`30H` `30H`) | `00H` |

- Device number: Specify the device number to be registered as a monitor data. (Page 397 Device codes and device numbers)

> **Point**
> When specifying bit devices at monitor data registration in word unit, set the device numbers in multiples of 16 ( 0, 16, ... in decimal notation).

■Response data

There is no response data for this command.

■Communication example

Register devices as a monitoring data in the CPU module with E71 mounted under the following conditions.

- Number of device points: 3 points
- Device number: Y46, M12, B2C

(Request data)

(ASCII code)

Request data: `Number of device points(0300) → Device number(592000000046) → Device number(4D200000000C) → Device number(422E0000002C)`
Bytes: `30H 33H 30H 30H 35H 39H 32H 30H 30H 30H 30H 30H 30H 30H 34H 36H 34H 44H 32H 30H 30H 30H 30H 30H 30H 30H 30H 43H 34H 32H 32H 45H 30H 30H 30H 30H 30H 30H 32H 43H`

Device labels printed under the cells: (Y46), (M12), (B2C).

(Binary code)

Request data: `Number of device points(03H 00H) → Device number(Y46: 46H 00H 00H 00H 20H 59H) → Device number(M12: 0CH 00H 00H 00H 20H 4DH) → Device number(B2C: 2CH 00H 00H 00H 20H 42H)`
Bytes: `03H 00H 46H 00H 00H 00H 20H 59H 0CH 00H 00H 00H 20H 4DH 2CH 00H 00H 00H 20H 42H`

**Monitor in bit units (command: 08)**

Monitor the bit devices for which monitor data is registered.

■Request data

There is no request data for this command.

■Response data

The value of the read device is stored in byte units.
The data order differs between ASCII code or binary code.

> **Point**
> If the number of device points registered to be monitored is an odd number, dummy data 0 (30H) is added when the monitoring is executed. For example, if the number of device points registered to be monitored is three points, data for four points is returned. The last byte will be a dummy data.

■Communication example

Monitor the devices registered with monitor data registration in the CPU module with E71 mounted under the following conditions.

- Number of registered device points: 3 points
- Number of registered device numbers: Y46, M12, B2C

(Response data)

(ASCII code)

Response data: `Subheader(88) → End code(00) → Data for the number of specified device points(1010)`
Bytes: `38H 38H 30H 30H 31H 30H 31H 30H`

Labels printed under the data cells: (Y46) under the first 1, (M12) under the 0, (B2C) under the second 1, "Dummy data" under the last 0 (shaded cell).

(Binary code)

Response data: `Subheader(88H) → End code(00H) → Data for the number of specified device points(10H 10H)`
Bytes: `88H 00H 10H 10H`

Bit expansion of the data: `1 0 1 0`, labels (Y46), (M12), (B2C), and "Dummy data" for the last 0 (shaded cell).

**Monitor in word units (command: 09)**

Monitor word devices and bit devices (16 point units) which are registered as a monitor data.

■Request data

There is no request data for this command.

■Response data

The value of the read device is stored in word units.
The data order differs between ASCII code or binary code.

■Communication example

Monitor the devices registered with monitor data registration in the CPU module with E71 mounted under the following conditions.

- Number of registered device numbers: Y50 to 5F, D38, W1E

(Response data)

(ASCII code)

Response data: `Subheader(89) → End code(00) → Data for the number of specified device points(E56D) → (1234) (D38) → (5678) (W1E)`
Bytes: `38H 39H 30H 30H 45H 35H 36H 44H 31H 32H 33H 34H 35H 36H 37H 38H`

Bit expansion of E56D: `1 1 1 0 0 1 0 1 0 1 1 0 1 1 0 1`, where the first 8 bits are (Y5F) to (Y58) and the last 8 bits are (Y57) to (Y50).

(Binary code)

Response data: `Subheader(89H) → End code(00H) → Data for the number of specified device points(6DH E5H) → (34H 12H) (D38) → (78H 56H) (W1E)`
Bytes: `89H 00H 6DH E5H 34H 12H 78H 56H`

Bit expansion of 6DH E5H: `0 1 1 0 1 1 0 1 1 1 1 0 0 1 0 1`, where the first 8 bits are (Y57) to (Y50) and the last 8 bits are (Y5F) to (Y58).

### 18.5 Read and Write Extended File Register

The extended file register is a memory area that stores required data and operation result for various data processing and AnACPU and AnUSCPU extended file register dedicated instructions by using the software package for extended file register 'SW0GHP-UTLPC-FN1' or 'SW0SRX-FNUP' (hereinafter abbreviated to UTLP-FN1 and FNUP). The extended file register uses free area of user memory area in CPU module as a file register.
This section explains the commands for reading and writing extended file register.
For the message formats other than request data and response data, refer to the following sections.
Page 391 Message Format, Page 391 Details of Setting Data

> **Restriction**
> For the considerations when reading and writing extended file register, refer to the following section.
> Page 371 Considerations for reading and writing extended file register
> For the specification method of the extended file register, refer to the following section.
> Page 372 Specification method for extended file register

#### Data to be specified in command

**Block number**

Specify the block number of the extended file register.

■Data communication in ASCII code

Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send 2-byte numerical value.

**Device number**

Specify the device number of the extended file register.
When reading or writing (command: 3B, 3C) extended file register directly, refer to the following section.
Page 373 Device number (address) specification using AnA/AnUCPU common commands

■Data communication in ASCII code

Convert the numerical value to 12-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send 6-byte numerical value.

#### Batch read (command: 17)

Read extended file register (R) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

(ASCII code)

`Block No. → Device number → Number of device points → 0 (30H) → 0 (30H)`

(Binary code)

`Device number → Block No. → Number of device points → 00H`

■Response data

The data for the number of device points are stored.
The data order differs between ASCII code or binary code. (Page 402 Read data, write data)

**Data specified by request data**

■Device number

Specify the head device of the extended file register to be read. (Page 418 Device number)

■Block number

Specify the block number of the extended file register to be read. (Page 418 Block number)

■Number of device points

Specify the points of extended file register to be read.
Specify '00H' when the number of device points is to 256 points. (Page 402 Number of device points)

**Communication example**

Read the extended file registers in the CPU module with E71 mounted under the following conditions.

- Block number: No.2
- Device number: R70
- Number of device points: 3 points

■Data communication in ASCII code

(Request data)

Request data: `Block No.(0002) → Device number(522000000046) → Number of device points(03) → (00)`
Bytes: `30H 30H 30H 32H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 34H 36H 30H 33H 30H 30H`

(Response data)

Response data: `Data for the number of specified device points(1234 8765 013F)`
Bytes: `31H 32H 33H 34H 38H 37H 36H 35H 30H 31H 33H 46H`
(R70 in No.2) (R71 in No.2) (R72 in No.2)

■Data communication in binary code

(Request data)

Request data: `Device number → Block No. → Number of device points → (00H)`
Bytes: `46H 00H 00H 00H 20H 52H 02H 00H 03H 00H`

(Response data)

Response data: `Data for the number of specified device points`
Bytes: `34H 12H 65H 87H 3FH 01H`
(R70 in No.2) (R71 in No.2) (R72 in No.2)

#### Batch write (command: 18)

Write to extended file register (R) in 1-point units.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

(ASCII code)

`Block No. → Device number → Number of device points → 0 (30H) → 0 (30H) → Data for the number of specified device points`

(Binary code)

`Device number → Block No. → Number of device points → 00H → Data for the number of specified device points`

■Response data

There is no response data for this command.

**Data specified by request data**

■Device number

Specify the head device number of the extended file register to be written. (Page 418 Device number)

■Block number

Specify the block number of the extended file register to be written. (Page 418 Block number)

■Number of device points

Specify the number of the data points to be write.
Specify '00H' when the number of device points is to 256 points. (Page 402 Number of device points)

■Data for the number of specified device points

Store the data to be written to the extended file registers.

**Communication example**

Write data to extended file registers in the CPU module with E71 mounted under the following conditions.

- Block number: No.3
- Device number: R100
- Number of device points: 3 points

■Data communication in ASCII code

(Request data)

Request data: `Block No.(0003) → Device number(522000000064) → Number of device points(03) → (00) → Data for the number of specified device points(0109 9876 1234)`
Bytes: `30H 30H 30H 33H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 36H 34H 30H 33H 30H 30H 30H 31H 30H 39H 39H 38H 37H 36H 31H 32H 33H 34H`
(R100 in No.3) (R101 in No.3) (R102 in No.3)

■Data communication in binary code

(Request data)

Request data: `Device number → Block No. → Number of device points → (00H) → Data for the number of specified device points`
Bytes: `64H 00H 00H 00H 20H 52H 03H 00H 03H 00H 09H 01H 76H 98H 34H 12H`
(R100 in No.3) (R101 in No.3) (R102 in No.3)

#### Test (random write) (command: 19)

Specify the block numbers and device numbers in 1-point units and write them randomly to extended file register (R).

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

(ASCII code)

`Number of device points → (unlabeled cell)(00) → Block No. → Device number → Write data`
Bracket "Data for the number of device points" over: `Block No.`, `Device number`, `Write data`
Bytes (unlabeled cell): `30H 30H`

(Binary code)

`Number of device points → (unlabeled cell) → Device number → Block No. → Write data`
Bracket "Data for the number of device points" over: `Device number`, `Block No.`, `Write data`
Bytes (unlabeled cell): `00H`

■Response data

There is no response data for this command.

**Data specified by request data**

■Number of device points

Specify the number of points of the data to be written. (Page 402 Number of device points)

■Device number

Specify the head device of the extended file register to be written. (Page 418 Device number)

■Block number

Specify the block number of the extended file register to be written. (Page 418 Block number)

■Write data

Store the data to be written to the extended file registers.

**Communication example**

Write data to extended file registers in the CPU module with E71 mounted under the following conditions.

- Data to be written: R26 in block number 2 and R19 in block number 3
- Number of device points: 2 points

■Data communication in ASCII code

(Request data)

Request data: `Number of device points(02) → (unlabeled cell)(00) → Block No.(0002) → Device number(52200000001A) → Write data(1234)`
Bytes: `30H 32H 30H 30H 30H 30H 30H 32H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 31H 41H 31H 32H 33H 34H`
(R26 in block No.2)

Request data (continued): `Block No.(0003) → Device number(522000000013) → Write data(0109)`
Bytes: `30H 30H 30H 33H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 31H 33H 30H 31H 30H 39H`
(R19 in block No.3)

■Data communication in binary code

(Request data)

Request data: `Number of device points(02H) → (unlabeled cell)(00H) → Device number(1AH 00H 00H 00H 20H 52H) → Block No.(02H 00H) → Write data(34H 12H) → Device number(13H 00H 00H 00H 20H 52H) → Block No.(03H 00H) → Write data(09H 01H)`
Bytes: `02H 00H 1AH 00H 00H 00H 20H 52H 02H 00H 34H 12H 13H 00H 00H 00H 20H 52H 03H 00H 09H 01H`
(R26 in block No.2) (R19 in block No.3)

#### Monitor extended file registers (command: 1A, 1B)

The contents of extended file registers in the CPU module can be monitored from an external device by registering the block numbers and device numbers to be monitored to E71 in advance and executing the monitor command from the external device.
When reading extended file register in batch, the read device numbers will be consecutive. However, when reading them using the monitor command, the extended file registers can be read randomly by specifying the file registers of arbitrary block numbers and device numbers.

**Procedure for monitoring**

1. Register block numbers and device numbers of the extended file registers to be monitored to E71 by monitor data registration.
2. Execute read processing by monitoring.
3. Process the data.
4. If do not change the devices to be monitored, return to step 2, and repeat the process.

> **Point**
> - When monitoring data as the procedure shown above, the monitor data registration is required. If monitoring data without registering the data, an error occurs (END code: 57H).
> - The content of registered monitor data is deleted when turning the power OFF or the resetting the CPU module.
> - There are three types of commands for monitor data registration; extended file register, device memory bit unit, and device memory word unit. The recently registered one command out of three types of commands can be registered to E71.
> - When registering device memory of the CPU module as a monitoring data from more than one external devices on the same station, the recently registered device memory will be available since the registration data is overwritten.

**Register monitor data (command: 1A)**

Register device numbers to be monitored in 1-point units.

■Request data

(ASCII code)

Request data: `Number of device points → (unlabeled cell)(00) → Block No. → Device number`
Bracket "Data for the number of device points" over: `Block No.`, `Device number`
Bytes (unlabeled cell): `30H 30H`

(Binary code)

Request data: `Number of device points → (unlabeled cell) → Device number → Block No.`
Bracket "Data for the number of device points" over: `Device number`, `Block No.`
Bytes (unlabeled cell): `00H`

- Number of device points: Specify the number of points of the extended file registers to be registered as a monitor data. (Page 402 Number of device points)
- Device number: Specify the extended file registers to be registered as a monitor data. (Page 418 Device number)
- Block number: Specify the block number of the extended file registers to be registered as a monitor data. (Page 418 Block number)

■Response data

There is no response data for this command.

■Communication example

Register devices as a monitoring data in the CPU module with E71 mounted under the following conditions.

- Data to be registered: R15 of block number 2 and R28 of block number 3

(Request data)

(ASCII code)

Request data: `Number of device points(02) → (unlabeled cell)(00) → Block No.(0002) → Device number(52200000000F)`
Bytes: `30H 32H 30H 30H 30H 30H 30H 32H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 30H 46H`
Label below: R15 of block No.2

Request data (continued): `Block No.(0003) → Device number(52200000001C)`
Bytes: `30H 30H 30H 33H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 31H 43H`
Label below: R28 of block No.3

> **Note:** In the PDF, the last character of the device number in the R15 row is printed as "6" although its byte is printed as 46H (the character F); the binary example shows 0FH for R15. The value is written above as F (device number characters: 5, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, F).

(Binary code)

Request data: `Number of device points(02H) → (unlabeled cell)(00H) → Device number(0FH 00H 00H 00H 20H 52H) → Block No.(02H 00H) → Device number(1CH 00H 00H 00H 20H 52H) → Block No.(03H 00H)`
Bytes: `02H 00H 0FH 00H 00H 00H 20H 52H 02H 00H 1CH 00H 00H 00H 20H 52H 03H 00H`
Labels below: (R15 in block No.2) under `0FH 00H 00H 00H 20H 52H 02H 00H`, (R28 in block No.3) under `1CH 00H 00H 00H 20H 52H 03H 00H`

**Monitoring (command: 1B)**

Monitor the extended file registers registered by monitor data registration.

■Data specified by request data

There is no request data for this command.

■Response data

Store the monitoring result.

■Communication example

Monitor the following extended file registers registered by monitor data registration.

- Monitor data registration: R15 in block number 2 and R28 in block number 3

(Response data)

(ASCII code)

Response data: `Monitoring result(E56D1234)`
Bytes: `45H 35H 36H 44H 31H 32H 33H 34H`
Labels below: (R15 in block No.2) under `E56D`, (R28 in block No.3) under `1234`

(Binary code)

Response data: `Monitoring result(6DH E5H 34H 12H)`
Bytes: `6DH E5H 34H 12H`
Labels below: (R15 in block No.2) for `6DH E5H`, (R28 in block No.3) for `34H 12H`

#### Direct read (command: 3B)

Read extended file register in 1-point (1 word) units by specifying the consecutive device numbers of extended file register.
This command is a dedicated command for AnACPU. (The command is equivalent to AnA/AnUCPU common commands of 1C protocol.)

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Device number → Number of device points → Fixed value`

■Response data

The data for the number of device points are stored.
(Page 402 Read data, write data)

**Data specified by request data**

■Device number

Specify the start address of the extended file register to be read. (Page 418 Device number)

■Number of device points

Specify the points of extended file register to be read.
Specify '00H' when the number of device points is to 256 points. (Page 402 Number of device points)

■Fixed value

Fixed to '0'.

| ASCII code | Binary code |
|---|---|
| `0 0` (30H 30H) | `00H` |

**Communication example**

Read the extended file registers directly in the CPU module with E71 mounted under the following conditions.

- Block number: No.0
- Device number: R70
- Number of device points: 4 points

■Data communication in ASCII code

(Request data)

Request data: `Block No.(0002) → Device number(522000000046) → Number of device points(04) → (unlabeled cell)(00)`
Bytes: `30H 30H 30H 32H 35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 34H 36H 30H 34H 30H 30H`

> **Note:** In the PDF, the ASCII request example prints a leading field labelled "Block No." with the value 0002, although the message format of this command has no Block No. field, the binary request example has no such field, and the conditions state "Block number: No.0". The PDF value is written as printed; the correct value cannot be determined from the PDF.

(Response data)

Response data: `Data for the number of specified device points(1234 8765 013F 0020)`
Bytes: `31H 32H 33H 34H 38H 37H 36H 35H 30H 31H 33H 46H 30H 30H 32H 30H`
Labels below: (R70 in No.0) under `1234`, (R71 in No.0) under `8765`, (R72 in No.0) under `013F`, (R73 in No.0) under `0020`

■Data communication in binary code

(Request data)

Request data: `Device number(46H 00H 00H 00H 20H 52H) → Number of device points(04H) → (unlabeled cell)(00H)`
Bytes: `46H 00H 00H 00H 20H 52H 04H 00H`

(Response data)

Response data: `Data for the number of specified device points(34H 12H 65H 87H 3FH 01H 20H 00H)`
Bytes: `34H 12H 65H 87H 3FH 01H 20H 00H`
Labels below: (R70 in No.0) for `34H 12H`, (R71 in No.0) for `65H 87H`, (R72 in No.0) for `3FH 01H`, (R73 in No.0) for `20H 00H`

#### Direct write (command: 3C)

Write extended file register in 1-point (1 word) units by specifying the consecutive device number of extended file register.
This command is a dedicated command for AnACPU. (The command is equivalent to AnA/AnUCPU common commands of 1C protocol.)

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Device number → Number of device points → Fixed value → Data for the number of specified device points`

■Response data

There is no response data for this command.

**Data specified by request data**

■Device number

Specify the start address of the extended file register to be written. (Page 418 Device number)

■Number of device points

Specify the number of extended file registers to be written. (Page 402 Number of device points)
Specify '00H' when the number of device points is to 256 points.

■Fixed value

Fixed to '0'.

| ASCII code | Binary code |
|---|---|
| `0 0` (30H 30H) | `00H` |

■Data for the number of specified device points

Stored data for the number of device points to be written.

**Communication example**

Write data to the extended file registers directly in the CPU module with E71 mounted under the following conditions.

- Block number: No.0
- Device number: R100
- Number of device points: 3 points

■Data communication in ASCII code

(Request data)

Request data: `Device number(522000000064) → Number of device points(03) → (unlabeled cell)(00) → Data for the number of specified device points(0109 9876 1234)`
Bytes: `35H 32H 32H 30H 30H 30H 30H 30H 30H 30H 36H 34H 30H 33H 30H 30H 30H 31H 30H 39H 39H 38H 37H 36H 31H 32H 33H 34H`
Labels below: (R100 in No.0) under `0109`, (R101 in No.0) under `9876`, (R102 in No.0) under `1234`

■Data communication in binary code

(Request data)

Request data: `Device number(64H 00H 00H 00H 20H 52H) → Number of device points(03H) → (unlabeled cell)(00H) → Data for the number of specified device points(09H 01H 76H 98H 34H 12H)`
Bytes: `64H 00H 00H 00H 20H 52H 03H 00H 09H 01H 76H 98H 34H 12H`
Labels below: (R100 in No.0) for `09H 01H`, (R101 in No.0) for `76H 98H`, (R102 in No.0) for `34H 12H`

### 18.6 Read and Write Buffer Memory of Special Function Module

The section explains the commands that read/write data from/to buffer memory of MELSEC-A series special function module.
For the message formats other than request data and response data, refer to the following sections.
Page 391 Message Format, Page 391 Details of Setting Data

> **Point**
> This command accesses in byte units.

#### Data to be specified in command

This section explains the contents and specification methods for data items which are set in each command related to the access to the special function module buffer memory.

**Start address**

Specify the start address of the buffer memory to be read/written.
The value to specify is the same as 1C frame.
Page 384 Special function module No.

■Data communication in ASCII code

Convert the numerical value to 6-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send 3-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.**
When the head area address is 1E1H

| ASCII code | Binary code |
|---|---|
| `0 0 0 1 E 1` (bytes: `30H 30H 30H 31H 45H 31H`) | `E1H 01H 00H` |

**Byte length**

Specify the byte length of the buffer memory to be read or written.
Specify '00' for 256 bytes.

■Data communication in ASCII code

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send a 1-byte numerical value.

**Ex.**
For 5 bytes, 20 bytes, 256 bytes.

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 5 bytes | `0 5` (bytes: `30H 35H`) | `05H` |
| 20 bytes | `1 4` (bytes: `31H 34H`) | `14H` |
| 256 bytes | `0 0` (bytes: `30H 30H`) | `00H` |

**Read data, write data**

The read buffer memory value is stored for reading, and the data to be written is stored for writing.
This function reads and writes data in byte units.
Page 160 Read data, write data

**Special function module No.**

Specify the last input/output signal (I/O address) of the special function module. (Specify the upper 2-digit of 3-digit representation.)
The value to specify is the same as 1C frame.
Page 384 Special function module No.

■Data communication in ASCII code

Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send a 1-byte numerical value.

#### Accessible modules

Special function modules that can be accessed buffer memory are the same as 1C frame.
Page 385 Accessible modules

#### Batch read (command: 0E)

Read the buffer memory of a special function module.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Start address → Byte length → Special function module No. → Fixed value`

■Response data

The data read from buffer memory is stored. (Page 433 Read data, write data)

**Data specified by request data**

■Start address

Specify the buffer memory start address to be read. (Page 432 Start address)

■Byte length

Specify the byte length of buffer memory to be read.
Specify '00H' when the number of device points is to 256 points. (Page 433 Byte length)

■Special function module No.

Specify the special function module No. of the buffer memory to be read.

■Fixed value

Fixed to '0'.

| ASCII code | Binary code |
|---|---|
| `0 0` (bytes: `30H 30H`) | `00H` |

**Communication example**

Read the buffer memory of the special function module with E71 mounted in the same station under the following conditions.

- Special function module No.: 13H (buffer memory whose input/output signal is from 120H to 13FH)
- Start address: 7F0H
- Byte length: 4

■Data communication in ASCII code

(Request data)

Request data: `Start address(0007F0) → Byte length(04) → Special function module No.(13) → (00)`
Bytes: `30H 30H 30H 37H 46H 30H 30H 34H 31H 33H 30H 30H`

(Response data)

Response data: `Data read(09182034)`
Bytes: `30H 39H 31H 38H 32H 30H 33H 34H`
Device labels under the bytes: `(7F0H) (7F1H) (7F2H) (7F3H)`

■Data communication in binary code

(Request data)

Request data: `Head address(F0H 07H 00H) → Byte length(04H) → Special function module No.(13H) → (00H)`
Bytes: `F0H 07H 00H 04H 13H 00H`

(Response data)

Response data: `Data read(09H 18H 20H 34H)`
Bytes: `09H 18H 20H 34H`
Device labels under the bytes: `(7F0H) (7F1H) (7F2H) (7F3H)`

#### Batch write (command: 0F)

Write data to the buffer memory of a special function module.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Start address → Byte length → Special function module No. → Fixed value → Write data (for the length of specified bytes)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Start address

Specify the buffer memory start address to be read. (Page 432 Start address)

■Byte length

Specify the byte length of buffer memory to be read.
Specify '00H' when the number of device points is to 256 points. (Page 433 Byte length)

■Special function module No.

Specify the special function module No. of the buffer memory to be read.

■Fixed value

Fixed to '0'.

| ASCII code | Binary code |
|---|---|
| `0 0` (bytes: `30H 30H`) | `00H` |

■Write data

Store the data to be written in a buffer memory. (Page 433 Read data, write data)

**Communication example**

Write data to buffer the memory of the special function module with E71 mounted in the same station under the following conditions.

- Special function module No.: 13H (buffer memory whose input/output signal is from 120H to 13FH)
- Start address: 750H
- Byte length: 4

■Data communication in ASCII code

(Request data)

Request data: `Start address(000750) → Byte length(04) → Special function module No.(13) → (00) → Write data(01234567)`
Bytes: `30H 30H 30H 37H 35H 30H 30H 34H 31H 33H 30H 30H 30H 31H 32H 33H 34H 35H 36H 37H`
Device labels under the write data bytes: `(750H) (751H) (752H) (753H)`

■Data communication in binary code

(Request data)

Request data: `Start address(50H 07H 00H) → Byte length(04H) → Special function module No.(13H) → (00H) → Write data(01H 23H 45H 67H)`
Bytes: `50H 07H 00H 04H 13H 00H 01H 23H 45H 67H`
Device labels under the write data bytes: `(750H) (751H) (752H) (753H)`
