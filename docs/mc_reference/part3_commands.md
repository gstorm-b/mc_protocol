# PART 3 COMMAND

This part explains the functions that can be specified by a message of MC protocol and the message format of request data and message data of each command.

- 7 COMMANDS AND FUNCTIONS
- 8 DEVICE ACCESS
- 9 LABEL ACCESS
- 10 BUFFER MEMORY ACCESS
- 11 CONTROL MODULE OPERATION
- 12 FILE CONTROL
- 13 SERIAL COMMUNICATION MODULE DEDICATED COMMANDS

---

## Reading Notes

- **Source:** Mc-protocol.pdf, Part 3 (PDF pages 61–286, printed pages 59–284). The text was compared page by page with the PDF (page images and text layer) in two passes.
- **Page references:** "Page N Title" cross-references keep the page numbers printed in the PDF (PDF page index = printed page number + 2).
- **Diagrams:** byte-layout diagrams and communication examples are transcribed as text: fields in the order they appear in the PDF (`Field(value) → Field(value)`), followed by the byte sequence. Some illustrative figures (system configuration, timing charts) are not reproduced.
- **Notes:** a `> **Note:**` block marks a place where the PDF itself has an evident misprint or is internally inconsistent. It states what the PDF prints and which value is used here. Where the correct value cannot be determined from the PDF, the text follows the PDF as printed.
- **`□`:** the PDF prints a hollow square as the last digit of the device extension subcommand ("008□"). The digit is given by Page 439 Subcommands for device extension specification (Appendix 1):

| Item | ASCII code | Binary code |
|---|---|---|
| MELSEC-Q/L series, word units | `0080` | `80H 00H` |
| MELSEC-Q/L series, bit units | `0081` | `81H 00H` |
| MELSEC-Q/L series, monitor condition | `00C0` | `C0H 00H` |
| MELSEC iQ-R series, word units | `0082` | `82H 00H` |
| MELSEC iQ-R series, bit units | `0083` | `83H 00H` |

---

## 7 COMMANDS AND FUNCTIONS

This chapter explains the commands of MC protocol.

The functions of a message is defined by each command. The message format for request data and response data varies with commands. Depending on the type of frame to be used, the specific value is assigned to a command. The value of command is specified at the head of a request data.

**Request message:** `Control code → Access route → Request data (Command → ...)`

**Response message:** `Control code → Access route → Response data`

The explanation of each command in Part 3, the message format of request data and response data are explained.

For the message formats other than request data and response data, refer to the following sections.

- Page 28 MESSAGES OF SERIAL COMMUNICATION MODULE
- Page 39 MESSAGES OF Ethernet INTERFACE MODULE

### 7.1 Command List

The following shows the list of commands.

There are some commands that cannot be executed while the CPU module is in RUN. Refer to the following section.
Page 464 Applicable Commands for Online Program Change

For details on the number of points processed per communication and modules can be accessed by each command, refer to the following sections.

- Page 466 Number of Processing per One Communication
- Page 471 Accessible Modules for Each Command

#### Commands for 4C/3C/4E/3E frame

The following shows the commands for 4C/3C/4E/3E frame.
For 4C/3C/4E/3E frame, specify subcommands in the request message as well.

**Device access**

| Function | Command name | Command | Subcommand | Description |
|---|---|---|---|---|
| Batch read and write | Batch read in word units | 0401 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Read values from devices in word units. Read the values in batch with specifying the consecutive device points. |
| Batch read and write | Batch read in bit units | 0401 | 0001 / 0081 (MELSEC-Q/L series); 0003 / 0083 (MELSEC iQ-R series) | Read values from devices in bit units. Read the values in batch with specifying the consecutive device points. |
| Batch read and write | Batch write in word units | 1401 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Write values to devices in word units. Write the consecutive devices in batch with specifying the consecutive device points. |
| Batch read and write | Batch write in bit units | 1401 | 0001 / 0081 (MELSEC-Q/L series); 0003 / 0083 (MELSEC iQ-R series) | Write values to devices in bit units. Write the consecutive devices in batch with specifying the consecutive device points. |
| Random read and write | Random read in word units | 0403 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Read values from devices in word or double-word units. Read device values with specifying device numbers. Discontinuous device numbers can be specified. |
| Random read and write | Random read in word units | 0403 | 0040 / 00C0 (Monitor condition specified) | Read values randomly in word units by specifying the monitor condition. The read timing can be changed. |
| Random read and write | Random write in word units (test) | 1402 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Write values to devices in word or double-word units. Write device values with specifying device numbers. Discontinuous device numbers can be specified. |
| Random read and write | Random write in bit units (test) | 1402 | 0001 / 0081 (MELSEC-Q/L series); 0003 / 0083 (MELSEC iQ-R series) | Write values to devices in bit units. Write device values with specifying device numbers. Discontinuous device numbers can be specified. |
| Batch read and write multiple blocks | Batch read multiple blocks | 0406 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Read values for specified multiple blocks by handling consecutive word devices or bit devices as one block. Each block can be specified with discontinuous device numbers. |
| Batch read and write multiple blocks | Batch write multiple blocks | 1406 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Write values for specified multiple blocks by handling consecutive word devices or bit devices as one block. Each block can be specified with discontinuous device numbers. |
| Monitor device memory | Register monitor data | 0801 | 0000 / 0080 (MELSEC-Q/L series); 0002 / 0082 (MELSEC iQ-R series) | Register devices to be monitored. |
| Monitor device memory | Register monitor data | 0801 | 0040 / 00C0 (Monitor condition specified) | Perform monitor data registration by specifying monitor conditions. The read timing can be changed. |
| Monitor device memory | Monitor | 0802 | 0000 | Read the values of registered devices. |

Even when an access target is a MELSEC iQ-R or MELSEC iQ-L series module, devices whose types and ranges are equivalent to MELSEC-Q/L series can be accessed by using subcommands for MELSEC-L/Q series. (Page 471 Accessible Modules for Each Command)

**Label access**

| Function | Command name | Command | Subcommand | Description |
|---|---|---|---|---|
| Batch read and write | Batch read array type labels | 041A | 0000 (MELSEC iQ-R series) | Read the values from array type labels. Read the values in batch with specifying the consecutive array elements. Specify the array type labels or array type elements of structure type labels. |
| Batch read and write | Batch write array type labels | 141A | 0000 (MELSEC iQ-R series) | Write the values to array type labels. Write the values in batch with specifying the consecutive array elements. Specify the array type labels or array type elements of structure type labels. |
| Random read and write | Random read labels | 041C | 0000 (MELSEC iQ-R series) | Read values with specifying multiple labels. |
| Random read and write | Random write labels | 141B | 0000 (MELSEC iQ-R series) | Write values with specifying multiple labels. |

**Buffer memory access**

| Function | Command name | Command | Subcommand | Description |
|---|---|---|---|---|
| Buffer memory | Batch read | 0613 | 0000 | Read data from the buffer memory of the host station (supported device). |
| Buffer memory | Batch write | 1613 | 0000 | Write data to the buffer memory of the host station (supported device). |
| Intelligent function module | Batch read | 0601 | 0000 | Read data from the buffer memory of an intelligent function module. |
| Intelligent function module | Batch write | 1601 | 0000 | Write data to the buffer memory of an intelligent function module. |

**Module control**

| Function | Command name | Command | Subcommand | Description |
|---|---|---|---|---|
| Remote control | Remote RUN | 1001 | 0000 | Perform remote RUN to the access target module. |
| Remote control | Remote STOP | 1002 | 0000 | Perform remote STOP to the access target module. |
| Remote control | Remote PAUSE | 1003 | 0000 | Perform remote PAUSE to the access target module. |
| Remote control | Remote latch clear | 1005 | 0000 | Perform remote latch clear to the access target module. |
| Remote control | Remote RESET | 1006 | 0000 | Perform remote RESET to the access target module. |
| Remote control | Read CPU model name | 0101 | 0000 | Read model name and model code from the access target module. |
| Remote password | Unlock | 1630 | 0000 | Specify a remote password to enable communications with other devices. (Change a device from the locked state to unlocked state.) |
| Remote password | Lock | 1631 | 0000 | Specify a remote password to disable communications with other devices. (Change a device from the unlocked state to locked state.) |
| Loopback test | — | 0619 | 0000 | Test to check whether communications between external device and connection station operate normally. |
| Clear error information | Turn indicator LED OFF, initialize communication error information/error code | 1617 | 000□ | Turn OFF the error LED, and initialize communication error information and error code. |

**File control**

| Function | Command name | Command | Subcommand | Description |
|---|---|---|---|---|
| File check | Read directory/file information | 1810 | 0000 (MELSEC-Q/L series); 0040 (MELSEC iQ-R series) | For the specified storage destination file, read the file name, file creation date and time (last edit date and time) etc. |
| File check | Search directory/file information | 1811 | 0000 (MELSEC-Q/L series); 0040 (MELSEC iQ-R series) | Read the file No. of the specified file. |
| File creation and deletion | Create new file | 1820 | 0000 (MELSEC-Q/L series); 0040 (MELSEC iQ-R series) | Create a file with specifying its size. (Reserve a storage area for the specified file.) |
| File creation and deletion | Delete file | 1822 | 0000 (MELSEC-Q series); 0004 (MELSEC-L series); 0040 (MELSEC iQ-R series) | Delete a file. |
| File creation and deletion | Copy file | 1824 | 0000 (MELSEC-Q series); 0004 (MELSEC-L series); 0040 (MELSEC iQ-R series) | Copy a file. |
| File modification | Modify file attribute | 1825 | 0000 (MELSEC-Q series); 0004 (MELSEC-L series); 0040 (MELSEC iQ-R series) | Change the file attribute. |
| File modification | Modify file creation date and time | 1826 | 0000 (MELSEC-Q/L series); 0040 (MELSEC iQ-R series) | Modify the file creation date and time (last edit date and time). |
| File modification | Open file | 1827 | 0000 (MELSEC-Q series); 0004 (MELSEC-L series); 0040 (MELSEC iQ-R series) | Open a file and lock the file so that the file contents are not modified from other devices. |
| File modification | Read file | 1828 | 0000 | Read a file content. |
| File modification | Write to file | 1829 | 0000 | Write content to a file. |
| File modification | Close file | 182A | 0000 | Close a file and unlock the file which has been locked by the 'open file' (command: 1827). |

For the QnACPU dedicated commands, refer to the following section.
Page 288 QnACPU Dedicated Commands List

**Serial communication dedicated commands**

| Function | Command name | Command | Subcommand | Description |
|---|---|---|---|---|
| User frame | Read registered data | 0610 | 0000 | Read the registered content of user frames. |
| User frame | Register data | 1610 | 0000 | Register user frames to C24. |
| User frame | Delete registered data | 1610 | 0001 | Delete the registered user frame. |
| Global | — | 1618 | 0000 / 0001 | Turn ON/OFF the global signal (X1A/X1B). This can be performed to a connected station and multidrop connection station. |
| Initialize transmission sequence | — | 1615 | 0000 | Terminate a current processing request, and place C24 into the wait state to receive commands. This can be performed to a connected station and multidrop connection station. |
| Switch mode | — | 1612 | 0000 | Switch the operation mode and transmission specifications of the specified interface. This can be performed to a connected station and multidrop connection station. |
| Programmable controller CPU monitoring | Register | 0630 | 0000 | Register the conditions to monitor and start the programmable controller CPU monitoring. |
| Programmable controller CPU monitoring | Deregister | 0631 | 0000 | End the programmable controller CPU monitoring. |
| On-demand | — | 2101 | — | Issue a transmission request to C24 from CPU module, and transmit data to external devices. |

#### Commands for 2C frame

The following shows the commands for 2C frame.
The commands for 4C/3C frame are equivalent to the following device access commands and subcommands.

| Function | Command name | 2C frame command | 4C/3C frame command | 4C/3C frame subcommand | Description |
|---|---|---|---|---|---|
| Batch read and write | Batch read in bit units | 1 | 0401 | 0001 | Read values from devices in bit units. Read the values in batch with specifying the consecutive device points. |
| Batch read and write | Batch read in word units | 2 | 0401 | 0000 | Read values from devices in word units. Read the values in batch with specifying the consecutive device points. |
| Batch read and write | Batch write in bit units | 3 | 1401 | 0001 | Write values to devices in bit units. Write the consecutive devices in batch with specifying the consecutive device points. |
| Batch read and write | Batch write in word units | 4 | 1401 | 0000 | Write values to devices in word units. Write the consecutive devices in batch with specifying the consecutive device points. |
| Random read and write | Random read in word units | 5 | 0403 | 0000 | Read values from devices in word or double-word units. Read device values with specifying device numbers. Discontinuous device numbers can be specified. |
| Random read and write | Random write in bit units (test) | 6 | 1402 | 0001 | Write values to devices in bit units. Write device values with specifying device numbers. Discontinuous device numbers can be specified. |
| Random read and write | Random write in word units (test) | 7 | 1402 | 0000 | Write values to devices in word or double-word units. Write device values with specifying device numbers. Discontinuous device numbers can be specified. |
| Monitor device memory | Register monitor data | 8 | 0801 | 0000 | Register devices to be monitored. |
| Monitor device memory | Monitor | 9 | 0802 | 0000 | Read the values of registered devices. |

#### Commands for 1C/1E frame

For the commands for 1C/1E frame, refer to the following sections.

- Page 349 Command and Function Lists for 1C Frame
- Page 396 Commands and Function List for 1E Frame

---

## 8 DEVICE ACCESS

This chapter explains the commands to read and write devices.

### 8.1 Data to be Specified in Commands

This section explains the contents and specification methods for data items which are set in each command related to device access.

#### Devices

Specify the device to be accessed by a device code and a device number.

- The data order differs between ASCII code or binary code.
- The data size to be set differs between MELSEC-Q/L series subcommands (subcommand: 0000, 0001) and MELSEC iQ-R series subcommands (subcommand: 0002, 0003).

**Device/device number field order**

| Subcommand type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `Device code (2 digits) → Device number (6 digits)` | `Device number (3 bytes) → Device code (1 byte)` |
| For MELSEC iQ-R series | `Device code (4 digits) → Device number (8 digits)` | `Device number (4 bytes) → Device code (2 bytes)` |

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

#### Device codes

Specify the device name to be accessed.
Specify the device within the range of the access target module.

For the values of each device code, refer to the following section.
Page 68 Device code list

■Data communication in ASCII code

Convert the numerical value to 2-digit or 4-digit ASCII code (hexadecimal), and send it from the upper digits.

- For MELSEC-Q/L series: 2-digit ASCII code
- For MELSEC iQ-R series: 4-digit ASCII code

The '*' in a device code can also be specified with a space (code: 20H).

■Data communication in binary code

Send the 1-byte or 2-byte numerical value from the lower byte (L: bits 0 to 7).

- For MELSEC-Q/L series: 1 byte
- For MELSEC iQ-R series: 2 bytes

**Ex.** For input (X)

| Subcommand type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `X` `*` → `58H` `2AH` | `9CH` |
| For MELSEC iQ-R series | `X` `*` `*` `*` → `58H` `2AH` `2AH` `2AH` | `9CH` `00H` |

#### Device number

Specify the number of device to be accessed.
Specify the device number within the range of the access target module.

■Data communication in ASCII code

Convert the numerical value to 6-digit or 8-digit ASCII code, and sent it from the upper digits.
Specify the device number in decimal or hexadecimal, depending on the device type. (Page 68 Device code list)

- For MELSEC-Q/L series: 6-digit ASCII code
- For MELSEC iQ-R series: 8-digit ASCII code (10 digits at device extension specification)

The '0' in the upper digits can also be specified with a space (code: 20H).

■Data communication in binary code

Send the 3-byte or 4-byte numerical value in order from the lower byte (L: bit 0 to 7).
For a device of which device number is in decimal, convert it to hexadecimal and specify.

- For MELSEC-Q/L series: 3 bytes*1
- For MELSEC iQ-R series: 4 bytes*1

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.**

For input (X) 1234 (a device of which device number is in hexadecimal)

| Subcommand type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `001234` (`30H 30H 31H 32H 33H 34H`) | `34H 12H 00H` |
| For MELSEC iQ-R series | `00001234` (`30H 30H 30H 30H 31H 32H 33H 34H`) | `34H 12H 00H 00H` |

For internal relay (M) 1234 (a device of which device number is in decimal)
For binary code, convert the device number to hexadecimal. '1234' (decimal) → '4D2' (hexadecimal)

| Subcommand type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `001234` (`30H 30H 31H 32H 33H 34H`) | `D2H 04H 00H` |
| For MELSEC iQ-R series | `00001234` (`30H 30H 30H 30H 31H 32H 33H 34H`) | `D2H 04H 00H 00H` |

For internal relay (M) 16 (with additional code)
For C24 binary code, specify '10H' as '10H + 10H'. (Page 35 Additional code (10H))

| Subcommand type | Binary code (For C24) | Binary code (For E71) |
|---|---|---|
| For MELSEC-Q/L series | `10H`(DLE, additional code)`+10H 00H 00H` | `10H 00H 00H` |
| For MELSEC iQ-R series | `10H`(DLE, additional code)`+10H 00H 00H 00H` | `10H 00H 00H 00H` |

#### Device code list

The following shows the device code of each device and the notation of device number (decimal/hexadecimal).
The data to be set differs between MELSEC-Q/L series commands (subcommand: 0000, 0001) and MELSEC iQ-R series subcommand (0002, 0003).

**Legend:** `—`: Inaccessible

| Device name | Symbol | Type | Notation | ASCII (Q/L) | Binary (Q/L) | ASCII (iQ-R) | Binary (iQ-R) |
|---|---|---|---|---|---|---|---|
| Special relay | SM | Bit | Decimal | SM | 91H | SM** | 0091H |
| Special register | SD | Word | Decimal | SD | A9H | SD** | 00A9H |
| Input | X | Bit | Hexadecimal | X* | 9CH | X*** | 009CH |
| Output | Y | Bit | Hexadecimal | Y* | 9DH | Y*** | 009DH |
| Internal relay | M | Bit | Decimal | M* | 90H | M*** | 0090H |
| Latch relay | L | Bit | Decimal | L* | 92H | L*** | 0092H |
| Annunciator | F | Bit | Decimal | F* | 93H | F*** | 0093H |
| Edge relay | V | Bit | Decimal | V* | 94H | V*** | 0094H |
| Link relay | B | Bit | Hexadecimal | B* | A0H | B*** | 00A0H |
| Data register | D | Word | Decimal | D* | A8H | D*** | 00A8H |
| Link register | W | Word | Hexadecimal | W* | B4H | W*** | 00B4H |
| Timer — Contact | TS | Bit | Decimal | TS | C1H | TS** | 00C1H |
| Timer — Coil | TC | Bit | Decimal | TC | C0H | TC** | 00C0H |
| Timer — Current value | TN | Word | Decimal | TN | C2H | TN** | 00C2H |
| Long timer*1 — Contact | LTS | Bit | Decimal | — | — | LTS* | 0051H |
| Long timer*1 — Coil | LTC | Bit | Decimal | — | — | LTC* | 0050H |
| Long timer*1 — Current value | LTN | Double word | Decimal | — | — | LTN* | 0052H |
| Retentive timer — Contact | STS | Bit | Decimal | SS | C7H | STS* | 00C7H |
| Retentive timer — Coil | STC | Bit | Decimal | SC | C6H | STC* | 00C6H |
| Retentive timer — Current value | STN | Word | Decimal | SN | C8H | STN* | 00C8H |
| Long retentive timer*1 — Contact | LSTS | Bit | Decimal | — | — | LSTS | 0059H |
| Long retentive timer*1 — Coil | LSTC | Bit | Decimal | — | — | LSTC | 0058H |
| Long retentive timer*1 — Current value | LSTN | Double word | Decimal | — | — | LSTN | 005AH |
| Counter — Contact | CS | Bit | Decimal | CS | C4H | CS** | 00C4H |
| Counter — Coil | CC | Bit | Decimal | CC | C3H | CC** | 00C3H |
| Counter — Current value | CN | Word | Decimal | CN | C5H | CN** | 00C5H |
| Long counter*1 — Contact | LCS | Bit | Decimal | — | — | LCS* | 0055H |
| Long counter*1 — Coil | LCC | Bit | Decimal | — | — | LCC* | 0054H |
| Long counter*1 — Current value | LCN | Double word | Decimal | — | — | LCN* | 0056H |
| Link special relay | SB | Bit | Hexadecimal | SB | A1H | SB** | 00A1H |
| Link special register | SW | Word | Hexadecimal | SW | B5H | SW** | 00B5H |
| Step relay | S | Bit | Decimal | S* | 98H | S*** | 0098H |
| Direct access input | DX | Bit | Hexadecimal | DX | A2H | DX** | 00A2H |
| Direct access output | DY | Bit | Hexadecimal | DY | A3H | DY** | 00A3H |
| Index register — Index register | Z | Word | Decimal | Z* | CCH | Z*** | 00CCH |
| Index register — Long index register*2 | LZ | Double word | Decimal | — | — | LZ** | 0062H |
| File register*3 — Block switching method | R | Word | Decimal | R* | AFH | R*** | 00AFH |
| File register*3 — Serial number access method | ZR | Word | Hexadecimal | ZR | B0H | ZR** | 00B0H |
| Extended data register*4 | D | Word | Decimal | D* | A8H | — | — |
| Extended link register*4 | W | Word | Hexadecimal | W* | B4H | — | — |
| Refresh data register | RD | Word | Decimal | — | — | RD** | 002CH |

| Category | Device name | Symbol | Type | Notation | ASCII (Q/L) | Binary (Q/L) | ASCII (iQ-R) | Binary (iQ-R) | Reference |
|---|---|---|---|---|---|---|---|---|---|
| Network No. specified device | Link direct device | J□\□ | | | | | | | Page 440 Accessing link direct devices |
| I/O No. specified device | | U | | | | | | | Page 442 Accessing module access devices; Page 444 Accessing CPU buffer memory access device |
| I/O No. specified device | Module access device | U□\G | Word | Decimal | G | ABH | G*** | 00ABH | Page 442 Accessing module access devices |
| I/O No. specified device | CPU buffer memory access device | U3E□\G | Word | Decimal | — | — | G** | 00ABH | Page 444 Accessing CPU buffer memory access device |
| I/O No. specified device | CPU buffer memory access device | U3E□\HG | Word | Decimal | — | — | HG** | 002EH | Page 444 Accessing CPU buffer memory access device |

*1 Page 69 Considerations when accessing long timer, long retentive timer, or long counter
*2 Page 69 Considerations when accessing long index register
*3 Page 69 Consideration when accessing file register
*4 Page 69 Consideration when accessing extended data register or extended link register

**Considerations**

■Devices that cannot be specified

- Devices which are not listed on the list cannot be specified by the command for device access of MC protocol.
- The available device type and device range are in accordance with the device specifications of access target module. Specify the device that can be used for the access target module.
- Accessing a local device is not available.
- When accessing a device that cannot be specified, create a program etc. to copy a value and store the value temporarily in the device that can be specified and access it.
- When a device can be assigned to a standard global label in GX Works3, even the device, to which a device code cannot be specified, can be accessed by specifying the label name. (Page 124 LABEL ACCESS)

■Considerations when accessing long timer, long retentive timer, or long counter

Use any of the following commands.

| Device | Type | Read | Write |
|---|---|---|---|
| Long timer, Long retentive timer | Contact (LTS, LSTS) | Page 86 Batch read in word units (command: 0401)*1 | Page 108 Random write in bit units (test) (command: 1402) |
| Long timer, Long retentive timer | Coil (LTC, LSTC) | Page 86 Batch read in word units (command: 0401)*1 | Page 108 Random write in bit units (test) (command: 1402) |
| Long timer, Long retentive timer | Current value (LTN, LSTN) | Page 86 Batch read in word units (command: 0401); Page 97 Random read in word units (command: 0403) | Page 104 Random write in word units (test) (command: 1402) |
| Long counter | Contact (LCS) | Page 86 Batch read in word units (command: 0401); Page 90 Batch read in bit units (command: 0401) | Page 92 Batch write in word units (command: 1401); Page 95 Batch write in bit units (command: 1401); Page 108 Random write in bit units (test) (command: 1402) |
| Long counter | Coil (LCC) | Page 86 Batch read in word units (command: 0401); Page 90 Batch read in bit units (command: 0401) | Page 92 Batch write in word units (command: 1401); Page 95 Batch write in bit units (command: 1401); Page 108 Random write in bit units (test) (command: 1402) |
| Long counter | Current value (LCN) | Page 86 Batch read in word units (command: 0401); Page 97 Random read in word units (command: 0403) | Page 92 Batch write in word units (command: 1401); Page 104 Random write in word units (test) (command: 1402) |

*1 When reading data with a current value (LTN, LSTN) specified, the values of contacts and coils will be stored in the read data.

■Considerations when accessing long index register

Use a command to which double word access points can be specified.

- Page 97 Random read in word units (command: 0403)
- Page 104 Random write in word units (test) (command: 1402)
- Page 120 Register monitor data (command: 0801)

■Consideration when accessing file register

The file register specified to "Use File Register of Each Program" in "CPU Parameter" or "PLC parameter" of the CPU module cannot be accessed from external devices.

If the file register of the CPU module is consist of multiple blocks, use the device code of the serial number access method.

To specify the file register with the serial number access method, refer to the manual of CPU module.

■Consideration when accessing extended data register or extended link register

If the access target CPU module does not support the access to the extended data register D65536 or later, and the extended link register W10000 or later, replace the extended data register to the file register (ZR) and specify again. For the replacement method, refer to the manual of Q/LCPU module.

#### Number of device points

Specify the number of device points to be read or written.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits. Use capitalized code for alphabetical letter.

■Data communication in binary code: Send the 2-byte numerical value*1 in order from the lower byte (L: bit 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 5 points and 20 points

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 5 points | `0005` (`30H 30H 30H 35H`) | `05H 00H` |
| 20 points | `0014` (`30H 30H 31H 34H`) | `14H 00H` |

**Access points**

Specify the number of device points to be accessed in word unit, double word unit, or bit unit.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits. Use capitalized code for alphabetical letter.

■Data communication in binary code: Send the 1-byte*1 numerical value (hexadecimal).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 5 points and 20 points

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 5 points | `05` (`30H 35H`) | `05H` |
| 20 points | `14` (`31H 34H`) | `14H` |

**Number of bit access points**

Specify the number of device points to be accessed in bit units.

**Number of word access points, number of double word access points**

Specify the number of device points to be accessed in word unit or double word unit.

#### Number of blocks

Specify the number of blocks of the device to be accessed in hexadecimal.

Set each number of blocks within the following range.

- Number of word device blocks + Number of bit device blocks ≤ 120

In the following case, calculate it as access point × 2.

- When accessing module of MELSEC iQ-R series by setting device extension specification (subcommand: 008□)

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits. Use capitalized code for alphabetical letter.

■Data communication in binary code: Send the 1-byte*1 numerical value (hexadecimal).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 5 points and 20 points

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 5 points | `05` (`30H 35H`) | `05H` |
| 20 points | `14` (`31H 34H`) | `14H` |

**Number of word device blocks**

Specify the number of blocks of the word device.

**Number of bit device blocks**

Specify the number of blocks of the bit device.

#### Read data, write data

The read device value is stored for reading, and the data to be written is stored for writing.
The data order differs between bit units or word units.

**For bit units**

The following shows the data to be read and written in bit units.

■Data communication in ASCII code

The ON/OFF status of each device are represented with single-digit ASCII code.

- For ON: '1' (31H)
- For OFF: '0' (30H)

■Data communication in binary code

Represent the ON/OFF status of each device in 4-bit per 1 point.

- For ON: '1'
- For OFF: '0'

When the number of points is odd number, the lowest 4 bits are set to '0'.

**Ex.** When indicating ON/OFF status of five points from M10 (M10=ON, M11=OFF, M12=ON, M13=OFF, M14=ON):

| | ASCII code | Binary code (For C24)*1 | Binary code (For E71) |
|---|---|---|---|
| Value | `1 0 1 0 1` → `31H 30H 31H 30H 31H` | `10H`(DLE)`+10H` `10H`(DLE)`+10H` `10H`(DLE)`+10H` (i.e. `10H 10H 10H 10H 10H 10H`) | `10H 10H 10H` |

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**For word units (16-point unit for bit device)**

The following shows the data to be read and written in word units.
When handling data other than bit data, refer to the following section.
Page 77 Considerations for handling real number data and character string data

■Data communication in ASCII code

Convert the 1-word(16 points of bit device) numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
Use capitalized code for alphabetical letter.
The ON/OFF status of bit device is a value of hexadecimal 1-digit in 4-point units.

**Ex.** When indicating ON/OFF status of 32 points from M16:

- Device code: `M*` → `4DH 2AH`
- Head device: `000016` → `30H 30H 30H 30H 31H 36H`
- Number of device points: `0002` → `30H 30H 30H 32H` (The device point value becomes "0002" in 16-points units.)
- Data: `AB1234CD` → `41H 42H 31H 32H 33H 34H 43H 44H` (the first Data is `AB12`, the second Data is `34CD`)
- Bits of the data (0 = OFF, 1 = ON), left to right: `A B 1 2` = `1010 1011 0001 0010` = M31 to M16; `3 4 C D` = `0011 0100 1100 1101` = M47 to M32

**Ex.** When indicating the stored data of D350 and D351:

- Device code: `D*` → `44H 2AH`
- Head device: `000350` → `30H 30H 30H 33H 35H 30H`
- Number of device points: `0002` → `30H 30H 30H 32H`
- Data: `56AB170F` → `35H 36H 41H 42H 31H 37H 30H 46H` (the first Data is `56AB`: the content of D350 indicates 56ABH (22187 in decimal); the second Data is `170F`: the content of D351 indicates 170FH (5903 in decimal))

■Data communication in binary code

Send the numerical value in order from the lower byte (L: bit 0 to 7) by handling 16 points unit as 2 bytes.

**Ex.** When indicating ON/OFF status of 32 points from M16: byte sequence `10H 00H 00H 90H 02H 00H 12H ABH CDH 34H`, decoded as:

- Head device (3 bytes, lower byte first): `10H 00H 00H` = `000010H` = 16 (M16)
- Device code (1 byte): `90H` (M)
- Number of device points (2 bytes): `02H 00H` = `0002H` (2 words) (The device point value becomes "02" in 16-points units.)
- Data (4 bytes): `12H ABH CDH 34H` (the first Data is `12H ABH`, the second Data is `CDH 34H`) → word 1 (M16–M31) = `AB12H`, word 2 (M32–M47) = `34CDH`
- Bits of the data (0 = OFF, 1 = ON): `12H` = `0001 0010` = M23 to M16; `ABH` = `1010 1011` = M31 to M24; `CDH` = `1100 1101` = M39 to M32; `34H` = `0011 0100` = M47 to M40

**Ex.** When indicating the stored data of D350 and D351: byte sequence `5EH 01H 00H A8H 02H 00H ABH 56H 0FH 17H`, decoded as:

- Head device (3 bytes, lower byte first): `5EH 01H 00H` = `00015EH` = 350 (D350)
- Device code (1 byte): `A8H` (D)
- Number of device points (2 bytes): `02H 00H` = `0002H` (2 words)
- Data (4 bytes): `ABH 56H 0FH 17H` (the first Data is `ABH 56H`, the second Data is `0FH 17H`) → word 1 (D350) = `56ABH` (22187 decimal), word 2 (D351) = `170FH` (5903 decimal)
- The stored value of the request data or the response data: `A B 5 6` `0 F 1 7`; the value to read or write: `5 6 A B` `1 7 0 F`

**For double word unit (32-point unit for bit device)**

The following shows the data to be read and written in double word units.

■Data communication in ASCII code

Convert the 2-word numerical value (32 points of bit device) to 8-digit ASCII code (hexadecimal), and send it from the upper digits.
Use capitalized code for alphabetical letter.
The ON/OFF status of the bit device is 1-digit hexadecimal value in 4-point units.

**Ex.** When indicating ON/OFF status of 32 points from M16: field layout `Device code → Device number → Data`, with device code `M*` (`4DH 2AH`), device number `000016` (`30H 30H 30H 30H 31H 36H`), and data `AB1234CD` (`41H 42H 31H 32H 33H 34H 43H 44H`). Bits of the data (0 = OFF, 1 = ON), left to right: `A B 1 2` = `1010 1011 0001 0010` = M47 to M32; `3 4 C D` = `0011 0100 1100 1101` = M31 to M16.

**Ex.** When indicating the stored data of D350 (D351): field layout `Device code → Device number → Data`, with device code `D*` (`44H 2AH`), device number `000350` (`30H 30H 30H 33H 35H 30H`), and data `170F56AB` (`31H 37H 30H 46H 35H 36H 41H 42H`) — the first 4 digits `170F`: the content of D351 indicates 170FH (5903 in decimal); the last 4 digits `56AB`: the content of D350 indicates 56ABH (22187 in decimal).

■Data communication in binary code

Send the numerical value in order from the lower byte (L: bit 0 to 7) by handling 32 points unit as 4 bytes.

**Ex.** When indicating ON/OFF status of 32 points from M16: byte sequence `10H 00H 00H 90H CDH 34H 12H ABH`, decoded as head device `10H 00H 00H` (M16), device code `90H` (M), data (4 bytes) `CDH 34H 12H ABH` → combined double-word value `AB1234CDH` with low word `34CDH` (M16–M31) and high word `AB12H` (M32–M47). Bits of the data (0 = OFF, 1 = ON): `CDH` = `1100 1101` = M23 to M16; `34H` = `0011 0100` = M31 to M24; `12H` = `0001 0010` = M39 to M32; `ABH` = `1010 1011` = M47 to M40.

**Ex.** When indicating the stored data of D350 (D351): byte sequence `5EH 01H 00H A8H ABH 56H 0FH 17H`, decoded as head device `5EH 01H 00H` (D350), device code `A8H` (D), data (4 bytes) `ABH 56H 0FH 17H` → D350 = `56ABH` (22187 decimal), D351 = `170FH` (5903 decimal). The stored value of the request data or the response data: `A B 5 6` `0 F 1 7`; the value to read or write: `5 6 A B` `1 7 0 F`.

> **Note:** In the PDF figures of this binary-code double word section (PDF page 78, printed page 76), the 3-byte field `10H 00H 00H` (`5EH 01H 00H` in the D350 example) is labeled "Number of device points"; its value is the head device (`000010H` = 16 = M16, `00015EH` = 350 = D350).

#### Considerations for handling real number data and character string data

The word data and double word data are handled as integer value (16-bit data or 32-bit data).
When data other than integer (real number, character string) is stored in a device, the stored value is read as integer value.

- When real number (0.75) is stored in D0 and D1: D0 = 0000H, D1 = 3F40H
- When character string ('12AB') is stored in D2 and D3: D2 = 3231H, D3 = 4241H

For data to be used as real number or character string data in the instructions of the programmable controller, write it to the device/label according to the defined data specification method. For more details on how to specify data used in instructions, refer to the programming manual of the CPU module used.

■For character string data

The following shows the images how character string data is stored.

| Item | For ASCII code character string | For ASCII code character string | For Unicode character string |
|---|---|---|---|
| Character string to be stored | 'ABC' | 'ABCD' | 'ABCD' |
| Character code | '41H', '42H', '43H' | '41H', '42H', '43H', '44H' | '0041H', '0042H', '0043H', '0044H' |
| Image when character string data is stored from D0 | NULL indicates 00H. D0 = B, A (4241H); D1 = NULL, C (0043H) | NULL indicates 00H. D0 = B, A (4241H); D1 = D, C (4443H); D2 = NULL, NULL (0000H) | NULL indicates 0000H. D0 = A (0041H); D1 = B (0042H); D2 = C (0043H); D3 = D (0044H); D4 = NULL (0000H) |

**Ex.** Write ASCII code character string data used in the instructions which handle character strings to word device

Store the character string ('ABCD') to D0 and D1: D0 = 4241H ('BA'), D1 = 4443H ('DC'). (The first character occupies the low byte of the word, the second character the high byte.)

Specify the following data for write data.

| Device | ASCII code | Binary code |
|---|---|---|
| D0 | `B A` = `4 2 4 1` → `34H 32H 34H 31H` | `A B` → `41H 42H` |
| D1 | `D C` = `4 4 4 3` → `34H 34H 34H 33H` | `C D` → `43H 44H` |
| D2 | `NULL NULL` = `0 0 0 0` → `30H 30H 30H 30H` | `NULL NULL` → `00H 00H` |

> **Note:** In the PDF figure of the ASCII code (PDF page 79, printed page 77), the last digit `1` of D0 (`4241`) is printed with `32H` below it; the ASCII code of the digit `1` is `31H`.

> When communicating ASCII code character string data in ASCII code, data is rearranged every two characters and stored.

#### Set/reset

Specify the ON/OFF status of bit device.

- For ON: '1'

| Subcommand type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `01` (`30H 31H`) | `01H` |
| For MELSEC iQ-R series | `0001` (`30H 30H 30H 31H`) | `01H 00H` |

- For OFF: '0'

| Subcommand type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `00` (`30H 30H`) | `00H` |
| For MELSEC iQ-R series | `0000` (`30H 30H 30H 30H`) | `00H 00H` |

#### Monitor condition specification

The following explains the data to be used when specifying monitor conditions by the following commands.

- Random read in word units (command: 0403)
- Register monitor data (command: 0801)

Monitor condition specification: `Monitor condition → Step No. specification → Device specification`

| Item | | | Description | Reference |
|---|---|---|---|---|
| Step No. specification | File specification | File No. | Specify the registration number of file which includes a program to be the conditions in a module. The number for specification can be obtained from the file control command. | Page 82 Step No. specification / Page 298 File No. / Page 299 File name, extension, and attribute |
| | | File name / Extension / Attribute | Specify the file name, extension, attribute of a file which includes a program to be the conditions. | |
| | SFC specification | SFC pattern | Specify this when a program is SFC. | Page 84 SFC specification |
| | | SFC block No. / SFC step No. | Specify the SFC block No. and SFC step No. which include a step to be the conditions. | |
| | Step No. | | Specify the step No., pointer (P) No., or interrupt pointer (I) No. for a program to be the condition. | Page 84 Step No. |
| Device specification | Word device value specification | Device | Specify the device to be a condition. | Page 85 Mask value, monitor conditions when word device value is specified |
| | | Mask value | Specify this when detecting arbitrary bit range of word device. | |
| | | Monitor condition value | Specify the device value to be a condition. | |
| | Bit device value specification | Device | Specify the device to be a condition. | Page 85 Monitor conditions when bit device value is specified |
| | | Monitor condition value | Specify the device value to be a condition. | |

The monitor conditions can be specified by following QCPUs.

- Basic model QCPU
- High Performance model QCPU
- Process CPU

To access a module which does not support this function, select the subcommand that does not specify monitor conditions. For more information on the supported modules, refer to the following section.
Page 471 Accessible Modules for Each Command

When the subcommands which do not specify monitor conditions are selected, each data for specifying the monitor conditions are not necessary.

**Specification of read timing by monitor condition**

The read timing can be changed by specifying monitor conditions according to the selection of subcommand.

■When do not specify monitor condition

Data is read by the END processing after a read request.

Timing chart: `Command message` (External device side, Timer 0 monitoring time) → `Read request` (Module side) → `END processing` (Program, Sequence scan) → `Data` (Module side) → `Response message` (Module side, Timer 2 monitoring time); Timer 1 monitoring time = from the end of the Command message to the start of the Response message.

■When monitor condition is specified

Data is read by the END processing after the specified monitor conditions are satisfied.

Timing chart: `Command message` (External device side, Timer 0 monitoring time) → `Read request` (Module side) → `Timing when the condition is met` → `END processing` (Program, Sequence scan) → `Data` (Module side) → `Response message` (Module side, Timer 2 monitoring time); Timer 1 monitoring time = from the end of the Command message to the start of the Response message.

Monitoring with multiple conditions to the device memory of the same CPU module cannot be performed at the same time. When this command, to which a monitor condition is specified, is executed while monitor with other conditions is being performed, the command is completed abnormally.

**Monitor condition**

Following conditions can be specified as "Monitor condition" for read timing.

| Conditions that can be specified | Condition satisfaction timing |
|---|---|
| Step No. specification | When specified program step is executed |
| Device specification — Word device value specification | When specified word device value reached the specified value |
| Device specification — Bit device value specification | When specified bit device turned ON/OFF |

When the step No. specification and device specification are specified together, the read processing is performed when both of the conditions are satisfied.

Specify the following values according to the conditions.
○: Specified, —: Not specified

| Step No. specification | Device specification (Word device value) | Device specification (Bit device value) | ASCII code | Binary code |
|---|---|---|---|---|
| ○ | — | — | `010F` (`30H 31H 30H 46H`) | `01H 0FH` |
| — | ○ | — | `020F` (`30H 32H 30H 46H`) | `02H 0FH` |
| ○ | ○ | — | `030F` (`30H 33H 30H 46H`) | `03H 0FH` |
| — | — | ○ | `040F` (`30H 34H 30H 46H`) | `04H 0FH` |
| ○ | — | ○ | `050F` (`30H 35H 30H 46H`) | `05H 0FH` |

#### Step No. specification

Specify a condition using a step No. of program.
Data is read at the END processing immediately after a step of the specified program is executed.

■When step No. is specified

Specify the following items.

| Item | | Description | Reference |
|---|---|---|---|
| Step No. specification | File specification — File No. | Specify the registration number of file which includes a program to be the conditions in a module. The number for specification can be obtained from the file control command.*1 | Page 298 File No. |
| | File specification — File name / Extension / Attribute | Specify the file name, extension, attribute of a file which includes a program to be the conditions. | Page 299 File name, extension, and attribute |
| | SFC specification — SFC pattern | Specify this when a program is SFC. | Page 84 SFC specification |
| | SFC specification — SFC block No. / SFC step No. | Specify the SFC block No. and SFC step No. which include a step to be the conditions. | |
| | Step No. | Specify the step No., pointer (P) No., or interrupt pointer (I) No. for a program to be the condition. | Page 84 Step No. |

*1 When 'FFFFH' is specified to the file , the specified file is searched with the file name and extension. In this case, a read and write request from a supported device to the CPU module may be delayed more than one sequence scan time.

> **Note:** In the PDF, the footnote reads "specified to the file , the specified file" (no word appears between "file" and the comma); the *1 mark is attached to the File No. description.

■When step No. is not specified

Set the following when a monitor condition without step No. specification is selected.

- File No.: 0
- File name, extension, attribute: Space (20H)
- SFC specification, step No.: 0

ASCII code: `[File specification: File No.(0000) → File name(20H × 8) → Extension(20H × 3) → Attribute(20H)] → [SFC specification: SFC pattern(0000) → Block No.(0000) → Step No.(0000)] → Step No.(00000000, 8 digits)`
Bytes: `30H 30H 30H 30H | 20H 20H 20H 20H 20H 20H 20H 20H | 20H 20H 20H | 20H | 30H 30H 30H 30H | 30H 30H 30H 30H | 30H 30H 30H 30H | 30H 30H 30H 30H 30H 30H 30H 30H`

Binary code: `[File specification: File No.(00H 00H) → File name(20H × 8) → Extension(20H × 3) → Attribute(20H)] → Step No.(00H 00H 00H 00H) → [SFC specification: Step No.(00H 00H) → Block No.(00H 00H) → SFC pattern(00H 00H)]`
Bytes: `00H 00H | 20H 20H 20H 20H 20H 20H 20H 20H | 20H 20H 20H | 20H | 00H 00H 00H 00H | 00H 00H | 00H 00H | 00H 00H`

#### Device specification

Specify a condition using a device and its value.
Data is read at the END processing immediately after the specified device reached the specified value.

■When word device value is specified

Set the following when a monitor condition which specifies word device value is selected.

| Item | | Description | Reference |
|---|---|---|---|
| Device specification | Word device value specification — Device | Specify the device to be a condition. | Page 65 Devices |
| | Word device value specification — Mask value | Specify this when detecting arbitrary bit range of word device. | Page 85 Mask value, monitor conditions when word device value is specified |
| | Word device value specification — Monitor condition value | Specify the device value to be a condition. | |
| | Bit device value specification — Device | Specify an arbitrary device. | — |
| | Bit device value specification — Monitor condition value | Specify the fixed value (0). | |

ASCII code: `[Word device value specification: Device(Device code → Device number) → Mask value → Monitor condition value] → [Bit device value specification (Fixed value for dummy): Device specification(M*, 000000) → Monitor condition value(00)]`
Bytes of the dummy part: `4DH 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H`

Binary code: `[Word device value specification: Device(Device number → Device code) → Mask value → Monitor condition value] → [Bit device value specification (Fixed value for dummy): Device specification(00H 00H 00H, 90H) → Monitor condition value(00H)]`
Bytes of the dummy part: `00H 00H 00H | 90H | 00H`

■When bit device value is specified

Set the following when a monitor condition which specifies bit device value is selected.

| Item | | Description | Reference |
|---|---|---|---|
| Device specification | Word device value specification — Device | Specify an arbitrary device. | — |
| | Word device value specification — Mask value | Specify the fixed value (0). | |
| | Word device value specification — Monitor condition value | Specify the fixed value (0). | |
| | Bit device value specification — Device | Specify the device to be a condition. | Page 65 Devices |
| | Bit device value specification — Monitor condition value | Specify the condition with the following value. 02H: Condition is satisfied at rising (OFF→ON); 04H: Condition is satisfied at falling (ON→OFF) | Page 85 Monitor conditions when bit device value is specified |

ASCII code: `[Word device value specification (Fixed value for dummy): Device(D*, 000000) → Mask value(0000) → Monitor condition value(0000)] → [Bit device value specification: Device(Device code → Device number) → Monitor condition value]`
Bytes of the dummy part: `44H 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H 30H 30H | 30H 30H 30H 30H`

Binary code: `[Word device value specification (Fixed value for dummy): Device(00H 00H 00H, A8H) → Mask value(00H 00H) → Monitor condition value(00H 00H)] → [Bit device value specification: Device(Device number → Device code) → Monitor condition value]`
Bytes of the dummy part: `00H 00H 00H | A8H | 00H 00H | 00H 00H`

■When no device is specified

Set the following when monitor condition without device specification is selected.

ASCII code: `D* | 000000 | 0000 | 0000 | M* | 000000 | 00`
Bytes: `44H 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H 30H 30H | 30H 30H 30H 30H | 4DH 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H`

Binary code: `00H 00H 00H | A8H | 00H 00H | 00H 00H | 00H 00H 00H | 90H | 00H`

#### SFC specification

The block No. and step No. of SFC (MELSAP3) program can be specified as a monitor condition.

Specify the following values.

| Condition | SFC pattern | SFC block No. | SFC step No. |
|---|---|---|---|
| Specify an SFC program. | 0003H | 0000H to 013FH (0 to 319) | 0000H to 01FFH (0 to 511) |
| Do not specify SFC program | 0000H | 0000H | 0000H |

■Data communication in ASCII code

Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send the 2-byte numerical value*1 in order from the lower byte (L: bit 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 0003H: ASCII `0003` (`30H 30H 30H 33H`); Binary `03H 00H`

**Step No.**

Specify the step No., pointer (P) No., or interrupt pointer (I) No. of the sequence program.
Specify a following 4-byte value.

| Condition | b31 | b30 | b29 to b0 |
|---|---|---|---|
| Specify the step No. of a sequence program | 0 | 0 | (Step No. of the arbitrary sequence program) |
| Specify pointer No. | 0 | 1 | (Arbitrary pointer No.) |
| Specify an interrupt pointer No. | 1 | 0 | (Arbitrary interrupt pointer No.) |
| Do not specify | `00000000H` | | |

■Data communication in ASCII code

Convert the numerical value to 8-digit (hexadecimal) ASCII code, and send it from the upper digits.

■Data communication in binary code

Send the 4-byte numerical value*1 in order from the lower byte (L: bit 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** When specifying the interrupt pointer I28 (8000001CH): ASCII `8000001C` (`38H 30H 30H 30H 30H 30H 31H 43H`); Binary `1CH 00H 00H 80H`

#### Mask value, monitor conditions when word device value is specified

Specify the value of word device to be set as a monitor condition.
Arbitrary bit range of word devices can only be specified by specifying mask value.
(Logical AND by each bit of the specified word device data and the designated mask value is compared with the monitor condition value.)

■Data communication in ASCII code

Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code

Send the 2-byte numerical value*1 in order from the lower byte (L: bit 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** Specifying when bit 0 to 14 of D0 reaches 1000 (3E8H) as a condition:

- Mask value: `7FFFH` → ASCII `7FFF` (`37H 46H 46H 46H`); Binary `FFH 7FH`
- Monitor condition value: `03E8H` → ASCII `03E8` (`30H 33H 45H 38H`); Binary `E8H 03H`

**Monitor conditions when bit device value is specified**

Specify a bit device status change as the monitor condition from either rising (OFF → ON) or falling (ON → OFF).

| Condition | ASCII code | Binary code |
|---|---|---|
| Rising (OFF→ON) | `02` (`30H 32H`) | `02H` |
| Falling (ON→OFF) | `04` (`30H 34H`) | `04H` |

### 8.2 Batch Read and Write

Read or write the values of consecutive devices in batch by specifying the number of device points.

#### Batch read in word units (command: 0401)

Read values from devices in word units.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(0401H) → Subcommand → Head device → Number of device points`
- ■Response data:
  The value of read device is stored in word units. The data order differs between ASCII code or binary code. (Page 72 Read data, write data)

**Data specified by request data**

■Command

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `0401` | `01H 04H` |
| 2C frame | `2` (32H) | — |

■Subcommand

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0000` | `00H 00H` |
| For MELSEC iQ-R series | `0002` | `02H 00H` |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

■Head device

Specify the head device of the consecutive devices. (Page 65 Devices)

The following devices cannot be specified.

- Long timer (contact: LTS, coil: LTC)
- Long retentive timer (contact: LSTS, coil: LSTC)
- Long index register (LZ)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter
Page 69 Considerations when accessing long index register

■Number of device points

Specify the number of device points to be read within the following range in word units. (Page 70 Number of device points)

| Access target | Range (Word device) | Range (Bit device) | Range (Double word device) |
|---|---|---|---|
| MELSEC iQ-R series module / MELSEC iQ-L series module / MELSEC-Q/L series module | 1 to 960 points | 1 to 960 words (1 to 15360 points) | 1 to 960 words (LCN: 1 to 480 points) (LTN, LSTN: 1 to 240 points) |
| MELSEC-QnA series module / Module on other station via MELSEC-QnA series network module | 1 to 480 points | 1 to 480 words (1 to 7680 points) | — |
| MELSEC-A series module | 1 to 64 points | 1 to 32 words (1 to 512 points) | — |

Read 16-point bit device by specifying one point of "Number of device points".
Set the head device number for MELSEC-A series module with a multiple of 16.

■Considerations for reading a long timer or long retentive timer device

When reading data with a current value (LTN, LSTN) specified as the head device, the values of contacts and coils will be stored in the response data.

The configuration of the response data is as follows:

| Data | Description |
|---|---|
| 1st word | The current value is stored. |
| 2nd word | The current value is stored. |
| 3rd word | b0: The value of the coil is stored. b1: The value of the contact is stored. b2 to b15: Used by the system |
| 4th word | Used by the system |

Specify four words per one device as the number of device points in the request data.

**Ex.** When reading the two points of the long timer (LT0 and LT1), specify LTN0 as the head device and eight words as the number of device points.

| Word | Content | Device |
|---|---|---|
| 0 to 1 | LTN0 | LT0 |
| 2 | b1: LTS0, b0: LTC0 | LT0 |
| 3 | Used by the system | LT0 |
| 4 to 5 | LTN1 | LT1 |
| 6 | b1: LTS1, b0: LTC1 | LT1 |
| 7 | Used by the system | LT1 |

**Communication example (Reading bit device)**

Read the value of M100 to M131 (for 2 words). (Subcommand: for MELSEC-Q/L series)

■Data communication in ASCII code

Request data: `Command(0401) → Subcommand(0000) → Device code(M*) → Head device number(000100) → Number of device points(0002)`
Bytes: `30H 34H 30H 31H | 30H 30H 30H 30H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 32H`

Response data (M100 to M131, 32 points): `1234 0002` → ASCII `31H 32H 33H 34H 30H 30H 30H 32H`
(Bits, 0 = OFF, 1 = ON: `1234` = `0001 0010 0011 0100` for M115 to M100; `0002` = `0000 0000 0000 0010` for M131 to M116)

■Data communication in binary code

Request data: `01H 04H 00H 00H 64H 00H 00H 90H 02H 00H`
(Command `01H 04H` = 0401H → Subcommand `00H 00H` = 0000H → Head device number `64H 00H 00H` = 000064H = 100 → Device code `90H` (M) → Number of device points `02H 00H` = 2)

Response data: `34H 12H 02H 00H` (lower byte first per word: word 1 (M100–M115) = `1234H`; word 2 (M116–M131) = `0002H`)
(Bits, 0 = OFF, 1 = ON: `34H` = `0011 0100` for M107 to M100; `12H` = `0001 0010` for M115 to M108; `02H` = `0000 0010` for M123 to M116; `00H` = `0000 0000` for M131 to M124)

**Communication example (Reading word device)**

Read values of T100 to T102. (Subcommand: for MELSEC-Q/L series)
T100 = 4660 (1234H), T101 = 2 (2H), T102 = 7663 (1DEFH) are stored.

■Data communication in ASCII code

Request data: `Command(0401) → Subcommand(0000) → Device code(TN) → Head device number(000100) → Number of device points(0003)`
Bytes: `30H 34H 30H 31H | 30H 30H 30H 30H | 54H 4EH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 33H`

Response data (T100, T101, T102): `1234 0002 1DEF` → ASCII `31H 32H 33H 34H 30H 30H 30H 32H 31H 44H 45H 46H`

■Data communication in binary code

Request data: `01H 04H 00H 00H 64H 00H 00H C2H 03H 00H`
(Command `01H 04H` = 0401H → Subcommand `00H 00H` = 0000H → Head device number `64H 00H 00H` = 100 → Device code `C2H` (TN) → Number of device points `03H 00H` = 3)

Response data: `34H 12H 02H 00H EFH 1DH` (T100 = `1234H`, T101 = `0002H`, T102 = `1DEFH`)

#### Batch read in bit units (command: 0401)

Read values from devices in bit units.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(0401H) → Subcommand → Head device → Number of device points`
- ■Response data:
  The value of read device is stored in bit units. The data order differs between ASCII code or binary code. (Page 72 Read data, write data)

**Data specified by request data**

■Command

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `0401` | `01H 04H` |
| 2C frame | `1` (31H) | — |

■Subcommand

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0001` | `01H 00H` |
| For MELSEC iQ-R series | `0003` | `03H 00H` |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

■Head device

Specify the head device of the consecutive devices. (Page 65 Devices)

The following devices cannot be specified.

- Long timer (contact: LTS, coil: LTC)
- Long retentive timer (contact: LSTS, coil: LSTC)
- Long index register (LZ)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter
Page 69 Considerations when accessing long index register

■Number of device points

Specify the number of device points to be read within the following range. (Page 70 Number of device points)

| Access target | C24 | E71 (ASCII code) | E71 (Binary code) |
|---|---|---|---|
| MELSEC iQ-R series module / MELSEC iQ-L series module / MELSEC-Q/L series module | 1 to 7904 points | 1 to 3584 points | 1 to 7168 points |
| MELSEC-QnA series module / Module on other station via MELSEC-QnA series network module | 1 to 3952 points | 1 to 1792 points | 1 to 3584 points |
| MELSEC-A series module | 1 to 256 points | 1 to 256 points | 1 to 256 points |

**Communication example**

Read values of M100 to M107. (Subcommand: for MELSEC-Q/L series)

■Data communication in ASCII code

Request data: `Command(0401) → Subcommand(0001) → Device code(M*) → Head device number(000100) → Number of device points(0008)`
Bytes: `30H 34H 30H 31H | 30H 30H 30H 31H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 38H`

Response data (M100 to M107, 0 = OFF, 1 = ON): `00010011` (8 single-digit ASCII values, one per point) → `30H 30H 30H 31H 30H 30H 31H 31H`

■Data communication in binary code

Request data: `01H 04H 01H 00H 64H 00H 00H 90H 08H 00H`
(Command `01H 04H` = 0401H → Subcommand `01H 00H` = 0001H → Head device number `64H 00H 00H` = 100 → Device code `90H` (M) → Number of device points `08H 00H` = 8)

Response data: `00H 01H 00H 11H` (4-bit-per-point packed nibbles for M100 to M107: M100/M101=0/0 (byte `00H`), M102/M103=0/1 (byte `01H`), M104/M105=0/0 (byte `00H`), M106/M107=1/1 (byte `11H`); 0 = OFF, 1 = ON)

#### Batch write in word units (command: 1401)

Write values to devices in word units.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1401H) → Subcommand → Head device → Number of device points → Write data`
- ■Response data:
  There is no response data for this command.

**Data specified by request data**

■Command

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `1401` (31H 34H 30H 31H) | `01H 14H` |
| 2C frame | `4` (34H) | — |

■Subcommand

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0000` (30H 30H 30H 30H) | `00H 00H` |
| For MELSEC iQ-R series | `0002` (30H 30H 30H 32H) | `02H 00H` |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

■Head device

Specify the head device of the consecutive devices. (Page 65 Devices)

The following devices cannot be specified.

- Long timer (contact: LTS, coil: LTC, current value: LTN)
- Long retentive timer (contact: LSTS, coil: LSTC, current value: LSTN)
- Long index register (LZ)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter
Page 69 Considerations when accessing long index register

■Number of device points

Specify the number of device points to be written within the following range in word units. (Page 70 Number of device points)

| Access target | Word device | Bit device | Double word device |
|---|---|---|---|
| MELSEC iQ-R series module / MELSEC iQ-L series module / MELSEC-Q/L series module | 1 to 960 points | 1 to 960 words (1 to 15360 points) | 1 to 960 words (1 to 480 points) |
| MELSEC-QnA series module / Module on other station via MELSEC-QnA series network module | 1 to 480 points | 1 to 480 words (1 to 7680 points) | — |
| MELSEC-A series module | 1 to 64 points | 1 to 10 words (1 to 160 points) | — |

For bit device, read 16-point bit device by specifying one point of "Number of device points".
Set the head device number for MELSEC-A series module with a multiple of 16.

■Write data

Specify the data to be written for the number of device points in hexadecimal. (Page 72 Read data, write data)

**Communication example (Writing bit device)**

Write the values to M100 to M131 (for 2 words). (Subcommand: for MELSEC-Q/L series)

■Data communication in ASCII code

Request data: `Command(1401) → Subcommand(0000) → Device code(M*) → Head device number(000100) → Number of device points(0002) → Write data(2347AB96)`
Bytes: `31H 34H 30H 31H | 30H 30H 30H 30H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 32H | 32H 33H 34H 37H 41H 42H 39H 36H`
(Write data bits, 0 = OFF, 1 = ON: `2347` = `0010 0011 0100 0111` for M115 to M100; `AB96` = `1010 1011 1001 0110` for M131 to M116)

■Data communication in binary code

Request data: `01H 14H 00H 00H 64H 00H 00H 90H 02H 00H 47H 23H 96H ABH`
(Command `01H 14H` = 1401H → Subcommand `00H 00H` = 0000H → Head device number `64H 00H 00H` = 100 → Device code `90H` (M) → Number of device points `02H 00H` = 2 → Write data `47H 23H 96H ABH`, i.e. word 1 (M100–M115) = `2347H`, word 2 (M116–M131) = `AB96H`)
(Write data bits, 0 = OFF, 1 = ON: `47H` = `0100 0111` for M107 to M100; `23H` = `0010 0011` for M115 to M108; `96H` = `1001 0110` for M123 to M116; `ABH` = `1010 1011` for M131 to M124)

**Communication example (Writing word device)**

Write '6549' (1995H) to D100, '4610' (1202H) to D101, and '4400' (1130H) to D102. (Subcommand: for MELSEC-Q/L series)

■Data communication in ASCII code

Request data: `Command(1401) → Subcommand(0000) → Device code(D*) → Head device number(000100) → Number of device points(0003) → Write data(199512021130)`
Bytes: `31H 34H 30H 31H | 30H 30H 30H 30H | 44H 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 33H | 31H 39H 39H 35H 31H 32H 30H 32H 31H 31H 33H 30H`
(Write data: D100 = `1995`, D101 = `1202`, D102 = `1130`)

■Data communication in binary code

Request data: `01H 14H 00H 00H 64H 00H 00H A8H 03H 00H 95H 19H 02H 12H 30H 11H`
(Command `01H 14H` = 1401H → Subcommand `00H 00H` = 0000H → Head device number `64H 00H 00H` = 100 → Device code `A8H` (D) → Number of device points `03H 00H` = 3 → Write data: D100 = `1995H` (`95H 19H`), D101 = `1202H` (`02H 12H`), D102 = `1130H` (`30H 11H`))

#### Batch write in bit units (command: 1401)

Write values to devices in bit units.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1401H) → Subcommand → Head device → Number of device points → Write data`
- ■Response data:
  There is no response data for this command.

**Data specified by request data**

■Command

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `1401` (31H 34H 30H 31H) | `01H 14H` |
| 2C frame | `3` (33H) | — |

■Subcommand

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0001` (30H 30H 30H 31H) | `01H 00H` |
| For MELSEC iQ-R series | `0003` (30H 30H 30H 33H) | `03H 00H` |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

■Head device

Specify the head device of the consecutive devices. (Page 65 Devices)

The following devices cannot be specified.

- Long timer (contact: LTS, coil: LTC, current value: LTN)
- Long retentive timer (contact: LSTS, coil: LSTC, current value: LSTN)
- Long counter (current value: LCN)
- Long index register (LZ)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter
Page 69 Considerations when accessing long index register

■Number of device points

Specify the number of device points to be written within the following range. (Page 70 Number of device points)

| Access target | C24 | E71 (ASCII code) | E71 (Binary code) |
|---|---|---|---|
| MELSEC iQ-R series module / MELSEC iQ-L series module / MELSEC-Q/L series module | 1 to 7904 points | 1 to 3584 points | 1 to 7168 points |
| MELSEC-QnA series module / Module on other station via MELSEC-QnA series network module | 1 to 3952 points | 1 to 1792 points | 1 to 3584 points |
| MELSEC-A series module | 1 to 160 points | 1 to 160 points | 1 to 160 points |

■Write data

Specify the value to be written to a device for the number equivalent to the specified number of device points. (Page 72 Read data, write data)

**Communication example**

Write values to M100 to M107. (Subcommand: for MELSEC-Q/L series)

■Data communication in ASCII code

Request data: `Command(1401) → Subcommand(0001) → Device code(M*) → Head device number(000100) → Number of device points(0008) → Write data(11001100)`
Bytes: `31H 34H 30H 31H | 30H 30H 30H 31H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 38H | 31H 31H 30H 30H 31H 31H 30H 30H`
(Write data for M100 to M107, 0 = OFF, 1 = ON: one ASCII digit per point, M100=1, M101=1, M102=0, M103=0, M104=1, M105=1, M106=0, M107=0)

■Data communication in binary code

Request data: `01H 14H 01H 00H 64H 00H 00H 90H 08H 00H 11H 00H 11H 00H`
(Command `01H 14H` = 1401H → Subcommand `01H 00H` = 0001H → Head device number `64H 00H 00H` = 100 → Device code `90H` (M) → Number of device points `08H 00H` = 8 → Write data `11H 00H 11H 00H`, i.e. 4-bit-per-point packed nibbles for M100 to M107: M100/M101=1/1 (byte `11H`), M102/M103=0/0 (byte `00H`), M104/M105=1/1 (byte `11H`), M106/M107=0/0 (byte `00H`); 0 = OFF, 1 = ON)

### 8.3 Random Read and Write

Read or write device values by specifying the device numbers. It can be specified with discontinuous device numbers.

#### Random read in word units (command: 0403)

Read values from devices in word units and double word units. It can be specified with discontinuous device number.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand → [Monitor condition specification designation] → Number of word access points (m points) → Number of double word access points (n points) → Device for word access (1st point) → ... → Device for word access (mth point) → Device for double word access (1st point) → ... → Device for double word access (nth point)`
- ■Response data:
  `Data read in word units (1st point) → ... → Data read in word units (mth point) → Data read in double word units (1st point) → ... → Data read in double word units (nth point)`
  The value of read device is stored in word units and in double word units. The data order differs between ASCII code or binary code. (Page 72 Read data, write data)

**Data specified by request data**

**Command**

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `0403` (30H 34H 30H 33H) | 03H 04H |
| 2C frame | `5` (35H) | — |

**Subcommand**

The read timing can be changed by specifying monitor conditions according to the selection of subcommand. (Page 79 Monitor condition specification)

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands that do not specify monitor conditions for MELSEC-Q/L series.

*When do not specify monitor condition*

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0000` (30H 30H 30H 30H) | 00H 00H |
| For MELSEC iQ-R series | `0002` (30H 30H 30H 32H) | 02H 00H |

*When specifying a monitoring condition[^monrestrict]*

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0040` (30H 30H 34H 30H) | 40H 00H |

[^monrestrict]: The access targets to which a monitor conditions can be specified have some restrictions. (Page 471 Accessible Modules for Each Command)

At monitor condition specification, use the subcommand 00C0 for the device extension specification. The message format for device extension specification is the same as that of 008□. Refer to it by substituting 008□ to 00C0. (Page 438 Read/Write by Device Extension Specification)

**Monitor condition specification**

Specify the conditions for timing to read data. (Page 79 Monitor condition specification)

When do not specify the monitor condition, the specification of this data item is not required.

**Number of word access points, number of double word access points**

Specify the number of device points to be read within the following range. (Page 70 Access points)

| Access target | Range |
|---|---|
| MELSEC iQ-R series module (subcommand: 0000) | 1 ≤ Number of word access points + Number of double word access points ≤ 192 points |
| MELSEC iQ-L series module (subcommand: 0000) | 1 ≤ Number of word access points + Number of double word access points ≤ 192 points |
| MELSEC-Q/L series module (subcommand: 0000) | 1 ≤ Number of word access points + Number of double word access points ≤ 192 points |
| MELSEC iQ-R series module (subcommand: 0002, 008□) | 1 ≤ Number of word access points + Number of double word access points ≤ 96 points |
| MELSEC iQ-L series module (subcommand: 0080) | 1 ≤ Number of word access points + Number of double word access points ≤ 96 points |
| MELSEC-Q/L series module (subcommand: 0080) | 1 ≤ Number of word access points + Number of double word access points ≤ 96 points |
| MELSEC-QnA series module | 1 ≤ Number of word access points + Number of double word access points ≤ 96 points |
| Module on other station via MELSEC-QnA series network module | 1 ≤ Number of word access points + Number of double word access points ≤ 96 points |
| MELSEC-A series module | Cannot be used. |

The number of point is specified in the following units depending on device type.

| Device type | Number of word access points | Number of double word access points |
|---|---|---|
| Bit device | 16-point units | 32-point units |
| Word device, double word device | 1 word units | 2 word units |

When using subcommand for MELSEC-Q/L series module, calculate it as access points × 2 in the following case.

- When specifying the file register (ZR) of High Performance model QCPU

**Device**

Specify the device to be read. (Page 65 Devices)

The number of points equivalent to the specified 'Number of word access points' and 'Number of double word access points' is specified for 'Device', respectively. When '0' is specified for the access points, this specification is not required.

The following devices cannot be specified.

- Long timer (contact: LTS, coil: LTC)
- Long retentive timer (contact: LSTS, coil: LSTC)
- Long counter (contact: LCS, coil: LCC)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter

**Communication example (Monitor condition is not specified)**

Read values of D0, T0, M100 to M115, X20 to X2F with word access. Read values of D1500 to D1501, Y160 to Y17F, M1111 to M1142 with 3 double word access. (Subcommand: for MELSEC-Q/L series)

D0 = 6549 (1995H), T0 = 4610 (1202H), D1500 = 20302 (4F4EH), D1501 = 19540 (4C54H) are stored.

> **Note:** The PDF text reads "4 double word access", but the request data of this example (Number of double word access points = 03 in ASCII code, 03H in binary code) specifies three double word accesses: D1500 to D1501, Y160 to Y17F and M1111 to M1142. The text here therefore says 3.

- ■Data communication in ASCII code:
  Request data: `Command(0403) → Subcommand(0000) → Number of word access points(04) → Number of double word access points(03) → Device for word access [Device code(D*) → Device number(000000) → Device code(TN) → Device number(000000) → Device code(M*) → Device number(000100) → Device code(X*) → Device number(000020)] → Device for double word access [Device code(D*) → Device number(001500) → Device code(Y*) → Device number(000160) → Device code(M*) → Device number(001111)]`
  Bytes: `30H 34H 30H 33H | 30H 30H 30H 30H | 30H 34H | 30H 33H | 44H 2AH | 30H 30H 30H 30H 30H 30H | 54H 4EH | 30H 30H 30H 30H 30H 30H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 58H 2AH | 30H 30H 30H 30H 32H 30H | 44H 2AH | 30H 30H 31H 35H 30H 30H | 59H 2AH | 30H 30H 30H 31H 36H 30H | 4DH 2AH | 30H 30H 31H 31H 31H 31H`
  (Word access: D0, T0, M100 (M100 to M115), X20 (X20 to X2F). Double word access: D1500 (D1500 to D1501), Y160 (Y160 to Y17F), M1111 (M1111 to M1142).)
  Response data: `Read data 1 in word units(1995) → Read data 2 in word units(1202) → Read data 3 in word units(2030) → Read data 4 in word units(4849) → Read data 1 in double word units(4C544F4E) → Read data 2 in double word units(C3DEB9AF) → Read data 3 in double word units(BADDBCB7)`
  Bytes: `31H 39H 39H 35H | 31H 32H 30H 32H | 32H 30H 33H 30H | 34H 38H 34H 39H | 34H 43H 35H 34H 34H 46H 34H 45H | 43H 33H 44H 45H 42H 39H 41H 46H | 42H 41H 44H 44H 42H 43H 42H 37H`
  (Read data 1 in word units = D0, Read data 2 = T0, Read data 3 = M115 to M100, Read data 4 = X2F to X20; Read data 1 in double word units = D1501 (`4C54`) then D1500 (`4F4E`), Read data 2 = Y17F to Y160, Read data 3 = M1142 to M1111. Bit patterns (0 = OFF, 1 = ON): `2030` = `0010 0000 0011 0000` (M115 to M100), `4849` = `0100 1000 0100 1001` (X2F to X20). The PDF's bit diagrams of the double word data omit the middle bits: `C3DEB9AF` (Y17F to Y160) is shown as `110000` ... `101111`, `BADDBCB7` (M1142 to M1111) as `1011101` ... `010110111`.)
- ■Data communication in binary code:
  Request data: `Command(03H 04H) → Subcommand(00H 00H) → Number of word access points(04H) → Number of double word access points(03H) → Device for word access [Device number(00H 00H 00H) → Device code(A8H) → Device number(00H 00H 00H) → Device code(C2H) → Device number(64H 00H 00H) → Device code(90H) → Device number(20H 00H 00H) → Device code(9CH)] → Device for double word access [Device number(DCH 05H 00H) → Device code(A8H) → Device number(60H 01H 00H) → Device code(9DH) → Device number(57H 04H 00H) → Device code(90H)]`
  Bytes: `03H 04H | 00H 00H | 04H | 03H | 00H 00H 00H | A8H | 00H 00H 00H | C2H | 64H 00H 00H | 90H | 20H 00H 00H | 9CH | DCH 05H 00H | A8H | 60H 01H 00H | 9DH | 57H 04H 00H | 90H`
  (Word access: D0, T0, M100, X20. Double word access: D1500, Y160, M1111.)
  Response data: `Data read 1 in word units(95H 19H) → Data read 2 in word units(02H 12H) → Data read 3 in word units(30H 20H) → Data read 4 in word units(49H 48H) → Data read 1 in double word units(4EH 4FH 54H 4CH) → Data read 2 in double word units(AFH B9H DEH C3H) → Data read 3 in double word units(B7H BCH DDH BAH)`
  Bytes: `95H 19H | 02H 12H | 30H 20H | 49H 48H | 4EH 4FH 54H 4CH | AFH B9H DEH C3H | B7H BCH DDH BAH`
  (Data read 1 in word units = D0, Data read 2 = T0, Data read 3 = M115 to M100, Data read 4 = X2F to X20; Data read 1 in double word units = D1500 (`4EH 4FH`) then D1501 (`54H 4CH`), Data read 2 = Y17F to Y160, Data read 3 = M1142 to M1111. Bit patterns (0 = OFF, 1 = ON): `30H` = M107 to M100 = `0011 0000`, `20H` = M115 to M108 = `0010 0000`; `49H` = X27 to X20 = `0100 1001`, `48H` = X2F to X28 = `0100 1000`; `AFH` = Y167 to Y160 = `1010 1111`, `C3H` = Y17F to Y178 = `1100 0011`; `B7H` = M1118 to M1111 = `1011 0111`, `BAH` = M1142 to M1135 = `1011 1010`.)

> **Note:** In the binary response diagram of the PDF, the third word-unit field is labelled "Data read 2 in word units" (its bit diagram shows that it is Data read 3, M107 to M100 and M115 to M108), and the last double word field is labelled "M1141 to M1111" (its bit diagrams show M1118 to M1111 and M1142 to M1135).

**Communication example (Monitor condition is specified)**

Read values of D0, T0, M100 to M115, X20 to X2F with word access. Read values of D1500 to D1501, Y160 to Y17F, M1111 to M1142 with 3 double word access. The monitor condition is as follows: When the value of link register (W100) reached '7BH' (123) while the step No.1000 of program file CONB1.QPG is being executed.

> **Note:** As in the previous example, the PDF text reads "4 double word access", while the request data specifies 03 (03H) double word access points; the text here says 3.

- ■Data communication in ASCII code:
  Request data: `Command(0403) → Subcommand(0040) → Monitor condition(030F) → File designation [File No.(0001) → File name(CONB1 followed by three spaces) → Extension(QPG) → Attribute(space)] → SFC designation (not specified) [SFC pattern(0000) → Block No.(0000) → Step No.(0000)] → Step No.(000003E8) → Word device value designation [Device code(W*) → Device number(000100) → Mask value(FFFF) → Monitor condition value(007B)] → Bit device value designation (fixed value for dummy) [Device code(M*) → Device number(000000) → Monitor condition value(00)] → Number of word access points(04) → Number of double word access points(03) → Device for word access [Device code(D*) → Device number(000000) → Device code(TN) → Device number(000000) → Device code(M*) → Device number(000100) → Device code(X*) → Device number(000020)] → Device for double word access [Device code(D*) → Device number(001500) → Device code(Y*) → Device number(000160) → Device code(M*) → Device number(001111)]`
  Bytes: `30H 34H 30H 33H | 30H 30H 34H 30H | 30H 33H 30H 46H | 30H 30H 30H 31H | 43H 4FH 4EH 42H 31H 20H 20H 20H | 51H 50H 47H | 20H | 30H 30H 30H 30H | 30H 30H 30H 30H | 30H 30H 30H 30H | 30H 30H 30H 30H 30H 33H 45H 38H | 57H 2AH | 30H 30H 30H 31H 30H 30H | 46H 46H 46H 46H | 30H 30H 37H 42H | 4DH 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H | 30H 34H | 30H 33H | 44H 2AH | 30H 30H 30H 30H 30H 30H | 54H 4EH | 30H 30H 30H 30H 30H 30H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 58H 2AH | 30H 30H 30H 30H 32H 30H | 44H 2AH | 30H 30H 31H 35H 30H 30H | 59H 2AH | 30H 30H 30H 31H 36H 30H | 4DH 2AH | 30H 30H 31H 31H 31H 31H`
  (File name `CONB1` followed by three spaces = `43H 4FH 4EH 42H 31H 20H 20H 20H`, Extension `QPG`, Attribute = space (`20H`). Step No. `000003E8` = 1000 (step No.1000). Word device value designation: link register W100, Monitor condition value `007B` = 7BH (123). Word access: D0, T0, M100, X20. Double word access: D1500, Y160, M1111.)
  Response data: It is the same as the communication example when monitor condition is not specified. (Page 100 Data communication in ASCII code)
- ■Data communication in binary code:
  Request data: `Command(03H 04H) → Subcommand(40H 00H) → Monitor condition(03H 0FH) → File designation [File No.(01H 00H) → File name(43H 4FH 4EH 42H 31H 20H 20H 20H) → Extension(51H 50H 47H) → Attribute(20H)] → Step No.(E8H 03H 00H 00H) → SFC designation (not specified) [Step No.(00H 00H) → Block No.(00H 00H) → SFC pattern(00H 00H)] → Word device value designation [Device number(00H 01H 00H) → Device code(B4H) → Mask value(FFH FFH) → Monitor condition value(7BH 00H)] → Bit device value designation (fixed value for dummy) [Device number(00H 00H 00H) → Device code(90H) → Monitor condition value(00H)] → Number of word access points(04H) → Number of double word access points(03H) → Device for word access [Device number(00H 00H 00H) → Device code(A8H) → Device number(00H 00H 00H) → Device code(C2H) → Device number(64H 00H 00H) → Device code(90H) → Device number(20H 00H 00H) → Device code(9CH)] → Device for double word access [Device number(DCH 05H 00H) → Device code(A8H) → Device number(60H 01H 00H) → Device code(9DH) → Device number(57H 04H 00H) → Device code(90H)]`
  Bytes: `03H 04H | 40H 00H | 03H 0FH | 01H 00H | 43H 4FH 4EH 42H 31H 20H 20H 20H | 51H 50H 47H | 20H | E8H 03H 00H 00H | 00H 00H | 00H 00H | 00H 00H | 00H 01H 00H | B4H | FFH FFH | 7BH 00H | 00H 00H 00H | 90H | 00H | 04H | 03H | 00H 00H 00H | A8H | 00H 00H 00H | C2H | 64H 00H 00H | 90H | 20H 00H 00H | 9CH | DCH 05H 00H | A8H | 60H 01H 00H | 9DH | 57H 04H 00H | 90H`
  (Step No. `E8H 03H 00H 00H` = 1000 (step No.1000). Word device value designation: link register W100, Monitor condition value `7BH 00H` = 7BH (123). Word access: D0, T0, M100, X20. Double word access: D1500, Y160, M1111.)
  Response data: It is the same as the communication example when monitor condition is not specified. (Page 101 Data communication in binary code)

#### Random write in word units (test) (command: 1402)

Write values to devices in word units and double word units. It can be specified with discontinuous device numbers.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand → Number of word access points (m points) → Number of double word access points (n points) → Device for word access [Device (first point) → Write data (first point) → ... → Device (mth point) → Write data (mth point)] → Device for double word access [Device (first point) → Write data (first point) → ... → Device (nth point) → Write data (nth point)]`
- ■Response data: There is no response data for this command.

> **Note:** In the PDF diagram (printed page 104), the last "Write data" of the word access part (after "Device (mth point)") is labelled "Write data (nth point)"; it is the write data of the mth point of the device for word access.

**Data specified by request data**

**Command**

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `1402` (31H 34H 30H 32H) | 02H 14H |
| 2C frame | `7` (37H) | — |

**Subcommand**

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0000` (30H 30H 30H 30H) | 00H 00H |
| For MELSEC iQ-R series | `0002` (30H 30H 30H 32H) | 02H 00H |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

**Number of word access points, number of double word access points**

Specify the number of device points to be written within the following range. (Page 70 Access points)

| Access target | Range |
|---|---|
| MELSEC iQ-R series module (subcommand: 0000) | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 1920 points |
| MELSEC iQ-L series module (subcommand: 0000) | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 1920 points |
| MELSEC-Q/L series module (subcommand: 0000) | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 1920 points |
| MELSEC iQ-R series module (subcommand: 0002, 008□) | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 960 points |
| MELSEC iQ-L series module (subcommand: 0080) | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 960 points |
| MELSEC-Q/L series module (subcommand: 0080) | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 960 points |
| MELSEC-QnA series module | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 960 points |
| Module on other station via MELSEC-QnA series network module | 1 ≤ (Number of word access points × 12) + (Number of double word access points × 14) ≤ 960 points |
| MELSEC-A series module | 1 ≤ Number of word access points ≤ 10 points |

The number of point is specified in the following units depending on device type.

| Device type | Number of word access points | Number of double word access points |
|---|---|---|
| Bit device | 16-point units | 32-point units |
| Word device, double word device | 1 word units | 2 word units |

**Device**

Specify a device to be written. (Page 65 Devices)

Set the head device number with a multiple of 16 for bit device access of MELSEC-A series module.

**Write data**

Specify the values to be written to device. (Page 72 Read data, write data)

Specify the write data in hexadecimal.

Specify 'Device' and 'Write data' with number of points which are specified in 'Number of word access points' and 'Number of double word access points'. When the access point is set to '0', the specification is not required.

**Communication example**

Write values to devices as follows. (Subcommand: for MELSEC-Q/L series)

| Item | Device to be written |
|---|---|
| Word access | D0, D1, M100 to M115, X20 to X2F |
| Double word access | D1500 to D1501, Y160 to Y17F, M1111 to M1142 |

- ■Data communication in ASCII code:
  Request data: `Command(1402) → Subcommand(0000) → Number of word access points(04) → Number of double word access points(03) → Device for word access [Device code(D*) → Device number(000000) → Write data(0550) → Device code(D*) → Device number(000001) → Write data(0575) → Device code(M*) → Device number(000100) → Write data 1(0540) → Device code(X*) → Device number(000020) → Write data 2(0583)] → Device for double word access [Device code(D*) → Device number(001500) → Write data(04391202) → Device code(Y*) → Device number(000160) → Write data 3(23752607) → Device code(M*) → Device number(001111) → Write data 4(04250475)]`
  Bytes: `31H 34H 30H 32H | 30H 30H 30H 30H | 30H 34H | 30H 33H | 44H 2AH | 30H 30H 30H 30H 30H 30H | 30H 35H 35H 30H | 44H 2AH | 30H 30H 30H 30H 30H 31H | 30H 35H 37H 35H | 4DH 2AH | 30H 30H 30H 31H 30H 30H | 30H 35H 34H 30H | 58H 2AH | 30H 30H 30H 30H 32H 30H | 30H 35H 38H 33H | 44H 2AH | 30H 30H 31H 35H 30H 30H | 30H 34H 33H 39H 31H 32H 30H 32H | 59H 2AH | 30H 30H 30H 31H 36H 30H | 32H 33H 37H 35H 32H 36H 30H 37H | 4DH 2AH | 30H 30H 31H 31H 31H 31H | 30H 34H 32H 35H 30H 34H 37H 35H`
  (Word access: D0 = `0550`, D1 = `0575`, M100 to M115 = `0540` (Write data 1), X20 to X2F = `0583` (Write data 2). Double word access: D1500 to D1501 = `04391202`, Y160 to Y17F = `23752607` (Write data 3), M1111 to M1142 = `04250475` (Write data 4). Bit patterns (0 = OFF, 1 = ON): Write data 1 in word units `0540` = `0000 0101 0100 0000` (M115 to M100), Write data 2 in word units `0583` = `0000 0101 1000 0011` (X2F to X20), Write data 3 in double word units `23752607`: `23` = Y17F to Y178 = `0010 0011`, `07` = Y167 to Y160 = `0000 0111`, Write data 4 in double word units `04250475`: `04` = M1142 to M1135 = `0000 0100`, `75` = M1118 to M1111 = `0111 0101`.)
- ■Data communication in binary code:
  Request data: `Command(02H 14H) → Subcommand(00H 00H) → Number of word access points(04H) → Number of double word access points(03H) → Device for word access [Device number(00H 00H 00H) → Device code(A8H) → Write data(50H 05H) → Device number(01H 00H 00H) → Device code(A8H) → Write data(75H 05H) → Device number(64H 00H 00H) → Device code(90H) → Write data 1(40H 05H) → Device number(20H 00H 00H) → Device code(9CH) → Write data 2(83H 05H)] → Device for double word access [Device number(DCH 05H 00H) → Device code(A8H) → Write data(02H 12H 39H 04H) → Device number(60H 01H 00H) → Device code(9DH) → Write data 3(07H 26H 75H 23H) → Device number(57H 04H 00H) → Device code(90H) → Write data 4(75H 04H 25H 04H)]`
  Bytes: `02H 14H | 00H 00H | 04H | 03H | 00H 00H 00H | A8H | 50H 05H | 01H 00H 00H | A8H | 75H 05H | 64H 00H 00H | 90H | 40H 05H | 20H 00H 00H | 9CH | 83H 05H | DCH 05H 00H | A8H | 02H 12H 39H 04H | 60H 01H 00H | 9DH | 07H 26H 75H 23H | 57H 04H 00H | 90H | 75H 04H 25H 04H`
  (Word access: D0 = `50H 05H`, D1 = `75H 05H`, M100 to M115 = `40H 05H` (Write data 1), X20 to X2F = `83H 05H` (Write data 2). Double word access: D1500 to D1501 = `02H 12H 39H 04H`, Y160 to Y17F = `07H 26H 75H 23H` (Write data 3), M1111 to M1142 = `75H 04H 25H 04H` (Write data 4). Bit patterns (0 = OFF, 1 = ON): Write data 1 in word units: `40H` = M107 to M100 = `0100 0000`, `05H` = M115 to M108 = `0000 0101`; Write data 2 in word units: `83H` = `1000 0011`, `05H` = `0000 0101`; Write data 3 in double word units: `07H` = Y167 to Y160 = `0000 0111`, `23H` = Y17F to Y178 = `0010 0011`; Write data 4 in double word units: `75H` = M1118 to M1111 = `0111 0101`, `04H` = M1142 to M1135 = `0000 0100`.)

#### Random write in bit units (test) (command: 1402)

Write values to devices in bit units. It can be specified with discontinuous device number.

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand → Number of bit access points (n points) → Device (first point) → Set/reset (first point) → ... → Device (nth point) → Set/reset (nth point)`
- ■Response data: There is no response data for this command.

**Request data**

**Command**

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `1402` (31H 34H 30H 32H) | 02H 14H |
| 2C frame | `6` (36H) | — |

**Subcommand**

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0001` (30H 30H 30H 31H) | 01H 00H |
| For MELSEC iQ-R series | `0003` (30H 30H 30H 33H) | 03H 00H |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

**Number of bit access points**

Specify the number of device points to be written within the following range. (Page 70 Number of device points)

| Access target | Range |
|---|---|
| MELSEC iQ-R series module (subcommand: 0001) | 1 to 188 points |
| MELSEC iQ-L series module (subcommand: 0001) | 1 to 188 points |
| MELSEC-Q/L series module (subcommand: 0001) | 1 to 188 points |
| MELSEC iQ-R series module (subcommand: 0003, 008□) | 1 to 94 points |
| MELSEC iQ-L series module (subcommand: 0081) | 1 to 94 points |
| MELSEC-Q/L series module (subcommand: 0081) | 1 to 94 points |
| MELSEC-QnA series module | 1 to 94 points |
| Module on other station via MELSEC-QnA series network module | 1 to 94 points |
| MELSEC-A series module | 1 to 20 points |

**Device**

Specify a device to be written. (Page 65 Devices)

Specify a bit device.

**Set/reset**

Specify the ON/OFF status of bit devices. (Page 78 Set/reset)

The number of points equivalent to the specified "Number of bit access points" is specified for "Device" and "Set/reset", respectively.

**Communication example**

Turn M50 OFF, and turn Y2F ON. (Subcommand: for MELSEC-Q/L series)

- ■Data communication in ASCII code:
  Request data: `Command(1402) → Subcommand(0001) → Number of bit access points(02) → Device code(M*) → Device number(000050) → Set/reset(00) → Device code(Y*) → Device number(00002F) → Set/reset(01)`
  Bytes: `31H 34H 30H 32H | 30H 30H 30H 31H | 30H 32H | 4DH 2AH | 30H 30H 30H 30H 35H 30H | 30H 30H | 59H 2AH | 30H 30H 30H 30H 32H 46H | 30H 31H`
  (M50 = Set/reset `00` (OFF), Y2F = Set/reset `01` (ON).)
- ■Data communication in binary code:
  Request data: `Command(02H 14H) → Subcommand(01H 00H) → Number of bit access points(02H) → Device number(32H 00H 00H) → Device code(90H) → Set/reset(00H) → Device number(2FH 00H 00H) → Device code(9DH) → Set/reset(01H)`
  Bytes: `02H 14H | 01H 00H | 02H | 32H 00H 00H | 90H | 00H | 2FH 00H 00H | 9DH | 01H`
  (M50 = Set/reset `00H` (OFF), Y2F = Set/reset `01H` (ON).)

### 8.4 Batch Read and Write Multiple Blocks

Read or write values for specified multiple blocks by handling consecutive devices as one block.

#### Batch read multiple blocks (command: 0406)

Read values for specified multiple blocks by handling consecutive word devices or bit devices as one block. Each block can be specified with discontinuous device numbers.

> When communicating with a Universal model QCPU or an LCPU, if other than "Specify service process execution counts" is selected for "Service Processing Setting" of the CPU module, data separation may occur. To avoid data separation, select "Specify service process execution counts".

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand → Number of word device blocks (m points) → Number of bit device blocks (n points) → Block of word device (first point) → ... → Block of word device (mth point) → Block of bit device (first point) → ... → Block of bit device (nth point)`
- ■Response data:
  The value of read device is stored in hexadecimal. The data order differs between ASCII code or binary code. (Page 72 Read data, write data)
  `Data for the number of word device blocks [Word device: Data in the first block → ... → Data in the mth block] → Data for the number of bit device blocks [Bit device: Data in the first block → ... → Data in the nth block]`

**Data specified by request data**

**Command**

| ASCII code | Binary code |
|---|---|
| `0406` (30H 34H 30H 36H) | 06H 04H |

**Subcommand**

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0000` (30H 30H 30H 30H) | 00H 00H |
| For MELSEC iQ-R series | `0002` (30H 30H 30H 32H) | 02H 00H |

**Number of word device blocks, number of bit device blocks**

Specify the number of device blocks to be read in hexadecimal. (Page 71 Number of blocks)

Specify the total number of each block within the following range.

| Access target | Range |
|---|---|
| MELSEC iQ-R series module (subcommand: 0000) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC iQ-L series module (subcommand: 0000) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC-Q/L series module (subcommand: 0000) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC-QnA series module | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC iQ-R series module (subcommand: 0002, 008□) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 60 points |
| MELSEC iQ-L series module (subcommand: 0080) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 60 points |
| MELSEC-Q/L series module (subcommand: 0080) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 60 points |

**Block of word device, block of bit device**

Specify the device to be read by handling consecutive devices as one block.

Specify the block with the number of points equivalent to the specified "Number of word device blocks" and "Number of bit device blocks", respectively. When '0' is specified, this specification is not required.

Specify the following items for each block:

Block (1 point): `Head device → Number of device points`

- Device: Specify the head device of the consecutive devices. (Page 65 Devices)
- Number of device points: Specify the number of device points to be read. (Page 70 Number of device points)

Specify the total number of device points for each block within the range of 1 to 960. Word device is 1-word per one point, and bit device is 16-bit for one point.

Use a bit device block when the contact and coil for the following devices are specified:

- Timer
- Retentive timer
- Counter

The following devices cannot be specified:

- Long timer (contact: LTS, coil: LTC, current value: LTN)
- Long retentive timer (contact: LSTS, coil: LSTC, current value: LSTN)
- Long counter (contact: LCS, coil: LCC, current value: LCN)
- Long index register (LZ)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter
Page 69 Considerations when accessing long index register

**Communication example**

Read values from device as follows. (Subcommand: for MELSEC-Q/L series)

| Item | Content to be read |
|---|---|
| Word device | Block 1: D0 to D3 (4 points); Block 2: W100 to W107 (8 points) |
| Bit device | Block 1: M0 to M31 (2 points); Block 2: M128 to M159 (2 points); Block 3: B100 to B12F (3 points) |

- ■Data communication in ASCII code (Request data):
  - `Command(0406) → Subcommand(0000) → Number of word device blocks(02) → Number of bit device blocks(03)`
    Bytes: `30H 34H 30H 36H | 30H 30H 30H 30H | 30H 32H | 30H 33H`
  - Word device block 1 (D0 to D3): `Device code(D*) → Device number(000000) → Number of device points(0004)`
    Bytes: `44H 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H 30H 34H`
  - Word device block 2 (W100 to W107): `Device code(W*) → Device number(000100) → Number of device points(0008)`
    Bytes: `57H 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 38H`
  - Bit device block 1 (M0 to M31): `Device code(M*) → Device number(000000) → Number of device points(0002)`
    Bytes: `4DH 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H 30H 32H`
  - Bit device block 2 (M128 to M159): `Device code(M*) → Device number(000128) → Number of device points(0002)`
    Bytes: `4DH 2AH | 30H 30H 30H 31H 32H 38H | 30H 30H 30H 32H`
  - Bit device block 3 (B100 to B12F): `Device code(B*) → Device number(000100) → Number of device points(0003)`
    Bytes: `42H 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 33H`
- ■Data communication in ASCII code (Response data):
  - Word device, data in the first block: D0 = `0008`, D1 = `2030`, D2 = `1545`, D3 = `2800`
    Bytes: `30H 30H 30H 38H | 32H 30H 33H 30H | 31H 35H 34H 35H | 32H 38H 30H 30H`
  - Word device, data in the second block: W100 = `0970` ... W107 = `0131`
    Bytes: `30H 39H 37H 30H | ... | 30H 31H 33H 31H`
  - Bit device, data in the first block: M15 to M0 = `2030`, M31 to M16 = `4849`
    Bytes: `32H 30H 33H 30H | 34H 38H 34H 39H`
  - Bit device, data in the second block: M143 to M128 = `C3DE`, M159 to M144 = `2800`
    Bytes: `43H 33H 44H 45H | 32H 38H 30H 30H`
  - Bit device, data in the third block: B10F to B100 = `0970`, B11F to B110 = `B9AF`, B12F to B120 = `B9AF`
    Bytes: `30H 39H 37H 30H | 42H 39H 41H 46H | 42H 39H 41H 46H`
  - Bit order of M15 to M0 (`2030`): `0010 0000 0011 0000` (0 = OFF, 1 = ON)
- ■Data communication in binary code (Request data):
  - `06H 04H 00H 00H 02H 03H` (Command `06H 04H` → Subcommand `00H 00H` → Number of word device blocks `02H` → Number of bit device blocks `03H`)
  - Word device block 1 (D0 to D3): `00H 00H 00H A8H 04H 00H` (Device number `00H 00H 00H` → Device code `A8H` → Number of device points `04H 00H`)
  - Word device block 2 (W100 to W107): `00H 01H 00H B4H 08H 00H` (Device number `00H 01H 00H` → Device code `B4H` → Number of device points `08H 00H`)
  - Bit device block 1 (M0 to M31): `00H 00H 00H 90H 02H 00H` (Device number `00H 00H 00H` → Device code `90H` → Number of device points `02H 00H`)
  - Bit device block 2 (M128 to M159): `80H 00H 00H 90H 02H 00H` (Device number `80H 00H 00H` → Device code `90H` → Number of device points `02H 00H`)
  - Bit device block 3 (B100 to B12F): `00H 01H 00H A0H 03H 00H` (Device number `00H 01H 00H` → Device code `A0H` → Number of device points `03H 00H`)
- ■Data communication in binary code (Response data):
  - Word device, data in the first block: D0 = `08H 00H`, D1 = `30H 20H`, D2 = `45H 15H`, D3 = `00H 28H`
  - Word device, data in the second block: W100 = `70H 09H` ... W107 = `31H 01H`
  - Bit device, data in the first block: M15 to M0 = `30H 20H`, M31 to M16 = `49H 48H`
  - Bit device, data in the second block: M143 to M128 = `DEH C3H`, M159 to M144 = `00H 28H`
  - Bit device, data in the third block: B10F to B100 = `70H 09H`, B11F to B110 = `AFH B9H`, B12F to B120 = `AFH B9H`
  - Bit order of M15 to M0 (`30H 20H`): M7 to M0 = `0011 0000` (30H), M15 to M8 = `0010 0000` (20H) (0 = OFF, 1 = ON)

#### Batch write multiple blocks (command: 1406)

Write values for specified multiple blocks by handling consecutive word devices or bit devices as one block. Each block can be specified with discontinuous device numbers.

> When communicating with a Universal model QCPU or an LCPU, if other than "Specify service process execution counts" is selected for "Service Processing Setting" of the CPU module, data separation may occur. To avoid data separation, select "Specify service process execution counts".

When accessing any of the following devices, use the device extension specification (subcommand: 008□).

- Link direct device
- Module access device
- CPU buffer memory access device

For the message format for device extension specification, refer to the following section.
Page 438 Read/Write by Device Extension Specification

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand → Number of word device blocks (m points) → Number of bit device blocks (n points) → Block of word device (1st point) → ... → Block of word device (mth point) → Block of bit device (1st point) → ... → Block of bit device (nth point)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

**Command**

| ASCII code | Binary code |
|---|---|
| `1406` (31H 34H 30H 36H) | 06H 14H |

**Subcommand**

| Type | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `0000` (30H 30H 30H 30H) | 00H 00H |
| For MELSEC iQ-R series | `0002` (30H 30H 30H 32H) | 02H 00H |

**Number of word device blocks, number of bit device blocks**

Specify the number of device blocks to be written in hexadecimal. (Page 71 Number of blocks)

Specify the total number of each block within the following range.

| Access target | Range |
|---|---|
| MELSEC iQ-R series module (subcommand: 0000) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC iQ-L series module (subcommand: 0000) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC-Q/L series module (subcommand: 0000) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC-QnA series module | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 120 points |
| MELSEC iQ-R series module (subcommand: 0002, 008□) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 60 points |
| MELSEC iQ-L series module (subcommand: 0080) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 60 points |
| MELSEC-Q/L series module (subcommand: 0080) | 1 ≤ Number of word device blocks + Number of bit device blocks ≤ 60 points |

**Block of word device, Block of bit device**

Specify the device to be written by handling consecutive devices as one block.

Specify the block with the number of points equivalent to the specified "Number of word device blocks" and "Number of bit device blocks", respectively. When '0' is specified, this specification is not required.

Specify the following items for each block:

`Block (1 point): Head device → Number of device points → Write data`

- Device: Specify the head device of the consecutive devices. (Page 65 Devices)
- Number of device points: Specify the number of device points to be written. (Page 70 Number of device points)
- Write data: Specify the data to be written for the number of device points in hexadecimal. (Page 72 Read data, write data)

Specify the number of device points within the following range.

| Subcommand | Range |
|---|---|
| When using MELSEC-Q/L series (0000, 0080) | 1 ≤ (Total number of each block × 4) + (Total number of device) ≤ 960 points |
| When using MELSEC iQ-R series (0002, 0082) | 1 ≤ (Total number of each block × 9) + (Total number of device) ≤ 960 points |

Word device is 1-word per one point, and bit device is 16-bit for one point.

Use a bit device block when the contact and coil for the following devices are specified:

- Timer
- Retentive timer
- Counter

The following devices cannot be specified:

- Long timer (contact: LTS, coil: LTC, current value: LTN)
- Long retentive timer (contact: LSTS, coil: LSTC, current value: LSTN)
- Long counter (contact: LCS, coil: LCC, current value: LCN)
- Long index register (LZ)

Page 69 Considerations when accessing long timer, long retentive timer, or long counter
Page 69 Considerations when accessing long index register

**Communication example**

Write values to devices as follows. (Subcommand: for MELSEC-Q/L series)

- ■Data communication in ASCII code (Request data):
  - `Command(1406) → Subcommand(0000) → Number of word device blocks(02) → Number of bit device blocks(03)`
    Bytes: `31H 34H 30H 36H | 30H 30H 30H 30H | 30H 32H | 30H 33H`
  - Word device block (D0 to D3): `Device code(D*) → Device number(000000) → Number of device points(0004) → Write data(D0 = 0008 ... D3 = 2800)`
    Bytes: `44H 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H 30H 34H | 30H 30H 30H 38H ... 32H 38H 30H 30H`
  - Word device block (W100 to W107): `Device code(W*) → Device number(000100) → Number of device points(0008) → Write data(W100 = 0970 ... W107 = 0131)`
    Bytes: `57H 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 38H | 30H 39H 37H 30H ... 30H 31H 33H 31H`
  - Bit device block (M0 to M31): `Device code(M*) → Device number(000000) → Number of device points(0002) → Write data(M15 to M0 = 2030, M31 to M16 = 4849)`
    Bytes: `4DH 2AH | 30H 30H 30H 30H 30H 30H | 30H 30H 30H 32H | 32H 30H 33H 30H | 34H 38H 34H 39H`
    > **Note:** In the PDF (printed page 117), the byte label under the first character "4" of the write data `4849` (M31 to M16) is printed as 32H. The ASCII code of "4" is 34H.
  - Bit device block (M128 to M159): `Device code(M*) → Device number(000128) → Number of device points(0002) → Write data(M143 to M128 = C3DE, M159 to M144 = 2800)`
    Bytes: `4DH 2AH | 30H 30H 30H 31H 32H 38H | 30H 30H 30H 32H | 43H 33H 44H 45H | 32H 38H 30H 30H`
  - Bit device block (B100 to B12F): `Device code(B*) → Device number(000100) → Number of device points(0003) → Write data(B10F to B100 = 0970 ... B12F to B120 = B9AF)`
    Bytes: `42H 2AH | 30H 30H 30H 31H 30H 30H | 30H 30H 30H 33H | 30H 39H 37H 30H ... 42H 39H 41H 46H`
  - Bit order of B12F to B120 (`B9AF`): `1011 1001 1010 1111` (0 = OFF, 1 = ON)
- ■Data communication in binary code (Request data):
  - `06H 14H 00H 00H 02H 03H` (Command `06H 14H` → Subcommand `00H 00H` → Number of word device blocks `02H` → Number of bit device blocks `03H`)
  - Word device block (D0 to D3): `00H 00H 00H A8H 04H 00H 08H 00H ... 00H 28H` (Device number `00H 00H 00H` → Device code `A8H` → Number of device points `04H 00H` → Write data: D0 = `08H 00H` ... D3 = `00H 28H`)
  - Word device block (W100 to W107): `00H 01H 00H B4H 08H 00H 70H 09H ... 31H 01H` (Device number `00H 01H 00H` → Device code `B4H` → Number of device points `08H 00H` → Write data: W100 = `70H 09H` ... W107 = `31H 01H`)
  - Bit device block (M0 to M31): `00H 00H 00H 90H 02H 00H 30H 20H 49H 48H` (Device number `00H 00H 00H` → Device code `90H` → Number of device points `02H 00H` → Write data: M15 to M0 = `30H 20H`, M31 to M16 = `49H 48H`)
  - Bit device block (M128 to M159): `80H 00H 00H 90H 02H 00H DEH C3H 00H 28H` (Device number `80H 00H 00H` → Device code `90H` → Number of device points `02H 00H` → Write data: M143 to M128 = `DEH C3H`, M159 to M144 = `00H 28H`)
  - Bit device block (B100 to B12F): `00H 01H 00H A0H 03H 00H 70H 09H ... AFH B9H` (Device number `00H 01H 00H` → Device code `A0H` → Number of device points `03H 00H` → Write data: B10F to B100 = `70H 09H` ... B12F to B120 = `AFH B9H`)
  - Bit order of B10F to B100 (`70H 09H`): B107 to B100 = `0111 0000` (70H), B10F to B108 = `0000 1001` (09H) (0 = OFF, 1 = ON)

### 8.5 Device Memory Monitor

Read the registered device data, and monitor it.

**Monitoring procedure**

The following shows the procedure to monitor devices.

1. **Registration of monitor device** — Register a device to be read. (Page 120 Register monitor data (command: 0801))
2. **Execution of monitor** — Read values from a registered device. (Page 121 Monitor (command: 0802))
3. **Registration of monitor device** — Reregister a device when a device to be read is changed. (Page 120 Register monitor data (command: 0801))

- Monitoring with multiple conditions to the device memory of the same CPU module cannot be performed at the same time. If monitor (command: 0802) is executed while monitor with other conditions is being performed, the command is completed abnormally.
- Register devices to be read with the 'register monitor data' (command: 0801) before executing the 'monitor' (command: 0802). If the 'monitor' (command: 0802) is executed without the registration, the command is completed abnormally.
- When the access target module is restarted, the registered content is deleted. Register the devices to be read again with the 'register monitor data' (command: 0801).

#### Register monitor data (command: 0801)

Register devices to be monitored.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand → [Monitor condition specification designation] → Number of word access points (m points) → Number of double word access points (n points) → Device for word access (1st point) → ... → Device for word access (mth point) → Device for double word access (1st point) → ... → Device for double word access (nth point)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

**Command**

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `0801` (30H 38H 30H 31H) | 01H 08H |
| 2C frame | `8` (38H) | — |

The data other than commands is the same as the data specified by 'random read in word units' (command: 0403).
Page 97 Random read in word units (command: 0403)

#### Monitor (command: 0802)

Read value of registered device.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command → Subcommand`
- ■Response data:
  `Read data in word units (1st point) → ... → Read data in word units (mth point) → Data read in double word units (1st point) → ... → Data read in double word units (nth point)`
  The value of read device is stored in word units and in double word units. The data order differs between ASCII code or binary code. (Page 72 Read data, write data)

**Data specified by request data**

**Command**

| Frame | ASCII code | Binary code |
|---|---|---|
| 4C/3C/4E/3E frame | `0802` (30H 38H 30H 32H) | 02H 08H |
| 2C frame | `9` (39H) | — |

**Subcommand**

| ASCII code | Binary code |
|---|---|
| `0000` (30H 30H 30H 30H) | 00H 00H |

For 2C frame, the specification is not required. Functions and specification methods are equivalent to the subcommands for MELSEC-Q/L series.

**Communication example**

Read the following devices registered by the 'register monitor data' (command: 0801):

- Word access: D0, T0, M100 to M115, X20 to X2F
- Double word access: D1500 to D1501, Y160 to Y17F, M1111 to M1142

D0 = 6549 (1995H), T0 = 4610 (1202H), D1500 = 20302 (4F4EH), D1501 = 19540 (4C54H) are stored.

- ■Data communication in ASCII code (Request data): `Command(0802) → Subcommand(0000)`
  Bytes: `30H 38H 30H 32H | 30H 30H 30H 30H`
- ■Data communication in ASCII code (Response data):
  - In word units: Read data 1 (D0) = `1995`, Read data 2 (T0) = `1202`, Read data 3 (M115 to M100) = `2030`, Read data 4 (X2F to X20) = `4849`
    Bytes: `31H 39H 39H 35H | 31H 32H 30H 32H | 32H 30H 33H 30H | 34H 38H 34H 39H`
  - In double word units: Read data 1 (D1501 = `4C54`, D1500 = `4F4E`) = `4C544F4E`, Read data 2 (Y17F to Y160) = `C3DEB9AF`, Read data 3 (M1142 to M1111) = `BADDBCB7`
    Bytes: `34H 43H 35H 34H 34H 46H 34H 45H | 43H 33H 44H 45H 42H 39H 41H 46H | 42H 41H 44H 44H 42H 43H 42H 37H`
  - Bit order of read data in word units (0 = OFF, 1 = ON): Read data 3, M115 to M100 (`2030`) = `0010 0000 0011 0000`; Read data 4, X2F to X20 (`4849`) = `0100 1000 0100 1001`
  - Bit order of read data in double word units (0 = OFF, 1 = ON; the middle bits are shown as dashes in the PDF): Read data 2, Y17F to Y160 (`C3DEB9AF`) = `1100 00 - - - 10 1111`; Read data 3, M1142 to M1111 (`BADDBCB7`) = `1011 101 - - - 0 1011 0111`
- ■Data communication in binary code (Request data): `02H 08H 00H 00H` (Command `02H 08H` → Subcommand `00H 00H`)
- ■Data communication in binary code (Response data):
  - In word units: Data read 1 (D0) = `95H 19H`, Data read 2 (T0) = `02H 12H`, Data read 3 (M115 to M100) = `30H 20H`, Data read 4 (X2F to X20) = `49H 48H`
  - In double word units: Data read 1 (D1500 = `4EH 4FH`, D1501 = `54H 4CH`) = `4EH 4FH 54H 4CH`, Data read 2 (Y17F to Y160) = `AFH B9H DEH C3H`, Data read 3 (M1142 to M1111) = `B7H BCH DDH BAH`
  - Bit order of data read in word units (0 = OFF, 1 = ON): Data read 3: M107 to M100 = `0011 0000` (30H), M115 to M108 = `0010 0000` (20H); Data read 4: X27 to X20 = `0100 1001` (49H), X2F to X28 = `0100 1000` (48H)
  - Bit order of data read in double word units (0 = OFF, 1 = ON): Data read 2: Y167 to Y160 = `1010 1111` (AFH), Y17F to Y178 = `1100 0011` (C3H); Data read 3: M1118 to M1111 = `1011 0111` (B7H), M1142 to M1135 = `1011 1010` (BAH)

---

## 9 LABEL ACCESS

This section explains the commands to read and write devices using the standard global label of GX Works3.

The commands can be used when the connected station and request target is MELSEC iQ-R series module.

- Local labels and module labels cannot be accessed.
- The global labels set with GX Works2 cannot be accessed.
- Safety global labels, safety local labels, and standard/safety shared global labels of the safety CPU cannot be accessed.
- Enable "Access from External Device" with the global label setting editor of GX Works3 at the time of label access. (Disabled by default.)

### 9.1 Data to be Specified in Commands

This section explains the contents and specification methods for data items which are set in each command related to label access.

#### Labels

Specify the global label name to be accessed.

The "Label name" is specified by variable length. Specify the length of character string by "Label name length". (Null is unnecessary at the end of a label name character string.)

**Field sequence:**

- ASCII code: `Label name length(4 digits) → Label name(Variable length)`
- Binary code: `Label name length(2 bytes) → Label name(Variable length)`

#### Label name length

Specify the number of characters of a label name.

■Data communication in ASCII code
Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code
Send 2-byte*1 numerical values from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** When the number of characters is three.

| | ASCII code | Binary code |
|---|---|---|
| Label name length | `30H 30H 30H 33H` (0003) | `03H 00H` |

#### Label name

Specify the character string of a label name.

■Data communication in ASCII code
Convert the numerical value of UTF-16, which indicates a global label name, to ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code
Send the numerical value of UTF-16, which indicates a global label name from lower byte (L: bit 0 to 7).

**Ex.** When 'A' (UTF-16: 'A' = 0041)

| | ASCII code | Binary code |
|---|---|---|
| Label name 'A' | `30H 30H 34H 31H` (0041) | `41H 00H` |

> **Note:** The PDF (PDF page 127, printed page 125) prints the last ASCII byte of this example as `41H` under the digit "1". The ASCII code of "1" is `31H`, so the sequence for "0041" is `30H 30H 34H 31H`.

**Labels of the simple data type**

Specify the label name as it is.

Bit specification (example: `Lbl.3`) and digit specification (example: `K4Lbl`) cannot be used.

**Ex.** When the label name is 'Lbl'

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l |
| UTF-16 | 004C / 0062 / 006C |
| ASCII code | 30303443 / 30303632 / 30303643 |
| Binary code | 4C00 / 6200 / 6C00 |

> **Note:** In the PDF, the first row of the character-code tables on PDF pages 127 and 128 (printed pages 125 and 126: simple data type, array type, two-dimensional/three-dimensional array, structure and array member) is printed as "File name". It holds the characters of the label name and is shown here as "Label name".

**Array type label**

Specify the element number with square brackets `[ ]` after label name. (UTF-16: `[` = 005B, `]` = 005D)

The element name of array cannot be specified in the square bracket. Specify a numerical value of the element number.

**Ex.** When the element number of array name 'Lbl' is '20'

- Label name length: `7H`
- Label name character string: `Lbl[20]`

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / [ / 2 / 0 / ] |
| UTF-16 | 004C / 0062 / 006C / 005B / 0032 / 0030 / 005D |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303330 / 30303544 |
| Binary code | 4C00 / 6200 / 6C00 / 5B00 / 3200 / 3000 / 5D00 |

■Decimal/hexadecimal notation of element numbers
An element number can be specified in decimal or hexadecimal.

The element numbers can be distinguished by attaching 'k' or 'h' in front of the numerical value. (UTF-16: 'k' = 006B, 'h' = 0068) When only numerical value is specified, it will be handled as a decimal number.

- Decimal: Numerical value only, or attach 'k' in front of the numerical value. (Example: `Lbl[10]`, `Lbl[k10]`)
- Hexadecimal: Attach 'h' in front of the numerical value. (Example: `Lbl[h10]`)

**■Two-dimensional array, three-dimensional array**

Up to three-dimensional arrays can be specified.

For two-dimension and three-dimension array, specify an element number by separating with comma (',') in a square bracket. (UTF-16: ',' = 002C)

**Ex.** When the element numbers are 2, 1, 3 of three-dimensional array of array name 'Lbl'

- Label name length: `AH`
- Label name character string: `Lbl[2,1,3]`

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / [ / 2 / , / 1 / , / 3 / ] |
| UTF-16 | 004C / 0062 / 006C / 005B / 0032 / 002C / 0031 / 002C / 0033 / 005D |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303243 / 30303331 / 30303243 / 30303333 / 30303544 |
| Binary code | 4C00 / 6200 / 6C00 / 5B00 / 3200 / 2C00 / 3100 / 2C00 / 3300 / 5D00 |

> Two-dimensional array and three-dimensional array bit type labels cannot be specified with the 'batch read/write array type labels' (command: 041A, 141A).

**Structure labels**

Specify the structure labels by attaching period '.' as an element name. (UTF-16: '.' = 002E)

Specify the element name for the end member. Only specifying a structure name cannot set the whole structure as a target.

**Ex.** For the element name 'Data' of the structure name 'Str1'

- Label name length: `9H`
- Label name character string: `Str1.Data`

| Item | Value of code corresponding to character |
|---|---|
| Label name | S / t / r / 1 / . / D / a / t / a |
| UTF-16 | 0053 / 0074 / 0072 / 0031 / 002E / 0044 / 0061 / 0074 / 0061 |
| ASCII code | 30303533 / 30303734 / 30303732 / 30303331 / 30303245 / 30303434 / 30303631 / 30303734 / 30303631 |
| Binary code | 5300 / 7400 / 7200 / 3100 / 2E00 / 4400 / 6100 / 7400 / 6100 |

■Array member
When the structure member is an array type, specify the array element number with square bracket `[ ]` as is the case in array type label.

**Ex.** When the element name 'Data' of structure name 'Str1', and 'Data' is a two-dimensional array element.

- Label name length: `EH`
- Label name character string: `Str1.Data[1,3]`

| Item | Value of code corresponding to character |
|---|---|
| Label name | S / t / r / 1 / . / D / a / t / a / [ / 1 / , / 3 / ] |
| UTF-16 | 0053 / 0074 / 0072 / 0031 / 002E / 0044 / 0061 / 0074 / 0061 / 005B / 0031 / 002C / 0033 / 005D |
| ASCII code | 30303533 / 30303734 / 30303732 / 30303331 / 30303245 / 30303434 / 30303631 / 30303734 / 30303631 / 30303542 / 30303331 / 30303243 / 30303333 / 30303544 |
| Binary code | 5300 / 7400 / 7200 / 3100 / 2E00 / 4400 / 6100 / 7400 / 6100 / 5B00 / 3100 / 2C00 / 3300 / 5D00 |

■Structure member
When the structure member is a structure type, specify the element name by delimiting with period '.' up to the end member.

**Ex.** When specifying the member, 'memberB1', of the structure member name 'memberA3' for the structure type label name 'LabelA'

- Label name character string: `LabelA.memberA3.memberB1`

> When arbitrary device is assigned, the structure type label with structure type members cannot be specified.

**Timer type label, counter type label**

The labels of the following data types are handled as a structure with a contact, coil, and current value for its element.

- Timer
- Retentive timer
- Counter
- Long timer
- Long retentive timer
- Long counter

Specify the following element name with adding a period '.'. (UTF-16: '.' = 002E)

| Item | Element name | Example when a label name is 'Lbl1' |
|---|---|---|
| Contact | S | Lbl1.S |
| Coil | C | Lbl1.C |
| Current value | N | Lbl1.N |

**Ex.** For the contact of timer type label name 'Lbl1'

- Label name length: `6H`
- Label name character string: `Lbl1.S`

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 1 / . / S |
| UTF-16 | 004C / 0062 / 006C / 0031 / 002E / 0053 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303331 / 30303245 / 30303533 |
| Binary code | 4C00 / 6200 / 6C00 / 3100 / 2E00 / 5300 |

> The labels of which data type is timer, counter, retentive timer, long timer, long counter, or long retentive timer cannot be specified with the 'batch read/write array type labels' (command: 041A, 141A).

**Abbreviation specification of label**

Abbreviation specification can be used when specifying a structure type label as an access target.

When a label name or a structure member name is specified by the abbreviation specification, the character string specified as a "label name" can be simplified using "%n" (n: offset value).

**Ex.** When a structure type label name 'LabelA' and its structure member name 'memberA3' is specified as abbreviation specification, they can be abbreviated as follows. (LabelA = %1, memberA3 = %2)

| Actual label name | Abbreviated label name |
|---|---|
| LabelA.memberA1 | %1.memberA1 |
| LabelA.memberA2 | %1.memberA2 |
| LabelA.memberA3.memberB1 | %1.%2.memberB1 |
| LabelA.memberA3.memberB2 | %1.%2.memberB2 |

**When do not abbreviate labels**
Specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| Label points | `30H 30H 30H 30H` (0000) | `00H 00H` |

**When abbreviate labels**
Specify the points to be specified and label name.

Label names can be abbreviated ('%1' to '%n') as an offset value (1 to n) in the specified order.

**Field sequence:** `Label points(n points) → Label specification (1st point): Label name length → Label name → ... → Label specification (nth point): Label name length → Label name`

The character strings that include a dot (.) cannot be abbreviated. Abbreviate character string in a label name or member name unit.

The label name and member name of the array type cannot be abbreviated.

**Ex.** The example that cannot be abbreviated is as follows:

| Actual label name | String that cannot be abbreviated | String that can be abbreviated |
|---|---|---|
| LabelA.memberA3.memberB1 | "LabelA.memberA3.memberB1", "LabelA.memberA3" | 'LabelA', 'memberA3', 'memberB1' |
| LabelA.memberA4[1].memberB1 | "memberA4", "memberA4[1]" | "LabelA", "memberB1" |

■Label points
Specify the label points which abbreviate a label names by abbreviation specification. (Page 130 Points)

■Label specification
Specify the following items for each label for the points specified by label points. (Page 124 Labels)

- Label name length: Specify the number of characters of a label name or structure member name.
- Label name: Specify a global label name or a structure member name.

**Ex.** When a structure type label name 'LabelA' and its structure member name 'memberA3' is specified as abbreviation specification

(1) LabelA

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / a / b / e / l / A |
| UTF-16 | 004C / 0061 / 0062 / 0065 / 006C / 0041 |
| ASCII code | 30303443 / 30303631 / 30303632 / 30303635 / 30303643 / 30303431 |
| Binary code | 4C00 / 6100 / 6200 / 6500 / 6C00 / 4100 |

(2) memberA3

| Item | Value of code corresponding to character |
|---|---|
| Label name | m / e / m / b / e / r / A / 3 |
| UTF-16 | 006D / 0065 / 006D / 0062 / 0065 / 0072 / 0041 / 0033 |
| ASCII code | 30303644 / 30303635 / 30303644 / 30303632 / 30303635 / 30303732 / 30303431 / 30303333 |
| Binary code | 6D00 / 6500 / 6D00 / 6200 / 6500 / 7200 / 4100 / 3300 |

■Data communication in ASCII code

`Label points(2) → Label specification (1st point): Label name length(6) → Label name(1) → Label specification (2nd point): Label name length(8) → Label name(2)`

Bytes: `30H 30H 30H 32H | 30H 30H 30H 36H | (1) | 30H 30H 30H 38H | (2)`

In the figure (1) and (2), set the value of "ASCII code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in binary code

`Label points(2) → Label specification (1st point): Label name length(6) → Label name(1) → Label specification (2nd point): Label name length(8) → Label name(2)`

Bytes: `02H 00H | 06H 00H | (1) | 08H 00H | (2)`

In the figure (1) and (2), set the value of "Binary code" indicated in the table of "Value of code corresponding to character" of each label name.

#### Points

Specify the number of the data to be read or written.

**Setting method**

The setting method of each item to specify the number of points is common.

Since data to be transmitted is 1920 bytes at maximum, the maximum number of points which can be specified varies depending on the label name length contained in the data.

■Data communication in ASCII code
Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code
Send 2-byte*1 numerical values from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 3 points:

| | ASCII code | Binary code |
|---|---|---|
| Points | `30H 30H 30H 33H` (0003) | `03H 00H` |

**Array points**

Specify the number of arrays.

**Label points**

Specify the number of labels.

#### Data type ID

The data type of the read label is stored.

When writing data, the data type ID is not specified. The specified Follow the data type of the specified label.

The value of data type ID of each data type is shown below.

| Data type | Data type ID (decimal) | Setting value (hexadecimal) |
|---|---|---|
| Bit | 1 | 01H |
| Word [Unsigned]/Bit String [16-bit] | 2 | 02H |
| Double Word [Unsigned]/Bit String [32-bit] | 3 | 03H |
| Word [Signed] | 4 | 04H |
| Double Word [Signed] | 5 | 05H |
| FLOAT [Single Precision] | 6 | 06H |
| FLOAT [Double Precision] | 7 | 07H |
| Time | 8 | 08H |
| String | 9 | 09H |
| String [Unicode] | 10 | 0AH |
| Pointer | Cannot be specified. | Cannot be specified. |
| Timer / Counter / Retentive timer — Contact / Coil | 1 | 01H |
| Timer / Counter / Retentive timer — Current value | 2 | 02H |
| Long timer / Long counter / Long retentive timer — Contact / Coil | 1 | 01H |
| Long timer / Long counter / Long retentive timer — Current value | 3 | 03H |

For an array type label and a structure type label, the data type of the element from which value is read is stored.

**Setting method**

■Data communication in ASCII code
Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code
Send a 1-byte numerical value.

**Ex.** Double Word [Signed]:

| | ASCII code | Binary code |
|---|---|---|
| Data type ID | `30H 35H` (05) | `05H` |

#### Data length, unit specification

Specify the length of data to be read/written.

**Unit specification**

Specify the units of data length when reading/writing data from/to array in batch.

■Bit specification
The value of data length is handled as the number of bits.

Specify this when the data type of the label is bit.

| | ASCII code | Binary code |
|---|---|---|
| Bit specification | `30H 30H 30H 30H` (0000) | `00H 00H` |

Read data or write data are stored in 16 point unit of bit devices (2 bytes).

When data length is not a multiple of 16 at the time of bit specification, '0' is stored in an invalid area.

■Byte specification
The value of data length is handled as a number of bytes of read data or write data.

Specify when the data type of a label is other than bit.

| | ASCII code | Binary code |
|---|---|---|
| Byte specification | `30H 30H 30H 31H` (0001) | `01H 00H` |

**Data length**

Specify the length of read data or write data.

Specify the following values according to the data type of a label.

Specifying and writing data length which is not suited for the data type of label results in abnormal completion.

| Data type | Batch read and write array type labels: Unit specification | Batch read and write array type labels: Array data length | Random read and write: Data length per 1 point label |
|---|---|---|---|
| Bit | Bit units | 1 × Number of array element | 2 |
| Word [Unsigned]/Bit String [16-bit] | Byte units | 2 × Number of array element | 2 |
| Double Word [Unsigned]/Bit String [32-bit] | Byte units | 4 × Number of array element | 4 |
| Word [Signed] | Byte units | 2 × Number of array element | 2 |
| Double Word [Signed] | Byte units | 4 × Number of array element | 4 |
| FLOAT [Single Precision] | Byte units | 4 × Number of array element | 4 |
| FLOAT [Double Precision] | Byte units | 8 × Number of array element | 8 |
| Time | Byte units | 4 × Number of array element | 4 |
| String | Byte units | Depend on the number of character strings (1 per 1 character, including end NULL)*1 | Depend on the number of character strings (1 per 1 character, including end NULL)*1 |
| String [Unicode] | Byte units | Depend on the number of character strings (2 per 1 character, including end NULL)*2 | Depend on the number of character strings (2 per 1 character, including end NULL)*2 |
| Pointer | Cannot be specified. | Cannot be specified. | Cannot be specified. |
| Timer / Counter / Retentive timer — Contact / Coil / Current value | Cannot be specified. | Cannot be specified. | 2 |
| Long timer / Long counter / Long retentive timer — Contact / Coil | Cannot be specified. | Cannot be specified. | 2 |
| Long timer / Long counter / Long retentive timer — Current value | Cannot be specified. | Cannot be specified. | 4 |

*1 Specify the following values per one array element/label. Character string length specified to "Data Length of Character String Data Type" with an Engineering tool + 1 (When the value is an odd number, add 1 and specify with an even number.)

*2 Specify the following values per one array element/label. (Character string length specified to "Data Length of Character String Data Type" with an Engineering tool + 1) × 2

■Data communication in ASCII code
Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code
Send 2-byte*1 numerical values from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Setting example**

■Batch read and write of array

For the bit specification, specify the number of bits to be accessed.

- Unit specification: Bit
- Label: Bit type 3 points
- Array data length: 1 bit × Number of array element (3) = 3 bits

| | ASCII code | Binary code |
|---|---|---|
| Array data length | `30H 30H 30H 33H` (0003) | `03H 00H` |

For byte specification, specify the data length of the label to be accessed in byte unit.

- Unit specification: Byte
- Label: Word type 5 points
- Array data length: 2 bytes × (5) = 10 bytes

| | ASCII code | Binary code |
|---|---|---|
| Array data length | `30H 30H 30H 41H` (000A) | `0AH 00H` |

> **Note:** The PDF (PDF page 135, printed page 133) prints the last ASCII byte of this example as `3AH` under the digit "A". The ASCII code of "A" is `41H`, so the sequence for "000A" is `30H 30H 30H 41H`.

■Random read and write

For bit type, data length will be 2 (fixed value).

- Label: Bit type 1 point
- Data length: 2 (fixed)

| | ASCII code | Binary code |
|---|---|---|
| Data length | `30H 30H 30H 32H` (0002) | `02H 00H` |

The character string type [Unicode] will be 2 bytes per one character of UTF-16 code.

- Label: Character string type [Unicode] one point
- "Data Length of Character String Data Type" of an Engineering tool: 32
- Data length: Number of characters (32 + 1) × Data length per one character string (2) = 66 (42H)

| | ASCII code | Binary code |
|---|---|---|
| Data length | `30H 30H 34H 32H` (0042) | `42H 00H` |

#### Read data, write data

The read data is stored for reading, and the data to be written is stored for writing.

Data is stored with variable length. The length of data is specified by "Data length." (Page 132 Data length)

The storing method of data is same as that of reading or writing data in word units (bit device in 16-points). (Page 72 Read data, write data)

**For bit type labels**

Read data and write data are handled in 16-point units, but '0' is stored in data other than label points to be accessed.

**Ex.** For bit type label 6 points: b15 to b6 = Fixed to 0; b5 to b0 = Access target bit data (`1 1 0 1 0 1` from b5 to b0).

**For character string type labels**

Store NULL at the end of the valid character string. The data after NULL of the read data will be undefined value.

| Item | Character string type of ASCII code | Character string type of ASCII code | Character string type of Unicode |
|---|---|---|---|
| Number of character strings of label | Odd | Even number | — |
| NULL code to be stored to the end | NULL (00H) | NULL (00H) × 2 | NULL (0000H) |
| Data length of array element/label per one point | Character string specified to "Data Length of Character String Data Type" + 1 | Character string specified to "Data Length of Character String Data Type" + 2 | (Character string specified to "Data Length of Character String Data Type" + 1) × 2 |

**Ex.** For read data of 1-point character string type (ASCII code) label

- Label character string: 'ABCD' (4 characters)
- "Data Length of Character String Data Type" of an Engineering tool: 32
- Data length: Number of characters (32 + 1) × Data length per one character string (1) = 34 (22H)

| | ASCII code | Binary code |
|---|---|---|
| Data (in stored order) | `B` = `34H 32H`, `A` = `34H 31H`, `D` = `34H 34H`, `C` = `34H 33H`, NULL = `30H 30H`, NULL = `30H 30H`, … | `A` = `41H`, `B` = `42H`, `C` = `43H`, `D` = `44H`, NULL = `00H`, NULL = `00H`, … |
| Length | ("Data Length of String Data Type"(32)+2)×2 bytes | "Data Length of String Data Type"(32)+2 bytes |

> **Note:** In the ASCII code figure, the PDF (PDF page 136, printed page 134) prints `32H` under the digit "1" of the second pair (A, digits "4 1"). The ASCII code of "1" is `31H`, so the pair for "A" is `34H 31H`.

> When communicating ASCII code character string data in ASCII code, data is rearranged every two characters and stored.

---

### 9.2 Batch Read and Write

Read/write data by specifying the continuous element of array in batch.

#### Batch read array type labels (command: 041A)

Read data by specifying the continuous element of array in batch.

Specify the array type label or array type element of structure label.

The labels other than array type can be specified in one point unit. (Specify the number of array element as '1'.)

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(041AH) → Subcommand(0000H) → Array points(n points) → Abbreviation specification → Array specification(first point) → ... → Array specification(nth point)`

■Response data

`Array points(n points) → Array data(first point) → ... → Array data(nth point)`

The read array data is stored for the number of array points which are specified with request data.

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| Command | `30H 34H 31H 41H` (041A) | `1AH 04H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| Subcommand | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Array points
Specify the point of array to be read. (Page 130 Points)

■Abbreviation specification
Specify the label name length and label name to be abbreviated. (Page 128 Abbreviation specification of label)

When do not abbreviate, specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| Abbreviation specification | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Array specification
Specify the details of arrays for number of arrays specified to array points.

`Array specification (Array one point)[Label name length → Label name → Unit specification → Fixed values → Array data length]`

Specify the following items for each array.

- Label name length, label name: Specify the label name and label length of a global label. (Page 124 Labels)
- Unit specification: For the bit type labels, specify the unit specification in bit units (0). As for other than bit type label, specify the unit specification in byte units (1).
- Array data length: Specify the data size of array in the unit specified with "Unit specification." (Page 132 Data length, unit specification)
- Fixed value: '0'

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | `30H 30H` (00) | `00H` |

**Data stored in response data**

■Array points
The same data as request data are stored.

■Array data
The read array data is stored for number of arrays which are specified with array point.

`Array data (Array one point)[Data type ID → Unit specification → Array data length → Read data]`

The following items are stored for each array.

- Data type ID: The data type of the label name is stored with the defined ID. (Page 131 Data type ID)
- Unit specification: For the bit type labels, specify the unit specification in bit units (0). As for other than bit type label, specify the unit specification in byte units (1).
- Array data length: Specify the data size of array in the unit specified with "Unit specification." (Page 132 Data length, unit specification)
- Read data: The value of the read label is stored. (Page 134 Read data, write data)

> When unit specification is bit specification, read data are stored in 16-bit (2-byte) units.

**Communication example (Bit specification)**

For one-dimensional array type label 'Lbl', read 2-bit data from `Lbl[2]`.

The value of the read label is as follows:

b15 to b2 = Fixed to 0; b1 to b0 = Access target data (`1 0` from b1 to b0). As a 16-bit value: `0000000000000010`.

■Data communication in ASCII code (Request data)

`Command(041A) → Subcommand(0000) → Array points(0001) → Abbreviation specification(0000) → Array specification (Array one point)[Label name length(0006) → Label name("Lbl[2]") → Unit specification(00) → Fixed value(00) → Array data length(0002)]`

In the figure (1), set the value of "ASCII code" indicated in the following table.

(1) Lbl[2]

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / [ / 2 / ] |
| UTF-16 | 004C / 0062 / 006C / 005B / 0032 / 005D |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303544 |

■Data communication in ASCII code (Response data)

`Array points(0001) → Array data (Array one point)[Data type ID(01) → Unit specification(00) → Array data length(0002) → Read data(0002)]`

> **Note:** The PDF figure (PDF page 139, printed page 137) prints the Read data of this ASCII code response as the 2 digits `02` (`30H 32H`). Read data are stored in 16-bit (2-byte) units for bit specification (`02H 00H` in the binary code response) and the corresponding ASCII code write example (PDF page 144, printed page 142) shows `0002`, so `0002` is shown above.

■Data communication in binary code (Request data)

```
1AH 04H 00H 00H 01H 00H 00H 00H 06H 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H 00H 00H 02H 00H
```

| Field | Bytes |
|---|---|
| Command | `1AH 04H` |
| Subcommand | `00H 00H` |
| Array points | `01H 00H` |
| Abbreviation specification | `00H 00H` |
| Label name length | `06H 00H` |
| Label name ("Lbl[2]") | `4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H` |
| Unit specification | `00H` (bit) |
| Fixed value | `00H` |
| Array data length | `02H 00H` |

■Data communication in binary code (Response data)

```
01H 00H 01H 00H 02H 00H 02H 00H
```

| Field | Bytes |
|---|---|
| Array points | `01H 00H` |
| Data type ID | `01H` (Bit) |
| Unit specification | `00H` (bit) |
| Array data length | `02H 00H` |
| Read data | `02H 00H` |

**Communication example (Byte specification)**

For one-dimensional array type label 'Lbl', read 5-word data from `Lbl[2]`.

■Data communication in ASCII code (Request data)

`Command(041A) → Subcommand(0000) → Array points(0001) → Abbreviation specification(0000) → Array specification (Array one point)[Label name length(0006) → Label name("Lbl[2]") → Unit specification(01) → Fixed value(00) → Array data length(000A)]`

In the figure (1), set the value of "ASCII code" indicated in the following table.

(1) Lbl[2]

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / [ / 2 / ] |
| UTF-16 | 004C / 0062 / 006C / 005B / 0032 / 005D |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303544 |

■Data communication in ASCII code (Response data)

`Array points(0001) → Array data (Array one point)[Data type ID(02) → Unit specification(01) → Array data length(000A) → Read data(0044 0061 0074 0061 0031)]`

> **Note:** In the PDF figure (PDF page 140, printed page 138) the Data type ID of this ASCII code response is printed as the digits `02`, but the byte row under it reads `30H 33H`. The Data type ID of this label is 02 (`02H` in the binary code response), so `02` is shown above.

■Data communication in binary code (Request data)

```
1AH 04H 00H 00H 01H 00H 00H 00H 06H 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H 01H 00H 0AH 00H
```

| Field | Bytes |
|---|---|
| Command | `1AH 04H` |
| Subcommand | `00H 00H` |
| Array points | `01H 00H` |
| Abbreviation specification | `00H 00H` |
| Label name length | `06H 00H` |
| Label name ("Lbl[2]") | `4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H` |
| Unit specification | `01H` (byte) |
| Fixed value | `00H` |
| Array data length | `0AH 00H` (10 bytes = 5 words) |

■Data communication in binary code (Response data)

```
01H 00H 02H 01H 0AH 00H 44H 00H 61H 00H 74H 00H 61H 00H 31H 00H
```

| Field | Bytes |
|---|---|
| Array points | `01H 00H` |
| Data type ID | `02H` (Word [Unsigned]/Bit String [16-bit]) |
| Unit specification | `01H` (byte) |
| Array data length | `0AH 00H` |
| Read data | `44H 00H 61H 00H 74H 00H 61H 00H 31H 00H` |

**Communication example (Abbreviate with structure type array)**

Read the following data from the structure label 'Typ1', which has the array type element.

- 8 bytes from `Typ1.led[2]`
- 4 bytes from `Typ1.No[1]`

The notation of each label when using abbreviation specification (Typ1= %1) is as follows.

(1) `Typ1` (2) `Typ1.led[2]` → `%1.led[2]` (3) `Typ1.No[1]` → `%1.No[1]`

(1)Typ1

| Item | Value of code corresponding to character |
|---|---|
| Label name | T / y / p / 1 |
| UTF-16 | 0054 / 0079 / 0070 / 0031 |
| ASCII code | 30303534 / 30303739 / 30303730 / 30303331 |
| Binary code | 5400 / 7900 / 7000 / 3100 |

(2)Typ1.led[2]

| Item | Value of code corresponding to character |
|---|---|
| Abbreviated notation | % / 1 / . / l / e / d / [ / 2 / ] |
| UTF-16 | 0025 / 0031 / 002E / 006C / 0065 / 0064 / 005B / 0032 / 005D |
| ASCII code | 30303235 / 30303331 / 30303245 / 30303643 / 30303635 / 30303634 / 30303542 / 30303332 / 30303544 |
| Binary code | 2500 / 3100 / 2E00 / 6C00 / 6500 / 6400 / 5B00 / 3200 / 5D00 |

(3)Typ1.No[1]

| Item | Value of code corresponding to character |
|---|---|
| Abbreviated notation | % / 1 / . / N / o / [ / 1 / ] |
| UTF-16 | 0025 / 0031 / 002E / 004E / 006F / 005B / 0031 / 005D |
| ASCII code | 30303235 / 30303331 / 30303245 / 30303445 / 30303646 / 30303542 / 30303331 / 30303544 |
| Binary code | 2500 / 3100 / 2E00 / 4E00 / 6F00 / 5B00 / 3100 / 5D00 |

■Data communication in ASCII code (Request data)

`Command(041A) → Subcommand(0000) → Array points(0002) → Abbreviation specification[Number of abbreviated points(0001) → Label name length(0004) → Label name("Typ1")] → Array spec. 1[Label name length(0009) → Label name("%1.led[2]") → Unit specification(01) → Fixed value(00) → Array data length(0008)] → Array spec. 2[Label name length(0008) → Label name("%1.No[1]") → Unit specification(01) → Fixed value(00) → Array data length(0004)]`

In the figure (1) to (3), set the value of "ASCII code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in ASCII code (Response data)

`Array points(0002) → Array 1 (Typ1.led)[Data type ID(02) → Unit specification(01) → Array data length(0008) → Read data(0031 0032 0033 0034)] → Array 2 (Typ1.No)[Data type ID(03) → Unit specification(01) → Array data length(0004) → Read data(0030 0031)]`

> **Note:** In the PDF figure (PDF page 142, printed page 140) the Data type ID of the first array (Read data of Typ1.led) in this ASCII code response is `02` (`30H 32H`), whereas the binary code response figure shows `03H` for it. The PDF does not show which one is correct, so each figure is reproduced as printed.

■Data communication in binary code (Request data)

```
1AH 04H 00H 00H 02H 00H 01H 00H 04H 00H [Typ1: 54H 00H 79H 00H 70H 00H 31H 00H]
09H 00H [%1.led[2]: 25H 00H 31H 00H 2EH 00H 6CH 00H 65H 00H 64H 00H 5BH 00H 32H 00H 5DH 00H] 01H 00H 08H 00H
08H 00H [%1.No[1]: 25H 00H 31H 00H 2EH 00H 4EH 00H 6FH 00H 5BH 00H 31H 00H 5DH 00H] 01H 00H 04H 00H
```

| Field | Bytes |
|---|---|
| Command | `1AH 04H` |
| Subcommand | `00H 00H` |
| Array points | `02H 00H` |
| Abbreviation specification: number of abbreviated points | `01H 00H` |
| Abbreviation specification: label name length | `04H 00H` |
| Abbreviation specification: label name ("Typ1") | `54H 00H 79H 00H 70H 00H 31H 00H` |
| Array spec. 1: label name length | `09H 00H` |
| Array spec. 1: label name ("%1.led[2]") | `25H 00H 31H 00H 2EH 00H 6CH 00H 65H 00H 64H 00H 5BH 00H 32H 00H 5DH 00H` |
| Array spec. 1: unit specification | `01H` (byte) |
| Array spec. 1: fixed value | `00H` |
| Array spec. 1: array data length | `08H 00H` |
| Array spec. 2: label name length | `08H 00H` |
| Array spec. 2: label name ("%1.No[1]") | `25H 00H 31H 00H 2EH 00H 4EH 00H 6FH 00H 5BH 00H 31H 00H 5DH 00H` |
| Array spec. 2: unit specification | `01H` (byte) |
| Array spec. 2: fixed value | `00H` |
| Array spec. 2: array data length | `04H 00H` |

> **Note:** In the PDF figure of this binary code request (PDF page 142, printed page 140), the label name length of array specification 2 (label name "%1.No[1]", 8 characters) is printed as `0BH 00H`, whereas the ASCII code request figure prints `0008` for it; `08H 00H` is shown above.

In the figure (1) to (3), set the value of "Binary code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in binary code (Response data)

```
02H 00H 03H 01H 08H 00H 31H 00H 32H 00H 33H 00H 34H 00H 03H 01H 04H 00H 30H 00H 31H 00H
```

| Field | Bytes |
|---|---|
| Array points | `02H 00H` |
| Array 1 (Typ1.led): Data type ID | `03H` (Double Word [Unsigned]/Bit String [32-bit]) |
| Array 1: Unit specification | `01H` (byte) |
| Array 1: Array data length | `08H 00H` |
| Array 1: Read data | `31H 00H 32H 00H 33H 00H 34H 00H` |
| Array 2 (Typ1.No): Data type ID | `03H` |
| Array 2: Unit specification | `01H` (byte) |
| Array 2: Array data length | `04H 00H` |
| Array 2: Read data | `30H 00H 31H 00H` |

#### Batch write array type labels (command: 141A)

Write the values in batch with specifying the consecutive array elements.

Specify the array type labels or array type elements of structure type labels.

The labels other than array type can be specified in one point unit. (Specify the number of array element as '1'.)

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(141AH) → Subcommand(0000H) → Array points(n points) → Abbreviation specification → Array specification(first point) → ... → Array specification(nth point)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| Command | `31H 34H 31H 41H` (141A) | `1AH 14H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| Subcommand | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Array points
Specify the point of array to be written. (Page 130 Points)

■Abbreviation specification
Specify the label name length and label name to be abbreviated. (Page 128 Abbreviation specification of label)

When do not abbreviate, specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| Abbreviation specification | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Array specification
Specify the details of arrays for number of arrays specified to array points.

`Label name length → Label name → Unit specification → Fixed values → Array data length → Write data` = Array specification (Array one point)

Specify the following items for each array.

- Label name length, label name: Specify the label name and label length of a global label. (Page 124 Labels)
- Unit specification: For the bit type labels, specify the unit specification in bit units (0). As for other than bit type label, specify the unit specification in byte units (1).
- Array data length: Specify the data size of array in the unit specified with "Unit specification." (Page 132 Data length, unit specification)
- Write data: Specify the value to be written. (Page 134 Read data, write data)
- Fixed value: '0'

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | `30H 30H` (00) | `00H` |

**Communication example (Bit specification)**

For one-dimensional array type label 'Lbl', write 2-bit data from `Lbl[2]`.

The value of the label to be written is as follows:

b15 to b2 = Fixed to 0; b1 to b0 = Access target data (`1 0` from b1 to b0). As a 16-bit value: `0000000000000010`.

■Data communication in ASCII code (Request data)

`Command(141A) → Subcommand(0000) → Array points(0001) → Abbreviation specification(0000) → Array specification (Array one point)[Label name length(0006) → Label name("Lbl[2]") → Unit specification(00) → Fixed value(00) → Array data length(0002) → Write data(0002)]`

```
31H 34H 31H 41H 30H 30H 30H 30H 30H 30H 30H 31H 30H 30H 30H 30H 30H 30H 30H 36H [Lbl[2]: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 35H 42H 30H 30H 33H 32H 30H 30H 35H 44H] 30H 30H 30H 30H 30H 30H 30H 32H 30H 30H 30H 32H
```

In the figure (1), set the value of "ASCII code" indicated in the following table.

(1) Lbl[2]

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / [ / 2 / ] |
| UTF-16 | 004C / 0062 / 006C / 005B / 0032 / 005D |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303544 |

■Data communication in binary code (Request data)

```
1AH 14H 00H 00H 01H 00H 00H 00H 06H 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H 00H 00H 02H 00H 02H 00H
```

| Field | Bytes |
|---|---|
| Command | `1AH 14H` |
| Subcommand | `00H 00H` |
| Array points | `01H 00H` |
| Abbreviation specification | `00H 00H` |
| Label name length | `06H 00H` |
| Label name ("Lbl[2]") | `4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H` |
| Unit specification | `00H` (bit) |
| Fixed value | `00H` |
| Array data length | `02H 00H` |
| Write data | `02H 00H` |

**Communication example (Byte specification)**

For one-dimensional array type label 'Lbl', write 5-word data from `Lbl[2]`.

■Data communication in ASCII code (Request data)

`Command(141A) → Subcommand(0000) → Array points(0001) → Abbreviation specification(0000) → Array specification (Array one point)[Label name length(0006) → Label name("Lbl[2]") → Unit specification(01) → Fixed value(00) → Array data length(000A) → Write data(0044 0061 0074 0061 0031)]`

```
31H 34H 31H 41H 30H 30H 30H 30H 30H 30H 30H 31H 30H 30H 30H 30H 30H 30H 30H 36H [Lbl[2]: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 35H 42H 30H 30H 33H 32H 30H 30H 35H 44H] 30H 31H 30H 30H 30H 30H 30H 41H 30H 30H 34H 34H 30H 30H 36H 31H 30H 30H 37H 34H 30H 30H 36H 31H 30H 30H 33H 31H
```

In the figure (1), set the value of "ASCII code" indicated in the following table.

(1) Lbl[2]

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / [ / 2 / ] |
| UTF-16 | 004C / 0062 / 006C / 005B / 0032 / 005D |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303544 |

■Data communication in binary code (Request data)

```
1AH 14H 00H 00H 01H 00H 00H 00H 06H 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H 01H 00H 0AH 00H 44H 00H 61H 00H 74H 00H 61H 00H 31H 00H
```

| Field | Bytes |
|---|---|
| Command | `1AH 14H` |
| Subcommand | `00H 00H` |
| Array points | `01H 00H` |
| Abbreviation specification | `00H 00H` |
| Label name length | `06H 00H` |
| Label name ("Lbl[2]") | `4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H` |
| Unit specification | `01H` (byte) |
| Fixed value | `00H` |
| Array data length | `0AH 00H` |
| Write data | `44H 00H 61H 00H 74H 00H 61H 00H 31H 00H` |

**Communication example (Abbreviate with structure type array)**

Write the following data to three-dimensional structure label 'Typ1', which has the array type element.

- 8 bytes from `Typ1.led[5]`
- 4 bytes from `Typ1.No[7]`

The notation of each label when using abbreviation specification (Typ1 = %1) is as follows:

(1) `Typ1` (2) `Typ1.led[5]` → `%1.led[5]` (3) `Typ1.No[7]` → `%1.No[7]`

(1)Typ1

| Item | Value of code corresponding to character |
|---|---|
| Label name | T / y / p / 1 |
| UTF-16 | 0054 / 0079 / 0070 / 0031 |
| ASCII code | 30303534 / 30303739 / 30303730 / 30303331 |
| Binary code | 5400 / 7900 / 7000 / 3100 |

(2)Typ1.led[5]

| Item | Value of code corresponding to character |
|---|---|
| Abbreviated notation | % / 1 / . / l / e / d / [ / 5 / ] |
| UTF-16 | 0025 / 0031 / 002E / 006C / 0065 / 0064 / 005B / 0035 / 005D |
| ASCII code | 30303235 / 30303331 / 30303245 / 30303643 / 30303635 / 30303634 / 30303542 / 30303335 / 30303544 |
| Binary code | 2500 / 3100 / 2E00 / 6C00 / 6500 / 6400 / 5B00 / 3500 / 5D00 |

(3)Typ1.No[7]

| Item | Value of code corresponding to character |
|---|---|
| Abbreviated notation | % / 1 / . / N / o / [ / 7 / ] |
| UTF-16 | 0025 / 0031 / 002E / 004E / 006F / 005B / 0037 / 005D |
| ASCII code | 30303235 / 30303331 / 30303245 / 30303445 / 30303646 / 30303542 / 30303337 / 30303544 |
| Binary code | 2500 / 3100 / 2E00 / 4E00 / 6F00 / 5B00 / 3700 / 5D00 |

■Data communication in ASCII code (Request data)

`Command(141A) → Subcommand(0000) → Array points(0002) → Abbreviation specification (%1)[Number of abbreviated points(0001) → Label name length(0004) → Label name("Typ1")] → Array spec. 1[Label name length(0009) → Label name("%1.led[5]") → Unit specification(01) → Fixed value(00) → Array data length(0008) → Write data(1234 5678 9ABC DEF0)] → Array spec. 2[Label name length(0008) → Label name("%1.No[7]") → Unit specification(01) → Fixed value(00) → Array data length(0004) → Write data(1234 5678)]`

```
31H 34H 31H 41H 30H 30H 30H 30H 30H 30H 30H 32H 30H 30H 30H 31H 30H 30H 30H 34H [Typ1: 30H 30H 35H 34H 30H 30H 37H 39H 30H 30H 37H 30H 30H 30H 33H 31H]
30H 30H 30H 39H [%1.led[5]: 30H 30H 32H 35H 30H 30H 33H 31H 30H 30H 32H 45H 30H 30H 36H 43H 30H 30H 36H 35H 30H 30H 36H 34H 30H 30H 35H 42H 30H 30H 33H 35H 30H 30H 35H 44H] 30H 31H 30H 30H 30H 30H 30H 38H 31H 32H 33H 34H 35H 36H 37H 38H 39H 41H 42H 43H 44H 45H 46H 30H
30H 30H 30H 38H [%1.No[7]: 30H 30H 32H 35H 30H 30H 33H 31H 30H 30H 32H 45H 30H 30H 34H 45H 30H 30H 36H 46H 30H 30H 35H 42H 30H 30H 33H 37H 30H 30H 35H 44H] 30H 31H 30H 30H 30H 30H 30H 34H 31H 32H 33H 34H 35H 36H 37H 38H
```

In the figure (1) to (3), set the value of "ASCII code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in binary code (Request data)

```
1AH 14H 00H 00H 02H 00H 01H 00H 04H 00H [Typ1: 54H 00H 79H 00H 70H 00H 31H 00H]
09H 00H [%1.led[5]: 25H 00H 31H 00H 2EH 00H 6CH 00H 65H 00H 64H 00H 5BH 00H 35H 00H 5DH 00H] 01H 00H 08H 00H 34H 12H 78H 56H BCH 9AH F0H DEH
08H 00H [%1.No[7]: 25H 00H 31H 00H 2EH 00H 4EH 00H 6FH 00H 5BH 00H 37H 00H 5DH 00H] 01H 00H 04H 00H 34H 12H 78H 56H
```

| Field | Bytes |
|---|---|
| Command | `1AH 14H` |
| Subcommand | `00H 00H` |
| Array points | `02H 00H` |
| Abbreviation specification: number of abbreviated points | `01H 00H` |
| Abbreviation specification: label name length | `04H 00H` |
| Abbreviation specification: label name ("Typ1") | `54H 00H 79H 00H 70H 00H 31H 00H` |
| Array spec. 1: label name length | `09H 00H` |
| Array spec. 1: label name ("%1.led[5]") | `25H 00H 31H 00H 2EH 00H 6CH 00H 65H 00H 64H 00H 5BH 00H 35H 00H 5DH 00H` |
| Array spec. 1: unit specification | `01H` (byte) |
| Array spec. 1: fixed value | `00H` |
| Array spec. 1: array data length | `08H 00H` |
| Array spec. 1: write data | `34H 12H 78H 56H BCH 9AH F0H DEH` |
| Array spec. 2: label name length | `08H 00H` |
| Array spec. 2: label name ("%1.No[7]") | `25H 00H 31H 00H 2EH 00H 4EH 00H 6FH 00H 5BH 00H 37H 00H 5DH 00H` |
| Array spec. 2: unit specification | `01H` (byte) |
| Array spec. 2: fixed value | `00H` |
| Array spec. 2: array data length | `04H 00H` |
| Array spec. 2: write data | `34H 12H 78H 56H` |

In the figure (1) to (3), set the value of "Binary code" indicated in the table of "Value of code corresponding to character" of each label name.

---

### 9.3 Random Read and Write

Specify a label and read/write value in one point unit. When reading and writing data in batch by specifying continuous elements of array, use batch read and write command. (Page 135 Batch Read and Write)

#### Random read labels (command: 041C)

Read value in one point units by specifying multiple labels.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(041CH) → Subcommand(0000H) → Label points(n points) → Abbreviation specification → Label specification(1st point) → ... → Label specification(nth point)`

■Response data

The data of read label is stored for the number of label points specified with request data.

`Label points(n points) → Label data(1st point) → ... → Label data(nth point)`

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| Command | `30H 34H 31H 43H` (041C) | `1CH 04H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| Subcommand | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Label points
Specify the point of label to read. (Page 130 Points)

■Abbreviation specification
Specify the label name length and label name to be abbreviated. (Page 128 Abbreviation specification of label)

When do not abbreviate, specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| Abbreviation specification | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Label specification
Specify the label name and label length of global label for the specified number of label points. (Page 124 Labels)

For a structure type label or array type label, specify the data of each element.

Field order per label: `Label name length → Label name`

**Data stored in response data**

■Label points
The same data as request data are stored.

■Label data
The data of read label for the specified number of label points is stored.

`Data type ID → Spare data → Read data length → Read data` = Label data (1 point)

The following items are stored for each label.

- Data type ID: The data type of label name is stored with the defined ID. (Page 131 Data type ID)
- Read data length: Specify the read data size in byte units. (Page 132 Data length, unit specification)
- Read data: The value of read label is stored in the format of "Data type ID". (Page 134 Read data, write data)
- Spare data: The 2-byte of system data for data communication in ASCII code, and the 1-byte*1 of system data for data communication in binary code is stored.

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Communication example**

Read data from three labels.

- `Lbl1` (bit type) = 1 (ON)
- `Lbl2.Lbl[2]` (word type array element of structure label) = 0031H
- `Lbl3` (word type) = 0001H

The notation of each label when using abbreviation specification (Lbl2 = %1) is as follows:

(1) `Lbl2` (abbreviation specification) (2) `Lbl1` (3) `Lbl2.Lbl[2]` → `%1.Lbl[2]` (4) `Lbl3`

(1) Lbl2 (abbreviation specification)

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 2 |
| UTF-16 | 004C / 0062 / 006C / 0032 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303332 |
| Binary code | 4C00 / 6200 / 6C00 / 3200 |

(2) Lbl1

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 1 |
| UTF-16 | 004C / 0062 / 006C / 0031 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303331 |
| Binary code | 4C00 / 6200 / 6C00 / 3100 |

(3) Lbl2.Lbl[2]

| Item | Value of code corresponding to character |
|---|---|
| Abbreviated notation | % / 1 / . / L / b / l / [ / 2 / ] |
| UTF-16 | 0025 / 0031 / 002E / 004C / 0062 / 006C / 005B / 0032 / 005D |
| ASCII code | 30303235 / 30303331 / 30303245 / 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303544 |
| Binary code | 2500 / 3100 / 2E00 / 4C00 / 6200 / 6C00 / 5B00 / 3200 / 5D00 |

(4) Lbl3

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 3 |
| UTF-16 | 004C / 0062 / 006C / 0033 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303333 |
| Binary code | 4C00 / 6200 / 6C00 / 3300 |

■Data communication in ASCII code (Request data)

```
30H 34H 31H 43H 30H 30H 30H 30H 30H 30H 30H 33H 30H 30H 30H 31H 30H 30H 30H 34H [Lbl2: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 32H]
30H 30H 30H 34H [Lbl1: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 31H]
30H 30H 30H 39H [%1.Lbl[2]: 30H 30H 32H 35H 30H 30H 33H 31H 30H 30H 32H 45H 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 35H 42H 30H 30H 33H 32H 30H 30H 35H 44H]
30H 30H 30H 34H [Lbl3: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 33H]
```

| Field | Bytes |
|---|---|
| Command | `30H 34H 31H 43H` (041C) |
| Subcommand | `30H 30H 30H 30H` (0000) |
| Label points | `30H 30H 30H 33H` (0003) |
| Abbreviation specification: number of abbreviated points | `30H 30H 30H 31H` (0001) |
| Abbreviation specification: label name length | `30H 30H 30H 34H` (0004) |
| Abbreviation specification: label name ("Lbl2") | `30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 32H` |
| Label spec. 1: label name length | `30H 30H 30H 34H` (0004) |
| Label spec. 1: label name ("Lbl1") | `30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 31H` |
| Label spec. 2: label name length | `30H 30H 30H 39H` (0009) |
| Label spec. 2: label name ("%1.Lbl[2]") | `30H 30H 32H 35H 30H 30H 33H 31H 30H 30H 32H 45H 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 35H 42H 30H 30H 33H 32H 30H 30H 35H 44H` |
| Label spec. 3: label name length | `30H 30H 30H 34H` (0004) |
| Label spec. 3: label name ("Lbl3") | `30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 33H` |

In the figure (1) to (4), set the value of "ASCII code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in ASCII code (Response data)

```
30H 30H 30H 33H 30H 31H 30H 30H 30H 30H 30H 32H 30H 30H 30H 31H 30H 32H 30H 30H 30H 30H 30H 32H 30H 30H 33H 31H 30H 32H 30H 30H 30H 30H 30H 32H 30H 30H 30H 31H
```

| Field | Bytes |
|---|---|
| Label points | `30H 30H 30H 33H` (0003) |
| Label 1 (Lbl1): Data type ID | `30H 31H` (01: Bit) |
| Label 1: Spare data | `30H 30H` (00) |
| Label 1: Read data length | `30H 30H 30H 32H` (0002) |
| Label 1: Read data | `30H 30H 30H 31H` (0001, ON) |
| Label 2 (Lbl2.Lbl[2]): Data type ID | `30H 32H` (02: Word [Unsigned]/Bit String [16-bit]) |
| Label 2: Spare data | `30H 30H` (00) |
| Label 2: Read data length | `30H 30H 30H 32H` (0002) |
| Label 2: Read data | `30H 30H 33H 31H` (0031) |
| Label 3 (Lbl3): Data type ID | `30H 32H` (02) |
| Label 3: Spare data | `30H 30H` (00) |
| Label 3: Read data length | `30H 30H 30H 32H` (0002) |
| Label 3: Read data | `30H 30H 30H 31H` (0001) |

■Data communication in binary code (Request data)

```
1CH 04H 00H 00H 03H 00H 01H 00H 04H 00H [Lbl2: 4CH 00H 62H 00H 6CH 00H 32H 00H]
04H 00H [Lbl1: 4CH 00H 62H 00H 6CH 00H 31H 00H]
09H 00H [%1.Lbl[2]: 25H 00H 31H 00H 2EH 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H]
04H 00H [Lbl3: 4CH 00H 62H 00H 6CH 00H 33H 00H]
```

| Field | Bytes |
|---|---|
| Command | `1CH 04H` |
| Subcommand | `00H 00H` |
| Label points | `03H 00H` |
| Abbreviation specification: number of abbreviated points | `01H 00H` |
| Abbreviation specification: label name length | `04H 00H` |
| Abbreviation specification: label name ("Lbl2") | `4CH 00H 62H 00H 6CH 00H 32H 00H` |
| Label spec. 1: label name length | `04H 00H` |
| Label spec. 1: label name ("Lbl1") | `4CH 00H 62H 00H 6CH 00H 31H 00H` |
| Label spec. 2: label name length | `09H 00H` |
| Label spec. 2: label name ("%1.Lbl[2]") | `25H 00H 31H 00H 2EH 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H` |
| Label spec. 3: label name length | `04H 00H` |
| Label spec. 3: label name ("Lbl3") | `4CH 00H 62H 00H 6CH 00H 33H 00H` |

In the figure (1) to (4), set the value of "Binary code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in binary code (Response data)

```
03H 00H 01H 00H 02H 00H 01H 00H 02H 00H 02H 00H 31H 00H 02H 00H 02H 00H 01H 00H
```

| Field | Bytes |
|---|---|
| Label points | `03H 00H` |
| Label 1 (Lbl1): Data type ID | `01H` (Bit) |
| Label 1: Spare data | `00H` |
| Label 1: Read data length | `02H 00H` |
| Label 1: Read data | `01H 00H` (1, ON) |
| Label 2 (Lbl2.Lbl[2]): Data type ID | `02H` (Word [Unsigned]/Bit String [16-bit]) |
| Label 2: Spare data | `00H` |
| Label 2: Read data length | `02H 00H` |
| Label 2: Read data | `31H 00H` (0031H) |
| Label 3 (Lbl3): Data type ID | `02H` |
| Label 3: Spare data | `00H` |
| Label 3: Read data length | `02H 00H` |
| Label 3: Read data | `01H 00H` (0001H) |

#### Random write labels (command: 141B)

Write value in one point units by specifying multiple labels.

**Message format**

The following shows the message format of the request data and response data of the command.

■Request data

`Command(141BH) → Subcommand(0000H) → Label points(n points) → Abbreviation specification → Label specification(1st point) → ... → Label specification(nth point)`

■Response data

There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| Command | "141B" (`31H 34H 31H 42H`) | `1BH 14H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| Subcommand | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Label points
Specify the number of label points to be written. (Page 130 Points)

■Abbreviation specification
Specify the label name length and label name to be abbreviated. (Page 124 Labels)

When do not abbreviate, specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| Abbreviation specification | `30H 30H 30H 30H` (0000) | `00H 00H` |

■Label specification
Specify the label name and write data for the specified number of label points.

`Label name length → Label name → Write data length → Write data` = Label specification (1 point)

Specify the following items for each label.

- Label name length, label name: Specify the label name and label length of a global label. (Page 124 Labels)
- Write data length: Specify he read data size in byte unit. (Page 132 Data length, unit specification)
- Write data: Store the values of labels to be written. (Page 134 Read data, write data)

> **Note:** The PDF prints "Specify he read data size in byte unit." for the "Write data length" item (evident typo; the item specifies the size of the write data).

**Communication example**

Write data to three labels.

- `Lbl1` (bit type) = 1 (ON)
- `Lbl2.Lbl[2]` (word type array element of structure label) = 0031H
- `Lbl3` (word type) = 0001H

The notation of each label when using abbreviation specification (Lbl2 = %1) is as follows:

(1) `Lbl2` (abbreviation specification) (2) `Lbl1` (3) `Lbl2.Lbl[2]` → `%1.Lbl[2]` (4) `Lbl3`

(1) Lbl2 (abbreviation specification)

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 2 |
| UTF-16 | 004C / 0062 / 006C / 0032 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303332 |
| Binary code | 4C00 / 6200 / 6C00 / 3200 |

(2) Lbl1

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 1 |
| UTF-16 | 004C / 0062 / 006C / 0031 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303331 |
| Binary code | 4C00 / 6200 / 6C00 / 3100 |

(3) Lbl2.Lbl[2]

| Item | Value of code corresponding to character |
|---|---|
| Abbreviated notation | % / 1 / . / L / b / l / [ / 2 / ] |
| UTF-16 | 0025 / 0031 / 002E / 004C / 0062 / 006C / 005B / 0032 / 005D |
| ASCII code | 30303235 / 30303331 / 30303245 / 30303443 / 30303632 / 30303643 / 30303542 / 30303332 / 30303544 |
| Binary code | 2500 / 3100 / 2E00 / 4C00 / 6200 / 6C00 / 5B00 / 3200 / 5D00 |

(4) Lbl3

| Item | Value of code corresponding to character |
|---|---|
| Label name | L / b / l / 3 |
| UTF-16 | 004C / 0062 / 006C / 0033 |
| ASCII code | 30303443 / 30303632 / 30303643 / 30303333 |
| Binary code | 4C00 / 6200 / 6C00 / 3300 |

■Data communication in ASCII code (Request data)

`Command(141B) → Subcommand(0000) → Label points(0003) → Abbreviation specification[Number of abbreviated points(0001) → Label name length(0004) → Label name("Lbl2")] → Label spec. 1[Label name length(0004) → Label name("Lbl1") → Write data length(0002) → Write data(0001)] → Label spec. 2[Label name length(0009) → Label name("%1.Lbl[2]") → Write data length(0002) → Write data(0031)] → Label spec. 3[Label name length(0004) → Label name("Lbl3") → Write data length(0002) → Write data(0001)]`

```
31H 34H 31H 42H 30H 30H 30H 30H 30H 30H 30H 33H 30H 30H 30H 31H 30H 30H 30H 34H [Lbl2: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 32H]
30H 30H 30H 34H [Lbl1: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 31H] 30H 30H 30H 32H 30H 30H 30H 31H
30H 30H 30H 39H [%1.Lbl[2]: 30H 30H 32H 35H 30H 30H 33H 31H 30H 30H 32H 45H 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 35H 42H 30H 30H 33H 32H 30H 30H 35H 44H] 30H 30H 30H 32H 30H 30H 33H 31H
30H 30H 30H 34H [Lbl3: 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 33H] 30H 30H 30H 32H 30H 30H 30H 31H
```

| Field | Bytes |
|---|---|
| Command | `31H 34H 31H 42H` (141B) |
| Subcommand | `30H 30H 30H 30H` (0000) |
| Label points | `30H 30H 30H 33H` (0003) |
| Abbreviation specification: number of abbreviated points | `30H 30H 30H 31H` (0001) |
| Abbreviation specification: label name length | `30H 30H 30H 34H` (0004) |
| Abbreviation specification: label name ("Lbl2") | `30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 32H` |
| Label spec. 1: label name length | `30H 30H 30H 34H` (0004) |
| Label spec. 1: label name ("Lbl1") | `30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 31H` |
| Label spec. 1: write data length | `30H 30H 30H 32H` (0002) |
| Label spec. 1: write data | `30H 30H 30H 31H` (0001, ON) |
| Label spec. 2: label name length | `30H 30H 30H 39H` (0009) |
| Label spec. 2: label name ("%1.Lbl[2]") | `30H 30H 32H 35H 30H 30H 33H 31H 30H 30H 32H 45H 30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 35H 42H 30H 30H 33H 32H 30H 30H 35H 44H` |
| Label spec. 2: write data length | `30H 30H 30H 32H` (0002) |
| Label spec. 2: write data | `30H 30H 33H 31H` (0031) |
| Label spec. 3: label name length | `30H 30H 30H 34H` (0004) |
| Label spec. 3: label name ("Lbl3") | `30H 30H 34H 43H 30H 30H 36H 32H 30H 30H 36H 43H 30H 30H 33H 33H` |
| Label spec. 3: write data length | `30H 30H 30H 32H` (0002) |
| Label spec. 3: write data | `30H 30H 30H 31H` (0001) |

In the figure (1) to (4), set the value of "ASCII code" indicated in the table of "Value of code corresponding to character" of each label name.

■Data communication in binary code (Request data)

```
1BH 14H 00H 00H 03H 00H 01H 00H 04H 00H [Lbl2: 4CH 00H 62H 00H 6CH 00H 32H 00H]
04H 00H [Lbl1: 4CH 00H 62H 00H 6CH 00H 31H 00H] 02H 00H 01H 00H
09H 00H [%1.Lbl[2]: 25H 00H 31H 00H 2EH 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H] 02H 00H 31H 00H
04H 00H [Lbl3: 4CH 00H 62H 00H 6CH 00H 33H 00H] 02H 00H 01H 00H
```

| Field | Bytes |
|---|---|
| Command | `1BH 14H` |
| Subcommand | `00H 00H` |
| Label points | `03H 00H` |
| Abbreviation specification: number of abbreviated points | `01H 00H` |
| Abbreviation specification: label name length | `04H 00H` |
| Abbreviation specification: label name ("Lbl2") | `4CH 00H 62H 00H 6CH 00H 32H 00H` |
| Label spec. 1: label name length | `04H 00H` |
| Label spec. 1: label name ("Lbl1") | `4CH 00H 62H 00H 6CH 00H 31H 00H` |
| Label spec. 1: write data length | `02H 00H` |
| Label spec. 1: write data | `01H 00H` (1, ON) |
| Label spec. 2: label name length | `09H 00H` |
| Label spec. 2: label name ("%1.Lbl[2]") | `25H 00H 31H 00H 2EH 00H 4CH 00H 62H 00H 6CH 00H 5BH 00H 32H 00H 5DH 00H` |
| Label spec. 2: write data length | `02H 00H` |
| Label spec. 2: write data | `31H 00H` (0031H) |
| Label spec. 3: label name length | `04H 00H` |
| Label spec. 3: label name ("Lbl3") | `4CH 00H 62H 00H 6CH 00H 33H 00H` |
| Label spec. 3: write data length | `02H 00H` |
| Label spec. 3: write data | `01H 00H` (0001H) |

In the figure (1) to (4), set the value of "Binary code" indicated in the table of "Value of code corresponding to character" of each label name.

---

## 10 BUFFER MEMORY ACCESS

This chapter explains the commands which read and write the buffer memory.

The buffer memory can be accessed with device access function using module access device (Un\G).

- Page 442 Accessing module access devices
- Page 65 DEVICE ACCESS

### 10.1 Buffer Memory

This section explains the command which reads and writes data to the buffer memory of the supported device connected to the external device.

The command can only be used for C24 (including multidrop connection station) and E71 connected to an external device. It cannot be used via network.

This command is processed by C24/E71 connected to the CPU module without waiting for the END process.

**Data to be specified in commands**

This section explains the contents and specification methods for data items which are set in each command related to the access to the host station (supported device) buffer memory.

#### Start address

Specify the start address of the buffer memory to be read/written.

- ■Data communication in ASCII code: Convert the numerical value to 8-digit (hexadecimal) ASCII code, and send it from the upper digits.
- ■Data communication in binary code: Send 4-byte numerical values from the lower byte (L: bits 0 to 7).

**Ex.** When the head area address is 1E1H

| | ASCII code | Binary code |
|---|---|---|
| Start address (000001E1H) | 30H 30H 30H 30H 30H 31H 45H 31H | E1H 01H 00H 00H |

#### Word length

Specify the word length of the buffer memory to be read/written.

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.** For 5 words and 20 words

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 5 words | 30H 30H 30H 35H | 05H 00H |
| 20 words | 30H 30H 31H 34H | 14H 00H |

#### Read data, write data

The read buffer memory value is stored for reading, and the data to be written is stored for writing.

This function reads/writes data in word unit.

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.** When the data for one buffer memory address is 09C1H

| | ASCII code | Binary code |
|---|---|---|
| Read/write data (09C1H) | 30H 39H 43H 31H | C1H 09H |

#### Batch read (command: 0613)

Read data from the buffer memory of the host station (supported device).

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(0613H) → Subcommand(0000H) → Start address → Word length`
- ■Response data:
  `Read data 1 → Read data 2 → ... → Read data n`
  *(The value read from the buffer memory is stored. The data order differs depending on the type of code, ASCII code or binary code. Page 152 Read data, write data)*

**Data specified by request data**

- ■Command
- ■Subcommand
- ■Start address: Specify the buffer memory start address to be read. (Page 151 Start address)
- ■Word length: Specify the word length of the buffer memory to be read. (Page 152 Word length)
  Specification range: 1H to 1E0H (480)
  Specify the access range within the range of buffer memory.
  (Start address + Word length − 1) ≤ Buffer memory range

| Field | ASCII code | Binary code |
|---|---|---|
| Command (0613H) | 30H 36H 31H 33H | 13H 06H |
| Subcommand (0000H) | 30H 30H 30H 30H | 00H 00H |

**Communication example**

Read the data of the buffer memory addresses from 78H to 81H (120 to 129).

■Data communication in ASCII code

(Request data)

| Command | Subcommand | Start address | Word length |
|---|---|---|---|
| 0613 (30H 36H 31H 33H) | 0000 (30H 30H 30H 30H) | 00000078 (30H 30H 30H 30H 30H 30H 37H 38H) | 000A (30H 30H 30H 41H) |

(Response data)

| Read data 1 (addr. 78H = 0500H) | Read data 2 (addr. 79H = 09C1H) | ... | Read data 10 (addr. 81H = 00C8H) |
|---|---|---|---|
| 0500 (30H 35H 30H 30H) | 09C1 (30H 39H 43H 31H) | ... | 00C8 (30H 30H 43H 38H) |

■Data communication in binary code

(Request data)

`13H 06H | 00H 00H | 78H 00H 00H 00H | 0AH 00H`
(Command | Subcommand | Start address | Word length)

(Response data)

`00H 05H | C1H 09H | ... | C8H 00H`
(Read data 1 = value of address 78H = 0500H | Read data 2 = value of address 79H = 09C1H | ... | Read data 10 = value of address 81H = 00C8H)

#### Batch write (command: 1613)

Write data to the buffer memory of the host station (supported device).

> Do not write any data in "System area" or "Write-protect area" in the buffer memory.
> Writing data to the "System area" or "Write-protect area" may cause malfunction of the programmable controller system.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1613H) → Subcommand(0000H) → Start address → Word length → Write data 1 → ... → Write data n`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command
- ■Subcommand
- ■Start address: Specify the buffer memory start address to be written. (Page 151 Start address)
- ■Word length: Specify the word length of the buffer memory to be written. (Page 152 Word length)
  Specification range: 1H to 1E0H (480)
- ■Write data: Specify the data to be written in the buffer memory. (Page 152 Read data, write data)
  Specify the access range within the range of buffer memory.
  (Start address + Word length − 1) ≤ Buffer memory range

| Field | ASCII code | Binary code |
|---|---|---|
| Command (1613H) | 31H 36H 31H 33H | 13H 16H |
| Subcommand (0000H) | 30H 30H 30H 30H | 00H 00H |

**Communication example**

Write the value to buffer memory addresses from 2680H to 2683H (9856 to 9859).

■Data communication in ASCII code

(Request data)

| Command | Subcommand | Start address | Word length | Write data 1 | ... | Write data 4 |
|---|---|---|---|---|---|---|
| 1613 (31H 36H 31H 33H) | 0000 (30H 30H 30H 30H) | 00002680 (30H 30H 30H 30H 32H 36H 38H 30H) | 0004 (30H 30H 30H 34H) | 2000 (32H 30H 30H 30H) | ... | 0000 (30H 30H 30H 30H) |

*(Write data 1 = value of address 2680H = 2000H; Write data 4 = value of address 2683H = 0H)*

■Data communication in binary code

(Request data)

`13H 16H | 00H 00H | 80H 26H 00H 00H | 04H 00H | 00H 20H | ... | 00H 00H`
(Command | Subcommand | Start address | Word length | Write data 1 | ... | Write data 4)

*(Write data 1 = value of address 2680H = 2000H; Write data 4 = value of address 2683H = 0H)*

### 10.2 Intelligent Function Module

The section explains the commands to read from/write to the buffer memory of an intelligent function module.

**Accessible modules**

The following shows the accessible intelligent functional modules to buffer memory.

**Accessing buffer memory using module access device (Un\G)**

The intelligent function modules, which can be accessed the module access device (Un\G), can be accessed by the device access function.

- Page 442 Accessing module access devices
- Page 65 DEVICE ACCESS

**Accessing buffer memory with calculating a start address**

Use the command (0601, 1601) shown in this section to access the following devices.

| Module | Model | Additional values when calculating start address |
|---|---|---|
| Load cell input module | Q61LD | 2000H |
| Loop control module | Q62HLC | 10000H |
| Analog-digital converter module | Q62AD-DGH, Q64AD, Q64AD-GH, Q66AD-DG, Q68AD-G, Q68ADV, Q68ADI | 1008H |
| Digital-analog converter module | Q62DA, Q62DA-FG, Q62DAN, Q64DA, Q64DAN, Q66DA-G, Q68DAV, Q68DAI, Q68DAVN, Q68DAIN | 1008H |
| Analog input/output module | Q64AD2DA | 2000H |
| Temperature control module | Q64TCTT, Q64TCRT, Q64TCTTBW, Q64TCRTBW | 1000H |
| Temperature input module (function version B) | Q64TD, Q64RD | 2000H |
| Temperature input module (function version C) | Q64TD, Q64TDV-GH, Q64RD, Q64RD-G | 8000H |
| Channel isolated thermocouple input module | Q68TD-G-H01, Q68TD-G-H02 | 1008H |
| Channel isolated RTD input module | Q68RD3-G | 1008H |
| ID Interface module | QD35ID1, QD35ID2 | 4000H |
| Intelligent communication module | QD51, QD51-R24 | 10000H |
| Channel isolated pulse input module | QD60P8-G | 2000H |
| High-speed counter module | QD62, QD62E, QD62D | 3CH |
| Multichannel high-speed counter module | QD63P6 | 2000H |
| 4Mpps capable high-speed counter module | QD64D2 | 2000H |
| Positioning module | QD70P4, QD70P8, QD70D4, QD70D8, QD72P3C3 | 5000H |
| Positioning module | QD75P1, QD75P2, QD75P4, QD75D1, QD75D2, QD75D4, QD75M1, QD75M2, QD75M4, QD75MH1, QD75MH2, QD75MH4 | 10000H |
| High speed data logger module | QD81DL96 | 10000H |
| CC-Link system master/local module | QJ61BT11, QJ61BT11N | 10000H |
| CC-Link/LT master module | QJ61CL12 | 01B4H |
| Serial communication module | QJ71C24N, QJ71C24N-R2, QJ71C24N-R4, QJ71C24, QJ71C24-R2 | 10000H |
| AS-i master module | QJ71AS92 | 10000H |
| Ethernet interface module | QJ71E71-100, QJ71E71-B5, QJ71E71-B2 | 10000H |
| FL-net (OPCN-2) interface module | QJ71FL71-T, QJ71FL71-B2, QJ71FL71-B5, QJ71FL71-T-F01, QJ71FL71-B2-F01, QJ71FL71-B5-F01 | 10000H |
| MODBUS interface module | QJ71MB91, QJ71MT91 | 10000H |
| MES interface module | QJ71MES96 | 10000H |
| Web server module | QJ71WS96 | 10000H |
| PROFIBUS-DP Interface module | QJ71PB92D | 10000H |
| PROFIBUS-DP Master module | QJ71PB92V | 10000H |
| PROFIBUS-DP Slave module | QJ71PB93D | 10004H |

By using the command (0601, 1601), MELSEC-QnA series special function modules can be accessed.

| Module | Model | Additional values when calculating start address |
|---|---|---|
| CC-Link system master/local module | AJ61QBT11, A1SJ61QBT11 | 2000H |
| Ethernet interface module | AJ71QE71, AJ71QE71-B5, A1SJ71QE71-B2, A1SJ71QE71-B5 | 4000H |
| Serial communication module | AJ71QC24, AJ71QC24-R2, AJ71QC24-4, AJ71QC24N, AJ71QC24N-R2, AJ71QC24N-R4, A1SJ71QC24, A1SJ71QC24-R2, A1SJ71QC24N, A1SJ71QC24N-R2 | 4000H |

When accessing buffer memory of MELSEC-A series special function modules, use the command of 1C/1E frame.

- 1C frame: Page 383 Read and write Buffer Memory of Special Function Module
- 1E frame: Page 432 Read and Write Buffer Memory of Special Function Module

**Data to be specified in commands**

This section explains the contents and specification methods for data items which are set in each command related to the access to the intelligent function module buffer memory.

#### Start address

Specify the start address of the buffer memory to be read/written.

**Calculation method**

To access the buffer memory of the intelligent function module which consisted of word units by byte unit, specify a start address calculated by byte unit.

Calculate the start address as follows:

`Start address = (Buffer memory address × 2) + Additional value of a module`

For the arbitrary additional value of the module, refer to the following section.

Page 157 Accessing buffer memory with calculating a start address

**Ex.** When specifying Q62DA buffer memory address 18H

`(18H × 2) + 1008H = 30H + 1008H = 1038H`

- ■Data communication in ASCII code: Convert the numerical value to 8-digit (hexadecimal) ASCII code, and send it from the upper digits.
- ■Data communication in binary code: Send 4-byte numerical values from the lower byte (L: bits 0 to 7).

**Ex.** When the start address is 1038H

| | ASCII code | Binary code |
|---|---|---|
| Start address (00001038H) | 30H 30H 30H 30H 31H 30H 33H 38H | 38H 10H 00H 00H |

#### Number of bytes

Specify the number of bytes of the data to be read/written.

**Calculation method**

The buffer memory for intelligent function module consists of two bytes (one word) for one area. Calculate the number of bytes by 2-byte per data for one buffer memory address.

`Number of bytes = (Number of buffer memory address × 2)`

**Ex.** When accessing the buffer memory address 160 to 161 (A0H to A1H)

`(161 − 160 + 1) × 2 = 2 × 2 = 4 bytes`

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits. Use capitalized code for alphabetical letter.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.** For 20 bytes

| Number of device points | ASCII code | Binary code |
|---|---|---|
| 20 bytes | 30H 30H 31H 34H | 14H 00H |

#### Module number

Specify the start input/output number of an intelligent function module to be accessed.

For the module number, specify the value obtained by dividing the start input/output number by 16 in 4 digits (hexadecimal).

**■When the number of occupied slots are 2**

Specify the value obtained by adding 1 to the module number for the following modules.

| Module type | Model |
|---|---|
| Temperature control module | Q64TCTTBW, Q64TCRTBW |
| Positioning module | QD70D4, QD70D8 |

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits. Use capitalized code for alphabetical letter.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.** When accessing the positioning module whose start input/output number is 0080H

| Module number | ASCII code | Binary code |
|---|---|---|
| For QD70P4: 0008H | 30H 30H 30H 38H | 08H 00H |
| For QD70D4 (occupied slots are 2): 0008H + 1 = 0009H | 30H 30H 30H 39H | 09H 00H |

**■Intelligent function module number of MELSECNET/H remote I/O station**

Intelligent function module number of MELSECNET/H remote I/O station is the upper 2 digits of the last number represented with 3-digit number of the following "Input/output signal based on the remote I/O station".

Specify this with "Input/output signal based on the remote I/O station" regardless of the contents of common parameters set in the master station of MELSECNET/H remote I/O network.

| Remote I/O station, station 1 | Input/output signal based on the remote I/O station | Input/output signal by common parameters |
|---|---|---|
| Power supply module | | |
| AJ72LP25 | | |
| Output module (32 points) | Y 00 to 1F | Y 400 to 41F |
| Output module (16 points) | Y 20 to 2F | Y 420 to 42F |
| Intelligent function module (32 points) | X/Y 30 to 4F | X/Y 430 to 44F |
| Output module (16 points) | Y 50 to 6F | Y 450 to 46F |
| Output module (32 points) | Y 70 to 8F | Y 470 to 48F |

Intelligent function module No. "04H" (diagram callout)

> **Note:** In the PDF diagram (PDF page 162, printed page 160) the fourth module is labelled "Output module (16 points)", but its ranges "Y 50 to 6F" and "Y 450 to 46F" cover 32 points. The PDF does not show which is correct, so the diagram is reproduced as printed.

#### Read data, write data

The read buffer memory value is stored for reading, and the data to be written is stored for writing.

This function reads/writes data in byte unit.

- ■Data communication in ASCII code: Handle a word data for one buffer memory address as 2-byte data. Convert the value to 2-digit (hexadecimal) ASCII code per one byte, and send it from the upper digit.
- ■Data communication in binary code: Handle a word data for one buffer memory address as 2-byte data. Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.** When the data for one buffer memory address is 09C1H

| | ASCII code | Binary code |
|---|---|---|
| Read/write data, byte unit (low byte C1H, high byte 09H) | 43H 31H 30H 39H | C1H 09H |

When reading data from the following buffer memory addresses, 0H to 2H (additional value of the start address: 10000H)

| Buffer memory | | |
|---|---|---|
| Address | Stored data (word unit) | Stored data (byte unit) |
| 0H | 0003H | 03H, 00H |
| 1H | 0001H | 01H, 00H |
| 2H | 0012H | 12H, 00H |

| | Read 6 bytes from address 0H | | Read 4 bytes from address 1H | |
|---|---|---|---|---|
| | Start address: 10000H | | Start address: 10002H | |
| Address | Read data (byte unit): ASCII code | Read data (byte unit): Binary code | Read data (byte unit): ASCII code | Read data (byte unit): Binary code |
| 0H | 30H, 33H / 30H, 30H | 03H / 00H | — | — |
| 1H | 30H, 31H / 30H, 30H | 01H / 00H | 30H, 31H / 30H, 30H | 01H / 00H |
| 2H | 31H, 32H / 30H, 30H | 12H / 00H | 31H, 32H / 30H, 30H | 12H / 00H |

#### Batch read (command: 0601)

Read data from the buffer memory of an intelligent function module.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(0601H) → Subcommand(0000H) → Start address → Number of bytes → Module No.`
- ■Response data:
  `Read data`
  *(The value read from the buffer memory is stored. Page 160 Read data, write data)*

**Data specified by request data**

- ■Command
- ■Subcommand
- ■Start address: Specify the buffer memory start address to be read. (Page 158 Start address)
- ■Number of bytes: Specify the number of bytes of the buffer memory to be read. (Page 159 Number of bytes)
  Specification range: 2H to 780H (1920)
- ■Module number: Specify the intelligent function module to be read. (Page 159 Module number)

Specify the access range within the range of buffer memory.

| Field | ASCII code | Binary code |
|---|---|---|
| Command (0601H) | 30H 36H 30H 31H | 01H 06H |
| Subcommand (0000H) | 30H 30H 30H 30H | 00H 00H |

**Communication example**

Read the data of the buffer memory addresses 1H to 2H of Q62DA whose input/output signal is from 30H to 4FH (module No.: 03H).

| Buffer memory of Q62DA | | | Data for command |
|---|---|---|---|
| Address | Name | Stored data (word unit) | Read data (byte unit) |
| 1H | CH1 digital value | 0001H | 01H, 00H |
| 2H | CH2 digital value | 0012H | 12H, 00H |

■Data communication in ASCII code

(Request data)

| Command | Subcommand | Start address | Number of bytes | Module No. |
|---|---|---|---|---|
| 0601 (30H 36H 30H 31H) | 0000 (30H 30H 30H 30H) | 0000100A (30H 30H 30H 30H 31H 30H 30H 41H) | 0004 (30H 30H 30H 34H) | 0003 (30H 30H 30H 33H) |

(Response data)

| Read data | |
|---|---|
| 01,00 (30H 31H 30H 30H) | 12,00 (31H 32H 30H 30H) |
| Values of address 1H =0001H | Values of address 2H =0012H |

■Data communication in binary code

(Request data)

`01H 06H | 00H 00H | 0AH 10H 00H 00H | 04H 00H | 03H 00H`
(Command | Subcommand | Start address | Number of bytes | Module No.)

(Response data)

`01H 00H 12H 00H`
(Read data: `01H 00H` = Values of address 1H =0001H; `12H 00H` = Values of address 2H =0012H)

#### Batch write (command: 1601)

Write data to the buffer memory of an intelligent function module.

> Do not write any data in "System area" or "Write-protect area" in the buffer memory.
> Writing data to the "System area" or "Write-protect area" may cause malfunction of the programmable controller system.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1601H) → Subcommand(0000H) → Start address → Number of bytes → Module No. → Write data`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command
- ■Subcommand
- ■Start address: Specify the buffer memory start address to be written. (Page 158 Start address)
- ■Number of bytes: Specify the number of bytes of the buffer memory to be written. (Page 159 Number of bytes)
  Specification range: 2H to 780H (1920)
- ■Module number: Specify the intelligent function module to be written. (Page 159 Module number)
- ■Write data: Specify the data to be written in the buffer memory. (Page 160 Read data, write data)

Specify the access range within the range of buffer memory.

| Field | ASCII code | Binary code |
|---|---|---|
| Command (1601H) | 31H 36H 30H 31H | 01H 16H |
| Subcommand (0000H) | 30H 30H 30H 30H | 00H 00H |

**Communication example**

Write data to the buffer memory addresses from 1H to 2H of Q62DA whose input/output signals are from 30H to 4FH (module No.: 03H).

| Buffer memory of Q62DA | | | Data for command |
|---|---|---|---|
| Address | Name | Stored data (word unit) | Write data (byte unit) |
| 1H | CH1 digital value | 01F4H | F4H, 01H |
| 2H | CH2 digital value | 03E8H | E8H, 03H |

■Data communication in ASCII code

(Request data)

| Command | Subcommand | Start address | Number of bytes | Module No. | Write data |
|---|---|---|---|---|---|
| 1601 (31H 36H 30H 31H) | 0000 (30H 30H 30H 30H) | 0000100A (30H 30H 30H 30H 31H 30H 30H 41H) | 0004 (30H 30H 30H 34H) | 0003 (30H 30H 30H 33H) | F4,01,E8,03 (46H 34H 30H 31H 45H 38H 30H 33H) |

| Write data | |
|---|---|
| F4,01 (46H 34H 30H 31H) | E8,03 (45H 38H 30H 33H) |
| Values of address 1H =01F4H | Values of address 2H =03E8H |

■Data communication in binary code

(Request data)

`01H 16H | 00H 00H | 0AH 10H 00H 00H | 04H 00H | 03H 00H | F4H 01H E8H 03H`
(Command | Subcommand | Start address | Number of bytes | Module No. | Write data)
(Write data: `F4H 01H` = Values of address 1H =01F4H; `E8H 03H` = Values of address 2H =03E8H)

---

## 11 CONTROL MODULE OPERATION

This chapter explains the commands for changing operation status and performing test using the functions of the module.

### 11.1 Data to be specified in commands

This section explains the contents and specification methods for data items which are set in each command related to module control.

#### Mode

Select the operation when the request target module is already in remote operation by other device.

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

| Process | | ASCII code | Binary code |
|---|---|---|---|
| Do not execute forcibly | Do not apply remote RUN/remote PAUSE while remote STOP/remote PAUSE is applied from other external device. | `0001` (30H 30H 30H 31H) | `01H 00H` (0001H) |
| Execute forcibly | Apply remote RUN/remote PAUSE while remote STOP/remote PAUSE is applied from other external device. | `0003` (30H 30H 30H 33H) | `03H 00H` (0003H) |

#### Clear mode

Select the range of the device memory to be cleared by the initialization processing at remote RUN.
When the device initial values are set, they will be reflected after the process with the selected clear mode.

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value.

| Process | | ASCII code | Binary code |
|---|---|---|---|
| Do not clear | Do not clear device memory. | `00` (30H 30H) | `00H` |
| Clear outside the range of latch | Clear the device memory outside the range of latch. | `01` (30H 31H) | `01H` |
| All clear | Clear all device memory including the range of latch. | `02` (30H 32H) | `02H` |

#### Model name and model code

The model name and model code of the access target module.

**Model name**

The character string of a model name is stored in 16-digit ASCII code.
If the read model name is less than 16 characters, a space (20H) is stored for the shortage of the characters.
The model name is stored in ASCII code while communicating in binary code.

**Ex.** For Q02HCPU

`Q02HCPU` + 9 spaces (padding to 16 digits) → ASCII/Binary (both use ASCII bytes):
`51H 30H 32H 48H 43H 50H 55H 20H 20H 20H 20H 20H 20H 20H 20H 20H`
(Q=51H, 0=30H, 2=32H, H=48H, C=43H, P=50H, U=55H, followed by 9× space 20H)

**Model code**

The model code of the module is stored.

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

**Ex.** For Q02HCPU (41H)

| | ASCII code | Binary code |
|---|---|---|
| Model code | `0041` → 30H 30H 34H 31H | `41H 00H` |

##### Model name and model code list

The following shows the list of model names and model codes.

**■MELSEC iQ-R series**

| Model name | Model code |
|---|---|
| RCPU | 0360H |
| R00CPU | 48A0H |
| R01CPU | 48A1H |
| R02CPU | 48A2H |
| R04CPU | 4800H |
| R04ENCPU | 4805H |
| R08CPU | 4801H |
| R08ENCPU | 4806H |
| R08PCPU | 4841H |
| R08PSFCPU | 4851H |
| R08SFCPU | 4891H |
| R16CPU | 4802H |
| R16ENCPU | 4807H |
| R16PCPU | 4842H |
| R16PSFCPU | 4852H |
| R16SFCPU | 4892H |
| R32CPU | 4803H |
| R32ENCPU | 4808H |
| R32PCPU | 4843H |
| R32PSFCPU | 4853H |
| R32SFCPU | 4893H |
| R120CPU | 4804H |
| R120ENCPU | 4809H |
| R120PCPU | 4844H |
| R120PSFCPU | 4854H |
| R120SFCPU | 4894H |
| RJ72GF15-T2 | 4860H |
| RJ72GF15-T2 (redundant system (single line) | 4861H |
| RJ72GF15-T2 (redundant system (redundant line)) | 4862H |

> **Note:** The PDF prints the 4861H row as "RJ72GF15-T2 (redundant system (single line)", with only one closing parenthesis.

> When a command is executed to an RCPU or a CC-Link IE Field Network remote head module from the module of which connected station is other than MELSEC iQ-R series, the model name 'RCPU' and the model code '0360H' are stored.

**■MELSEC iQ-L series**

| Model name | Model code |
|---|---|
| L04HCPU | 055DH |
| L08HCPU | 055EH |
| L16HCPU | 055FH |

**■MELSEC-L series**

| Model name | Model code |
|---|---|
| L02SCPU, L02SCPU-P | 0543H |
| L02CPU, L02CPU-P | 0541H |
| L06CPU, L06CPU-P | 0544H |
| L26CPU, L26CPU-P | 0545H |
| L26CPU-BT, L26CPU-PBT | 0542H |
| LJ72GF15-T2 | 0641H |

**■MELSEC-Q series**

| Model name | Model code |
|---|---|
| Q00JCPU | 0250H |
| Q00CPU | 0251H |
| Q01CPU | 0252H |
| Q02CPU, Q02HCPU, Q02PHCPU | 0041H |
| Q06HCPU, Q06PHCPU | 0042H |
| Q12HCPU, Q12PHCPU | 0043H |
| Q25HCPU, Q25PHCPU | 0044H |
| Q12PRHCPU | 004BH |
| Q25PRHCPU | 004CH |
| Q00UJCPU | 0260H |
| Q00UCPU | 0261H |
| Q01UCPU | 0262H |
| Q02UCPU | 0263H |
| Q03UDCPU, Q03UDECPU | 0268H |
| Q03UDVCPU | 0366H |
| Q04UDHCPU, Q04UDEHCPU | 0269H |
| Q04UDVCPU, Q04UDPVCPU | 0367H |
| Q06UDHCPU, Q06UDEHCPU | 026AH |
| Q06UDVCPU, Q06UDPVCPU | 0368H |
| Q10UDHCPU, Q10UDEHCPU | 0266H |
| Q13UDHCPU, Q13UDEHCPU | 026BH |
| Q13UDVCPU, Q13UDPVCPU | 036AH |
| Q20UDHCPU, Q20UDEHCPU | 0267H |
| Q26UDHCPU, Q26UDEHCPU | 026CH |
| Q26UDVCPU, Q26UDPVCPU | 036CH |
| Q50UDEHCPU | 026DH |
| Q100UDEHCPU | 026EH |
| QS001CPU | 0230H |

#### Remote password

**Remote password length**

Specify the number of characters of remote password.

- For MELSEC-Q/L series modules, the character string is fixed to 4 characters.
- For MELSEC iQ-R series modules, specify the number of character string of the remote password (6 to 32 characters).

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte*1 numerical values from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 4 characters, 32 characters

| Access target | ASCII code | Binary code |
|---|---|---|
| MELSEC-Q/L series module (4 characters fixed) | `0004` → 30H 30H 30H 34H | `04H 00H` |
| MELSEC iQ-R series module (32 characters) | `0020` → 30H 30H 32H 30H | `20H 00H` |

**Remote password**

The remote password is set by Engineering tool.
Specify the remote password in ASCII code while communicating in binary code.

**Ex.** When the password is 'ABCDEF'

`ABCDEF` → ASCII code = Binary code = `41H 42H 43H 44H 45H 46H`

#### Loopback data

**Loopback data length**

Number of bytes of loopback data.

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte*1 numerical values from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For 5 bytes

| | ASCII code | Binary code |
|---|---|---|
| Loopback data length | `0005` → 30H 30H 30H 35H | `05H 00H` |

**Loopback data**

The following characters can be used.

- 0 to 9 (30H to 39H)
- A to F (41H to 46H)

Specify the loopback data in ASCII code while communicating in binary code.

**Ex.** For 'ABCDEF'

| | ASCII code | Binary code |
|---|---|---|
| Loopback data | `41H 42H 43H 44H 45H 46H` | `41H 42H 43H 44H 45H 46H` |

#### Communication error information

A data to specify the communication error information to be initialized.
The data is equivalent to the buffer memory '0H', '1H' of MELSEC-Q/L series C24.
Specify the value (16-bit integer) in bit unit. The communication error information and its corresponding bit are as follows.

| Item | b15 | b14 | b13 to b8 | b7 | b6 | b5 | b4 | b3 | b2 | b1 | b0 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Communication error information (CH1) | — | — | — | NEU | ACK | NAK | C/N | P/S | PRO | SIO | SD.WAIT |
| Communication error information (CH2) | CH1.ERR | CH2.ERR | | | | | | | | | |

The communication error information to be initialized can be specified by turning the corresponding bit ON (1).
When the command is executed, the following corresponding bit of the buffer memory turns OFF (0).
"LED lighting status, communication error status" (Buffer memory 513 (201H), 514 (202H))
For MELSEC iQ-R series C24, specify '0'.

**Setting method**

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte*1 numerical values from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** When initializing C/N, P/S, PRO, and SIO (bit 1 to 4) with MELSEC-Q/L series C24.

| Item | ASCII code | Binary code |
|---|---|---|
| Communication error information (CH1) | `001E` → 30H 30H 31H 45H | `1EH 00H` |
| Communication error information (CH2) | `0000` → 30H 30H 30H 30H | `00H 00H` |

**Ex.** For MELSEC iQ-R series

| Item | ASCII code | Binary code |
|---|---|---|
| Communication error information (CH1) | `0000` → 30H 30H 30H 30H | `00H 00H` |
| Communication error information (CH2) | `0000` → 30H 30H 30H 30H | `00H 00H` |

---

### 11.2 Remote Operation

Change the operation status of CPU module.
For the remote operation function, refer to the manual of each CPU module.

> - When powering OFF to ON or resetting the access target CPU after applying remote RUN/STOP/PAUSE, the information of the remote operation will be cancelled. After powering OFF to ON or resetting CPU, the CPU operates with the status of the switch on the CPU module.
> - When the system protection of the access target module is enabled, the remote operation cannot be performed and an error response will be returned. Disable the system protection of the CPU module.
> - For E71, the communication using UDP/IP is recommended. For the communication using TCP/IP, re-establishment of the connection is required because the connection is disconnected at resetting CPU module.
> - One communication of command provides remote operation for 1 station.

#### Remote RUN (command: 1001)

Perform remote RUN to the access target module.
The command can be executed when the switch of the access target module is RUN. In the STOP status, the command is completed normally, however, the access target CPU will not be in RUN status.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1001H) → Subcommand(0000H) → Mode → Clear mode → Fixed values`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1001H`
  - ASCII code: `1001` (31H 30H 30H 31H)
  - Binary code (for C24)*1: `01H 10H 10H` (the middle 10H is marked "DLE")
  - Binary code (for E71): `01H 10H`

  *1 For C24, an additional code is added. (Page 35 Additional code (10H))
- ■Subcommand: `0000H`
  - ASCII code: `0000` (30H 30H 30H 30H)
  - Binary code: `00H 00H`
- ■Mode: Select the operation when the request target module is already in remote operation by other device. (Page 165 Mode)
  - `0001H`: Do not execute forcibly
  - `0003H`: Execute forcibly
- ■Clear mode: Select the range of the device memory to be cleared by the initialization processing at remote RUN. (Page 165 Clear mode)
  - `00H`: Do not clear
  - `01H`: Clear only outside the latch range
  - `02H`: All clear
- ■Fixed value: Fixed to '0'.
  - ASCII code: `00` (30H 30H)
  - Binary code: `00H`

**Communication example**

Remote RUN is performed when the mode is set to "Do not execute forcibly", and the clear mode is set to "All clear" (Mode = 0001H, Clear mode = 02H, Fixed value = 00H).

■Data communication in ASCII code (Request data)

| Command | Subcommand | Mode | Clear mode | Fixed value |
|---|---|---|---|---|
| `1001` (31H 30H 30H 31H) | `0000` (30H 30H 30H 30H) | `0001` (30H 30H 30H 31H) | `02` (30H 32H) | `00` (30H 30H) |

■Data communication in binary code (Request data)

| Command | Subcommand | Mode | Clear mode | Fixed value |
|---|---|---|---|---|
| `01H 10H` | `00H 00H` | `01H 00H` | `02H` | `00H` |

#### Remote STOP (command: 1002)

Perform remote STOP to the access target module.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1002H) → Subcommand(0000H) → Fixed values(0001H)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1002H`
  - ASCII code: `1002` (31H 30H 30H 32H)
  - Binary code (for C24)*1: `02H 10H 10H` (the middle 10H is marked "DLE")
  - Binary code (for E71): `02H 10H`

  *1 For C24, an additional code is added. (Page 35 Additional code (10H))
- ■Subcommand: `0000H`
  - ASCII code: `0000` (30H 30H 30H 30H)
  - Binary code: `00H 00H`
- ■Fixed value: The value is `0001H`.

**Communication example**

Perform remote STOP.

■Data communication in ASCII code (Request data)

`1002` `0000` `0001` → `31H 30H 30H 32H 30H 30H 30H 30H 30H 30H 30H 31H`

■Data communication in binary code (Request data)

`02H 10H` `00H 00H` `01H 00H` → `02H 10H 00H 00H 01H 00H`

#### Remote PAUSE (command: 1003)

Perform remote PAUSE to the access target module.
The command can be executed when the switch of the access target module is RUN. In the STOP status, the command is completed normally, however, the access target CPU will not be in PAUSE status.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1003H) → Subcommand(0000H) → Mode`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1003H`
  - ASCII code: `1003` (31H 30H 30H 33H)
  - Binary code (for C24)*1: `03H 10H 10H` (the middle 10H is marked "DLE")
  - Binary code (for E71): `03H 10H`

  *1 For C24, an additional code is added. (Page 35 Additional code (10H))
- ■Subcommand: `0000H`
  - ASCII code: `0000` (30H 30H 30H 30H)
  - Binary code: `00H 00H`
- ■Mode: Select the operation when the request target module is already in remote operation by other device. (Page 165 Mode)
  - `0001H`: Do not execute forcibly
  - `0003H`: Execute forcibly

**Communication example**

Remote PAUSE is performed when the mode is set to "Do not execute forcibly".

■Data communication in ASCII code (Request data)

`1003` `0000` `0001` → `31H 30H 30H 33H 30H 30H 30H 30H 30H 30H 30H 31H`

■Data communication in binary code (Request data)

`03H 10H` `00H 00H` `01H 00H` → `03H 10H 00H 00H 01H 00H`

#### Remote latch clear (command: 1005)

Perform remote latch clear to the access target module.

> - Execute the command after changing the status of the access target module to STOP.
> - The remote latch clear cannot be performed when the access target CPU is in remote STOP or remote PAUSE by other devices. The command will be terminated abnormally. Clear the remote STOP or remote PAUSE before executing the command.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1005H) → Subcommand(0000H) → Fixed values(0001H)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1005H`
  - ASCII code: `1005` (31H 30H 30H 35H)
  - Binary code (for C24)*1: `05H 10H 10H` (the middle 10H is marked "DLE")
  - Binary code (for E71): `05H 10H`

  *1 For C24, an additional code is added. (Page 35 Additional code (10H))
- ■Subcommand: `0000H`
  - ASCII code: `0000` (30H 30H 30H 30H)
  - Binary code: `00H 00H`
- ■Fixed value: The value is `0001H`.

**Communication example**

Perform remote latch clear.

■Data communication in ASCII code (Request data)

`1005` `0000` `0001` → `31H 30H 30H 35H 30H 30H 30H 30H 30H 30H 30H 31H`

■Data communication in binary code (Request data)

`05H 10H` `00H 00H` `01H 00H` → `05H 10H 00H 00H 01H 00H`

#### Remote RESET (command: 1006)

Perform remote RESET to the access target module.

> - Execute the command after changing the status of the access target module to STOP. If the CPU module is stopped due to the error, the command can be executed even when the switch of the CPU module is in the position of RUN.
> - If a remote RESET operation enable/disable setting exists in the access target parameter, set it to enable.
> - Remote RESET may not be performed due to the hardware error of the access target device.
> - When performing remote RESET, the response message may not be returned because the access target CPU is reset.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1006H) → Subcommand(0000H) → Fixed values(0001H)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1006H`
  - ASCII code: `1006` (31H 30H 30H 36H)
  - Binary code (for C24)*1: `06H 10H 10H` (the middle 10H is marked "DLE")
  - Binary code (for E71): `06H 10H`

  *1 For C24, an additional code is added. (Page 35 Additional code (10H))
- ■Subcommand: `0000H`
  - ASCII code: `0000` (30H 30H 30H 30H)
  - Binary code: `00H 00H`
- ■Fixed value: The value is `0001H`.

**Communication example**

Perform remote RESET.

■Data communication in ASCII code (Request data)

`1006` `0000` `0001` → `31H 30H 30H 36H 30H 30H 30H 30H 30H 30H 30H 31H`

■Data communication in binary code (Request data)

`06H 10H` `00H 00H` `01H 00H` → `06H 10H 00H 00H 01H 00H`

#### Read CPU model name (command: 0101)

Read model name and model code from the access target module.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(0101H) → Subcommand(0000H)`
- ■Response data: `Model name (16 bytes) → Model code (ASCII code: 4 bytes, binary code: 2 bytes)` — Model name and model code are stored. (Page 166 Model name and model code)
  - Discriminate the model name of the CPU with the model code.

**Data specified by request data**

- ■Command: `0101H`
  - ASCII code: `0101` (30H 31H 30H 31H)
  - Binary code: `01H 01H`
- ■Subcommand: `0000H`
  - ASCII code: `0000` (30H 30H 30H 30H)
  - Binary code: `00H 00H`

**Communication example**

Execute the command for Q02UCPU to read the model name and model code.

■Data communication in ASCII code

Request data: `0101` `0000` → `30H 31H 30H 31H 30H 30H 30H 30H`

Response data: Model name `Q02UCPU` + 9 spaces, Model code `0263`
`51H 30H 32H 55H 43H 50H 55H 20H 20H 20H 20H 20H 20H 20H 20H 20H 30H 32H 36H 33H`

■Data communication in binary code

Request data: `01H 01H 00H 00H`

Response data: Model name `Q02UCPU` + 9 spaces (ASCII, even in binary-code communication), Model code `63H 02H` (0263H)
`51H 30H 32H 55H 43H 50H 55H 20H 20H 20H 20H 20H 20H 20H 20H 20H 63H 02H`

---

### 11.3 Remote Password

This section explains the commands that unlock or lock the remote password.
For details on the remote password, refer to the manuals of access target CPU or CPU module.

The command can only be used for C24 (including multidrop connection station) and E71 connected to the external device. It cannot be used via network.
For the modem connection, access route must be set as the same route as that of the connected station (host station). (Page 45 ACCESS ROUTE SETTINGS)

**Execution procedure**

The communication with the module in which the remote password is set, follow the procedure shown below.

1. Line connection by modem (C24)/open processing for connection (E71)
2. Access permission (unlock processing) (Page 180 Unlock (command: 1630))
3. Access processing — Perform data communication by various commands of MC protocol.
4. Access prohibition (lock processing) (Page 182 Lock (command: 1631)) — For C24 or TCP/IP communication of E71, the lock processing is performed automatically at modem disconnection/close processing.
5. Line disconnection/close processing of connection

All the commands received when the remote password is locked will be an error response. Perform data communication after the unlock processing.

#### Unlock (command: 1630)

Specify the remote password to unlock the module. (The module can communicate.)

If the incorrect password is entered several times, the password is locked out and cannot be cleared for a while.
When the command is sent to the unlocked module, the unlock status is not changed. (The password verification is not performed.)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1630H) → Subcommand(0000H) → Remote password length → Remote password`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1630H`
- ■Subcommand: `0000H`
- ■Remote password length: Specify the number of characters of remote password. (Page 169 Remote password length)
- ■Remote password: Specify the set remote password. (Page 169 Remote password)

Field byte encoding: Command `1630` ASCII (31H 36H 33H 30H) / Binary `30H 16H`; Subcommand `0000` ASCII (30H 30H 30H 30H) / Binary `00H 00H`.

**Communication example (for MELSEC-Q series)**

Unlock the MELSEC-Q/L series module in which the following remote password has been set: Remote password `'1234'` (4 characters).

■Data communication in ASCII code (Request data)

`1630` `0000` `0004` `1234` → `31H 36H 33H 30H 30H 30H 30H 30H 30H 30H 30H 34H 31H 32H 33H 34H`

■Data communication in binary code (Request data)

Command `30H 16H`, Subcommand `00H 00H`, Remote password length `04H 00H`, Remote password `31H 32H 33H 34H` → `30H 16H 00H 00H 04H 00H 31H 32H 33H 34H`

**Communication example (files for MELSEC iQ-R series)**

Unlock the MELSEC iQ-R series module in which the following remote password has been set: Remote password `'abcdefghijklmnopqrstuvwxyz'` (26 characters).

■Data communication in ASCII code (Request data)

`1630` `0000` `001A` (26 = 001AH) + password characters `a`–`z` →
`31H 36H 33H 30H 30H 30H 30H 30H 30H 30H 31H 41H` followed by
`61H 62H 63H 64H 65H 66H 67H 68H 69H 6AH 6BH 6CH 6DH 6EH 6FH 70H 71H 72H 73H 74H 75H 76H 77H 78H 79H 7AH`

■Data communication in binary code (Request data)

Command `30H 16H`, Subcommand `00H 00H`, Remote password length `1AH 00H`, Remote password (ASCII) `61H`…`7AH` (a–z) →
`30H 16H 00H 00H 1AH 00H` followed by `61H 62H 63H 64H 65H 66H 67H 68H 69H 6AH 6BH 6CH 6DH 6EH 6FH 70H 71H 72H 73H 74H 75H 76H 77H 78H 79H 7AH`

#### Lock (command: 1631)

Specify the remote password to lock the module. (The module cannot communicate.)
When the command is sent to the locked module, the lock status is not changed. (The password verification is not performed.)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1631H) → Subcommand(0000H) → Remote password length → Remote password`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1631H`
- ■Subcommand: `0000H`
- ■Remote password length: Specify the number of characters of remote password. (Page 169 Remote password length)
- ■Remote password: Specify the set remote password. (Page 169 Remote password)

Field byte encoding: Command `1631` ASCII (31H 36H 33H 31H) / Binary `31H 16H`; Subcommand `0000` ASCII (30H 30H 30H 30H) / Binary `00H 00H`.

**Communication example (for MELSEC-Q series)**

Lock the MELSEC-Q/L series module in which the following remote password has been set: Remote password `'1234'` (4 characters).

■Data communication in ASCII code (Request data)

`1631` `0000` `0004` `1234` → `31H 36H 33H 31H 30H 30H 30H 30H 30H 30H 30H 34H 31H 32H 33H 34H`

■Data communication in binary code (Request data)

`31H 16H 00H 00H 04H 00H 31H 32H 33H 34H`

**Communication example (files for MELSEC iQ-R series)**

Lock the MELSEC iQ-R series module in which the following password has been set: Remote password `'abcdefghijklmnopqrstuvwxyz'` (26 characters).

■Data communication in ASCII code (Request data)

`1631` `0000` `001A` + password characters `a`–`z` →
`31H 36H 33H 31H 30H 30H 30H 30H 30H 30H 31H 41H` followed by
`61H 62H 63H 64H 65H 66H 67H 68H 69H 6AH 6BH 6CH 6DH 6EH 6FH 70H 71H 72H 73H 74H 75H 76H 77H 78H 79H 7AH`

■Data communication in binary code (Request data)

`31H 16H 00H 00H 1AH 00H` followed by `61H 62H 63H 64H 65H 66H 67H 68H 69H 6AH 6BH 6CH 6DH 6EH 6FH 70H 71H 72H 73H 74H 75H 76H 77H 78H 79H 7AH`

---

### 11.4 Loopback Test

This chapter explains the commands for testing the connection and data communication between an external device and connected station.

The command can only be used for C24 (including multidrop connection station) and E71 connected to the external device. It cannot be used via network.

#### Loopback test (command: 0619)

Perform the test to check whether the communication between the external device and connected station is normal. By performing the loopback test, the connection with external devices and operation of data communication can be checked.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(0619H) → Subcommand(0000H) → Number of loopback data → Loopback data`
- ■Response data: `Number of loopback data → Loopback data` — the same data as "Number of loopback data" and "Loopback data" specified for request message is stored. (Page 170 Loopback data)

**Data specified by request data**

- ■Command: `0619H`
- ■Subcommand: `0000H`
- ■Number of loopback data, loopback data: Specify the data to be transmitted by loopback test. (Page 170 Loopback data)
  The data can be specified within the range (numerals 0 to 9 and characters A to F) of 1 to 960 bytes.

Field byte encoding: Command `0619` ASCII (30H 36H 31H 39H) / Binary `19H 06H`; Subcommand `0000` ASCII (30H 30H 30H 30H) / Binary `00H 00H`.

**Communication example**

Perform the loopback test with the following loopback data.

- Loopback data: `'ABCDE'` (5 characters)

■Data communication in ASCII code

Request data: Command `0619`, Subcommand `0000`, Number of loopback data `0005`, Loopback data `ABCDE` →
`30H 36H 31H 39H 30H 30H 30H 30H 30H 30H 30H 35H 41H 42H 43H 44H 45H`

Response data: Number of loopback data `0005`, Loopback data `ABCDE` →
`30H 30H 30H 35H 41H 42H 43H 44H 45H`

■Data communication in binary code

Request data: `19H 06H 00H 00H 05H 00H 41H 42H 43H 44H 45H`
(Command `19H 06H` = 0619H; Subcommand `00H 00H` = 0000H; Number of loopback data `05H 00H` = 0005H; Loopback data `41H 42H 43H 44H 45H` = 'ABCDE')

Response data: `05H 00H 41H 42H 43H 44H 45H`
(Number of loopback data `05H 00H` = 0005H; Loopback data `41H 42H 43H 44H 45H` = 'ABCDE')

---

### 11.5 Clear Error Information

This section explains the command to initialize LED display and error information of buffer memory, and recover the supported device.
For details of the related LEDs, input/output signals, and buffer memory, refer to the manual of the access target module.

#### Turn indicator LED OFF, initialize error code (command: 1617)

Turn OFF the indicator LED of the serial communication module, and initialize the communication error information and error codes.

The command can only be used for C24 (including multidrop connection station) connected to the external device. It cannot be used via network.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1617H) → Subcommand → Communication error information (CH1) → Communication error information (CH2)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1617H`
  Field byte encoding: Command `1617` ASCII (31H 36H 31H 37H) / Binary `17H 16H`.
- ■Subcommand

| Target | Subcommand ASCII code | Subcommand Binary code | Value |
|---|---|---|---|
| MELSEC-Q/L series — CH1 side | `0005` (30H 30H 30H 35H) | `05H 00H` | 0005H |
| MELSEC-Q/L series — CH2 side | `000A` (30H 30H 30H 41H) | `0AH 00H` | 000AH |
| MELSEC-Q/L series — CH1 side, CH2 side | `000F` (30H 30H 30H 46H) | `0FH 00H` | 000FH |
| MELSEC iQ-R series — CH1 side, CH2 side | `0001` (30H 30H 30H 31H) | `01H 00H` | 0001H |

For C24 of MELSEC-Q/L series, 0 to 3 bits of subcommands are equivalent to the following functions of C24.
The settings in the table above is recommended, even though the initialization can be performed with the values (0001H to 000FH) which combined ON/OFF arbitrarily.
- Bit 0: CH1 Error initialization request (YE) ON
- Bit 1: CH2 Error initialization request (YF) ON
- Bit 2: LED for CH1 OFF, communication error information initialization request (buffer memory address: 0H) ON
- Bit 3: LED for CH2 OFF, communication error information initialization request (buffer memory address: 1H) ON

- ■Communication error information: Specify the items in "LED lighting status, communication error status" to be initialized. (Page 171 Communication error information)
  For MELSEC iQ-R series C24, specify '0'.

**Communication example**

Perform the following operations for CH1 interface of QJ71C24N-R2.

- ERR LED: OFF
- Input signal XE "Error occurrence": OFF
- Buffer memory 513 (201H) "LED lighting status, communication error status": Initialized (all items are OFF)
- Error code of buffer memory: Initialized (clear)

■Data communication in ASCII code (Request data)

Command `1617`, Subcommand `0005`, Communication error information (CH1) `00FF`, Communication error information (CH2) `0000`
→ `31H 36H 31H 37H 30H 30H 30H 35H 30H 30H 46H 46H 30H 30H 30H 30H`

> **Note:** The PDF prints the command bytes of this ASCII example as `31H 36H 33H 31H` under the characters "1617". This is an evident misprint: the Command field for 1617 is `31H 36H 31H 37H` (see Command above), so the bytes above use `31H 36H 31H 37H`.

■Data communication in binary code (Request data)

`17H 16H 05H 00H FFH 00H 00H 00H`
(Command `17H 16H` = 1617H; Subcommand `05H 00H` = 0005H (CH1 side); Communication error information (CH1) `FFH 00H` = 00FFH; Communication error information (CH2) `00H 00H` = 0000H)

#### Turn COM.ERR. LED OFF (command: 1617)

Turn the COM.ERR.LED of Ethernet interface module OFF.

The commands can be used for E71 connected to an external device and cannot be used via network.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(1617H) → Subcommand(0000H)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

- ■Command: `1617H`
- ■Subcommand: `0000H`

Field byte encoding: Command `1617` ASCII (31H 36H 31H 37H) / Binary `17H 16H`; Subcommand `0000` ASCII (30H 30H 30H 30H) / Binary `00H 00H`.

**Communication example**

Turn COM.ERR.LED OFF.

■Data communication in ASCII code (Request data)

Command `1617`, Subcommand `0000` → `31H 36H 31H 37H 30H 30H 30H 30H`

> **Note:** The PDF prints the command bytes of this ASCII example as `31H 36H 33H 31H` under the characters "1617". This is an evident misprint: the Command field for 1617 is `31H 36H 31H 37H` (see Command above), so the bytes above use `31H 36H 31H 37H`.

■Data communication in binary code (Request data)

`17H 16H 00H 00H`
(Command `17H 16H` = 1617H; Subcommand `00H 00H` = 0000H)

---

## 12 FILE CONTROL

This chapter explains the commands that operates files in the supported devices the CPU module.

Use this function in the following situations:

- To check the parameters and programs stored in the CPU module
- To change the parameters and programs in the CPU module according to the control content

For file name, extension, and storage location of files that can be handled by MC protocol, refer to the manual of the module to be accessed.

### 12.1 Execution Procedure

The following shows the file control procedures.

#### Procedure to read information from all files in directory (folder)

1. Read the file information from the head of the file.
   Specify '1' for "Head file No." and '36' (upper limit) for "Number of requested file", and execute the 'read directory/file information' command. The file information of "Number of file information" is stored in the response data.
   Page 210 Read directory/file information (command: 1810)
2. Check if there is a file from which the file information is not read.
   When "Number of file information" of the response data is in the status as shown below, it indicates that file information of all the files have been read. Complete the processing.
   - MELSEC-Q/L series (when using subcommand '0000'): "Number of file information" < "Number of requested file"
   - MELSEC iQ-R series (when using subcommand '0040'): "Number of file information" = -1 (FFFFH)
3. Read the file information from a file from which the file information is not read.
   Specify "Number of requested file" = 36 (upper limit) to the request data, and execute the command.
   For "Head file No.", specify one of the following value.
   - MELSEC-Q/L series (when using subcommand '0000'): "Head file No." = previous "Head file No." + "Number of file information"
   - MELSEC iQ-R series (when using subcommand '0040'): "Head file No." = previous "Last file No." + 1
   Page 210 Read directory/file information (command: 1810)
4. Repeat the procedure of Step 2 and later.

> The correct information cannot be obtained if the file operation is performed from other devices while reading information of all the files in the directory. Do not perform file operation from other devices while reading file information.

#### Procedure to read files

1. Check for file existence.
   Any of the following commands can be used.
   - Page 210 Read directory/file information (command: 1810)
   - Page 216 Search directory/file information (command: 1811)
2. Read the files.
   For read command, use open and close command to prohibit access from other devices.
   Execute the commands in the following order.
   - Page 235 Open file (command: 1827)
   - Page 239 Read file (command: 1828)
   - Page 243 Close file (command: 182A)

#### Procedure to overwrite files

1. Check for file existence.
   Any of the following commands can be used.
   - Page 210 Read directory/file information (command: 1810)
   - Page 216 Search directory/file information (command: 1811)
2. Write data to the file.
   For writing data, use open and close command to prohibit access from other devices.
   Execute the commands in the following order.
   - Page 235 Open file (command: 1827)
   - Page 241 Write to file (command: 1829)
   - Page 243 Close file (command: 182A)

In the following cases, create a new file and write data to it after deleting the target file.

- When the target file is sequence program file (`*.PRG`) or FB file (`*.PFB`) of MELSEC iQ-R series.
- When changing the file size of MELSEC-Q/L series is required.

Page 192 Procedure to delete files
Page 191 Procedure to create new file and write data

#### Procedure to create new file and write data

The procedure varies depending on types of file. Refer to procedure according to file types.

| File type | File extension: MELSEC-Q/L series | File extension: MELSEC iQ-R series | Reference |
|---|---|---|---|
| Header statement file | DAT | — | Page 191 When creation of temporary file is required |
| Sequence program file | QPG | PRG | Page 191 When creation of temporary file is required |
| Device comment file | QCD | DCM | Page 191 When creation of temporary file is required |
| Device initial value file | QDI | DID | Page 191 When creation of temporary file is required |
| FB file | — | PFB | Page 191 When creation of temporary file is required |
| File other than above |  |  | Page 191 When creation of temporary file is not required |

> Before creating a file, secure the free space of the target memory. It can be checked and secured by an Engineering tool.

**When creation of temporary file is required**

1. Create a new temporary file.
   Register the file name and reserve the required capacity for the file.
   The extension of the temporary file must be other than DAT, PRG, QPG, PFB, QCD, DCM, QDI, DID.
   - Page 219 Create new file (command: 1820)
2. Write data to the file.
   For writing data, use open and close command to prohibit access from other devices.
   Execute the commands in the following order.
   - Page 235 Open file (command: 1827)
   - Page 241 Write to file (command: 1829)
   - Page 243 Close file (command: 182A)
3. Create a file with the target file extension using the copy function.
   After copying a file, delete the temporary file of the copy source as necessary.
   - Page 225 Copy file (command: 1824)
   - Page 222 Delete file (command: 1822)

**When creation of temporary file is not required**

1. Check for file existence.
   Any of the following commands can be used.
   - Page 210 Read directory/file information (command: 1810)
   - Page 216 Search directory/file information (command: 1811)
2. Create a new file.
   Register the file name to reserve the required capacity for the file.
   - Page 219 Create new file (command: 1820)
3. Write data to the file.
   For writing data, use open and close command to prohibit access from other devices.
   Execute the commands in the following order.
   - Page 235 Open file (command: 1827)
   - Page 241 Write to file (command: 1829)
   - Page 243 Close file (command: 182A)

#### Procedure to delete files

1. Check for file existence.
   Any of the following commands can be used.
   - Page 210 Read directory/file information (command: 1810)
   - Page 216 Search directory/file information (command: 1811)
2. Delete the file.
   - Page 222 Delete file (command: 1822)

#### Procedure to copy files

> Before copying file, secure the free area of the target memory. It can be checked and secured with Engineering tool.

1. Check for file existence.
   Any of the following commands can be used.
   - Page 210 Read directory/file information (command: 1810)
   - Page 216 Search directory/file information (command: 1811)
2. Copy the file.
   After copying a file, delete the file of the copy source as necessary.
   - Page 225 Copy file (command: 1824)
   - Page 222 Delete file (command: 1822)

#### Procedure to modify file creation date and time

Modify the date of file creation by the 'modify file creation date and time' (command: 1826).

Page 232 Modify file creation date and time (command: 1826)

---

### 12.2 Considerations

The following shows the considerations for file control.

**Files such as read programs and parameters**

If the files such as program files and parameters which affect the system are read from the CPU module, keep the files for backup. Do not edit the data in the file on an external device. If the programs or parameters are required to be changed, use an Engineering tool.

**Access for '$MELPRJ$' folder of RCPU**

The $MELPRJ$ folder of RCPU is the folder that controls the data written from an Engineering. Do not access the $MELPRJ$ folder other than the purpose of data backup or restoration.

When performing data backup or restoration in the '$MELPRJ$' folder, read/write all the files in the '$MELPRJ$' folder. If only a part of '$MELPRJ$' folder is changed, it may not operate properly.

**If the file is protected**

When executing the following command, disable the protection (system protect of CPU module, the protection switch of SD memory card) of the access target CPU. If the command is executed with the access target protected, the command will be terminated abnormally.

| Function | Reference |
|---|---|
| Create new file (Register file name) | Page 219 Create new file (command: 1820) |
| Delete file | Page 222 Delete file (command: 1822) |
| Copy file | Page 225 Copy file (command: 1824) |
| Modify file attribute | Page 229 Modify file attribute (command: 1825) |
| Modify file creation date and time | Page 232 Modify file creation date and time (command: 1826) |
| Write to file | Page 241 Write to file (command: 1829) |

**Files that cannot be modified while CPU module is in RUN**

The executing file cannot be modified while the CPU module is in RUN.

When data write or delete command is executed to a file being executed, place CPU module in the STOP status. If the command is executed during RUN, the command completes abnormally.

Page 464 Applicable Commands for Online Program Change

---

### 12.3 Data to be specified in commands

This section explains the contents and specification methods for data items which are set in each command related to file control.

#### Password

Specify the password of the file to be accessed.

The specification of a password differs depending on the module. Specify a password which corresponds to the module of access target. For details on the password, refer to the manual of access target module.

| Access target | Password to be set | Subcommand | Data specified by Message |
|---|---|---|---|
| MELSEC-Q series module | Password (4 characters) | 0000 | Password character string (fixed to 4 characters) |
| MELSEC-Q series module | File password 32 (4 characters) | 0000 | Password character string (fixed to 4 characters) |
| MELSEC-L series module | File password 32 (4 to 32 characters) | 0004 | Password character string (fixed to 32 characters)*1 |
| MELSEC iQ-R series module | File password (6 to 32 characters) | 0040 | Number of password characters, password character string (variable length) |

*1 If the password is less than 32 characters, append a space (code: 20H).

**Password for MELSEC-Q series module (4 characters)**

Use the subcommand '0000'.

■When a password is set

Send password character string in 4-digit ASCII code. Specify the password in ASCII code during data communication in binary code as well.

**Ex.** When the password is 'ABCD'

| A | B | C | D |
|---|---|---|---|
| 41H | 42H | 43H | 44H |

(Same byte sequence `41H 42H 43H 44H` for both ASCII code and binary code data communication.)

> When setting the password with the file password 32 function of High-speed universal model QCPU, set it with four characters.

■When a password is not set

Specify 20H for 4 bytes: `20H 20H 20H 20H`

**File password 32 of MELSEC-L series (4 to 32 characters)**

Use the subcommand '0004'.

> Subcommand '0000' can be used only when the password is not set to the file. When subcommand 0000 is used to MELSEC-L series module, specify 20H for 4 bytes.

■When a password is set

Send password character string in 32-digit ASCII code. Specify the password in ASCII code during data communication in binary code as well. If the password is less than 32 characters, a space (20H) is added for the shortage of the characters.

**Ex.** When the password is 'ABCDEF' (32 digits total, padded with spaces)

`41H 42H 43H 44H 45H 46H` followed by `20H` repeated 26 times to fill the 32-byte field (same byte sequence for ASCII code and binary code data communication).

■When a password is not set

Specify 20H for 32 bytes (`20H` × 32).

**File password for MELSEC iQ-R series (6 to 32 characters)**

Use the subcommand '0040'.

> If multiple wrong passwords are attempted continuously, the password will be locked out and cannot unlock the password for a while.

■When a password is set

Specify the number of characters and the character string of password. Specify 'Password character string' with variable length. The length of data is specified in 'Number of characters'. (Page 199 Number of characters)

Field sequence: `Number of characters (4 digits ASCII / 2 bytes binary) → Password character string (variable length)`

Specify the password character string in ASCII code during data communication in binary code as well.

**Ex.** Password is "ABCDEFGHIJKLMNOPQRSTUVWXYZ" (26 characters, 1AH)

- ASCII code: Number of characters `001A` → `30H 30H 31H 41H`; Password character string → `41H 42H 43H 44H ... 5AH` (one ASCII byte per character, A=41H ... Z=5AH)
- Binary code: Number of characters (2 bytes, lower byte first) → `1AH 00H`; Password character string → `41H 42H 43H 44H ... 5AH` (ASCII byte per character, same as ASCII code representation)

■When a password is not set

Specify with the number of character '0'.

- ASCII code: `30H 30H 30H 30H`
- Binary code: `00H 00H`

---

#### Drive No.

This is a data to specify the drive in a CPU module of which files are to be managed.

| Specified value | Target drive: RCPU | Target drive: LCPU/QCPU/QnACPU |
|---|---|---|
| 0000H | — | Program memory |
| 0001H | Device/label memory (file storage area)<br>The same operation when 0003H is specified. | SRAM card |
| 0002H | SD memory card | Flash card, ATA card, SD memory card |
| 0003H | Device/label memory (file storage area) | Standard RAM |
| 0004H | Data memory | Standard ROM |

> The program memory of RCPU cannot be accessed. When reading/writing program files, use the data memory. (Page 193 Access for '$MELPRJ$' folder of RCPU)

**Setting method**

■Data communication in ASCII code: Convert the numerical value that indicates access target drive to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 2-byte numerical values that indicate access target drive from the lower byte (L: bits 0 to 7).

**Ex.** Drive No. is '0003H'

- ASCII code: `30H 30H 30H 33H`
- Binary code: `03H 00H`

---

#### File No.

This is a number for module to control files.

File No. can be obtained by following command.

Page 216 Search directory/file information (command: 1811)

**File No. of MELSEC-Q/L series module**

Use the subcommand '0000'. A file No. can be specified within the range of 1 to 256 (1H to 100H).

■Data communication in ASCII code: Convert the file No. to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 2-byte*1 numerical values that indicate file No. from lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** 1FH

- ASCII code: `30H 30H 31H 46H`
- Binary code: `1FH 00H`

**File No. of MELSEC iQ-R series module**

Use the subcommand '0040'.

■Data communication in ASCII code: Convert the file No. to ASCII code 8 digits (hexadecimal), and transmit it from the upper digits.

■Data communication in binary code: Send 4-byte*1 numerical values that indicate file No. from the lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** 1FH

- ASCII code: `30H 30H 30H 30H 30H 30H 31H 46H` (8-digit hex "0000001F")
- Binary code: `1FH 00H 00H 00H`

---

#### Number of files

Specify the number of files to be accessed. The number of registered files or the number of accessed files are returned.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 2-byte numerical values*1 from the lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** Number of file is 3

- ASCII code: `30H 30H 30H 33H`
- Binary code: `03H 00H`

---

#### Number of characters

This indicates the number of characters of variable length character string to be specified.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 2-byte numerical values*1 from the lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** Number of characters is 86 characters (56H)

- ASCII code: `30H 30H 35H 36H`
- Binary code: `56H 00H`

---

#### Directory specification

Specify the absolute path to a file to be accessed.

Specify 'Path name' with variable length. The length of data is specified in 'Number of characters'. (Page 199 Number of characters)

Field sequence: `Number of characters (4 digits ASCII / 2 bytes binary) → Path name (variable length)`

Specify '0' for the root directory (root folder)

- ASCII code: `30H 30H 30H 30H`
- Binary code: `00H 00H`

---

#### Path name

Specify the absolute path by UTF-16 from the root folder.

"Drive name:\" is not required in front of the path. Use '\' (005CH) for the delimiter between the folder names.

■Data communication in ASCII code: Convert the numerical value of UTF-16, which indicates a path character string, to ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send the numerical value of UTF-16 indicating the path character string from lower bytes (L: bits 0 to 7).

**Ex.** Folder root folder 'A' (UTF-16: 'A'= 0041)

- ASCII code (4-digit ASCII representation of the UTF-16 code, "0041"): `30H 30H 34H 31H`
- Binary code (2 bytes, lower byte first): `41H 00H`

---

#### File name specification

Specify the file name to be accessed.

A file name differs in specification by the module. Specify a corresponding file name for the module of access target. For details of usable file name, refer to the manual of module for access target.

| Access target | Usable file name | Subcommand | Data specified by Message |
|---|---|---|---|
| MELSEC-Q/L series module | File name (up to 8 characters) and extension (3 characters)<br>Specify with ASCII code character string. | 0000, 0004 | • File name (8 characters fixed) and extension (3 characters fixed)<br>• Number of characters (up to 12 characters) and file name character string (including the extension) |
| MELSEC iQ-R series module | Path name, file name (up to 60 characters) and extension<br>Specify with Unicode character string. | 0040 | • Number of characters (up to 252 characters) and file name character string (including the path and extension) |

> Files other than the one described as usable files in the manuals of modules are for system. Do not change the file name.

**File name of MELSEC-Q/L series module (File name and extension)**

When using the subcommand 0000 for the read directory/file information (command: 1810), the file name (8 characters fixed) and the extension (3 characters fixed) are stored in the response data. A period is not inserted between the file name and the extension.

Field sequence: `File name (8 digits) → Extension (3 digits)`

Specify the file name and the extension in ASCII code during data communication in binary code as well.

**Ex.** The file name and the extension are "ABCDEFGH.QPG".

`41H 42H 43H 44H 45H 46H 47H 48H` (ABCDEFGH) `51H 50H 47H` (QPG) — same byte sequence for ASCII code and binary code data communication.

**File name of MELSEC-Q/L series module (Number of characters and file name)**

For the data other than response data of read directory/file information (command: 1810), specify with number of characters and file name character string.

Use the subcommand '0000' or '0004'.

The file name and the extension are specified as a variable length character string with a period inserted between them. Specify the length of character string with "number of characters". (Page 199 Number of characters)

Field sequence: `Number of characters (4 digits ASCII / 2 bytes binary) → File name + "." + Extension (variable length)`

Specify "File name" + "Period (2EH)" + "Extension" in ASCII code during data communication in binary code as well.

**Ex.** File name and the extension are "ABC.QPG". (7 characters including a period)

- ASCII code: Number of characters `0007` → `30H 30H 30H 37H`; File name string → `41H 42H 43H 2EH 51H 50H 47H` (A B C . Q P G)
- Binary code: Number of characters (2 bytes) → `07H 00H`; File name string → `41H 42H 43H 2EH 51H 50H 47H` (same ASCII representation)

**File name of MELSEC iQ-R series module (Number of characters and file name)**

Use the subcommand '0040'.

Specify the file name including absolute path from the root folder with variable length. The file name and the extension are specified by inserting a period between them. Specify the length of character string with "number of characters". (Page 199 Number of characters)

Field sequence: `Number of characters (4 digits ASCII / 2 bytes binary) → File name + "." + Extension (variable length, UTF-16)`

Specify "Path name" + "Period (002EH)" + "Extension" by Unicode(UTF-16).

"Drive name:\" is not required in front of the path. Use '\' (005CH) for the delimiter between the folder names.

For the characters that cannot be used for a file name and naming rules, refer to the manual of the access target module.

■Data communication in ASCII code: Convert the numerical value of UTF-16, which indicates a file name, to ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send the numerical value of UTF-16 that indicates file name from the lower byte (L: bits 0 to 7).

**Ex.** When the file name and the extension are 'LINE\LINE.CSV' (13 characters)

Number of characters = 13 (000DH):

- ASCII code: `30H 30H 30H 44H` → File name + "." + Extension: `LINE\LINE.CSV`
- Binary code: `0DH 00H` → File name + "." + Extension: `LINE\LINE.CSV`

The value of the file name 'LINE\LINE.CSV' is as follows.

| Item | Value of code corresponding to character | | | | | | | | | | | | |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| File name | L | I | N | E | \ | L | I | N | E | . | C | S | V |
| UTF-16 | 004C | 0049 | 004E | 0045 | 005C | 004C | 0049 | 004E | 0045 | 002E | 0043 | 0053 | 0056 |
| ASCII code (4 bytes per char, upper digits first) | 30H 30H 34H 43H | 30H 30H 34H 39H | 30H 30H 34H 45H | 30H 30H 34H 35H | 30H 30H 35H 43H | 30H 30H 34H 43H | 30H 30H 34H 39H | 30H 30H 34H 45H | 30H 30H 34H 35H | 30H 30H 32H 45H | 30H 30H 34H 33H | 30H 30H 35H 33H | 30H 30H 35H 36H |
| Binary code (2 bytes per char, lower byte first) | 4CH 00H | 49H 00H | 4EH 00H | 45H 00H | 5CH 00H | 4CH 00H | 49H 00H | 4EH 00H | 45H 00H | 2EH 00H | 43H 00H | 53H 00H | 56H 00H |

---

#### Attribute

This indicates whether the data can be written to a file or directory.

**File attribute**

| Read-only attribute | Archive attribute | ASCII code | Binary code |
|---|---|---|---|
| Read-only | OFF | `30H 30H 30H 31H` (0001) | `01H 00H` |
| Read-only | ON | `30H 30H 32H 31H` (0021) | `21H 00H` |
| Writable, readable | OFF | `30H 30H 30H 30H` (0000) | `00H 00H` |
| Writable, readable | ON | `30H 30H 32H 30H` (0020) | `20H 00H` |

> Do not access the file in which the value other than above is stored in the attribute since the files are reserved for system use.

**Attribute of directory (folder)**

| Read-only attribute | Archive attribute | ASCII code | Binary code |
|---|---|---|---|
| Read-only | OFF | `30H 30H 31H 31H` (0011) | `11H 00H` |
| Read-only | ON | `30H 30H 33H 31H` (0031) | `31H 00H` |
| Writable, readable | OFF | `30H 30H 31H 30H` (0010) | `10H 00H` |
| Writable, readable | ON | `30H 30H 33H 30H` (0030) | `30H 00H` |

> Do not access the directory (folder) in which the value other than above is stored in the attribute since it is reserved for system use.

---

#### Creation date and time (last edit date and time)

This is a date and time when the current file contents are registered.

**Setting method**

Represent the date (year, month, day) and time (hour, minute, second) with 16-bit value, respectively.

■Data communication in ASCII code: Convert the respective numerical value to 4-digit ASCII code (hexadecimal) and send from the upper digits (time, year).

■Data communication in binary code: Send the respective 2-byte numerical value from the lower byte (L: bits 0 to 7).

**Date (year, month, day)**

Represent the year, month, and day with 16-bit value.

- Year: The binary value is represented with bits 9 to 15 by setting the year 1980 to '0'.*1
- Month: The binary value is represented with bits 5 to 8.
- Day: The binary value is represented with bits 0 to 4.

*1 This indicates incremental number of year by regarding 1980 as '0'.

**Ex.** April 1st, 2010

- Year: 2010 − 1980 = 30 (1EH) = `0011110`b (bits 9–15)
- Month: 4 (4H) = `0100`b (bits 5–8)
- Day: 1 (1H) = `00001`b (bits 0–4)
- Combined 16-bit value: `0011110 0100 00001`b = 3C81H (numerical values for 4 bits: 3H, CH, 8H, 1H)

- ASCII code (4-digit hex "3C81"): `33H 43H 38H 31H`
- Binary code (2 bytes, lower byte first): `81H 3CH`

**Time (hour, minute, second)**

Represent the hour, minute, second with 16-bit value.

- Hour: The binary value is represented with bits 11 to 15.
- Minute: The binary value is represented with bits 5 to 10.
- Second: The binary value divided by 2 is represented with bits 0 to 4.

**Ex.** When 20:50:58

- Hour: 20 (14H) = `10100`b (bits 11–15)
- Minute: 50 (32H) = `110010`b (bits 5–10)
- Second: 58 / 2 = 29 (1DH) = `11101`b (bits 0–4)
- Combined 16-bit value: `10100 110010 11101`b = A65DH (numerical values for 4 bits: AH, 6H, 5H, DH)

- ASCII code (4-digit hex "A65D"): `41H 36H 35H 44H`
- Binary code (2 bytes, lower byte first): `5DH A6H`

---

#### File size

This indicates the file capacity in byte units.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 8-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 4-byte numerical values*1 from the lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** The file size is 7168 bytes (1C00H)

- ASCII code (8-digit hex "00001C00"): `30H 30H 30H 30H 31H 43H 30H 30H`
- Binary code (4 bytes, lower byte first): `00H 1CH 00H 00H`

---

#### File pointer No.

This is a number for CPU module to manage files.

The file pointer No. can be acquired with the following command.

Page 235 Open file (command: 1827)

**Setting method**

■Data communication in ASCII code: Send 4-byte*1 ASCII code data.

■Data communication in binary code: Send 2-byte*1 numerical value.

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** For AH

- ASCII code: `30H 30H 30H 41H`
- Binary code: `0AH 00H`

> **Note:** In the PDF example diagram, the last ASCII byte under the character "A" is printed as `3AH`. The ASCII code of "A" is `41H`, which is used above.

---

#### Offset address

Specify the address (one address/one byte) from the head (offset address: 0H) of each file with an even number.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 8-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 4-byte numerical values*1 from the lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

The offset address ranges from 0 (head of file) to (File size) - 1.

**Ex.** Offset address is 780H (1920)

- ASCII code (8-digit hex "00000780"): `30H 30H 30H 30H 30H 37H 38H 30H`
- Binary code (4 bytes, lower byte first): `80H 07H 00H 00H`

---

#### Number of bytes

Specify the number of bytes of data to be read or written as one address/one byte.

**Setting method**

■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 2-byte numerical values*1 from the lower byte (L: bits 0 to 7).

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

**Ex.** Number of bytes is 780H (1920)

- ASCII code: `30H 37H 38H 30H`
- Binary code: `80H 07H`

---

#### Read data, write data

This is a content of file to be read/written. The data for 1 address is handled as 1-byte.

**Read data**

The data which have been read is stored. The "Read data" is variable length. The length of data is specified with "Number of bytes read".

**Write data**

The data to be written is stored. The "Write data" is variable length. The length of data is specified with "Number of bytes written". The order of data must be the same as the read data.

■Data communication in ASCII code: Convert the 1-byte data (1 address) to 2-digit ASCII code (hexadecimal), and send it from the upper digits.

■Data communication in binary code: Send 1 address as 1 byte.

---

#### Open mode

Specify whether the specified file is open for reading or for writing with the 'open file' (command: 1827).

| Item | ASCII code | Binary code |
|---|---|---|
| For reading | `30H 30H 30H 30H` (0000) | `00H 00H` |
| For writing | `30H 31H 30H 30H` (0100) | `00H 01H` |

---

#### Close type

Specify a target to be closed with the 'close file' (command: 182A).

When all files are specified for unlock, the external device that executes the close (command: 182A) closes all files being open regardless of the hierarchy of drive No. or directory (folder). (There is no difference between 0001H and 0002H.)

The files which were opened by the other devices cannot be closed. Executing the command to files locked by the other external device results in abnormal completion of the command.

> Files are closed by restarting a module (such as resetting CPU module).

| File to unlock | ASCII code | Binary code |
|---|---|---|
| A file specified by the file pointer | `30H 30H 30H 30H` (0000) | `00H 00H` |
| All files | `30H 30H 30H 31H` (0001) | `01H 00H` |
| All files | `30H 30H 30H 32H` (0002) | `02H 00H` |

---

### 12.4 File Check

Read directory (folder), file name, file creation date and time, and file No. in the specified drive.
The presence of files to be accessed and the file No. to access can be checked.

#### Read directory/file information (command: 1810)

For the specified storage destination file, read the file name, file creation date and time (last edit date and time) etc.
Read the file information from the head file specified with the file No. to the specified number of files.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1810) → Subcommand → Fixed values → Drive No. → Head file No. → Number of requested files → Directory specification`
- ■Response data:
  `Number of file information (n points) → Last file No. → [File → Last edit date and time → File size] = Directory/file information (1st point) → ... → [File → Last edit date and time → File size] = Directory/file information (nth point)`

**Data specified by request data**

■Command

| Item | ASCII code | Binary code (for C24)*1 | Binary code (for E71) |
|---|---|---|---|
| Command 1810 | `31H 38H 31H 30H` ("1810") | `DLE(10H) 10H 18H` | `10H 18H` |

*1 For C24, an additional code is added. (Page 35 Additional code (10H))

■Subcommand

| Item | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `30H 30H 30H 30H` ("0000") | `00H 00H` |
| For MELSEC iQ-R series | `30H 30H 34H 30H` ("0040") | `40H 00H` |

■Fixed value

Specify 30H in 4 bytes.

ASCII code, binary code: `30H 30H 30H 30H`

■Drive No.

Specify the access target drive. (Page 197 Drive No.)

■Head file No.

Specify the file No. of the file from which the file information is to be read. (Page 198 File No.)

File No. can be obtained by following command.
Page 216 Search directory/file information (command: 1811)

■Number of requested file

Specify the number of files from which file information is read in the range of 1 to 36. (Page 199 Number of files)

■Directory specification

Depending on the access target, specify any of the following:

- MELSEC iQ-R series (when using subcommand '0040'): Specify the target folder with an absolute path. (Page 199 Directory specification)
- MELSEC-Q/L series (when using subcommand '0000'): Specify '0'.

ASCII code: `30H 30H 30H 30H`; Binary code: `00H 00H`

When checking all files in the specified directory (folder), refer to the following section.
Page 189 Procedure to read information from all files in directory (folder)

**Data stored in response data**

■Number of file information

The number of file information which has been read is stored. (Page 199 Number of files)

In the following situation, the number of file information will be fewer than the number of requested file.

- When using subcommand '0040', the data of "Number of requested file" cannot be stored at one communication because the file name length is too long.
- When the number of files exist following the "Head file No." is less than the "Number of requested file".

When no files exist after the specified head file No., the following value is stored.

- MELSEC-Q/L series (when using subcommand '0000'): "Number of file information" = '0'
- MELSEC iQ-R series (when using subcommand '0040'): "Number of file information" = -1 (FFFFH)

■Last file No.

The file No. of the last file from which file information was read is stored. (Page 198 File No.)

For MELSEC-Q/L series (subcommand '0000'), last file No. is not stored.

File No. is also assigned for the system-reserved data that cannot acquire the directory/file information, therefore, the "Last file No." may differ from the file No. of the last file for the read file information.

Acquire the file No. using the following command.
Page 216 Search directory/file information (command: 1811)

Use "Last file No." in the following process.
Page 189 Procedure to read information from all files in directory (folder)

■Directory/file information

Information for number of file information is stored.

When folders exist in the specified storage destination, the information for the folder is also read.

The information of the current directory (.) and the parent directory (..) are read at the same time.

The following items are stored for each file.

- File name specification: The file name is stored. The format differs depending on the subcommand. (Path name is not included in the file name even when using the subcommand 0040.)

| Subcommand | Reference |
|---|---|
| For MELSEC-Q/L series (Subcommand: 0000) | Page 201 File name of MELSEC-Q/L series module (File name and extension) |
| For MELSEC iQ-R series (Subcommand: 0040) | Page 203 File name of MELSEC iQ-R series module (Number of characters and file name) |

- Attribute: The file attributes are stored. (Page 204 Attribute)
- Last edit time, Last edit date: The last edit date and time are stored. (Page 205 Creation date and time (last edit date and time))
- Spare data: The system data which are 18 or 4 bytes during data communication in ASCII code and 9 or 2 bytes*1 during data communication in binary code are stored.
- File size: Capacity of file is stored in byte unit. (Page 206 File size)

*1 For C24, the additional code may be added. (Page 35 Additional code (10H))

For a folder, the following values will be stored.

- Attribute: Attribute of the directory (folder) is stored. (Page 204 Attribute of directory (folder))
- Last edit time, Last edit date: The folder creation date and time are stored.
- File size: '0' is stored.

File information (1 point) field sequence: `[File name specification → Attribute → Spare data (ASCII: 18 bytes / Binary: 9 bytes)] = File → [Last edit time → Last edit date → Spare data (ASCII: 4 bytes / Binary: 2 bytes)] = Last edit date and time → File size`

**Communication example (files for MELSEC-Q/L series)**

Read directory/file information under the following conditions.

- Drive No.: 0
- Head file No.: 1
- Number of requested files: 3

■Data communication in ASCII code

(Request data)

| Subcommand | (Fixed value) | Drive No. | Head file No. | Number of file requests | Directory specification (Fixed values) |
|---|---|---|---|---|---|
| 0000 | 0000 | 0000 | 0001 | 0003 | 0000 |

`1810 → 0000 → 0000 → 0000 → 0001 → 0003 → 0000`
`31H 38H 31H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 31H 30H 30H 30H 33H 30H 30H 30H 30H`

(Response data)

`Number of file information → Directory/file information 1 → Directory/file information 2 → Directory/file information 3`

Number of file information: `0003` (`30H 30H 30H 33H`)

| Field | File name | Extension | Attribute | Spare data (18 bytes) |
|---|---|---|---|---|
| Directory/file information 1 | ABCDEFGH | QPG | 0001 | ... |

`41H 42H 43H 44H 45H 46H 47H 48H 51H 50H 47H 30H 30H 30H 31H`

| Field | Last edit time | Last edit date | Spare data | File size |
|---|---|---|---|---|
| Directory/file information 1 (cont.) | `A65D` ("20 h 58 m 58 s") | `3C81` ("April 1, 2010") | (4 bytes) | `00000400` ("1k byte") |

`41H 36H 35H 44H 33H 43H 38H 31H` (Last edit time, Last edit date), then Spare data (4 bytes), then `30H 30H 30H 30H 30H 34H 30H 30H` (File size)

> **Note:** In the examples of this command (Q/L and iQ-R, ASCII and binary code) the PDF labels the last edit time `A65D` (binary code: `5DH A6H`) as "20 h 58 m 58 s", whereas the time example on Page 206 (Ex. When 20:50:58) shows the same value `A65D` (`5DH A6H`) as 20:50:58.

■Data communication in binary code

(Request data)

`Command → Subcommand → (Fixed value) → Drive No. → Head file No. → Number of file requests → Directory specification (Fixed values)`
`10H 18H → 00H 00H → 30H 30H 30H 30H → 00H 00H → 01H 00H → 03H 00H → 00H 00H`

(Response data)

`Number of file information → Directory/file information 1 → Directory/file information 2 → Directory/file information 3`

Number of file information: `03H 00H`; then for Directory/file information 1: File name `ABCDEFGH` (`41H 42H 43H 44H 45H 46H 47H 48H`), Extension `QPG` (`51H 50H 47H`), Attribute `01H 00H`, Spare data (9 bytes), Last edit time `5DH A6H` ("20 h 58 m 58 s"), Last edit date `81H 3CH` ("April 1, 2010"), Spare data (2 bytes), File size `00H 04H 00H 00H` ("1k byte").

**Communication example (files for MELSEC iQ-R series)**

Read directory/file information under the following conditions.

- Drive No.: 4
- Head file No.: 1
- Number of requested files: 3

The path names of directory are shown in the following table.

(1) SUBDIR (6 characters)

| Item | Value of code corresponding to character |
|---|---|
| Path name | S U B D I R |
| UTF-16 | 0053 0055 0042 0044 0049 0052 |
| ASCII code | 30303533 30303535 30303432 30303434 30303439 30303532 |
| Binary code | 5300 5500 4200 4400 4900 5200 |

The following shows the file name of a directory/file information to be read. The current directory information is stored in the directory/file information 1. The parent directory information is stored in the directory/file information 2.

| Directory | Number of file name characters | Character code |
|---|---|---|
| Current directory | 1 | File name: `.` — UTF-16 `002E`; ASCII code `30303245`; Binary code `2E00` |
| Parent directory | 2 | File name: `. .` — UTF-16 `002E 002E`; ASCII code `30303245 30303245`; Binary code `2E00 2E00` |

The following shows the file name of the directory/file information 3.

(2) LINE.CSV (8 characters)

| Item | Value of code corresponding to character |
|---|---|
| File name | L I N E . C S V |
| UTF-16 | 004C 0049 004E 0045 002E 0043 0053 0056 |
| ASCII code | 30303443 30303439 30303445 30303435 30303245 30303433 30303533 30303536 |
| Binary code | 4C00 4900 4E00 4500 2E00 4300 5300 5600 |

■Data communication in ASCII code

(Request data)

`Subcommand(0040) → (Fixed value)(0000) → Drive No.(0004) → Head file No.(00000001) → Number of requested files(0003) → [Number of characters(0006) → Path name(1)] = Directory specification`

`1810 → 0040 → 0000 → 0004 → 00000001 → 0003 → 0006 → (1)`
`31H 38H 31H 30H 30H 30H 34H 30H 30H 30H 30H 30H 30H 30H 30H 34H 30H 30H 30H 30H 30H 30H 30H 31H 30H 30H 30H 33H 30H 30H 30H 36H (1)`

In the figure (1), set the value of "ASCII code" in the table of "Value of code corresponding to character" in "Path name".

(Response data)

`Number of file information → Last file No. → Directory/file information 1 → Directory/file information 2 → Directory/file information 3`

Number of file information `0003`, Last file No. `00000009`
`30H 30H 30H 33H 30H 30H 30H 30H 30H 30H 30H 39H`

For Directory/file information 3: Number of characters `0008` (`30H 30H 30H 38H`), File name (2), Attribute `0001` (`30H 30H 30H 31H`), Spare data (18 bytes), Last edit time `A65D` (`41H 36H 35H 44H`, "20 h 58 m 58 s"), Last edit date `3C81` (`33H 43H 38H 31H`, "April 1, 2010"), Spare data (4 bytes), File size `00000400` (`30H 30H 30H 30H 30H 34H 30H 30H`, "1k byte").

In the figure (2), set the value of "ASCII code" in the table of "Value of code corresponding to character" in "File name".

■Data communication in binary code

(Request data)

`Subcommand(40H 00H) → (Fixed value)(30H 30H 30H 30H) → Drive No.(04H 00H) → Head file No.(01H 00H 00H 00H) → Number of requested files(03H 00H) → [Number of characters(06H 00H) → Path name(1)] = Directory specification`

`10H 18H 40H 00H 30H 30H 30H 30H 04H 00H 01H 00H 00H 00H 03H 00H 06H 00H (1)`

In the figure (1), set the value of "Binary code" in the table of "Value of code corresponding to character" in "Path name".

(Response data)

`Number of file information → Last file No. → Directory/file information 1 → Directory/file information 2 → Directory/file information 3`

Number of file information `03H 00H`, Last file No. `09H 00H 00H 00H`. For Directory/file information 3: Number of characters `08H 00H`, File name (2), Attribute `01H 00H`, Spare data (9 bytes), Last edit time `5DH A6H` ("20 h 58 m 58 s"), Last edit date `81H 3CH` ("April 1, 2010"), Spare data (2 bytes), File size `00H 04H 00H 00H` ("1k byte").

In the figure (2), set the value of "Binary code" in the table of "Value of code corresponding to character" in "File name".

#### Search directory/file information (command: 1811)

Read the file No. of the specified file.
The file No. indicates the registration number of a file when the file is written to the module.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1811) → Subcommand → Fixed values 1 → Drive No. → Fixed values 2 → Number of file name characters → File name`
- ■Response data:
  `File No.`

File No. is stored. (Page 198 File No.)

If the file with the specified name does not exist, the command completes abnormally.

**Data specified by request data**

■Command

| Item | ASCII code | Binary code |
|---|---|---|
| Command 1811 | `31H 38H 31H 31H` ("1811") | `11H 18H` |

■Subcommand

| Item | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `30H 30H 30H 30H` ("0000") | `00H 00H` |
| For MELSEC iQ-R series | `30H 30H 34H 30H` ("0040") | `40H 00H` |

■Fixed value 1

Specify the following fixed value.

- MELSEC iQ-R series (when using subcommand '0040'): Specify '0'.

ASCII code: `30H 30H 30H 30H`; Binary code: `00H 00H 00H 00H`

- MELSEC-Q/L series (when using subcommand '0000'): Specify 20H in 4 bytes.

ASCII code, binary code: `20H 20H 20H 20H`

■Drive No.

Specify the access target drive. (Page 197 Drive No.)

■Fixed value 2

Specify '0'.

ASCII code: `30H 30H 30H 30H`; Binary code: `00H 00H`

■Number of file name characters, file name

Specify the file name of which file No. is to be read. (Page 201 File name specification)

**Communication example (files for MELSEC-Q series)**

Read the file No. in the following conditions.

- Password: 4 spaces (code: 20H)
- Drive No.: 0
- File name: ABC.QPG (file No.6)

■Data communication in ASCII code

(Request data)

`Command → Subcommand → (Fixed value 1) → Drive No. → (Fixed value 2) → Number of file name characters → File name`
`1811 → 0000 → 20H 20H 20H 20H → 0000 → 0000 → 0007 → ABC.QPG`
`31H 38H 31H 31H 30H 30H 30H 30H 20H 20H 20H 20H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 37H 41H 42H 43H 2EH 51H 50H 47H`

(Response data)

File No.: `0006` → `30H 30H 30H 36H`

■Data communication in binary code

(Request data)

`Subcommand(00H 00H) → (Fixed value 1)(20H 20H 20H 20H) → Drive No.(00H 00H) → (Fixed value 2)(00H 00H) → Number of file name characters(07H 00H) → File name(ABC.QPG)`
`11H 18H 00H 00H 20H 20H 20H 20H 00H 00H 00H 00H 07H 00H 41H 42H 43H 2EH 51H 50H 47H`

(Response data)

File No.: `06H 00H`

**Communication example (files for MELSEC iQ-R series)**

Read the file No. in the following conditions.

- Drive No.: 4
- File No.: 6

The file name is as follows:

(1) LINE\LINE.CSV (13 characters)

| Item | Value of code corresponding to character |
|---|---|
| File name | L I N E \ L I N E . C S V |
| UTF-16 | 004C 0049 004E 0045 005C 004C 0049 004E 0045 002E 0043 0053 0056 |
| ASCII code | 3030 3443 3030 3439 3030 3445 3030 3435 3030 3543 3030 3443 3030 3439 3030 3445 3030 3435 3030 3245 3030 3433 3030 3533 3030 3536 |
| Binary code | 4C00 4900 4E00 4500 5C00 4C00 4900 4E00 4500 2E00 4300 5300 5600 |

■Data communication in ASCII code

(Request data)

`Subcommand(0040) → (Fixed value 1)(0000) → Drive No.(0004) → (Fixed value 2)(0000) → Number of file name characters(000D) → File name(1)`
`1811 → 0040 → 0000 → 0004 → 0000 → 000D → (1)`
`31H 38H 31H 31H 30H 30H 34H 30H 30H 30H 30H 30H 30H 30H 30H 34H 30H 30H 30H 30H 30H 30H 30H 44H (1)`

In the figure (1), set the value of "ASCII code" in the table of "Value of code corresponding to character" in "File name".

(Response data)

File No.: `00000006` → `30H 30H 30H 30H 30H 30H 30H 36H`

> **Note:** On PDF page 220 (printed page 218) the ASCII-code response is printed with the digits `00000006`, but the byte under the seventh digit is printed as `39H` (`30H 30H 30H 30H 30H 30H 39H 36H`). This is an evident typo in the PDF: the condition is File No.: 6 and the binary-code response is `06H 00H 00H 00H`, so the ASCII-code bytes for File No. 6 are `30H 30H 30H 30H 30H 30H 30H 36H`.

■Data communication in binary code

(Request data)

`Subcommand(40H 00H) → (Fixed value 1)(00H 00H 00H 00H) → Drive No.(04H 00H) → (Fixed value 2)(00H 00H) → Number of file name characters(0DH 00H) → File name(1)`
`11H 18H 40H 00H 00H 00H 00H 00H 04H 00H 00H 00H 0DH 00H (1)`

In the figure (1), set the value of "Binary code" in the table of "Value of code corresponding to character" in "File name".

(Response data)

File No.: `06H 00H 00H 00H`

---

### 12.5 File Creation and Deletion

Create a new file or delete a file.

#### Create new file (command: 1820)

Create a file with specifying its size.

A folder cannot be created with this command. Create a folder with an Engineering tool.

> The time on a module is registered as the last edit date and time in a file created using this function.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1820) → Subcommand → Password → Drive No. → File size → Number of file name characters → File name`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| Item | ASCII code | Binary code |
|---|---|---|
| Command 1820 | `31H 38H 32H 30H` ("1820") | `20H 18H` |

■Subcommand

| Item | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q/L series | `30H 30H 30H 30H` ("0000") | `00H 00H` |
| For MELSEC iQ-R series | `30H 30H 34H 30H` ("0040") | `40H 00H` |

■Password

Specify the following fixed value.

- MELSEC iQ-R series (when using subcommand '0040'): Specify '0'.

ASCII code: `30H 30H 30H 30H`; Binary code: `00H 00H 00H 00H`

- MELSEC-Q/L series (when using subcommand '0000'): Specify 20H in 4 bytes.

ASCII code, binary code: `20H 20H 20H 20H`

■Drive No.

Specify the access target drive. (Page 197 Drive No.)

■File size

Specify the file capacity in byte unit. (Page 206 File size)

■Number of file name characters, file name

Specify the file name to be created. (Page 201 File name specification)

**Communication example (files for MELSEC-Q series)**

Create a new file in the following conditions:

- Password: 4 spaces (code: 20H)
- Drive No.: 0
- File name: ABC.CSV
- File size: 1K byte

■Data communication in ASCII code

(Request data)

`Subcommand → Password → Drive No. → File size → Number of file name characters → File name`
`1820 → 0000 → 20H×4 → 0000 → 00004000 → 0007 → ABC.CSV`
`31H 38H 32H 30H 30H 30H 30H 30H 20H 20H 20H 20H 30H 30H 30H 30H 30H 30H 30H 30H 34H 30H 30H 30H 30H 30H 30H 37H 41H 42H 43H 2EH 43H 53H 56H`

■Data communication in binary code

(Request data)

`Subcommand(00H 00H) → Password(20H 20H 20H 20H) → Drive No.(00H 00H) → File size(00H 40H 00H 00H) → Number of file name characters(07H 00H) → File name(ABC.CSV)`
`20H 18H 00H 00H 20H 20H 20H 20H 00H 00H 00H 40H 00H 00H 07H 00H 41H 42H 43H 2EH 43H 53H 56H`

> **Note:** The PDF prints the file size of this example as `00004000` in the ASCII code data and as `00H 40H 00H 00H` in the binary code data (4000H = 16384 bytes), although the condition above states "File size: 1K byte".

**Communication example (files for MELSEC iQ-R series)**

Create a new file in the following conditions:

- Drive No.: 4
- File size: 7168 byte
- File name: LINE.CSV (8 characters)

The value of the file name is as follows:

(1) LINE.CSV (8 characters)

| Item | Value of code corresponding to character |
|---|---|
| File name | L I N E . C S V |
| UTF-16 | 004C 0049 004E 0045 002E 0043 0053 0056 |
| ASCII code | 30303443 30303439 30303445 30303435 30303245 30303433 30303533 30303536 |
| Binary code | 4C00 4900 4E00 4500 2E00 4300 5300 5600 |

■Data communication in ASCII code

(Request data)

`Subcommand(0040) → (Fixed value)(0000) → Drive No.(0004) → File size(00001C00) → Number of file name characters(0008) → File name(1)`
`1820 → 0040 → 0000 → 0004 → 00001C00 → 0008 → (1)`
`31H 38H 32H 30H 30H 30H 34H 30H 30H 30H 30H 30H 30H 30H 30H 34H 30H 30H 30H 30H 31H 43H 30H 30H 30H 30H 30H 38H`

In the figure (1), set the value of "ASCII code" in the table of "Value of code corresponding to character" in "File name".

■Data communication in binary code

(Request data)

`Subcommand(40H 00H) → (Fixed value)(00H 00H 00H 00H) → Drive No.(04H 00H) → File size(00H 1CH 00H 00H) → Number of file name characters(08H 00H) → File name(1)`
`20H 18H 40H 00H 00H 00H 00H 00H 04H 00H 00H 1CH 00H 00H 08H 00H (1)`

In the figure (1), set the value of "Binary code" in the table of "Value of code corresponding to character" in "File name".

#### Delete file (command: 1822)

Delete a file.

> If files are deleted while a programmable controller system is in operation, the system may be stopped. Determine the timing of file deletion for the entire programmable controller system.
>
> - The files on which the 'open file' command is being executed cannot be deleted. Close the file before deleting it. (Page 235 Open file (command: 1827), Page 243 Close file (command: 182A))
> - The file being executed cannot be deleted when CPU module is state of RUN. Delete the file after placing CPU module to the STOP state. (Page 464 Applicable Commands for Online Program Change)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1822) → Subcommand → Password → Drive No. → Number of file name characters → File name`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| Item | ASCII code | Binary code |
|---|---|---|
| Command 1822 | `31H 38H 32H 32H` ("1822") | `22H 18H` |

■Subcommand

| Item | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q series | `30H 30H 30H 30H` ("0000") | `00H 00H` |
| For MELSEC-L series | `30H 30H 30H 34H` ("0004") | `04H 00H` |
| For MELSEC iQ-R series | `30H 30H 34H 30H` ("0040") | `40H 00H` |

■Password

Specify the password of the file. (Page 194 Password)

■Drive No.

Specify the access target drive. (Page 197 Drive No.)

■Number of file name characters, file name

Specify the file name to be deleted. (Page 201 File name specification)

**Communication example (files for MELSEC-Q series)**

Delete the file under the following conditions:

- Password: 1234
- Drive No.: 0
- File name: ABC.QPG

■Data communication in ASCII code

(Request data)

`Subcommand → Password → Drive No. → Number of file name characters → File name`
`1822 → 0000 → 1234 → 0000 → 0007 → ABC.QPG`
`31H 38H 32H 32H 30H 30H 30H 30H 31H 32H 33H 34H 30H 30H 30H 30H 30H 30H 30H 37H 41H 42H 43H 2EH 51H 50H 47H`

■Data communication in binary code

(Request data)

`Subcommand(00H 00H) → Password(31H 32H 33H 34H) → Drive No.(00H 00H) → Number of file name characters(07H 00H) → File name(ABC.QPG)`
`22H 18H 00H 00H 31H 32H 33H 34H 00H 00H 07H 00H 41H 42H 43H 2EH 51H 50H 47H`

**Communication example (files for MELSEC-L series)**

Delete the file under the following conditions:

- Password: AbCd1234□...□(24 spaces, code: 20H)
- Drive No.: 0
- File name: MAIN.QPG

■Data communication in ASCII code

(Request data)

`Subcommand → Password (fixed to 32 characters) → Drive No. → Number of file name characters → File name`
`1822 → 0004 → AbCd1234 ...(24 spaces) → 0000 → 0008 → MAIN.QPG`
`31H 38H 32H 32H 30H 30H 30H 34H 41H 62H 43H 64H 31H 32H 33H 34H 20H ... 20H 30H 30H 30H 30H 30H 30H 30H 38H 4DH 41H 49H 4EH 2EH 51H 50H 47H`

■Data communication in binary code

(Request data)

`Subcommand(04H 00H) → Password (fixed to 32 characters) → Drive No.(00H 00H) → Number of file name characters(08H 00H) → File name(MAIN.QPG)`
`22H 18H 04H 00H 41H 62H 43H 64H 31H 32H 33H 34H 20H ... 20H 00H 00H 08H 00H 4DH 41H 49H 4EH 2EH 51H 50H 47H`

**Communication example (files for MELSEC iQ-R series)**

Delete the file under the following conditions:

- Password: A to Z (26 characters)
- Drive No.: 4

The file name is as follows:

(1) LINE.CSV (8 characters)

| Item | Value of code corresponding to character |
|---|---|
| File name | L I N E . C S V |
| UTF-16 | 004C 0049 004E 0045 002E 0043 0053 0056 |
| ASCII code | 30303443 30303439 30303445 30303435 30303245 30303433 30303533 30303536 |
| Binary code | 4C00 4900 4E00 4500 2E00 4300 5300 5600 |

■Data communication in ASCII code

(Request data)

`Subcommand(0040) → Number of password characters(001A) → Password(AB...Z, 26 chars) → Drive No.(0004) → Number of file name characters(0008) → File name(1)`
`1822 → 0040 → 001A → AB…Z (26 characters: "ABCDEFGHIJKLMNOPQRSTUVWXYZ") → 0004 → 0008 → (1)`
`31H 38H 32H 32H 30H 30H 34H 30H 30H 30H 31H 41H 41H 42H 43H 44H ... 5AH 30H 30H 30H 34H 30H 30H 30H 38H (1)`

In the figure (1), set the value of "ASCII code" in the table of "Value of code corresponding to character" in "File name".

■Data communication in binary code

(Request data)

`Subcommand(40H 00H) → Number of password characters(1AH 00H) → Password(AB...Z) → Drive No.(04H 00H) → Number of file name characters(08H 00H) → File name(1)`
`22H 18H 40H 00H 1AH 00H 41H 42H 43H 44H ... 5AH 04H 00H 08H 00H (1)`

In the figure (1), set the value of "Binary code" in the table of "Value of code corresponding to character" in "File name".

> **Note:** The PDF prints `54H` under "Z" of the password in the ASCII code data (the ASCII code of "Z" is `5AH`, as printed in the binary code data) and `0DH 00H` for Number of file name characters in the binary code data (the file name "LINE.CSV" has 8 characters and the ASCII code data shows `0008`). The correct values `5AH` and `08H 00H` are shown above.

#### Copy file (command: 1824)

Copy a file.

> When this command is executed to files of parameter and a program being executed, place CPU module in the STOP status. (Page 464 Commands that cannot be executed during RUN)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1824) → Subcommand → Fixed values → [Copy destination: Password → Drive No. → Number of file name characters → File name] → [Copy source: Password → Drive No. → Number of file name characters → File name]`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| Item | ASCII code | Binary code |
|---|---|---|
| Command 1824 | `31H 38H 32H 34H` ("1824") | `24H 18H` |

■Subcommand

| Item | ASCII code | Binary code |
|---|---|---|
| For MELSEC-Q series | `30H 30H 30H 30H` ("0000") | `00H 00H` |
| For MELSEC-L series | `30H 30H 30H 34H` ("0004") | `04H 00H` |
| For MELSEC iQ-R series | `30H 30H 34H 30H` ("0040") | `40H 00H` |

■Fixed value

Specify '0'.

ASCII code: 16 characters of `30H` (16 bytes); Binary code: 8 bytes of `00H` (8 bytes)

■Password

Specify the password of the access target file. (Page 194 Password)

■Drive No.

Specify the access target drive. (Page 197 Drive No.)

Program memory (drive No.0) of RCPU cannot be specified.

■Number of file name characters, file name

Specify the file name to be copied. (Page 201 File name specification)

**Communication example (files for MELSEC-Q series)**

Copy the file under the following conditions:

- Copy source/destination password: 1234
- Drive No. of copy source, drive No. of copy destination: 0
- Copy source file name: ABC.QPG
- Copy source file name: CBA.QPG

■Data communication in ASCII code

(Request data)

`Subcommand → Fixed values (16 characters) → [Copy destination: Password → Drive No. → Number of file name characters → File name] → [Copy source: Password → Drive No. → Number of file name characters → File name]`

Copy destination: Password `1234`, Drive No. `0000`, Number of file name characters `0007`, File name `CBA.QPG`
Copy source: Password `1234`, Drive No. `0000`, Number of file name characters `0007`, File name `ABC.QPG`

`1824 → 0000 → 0000...0000 (16 characters) → 1234 → 0000 → 0007 → CBA.QPG → 1234 → 0000 → 0007 → ABC.QPG`
`31H 38H 32H 34H 30H 30H 30H 30H 30H 30H ... 30H 30H 31H 32H 33H 34H 30H 30H 30H 30H 30H 30H 30H 37H 43H 42H 41H 2EH 51H 50H 47H` (command, subcommand, fixed values, copy destination)
`31H 32H 33H 34H 30H 30H 30H 30H 30H 30H 30H 37H 41H 42H 43H 2EH 51H 50H 47H` (copy source)

■Data communication in binary code

(Request data)

Command, subcommand, fixed values, copy destination: `24H 18H(Command) → 00H 00H(Subcommand) → 00H×8(Fixed values) → 31H 32H 33H 34H(Password "1234") → 00H 00H(Drive No.) → 07H 00H(Number of file name characters) → 43H 42H 41H 2EH 51H 50H 47H(File name "CBA.QPG")`
`24H 18H 00H 00H 00H 00H 00H 00H 00H 00H 00H 00H 31H 32H 33H 34H 00H 00H 07H 00H 43H 42H 41H 2EH 51H 50H 47H`

Copy source: `Password(31H 32H 33H 34H) → Drive No.(00H 00H) → Number of file name characters(07H 00H) → File name(41H 42H 43H 2EH 51H 50H 47H "ABC.QPG")`
`31H 32H 33H 34H 00H 00H 07H 00H 41H 42H 43H 2EH 51H 50H 47H`

> **Note:** The PDF labels both file name conditions "Copy source file name". In the request data of the PDF, CBA.QPG is in the copy destination position and ABC.QPG is in the copy source position.

**Communication example (files for MELSEC-L series)**

Copy the file under the following conditions:

- Password copy source/destination AbCd1234□...□(24 spaces, code: 20H)
- Drive No. of copy source, drive No. of copy destination: 0
- Copy source file name: MAIN.QPG
- Copy source file name: DEST.QPG

■Data communication in ASCII code

(Request data)

Copy destination: Password (fixed to 32 characters) `AbCd1234` + 24 spaces, Drive No. `0000`, Number of file name characters `0008`, File name `DEST.QPG`
Copy source: Password (fixed to 32 characters) `AbCd1234` + 24 spaces, Drive No. `0000`, Number of file name characters `0008`, File name `MAIN.QPG`

`1824 → 0004 → 0000...0000 (16 characters) → AbCd1234...(32 chars total) → 0000 → 0008 → DEST.QPG → AbCd1234...(32 chars total) → 0000 → 0008 → MAIN.QPG`
`31H 38H 32H 34H 30H 30H 30H 34H 30H 30H ... 30H 30H 41H 62H 43H 64H 31H 32H 33H 34H 20H ... 20H 30H 30H 30H 30H 30H 30H 30H 38H 44H 45H 53H 54H 2EH 51H 50H 47H` (command, subcommand, fixed values, copy destination)
`41H 62H 43H 64H 31H 32H 33H 34H 20H ... 20H 30H 30H 30H 30H 30H 30H 30H 38H 4DH 41H 49H 4EH 2EH 51H 50H 47H` (copy source)

■Data communication in binary code

(Request data)

Command, subcommand, fixed values, copy destination: `24H 18H(Command) → 04H 00H(Subcommand) → 00H×8(Fixed values) → 41H 62H 43H 64H 31H 32H 33H 34H 20H...20H(Password, 32 bytes) → 00H 00H(Drive No.) → 08H 00H(Number of file name characters) → 44H 45H 53H 54H 2EH 51H 50H 47H(File name "DEST.QPG")`
`24H 18H 04H 00H 00H 00H 00H 00H 00H 00H 00H 00H 41H 62H 43H 64H 31H 32H 33H 34H 20H ... 20H 00H 00H 08H 00H 44H 45H 53H 54H 2EH 51H 50H 47H`

Copy source: `Password(41H 62H 43H 64H 31H 32H 33H 34H 20H...20H) → Drive No.(00H 00H) → Number of file name characters(08H 00H) → File name(4DH 41H 49H 4EH 2EH 51H 50H 47H "MAIN.QPG")`
`41H 62H 43H 64H 31H 32H 33H 34H 20H ... 20H 00H 00H 08H 00H 4DH 41H 49H 4EH 2EH 51H 50H 47H`

> **Note:** The PDF labels both file name conditions "Copy source file name". In the request data of the PDF, DEST.QPG is in the copy destination position and MAIN.QPG is in the copy source position.

**Communication example (files for MELSEC iQ-R series)**

Copy the file under the following conditions:

- Drive No. of copy source: 2
- Drive No. of copy destination: 4

The file name of the copy source and copy destination is as follows.

(1) LINE.CSV (8 characters)

| Item | Value of code corresponding to character |
|---|---|
| File name | L I N E . C S V |
| UTF-16 | 004C 0049 004E 0045 002E 0043 0053 0056 |
| ASCII code | 30303443 30303439 30303445 30303435 30303245 30303433 30303533 30303536 |
| Binary code | 4C00 4900 4E00 4500 2E00 4300 5300 5600 |

■Data communication in ASCII code

(Request data)

`Subcommand(0040) → Fixed values (16 characters, "0000000000000000") → Number of copy destination password characters(001A) → Copy destination password(AB…Z, 26 chars) → Copy destination drive No.(0004) → Number of copy destination file name characters(0008) → Copy destination file name(1) → Number of copy source password characters(001A) → Copy source password(AB…Z, 26 chars) → Copy source drive No.(0002) → Number of copy source file name characters(0008) → Copy source file name(1)`

`1824 → 0040 → 0000 0000 0000 0000 → 001A → AB…Z → 0004 → 0008 → (1) → 001A → AB…Z → 0002 → 0008 → (1)`
`31H 38H 32H 34H 30H 30H 34H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H 30H` (command+subcommand+fixed values)
`30H 30H 31H 41H 41H 42H 43H 44H ... 54H 30H 30H 30H 34H 30H 30H 30H 38H` (copy destination: num password chars, password, drive No., num file name chars)
`30H 30H 31H 41H 41H 42H 43H 44H ... 54H 30H 30H 30H 32H 30H 30H 30H 38H` (copy source: num password chars, password, drive No., num file name chars)

In the figure (1), set the value of "ASCII code" in the table of "Value of code corresponding to character" in "File name".

■Data communication in binary code

(Request data)

`24H 18H(Command) → 04H 00H(Subcommand) → 00H×8(Fixed values) → 1AH 00H(Number of copy destination password characters) → 41H 42H 43H 44H...54H(Copy destination password) → 04H 00H(Copy destination drive No.) → 08H 00H(Number of copy destination file name characters) → (1) → 1AH 00H(Number of copy source password characters) → 41H 42H 43H 44H...5AH(Copy source password) → 02H 00H(Copy source drive No.) → 08H 00H(Number of copy source file name characters) → (1)`

`24H 18H 04H 00H 00H 00H 00H 00H 00H 00H 00H 00H 1AH 00H 41H 42H 43H 44H ... 54H 04H 00H 08H 00H (1) 1AH 00H 41H 42H 43H 44H ... 5AH 02H 00H 08H 00H (1)`

In the figure (1), set the value of "Binary code" in the table of "Value of code corresponding to character" in "File name".

> **Note:** In the binary code data the PDF prints `04H 00H` in the Subcommand field, whereas the Subcommand table above gives `40H 00H` (binary code) for the MELSEC iQ-R series and the ASCII code data uses `0040`.

> **Note:** The PDF prints `54H` (instead of `5AH`, the ASCII code of "Z") under "Z" of the password in the ASCII code data (copy destination and copy source) and of the copy destination password in the binary code data. The copy source password in the binary code data is printed with `5AH`.

---

### 12.6 File Modification

Read or write data from/to the specified file. Lock the file with the open/close file command during reading or writing so that the file contents will not be changed from other devices. The file is not required to be locked with the open file command when commands other than read/write file command are executed.

When modifying the file attributes and the last edit date and time, executing the open command is not required.

#### Modify file attribute (command: 1825)

Change the file attributes (read only/writable).

> When this command is executed to files of parameter and a program being executed, place CPU module in the STOP status. (Page 464 Commands that cannot be executed during RUN)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1825H) → Subcommand → Password → Drive No. → Attribute → Number of file name characters → File name`
- ■Response data: There is no response data for this command.

**Command / Subcommand**

| Item | ASCII code | Binary code |
|---|---|---|
| Command | 1825 → `31H 38H 32H 35H` | `25H 18H` |
| Subcommand (For MELSEC-Q series) | 0000 → `30H 30H 30H 30H` | `00H 00H` |
| Subcommand (For MELSEC-L series) | 0004 → `30H 30H 30H 34H` | `04H 00H` |
| Subcommand (For MELSEC iQ-R series) | 0040 → `30H 30H 34H 30H` | `40H 00H` |

**Data specified by request data**

| Item | Description | Reference |
|---|---|---|
| Command | Fixed value: 1825H | — |
| Subcommand | 0000H (MELSEC-Q series) / 0004H (MELSEC-L series) / 0040H (MELSEC iQ-R series) | — |
| Password | Specify the password of the access target file. | Page 194 Password |
| Drive No. | Specify the access target drive. | Page 197 Drive No. |
| Attribute | Specify the file attribute. • Read only: 01H • Readable, writable: 20H. Do not specify the value other than above since the values are reserved for system use. | Page 204 Attribute |
| Number of file name characters, file name | Specify the file name to modify attribute. | Page 201 File name specification |

**Communication example (files for MELSEC-Q series)**

Change the file attribute in the following conditions.

- Password: 1234
- Drive No.: 0
- File name: ABC.QPG
- Attribute: Read only: 01H

Field order: `Command → Subcommand → Password → Drive No. → Attribute → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 35H` [Command 1825] `30H 30H 30H 30H` [Subcommand 0000] `31H 32H 33H 34H` [Password 1234] `30H 30H 30H 30H` [Drive No. 0000] `30H 30H 30H 31H` [Attribute 0001] `30H 30H 30H 37H` [Number of file name characters 0007] `41H 42H 43H 2EH 51H 50H 47H` [File name ABC.QPG]
- ■Data communication in binary code (Request data): `25H 18H` [Command] `00H 00H` [Subcommand] `31H 32H 33H 34H` [Password "1234"] `00H 00H` [Drive No.] `01H 00H` [Attribute] `07H 00H` [Number of file name characters] `41H 42H 43H 2EH 51H 50H 47H` [File name]

**Communication example (files for MELSEC-L series)**

Change the file attribute in the following conditions.

- Password: AbCd1234□...□(24 spaces, code: 20H)
- Drive No.: 0
- File name: MAIN.QPG
- Attribute: Read only: 01H

Field order: `Command → Subcommand → Password (fixed to 32 characters) → Drive No. → Attribute → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 35H` [Command 1825] `30H 30H 30H 34H` [Subcommand 0004] `41H 62H 43H 64H 31H 32H 33H 34H 20H...20H` (24× `20H`) [Password "AbCd1234" + 24 spaces] `30H 30H 30H 30H` [Drive No. 0000] `30H 30H 30H 31H` [Attribute 0001] `30H 30H 30H 38H` [Number of file name characters 0008] `4DH 41H 49H 4EH 2EH 51H 50H 47H` [File name MAIN.QPG]
- ■Data communication in binary code (Request data): `25H 18H` [Command] `04H 00H` [Subcommand] `41H 62H 43H 64H 31H 32H 33H 34H 20H...20H` (24× `20H`) [Password, raw ASCII, fixed 32 bytes] `00H 00H` [Drive No.] `01H 00H` [Attribute] `08H 00H` [Number of file name characters] `4DH 41H 49H 4EH 2EH 51H 50H 47H` [File name]

**Communication example (files for MELSEC iQ-R series)**

Change the file attribute in the following conditions.

- Password: A to Z (26 characters)
- Drive No.: 4
- Attribute: Read only: 01H

The file name is as follows:

(1) LINE.CSV (8 characters)

| Item | Value of code corresponding to character | | | | | | | |
|---|---|---|---|---|---|---|---|---|
| File name | L | I | N | E | . | C | S | V |
| UTF-16 | 004C | 0049 | 004E | 0045 | 002E | 0043 | 0053 | 0056 |
| ASCII code | 30303443 | 30303439 | 30303445 | 30303435 | 30303245 | 30303433 | 30303533 | 30303536 |
| Binary code | 4C00 | 4900 | 4E00 | 4500 | 2E00 | 4300 | 5300 | 5600 |

Field order (iQ-R): `Command → Subcommand → Number of password characters → Password → Drive No. → Attribute → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 35H` [Command 1825] `30H 30H 34H 30H` [Subcommand 0040] `30H 30H 31H 41H` [Number of password characters 001A = 26] `41H 42H 43H 44H ... 5AH` [Password "A"..."Z", 26 bytes] `30H 30H 30H 34H` [Drive No. 0004] `30H 30H 30H 31H` [Attribute 0001] `30H 30H 30H 38H` [Number of file name characters 0008] `30H 30H 34H 43H 30H 30H 34H 39H 30H 30H 34H 45H 30H 30H 34H 35H 30H 30H 32H 45H 30H 30H 34H 33H 30H 30H 35H 33H 30H 30H 35H 36H` [File name, each UTF-16 code unit as 4 ASCII hex digits]
- ■Data communication in binary code (Request data): `25H 18H` [Command] `40H 00H` [Subcommand] `1AH 00H` [Number of password characters] `41H 42H 43H 44H ... 5AH` [Password, 26 raw bytes] `04H 00H` [Drive No.] `01H 00H` [Attribute] `08H 00H` [Number of file name characters] `4CH 00H 49H 00H 4EH 00H 45H 00H 2EH 00H 43H 00H 53H 00H 56H 00H` [File name, UTF-16LE]

#### Modify file creation date and time (command: 1826)

Modify the file creation date and time.

> When this command is executed to files of parameter and a program being executed, place CPU module in the STOP status. (Page 464 Commands that cannot be executed during RUN)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1826H) → Subcommand → Fixed value → Drive No. → Date to change → Time to change → Number of file name characters → File name`
- ■Response data: There is no response data for this command.

**Command / Subcommand**

| Item | ASCII code | Binary code |
|---|---|---|
| Command | 1826 → `31H 38H 32H 36H` | `26H 18H` |
| Subcommand (For MELSEC-Q/L series) | 0000 → `30H 30H 30H 30H` | `00H 00H` |
| Subcommand (For MELSEC iQ-R series) | 0040 → `30H 30H 34H 30H` | `40H 00H` |

**Data specified by request data**

| Item | Description | Reference |
|---|---|---|
| Command | Fixed value: 1826H | — |
| Subcommand | 0000H (MELSEC-Q/L series) / 0040H (MELSEC iQ-R series) | — |
| Fixed value | Specify '0'. ASCII code: 0000 → `30H 30H 30H 30H` (4 characters); Binary code: `00H 00H 00H 00H` (4 bytes) | — |
| Drive No. | Specify the access target drive. | Page 197 Drive No. |
| Date to change, Time to change | The file of the last edit date and time is modified with the specified date and time. | Page 205 Creation date and time (last edit date and time) |
| Number of file name characters, file name | Specify the file name to modify attribute. | Page 201 File name specification |

**Communication example (files for MELSEC-Q series)**

Modify the date and time of file creation under the following conditions.

- Drive No.: 0
- Date to change: 2010/04/01
- Time to change: 20:50:58
- File name: ABC.QPG

Field order: `Command → Subcommand → Fixed value → Drive No. → Date to change → Time to change → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 36H` [Command 1826] `30H 30H 30H 30H` [Subcommand 0000] `30H 30H 30H 30H` [Fixed value 0000] `30H 30H 30H 30H` [Drive No. 0000] `33H 43H 38H 31H` [Date to change 3C81] `41H 36H 35H 44H` [Time to change A65D] `30H 30H 30H 37H` [Number of file name characters 0007] `41H 42H 43H 2EH 51H 50H 47H` [File name ABC.QPG]
- ■Data communication in binary code (Request data): `26H 18H` [Command] `00H 00H` [Subcommand] `00H 00H 00H 00H` [Fixed value, 4 bytes] `00H 00H` [Drive No.] `81H 3CH` [Date to change, LE] `5DH A6H` [Time to change, LE] `07H 00H` [Number of file name characters] `41H 42H 43H 2EH 51H 50H 47H` [File name]

**Communication example (files for MELSEC iQ-R series)**

Modify the date and time of file creation under the following conditions.

- Drive No.: 4
- Date to change: 2010/04/01
- Time to change: 20:50:58

The file name is as follows:

(1) LINE.CSV (8 characters)

| Item | Value of code corresponding to character | | | | | | | |
|---|---|---|---|---|---|---|---|---|
| File name | L | I | N | E | . | C | S | V |
| UTF-16 | 004C | 0049 | 004E | 0045 | 002E | 0043 | 0053 | 0056 |
| ASCII code | 30303443 | 30303439 | 30303445 | 30303435 | 30303245 | 30303433 | 30303533 | 30303536 |
| Binary code | 4C00 | 4900 | 4E00 | 4500 | 2E00 | 4300 | 5300 | 5600 |

- ■Data communication in ASCII code (Request data): `31H 38H 32H 36H` [Command 1826] `30H 30H 34H 30H` [Subcommand 0040] `30H 30H 30H 30H` [Fixed value 0000] `30H 30H 30H 34H` [Drive No. 0004] `33H 43H 38H 31H` [Date to change 3C81] `41H 36H 35H 44H` [Time to change A65D] `30H 30H 30H 38H` [Number of file name characters 0008] `30H 30H 34H 43H 30H 30H 34H 39H 30H 30H 34H 45H 30H 30H 34H 35H 30H 30H 32H 45H 30H 30H 34H 33H 30H 30H 35H 33H 30H 30H 35H 36H` [File name, UTF-16 code units as 4 ASCII hex digits each]
- ■Data communication in binary code (Request data): `26H 18H` [Command] `40H 00H` [Subcommand] `00H 00H 00H 00H` [Fixed value, 4 bytes] `04H 00H` [Drive No.] `81H 3CH` [Date to change] `5DH A6H` [Time to change] `08H 00H` [Number of file name characters] `4CH 00H 49H 00H 4EH 00H 45H 00H 2EH 00H 43H 00H 53H 00H 56H 00H` [File name, UTF-16LE]

#### Open file (command: 1827)

Open a file and lock the file so that the file contents are not modified from other devices.

The file is unlocked by any of the following.

- Close file (command: 182A)
- Restarting a module. (Resetting the CPU module, etc.)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1827H) → Subcommand → Password → Open mode → Drive No. → Number of file name characters → File name`
- ■Response data:
  `File pointer No.` — The file pointer No. is stored. (Page 207 File pointer No.)

**Command / Subcommand**

| Item | ASCII code | Binary code |
|---|---|---|
| Command | 1827 → `31H 38H 32H 37H` | `27H 18H` |
| Subcommand (For MELSEC-Q series) | 0000 → `30H 30H 30H 30H` | `00H 00H` |
| Subcommand (For MELSEC-L series) | 0004 → `30H 30H 30H 34H` | `04H 00H` |
| Subcommand (For MELSEC iQ-R series) | 0040 → `30H 30H 34H 30H` | `40H 00H` |

**Data specified by request data**

| Item | Description | Reference |
|---|---|---|
| Command | Fixed value: 1827H | — |
| Subcommand | 0000H (MELSEC-Q series) / 0004H (MELSEC-L series) / 0040H (MELSEC iQ-R series) | — |
| Password | Specify the password of the access target file. | Page 194 Password |
| Open mode | Specify whether the specified file is open for reading or for writing. • Open for read: 0000H • Open for write: 0100H | Page 209 Open mode |
| Drive No. | Specify the access target drive. | Page 197 Drive No. |
| Number of file name characters, file name | Specify the file name to be open. | Page 201 File name specification |

**Communication example (files for MELSEC-Q series)**

Open the file of QCPU in the following conditions.

- Password: 1234
- Drive No.: 0
- File name: ABC.QPG
- Open mode: Open for write

Field order: `Command → Subcommand → Password → Open mode → Drive No. → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 37H` [Command 1827] `30H 30H 30H 30H` [Subcommand 0000] `31H 32H 33H 34H` [Password 1234] `30H 31H 30H 30H` [Open mode 0100] `30H 30H 30H 30H` [Drive No. 0000] `30H 30H 30H 37H` [Number of file name characters 0007] `41H 42H 43H 2EH 51H 50H 47H` [File name ABC.QPG]
  (Response data): `30H 30H 30H 30H` [File pointer No. 0000]
- ■Data communication in binary code (Request data): `27H 18H` [Command] `00H 00H` [Subcommand] `31H 32H 33H 34H` [Password "1234"] `00H 01H` [Open mode 0100, LE] `00H 00H` [Drive No.] `07H 00H` [Number of file name characters] `41H 42H 43H 2EH 51H 50H 47H` [File name]
  (Response data): `00H 00H` [File pointer No.]

**Communication example (files for MELSEC-L series)**

Open the file of LCPU in the following condition.

- Password: AbCd1234□...□(24 spaces, code: 20H)
- Drive No.: 0
- File name: MAIN.QPG
- Open mode: Open for write

Field order: `Command → Subcommand → Password (fixed to 32 characters) → Open mode → Drive No. → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 37H` [Command 1827] `30H 30H 30H 34H` [Subcommand 0004] `41H 62H 43H 64H 31H 32H 33H 34H 20H...20H` (24× `20H`) [Password] `30H 31H 30H 30H` [Open mode 0100] `30H 30H 30H 30H` [Drive No. 0000] `30H 30H 30H 38H` [Number of file name characters 0008] `4DH 41H 49H 4EH 2EH 51H 50H 47H` [File name MAIN.QPG]
  (Response data): `30H 30H 30H 30H` [File pointer No. 0000]
- ■Data communication in binary code (Request data): `27H 18H` [Command] `04H 00H` [Subcommand] `41H 62H 43H 64H 31H 32H 33H 34H 20H...20H` (24× `20H`) [Password, 32 bytes] `00H 01H` [Open mode 0100, LE] `00H 00H` [Drive No.] `08H 00H` [Number of file name characters] `4DH 41H 49H 4EH 2EH 51H 50H 47H` [File name]
  (Response data): `00H 00H` [File pointer No.]

**Communication example (files for MELSEC iQ-R series)**

Open a file of MELSEC iQ-R series CPU module.

- Password: A to Z (26 characters)
- Drive No.: 4
- Open mode: Open for write
The file name is as follows:

(1) LINE.CSV (8 characters)

| Item | Value of code corresponding to character | | | | | | | |
|---|---|---|---|---|---|---|---|---|
| File name | L | I | N | E | . | C | S | V |
| UTF-16 | 004C | 0049 | 004E | 0045 | 002E | 0043 | 0053 | 0056 |
| ASCII code | 30303443 | 30303439 | 30303445 | 30303435 | 30303245 | 30303433 | 30303533 | 30303536 |
| Binary code | 4C00 | 4900 | 4E00 | 4500 | 2E00 | 4300 | 5300 | 5600 |

Field order (iQ-R): `Command → Subcommand → Number of password characters → Password → Open mode → Drive No. → Number of file name characters → File name`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 37H` [Command 1827] `30H 30H 34H 30H` [Subcommand 0040] `30H 30H 31H 41H` [Number of password characters 001A = 26] `41H 42H 43H 44H ... 5AH` [Password "A"..."Z", 26 bytes] `30H 31H 30H 30H` [Open mode 0100] `30H 30H 30H 34H` [Drive No. 0004] `30H 30H 30H 38H` [Number of file name characters 0008] `30H 30H 34H 43H 30H 30H 34H 39H 30H 30H 34H 45H 30H 30H 34H 35H 30H 30H 32H 45H 30H 30H 34H 33H 30H 30H 35H 33H 30H 30H 35H 36H` [File name, UTF-16 code units as 4 ASCII hex digits each]
  (Response data): `30H 30H 30H 30H` [File pointer No. 0000]
- ■Data communication in binary code (Request data): `27H 18H` [Command] `40H 00H` [Subcommand] `1AH 00H` [Number of password characters] `41H 42H 43H 44H ... 5AH` [Password, 26 raw bytes] `00H 01H` [Open mode 0100, LE] `04H 00H` [Drive No.] `08H 00H` [Number of file name characters] `4CH 00H 49H 00H 4EH 00H 45H 00H 2EH 00H 43H 00H 53H 00H 56H 00H` [File name, UTF-16LE]
  (Response data): `00H 00H` [File pointer No.]

> **Note:** In the binary-code diagram of this example (PDF page 240, printed page 238), the last Password byte (under "Z") is printed as `34H`. This is an evident typo in the PDF: "Z" is `5AH`, as printed in the ASCII-code diagram of the same example.

#### Read file (command: 1828)

Read a file content.

Use the open/close command in order to prohibit access from other devices when this command is used.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1828H) → Subcommand → File pointer No. → Offset address → Number of bytes read`
- ■Response data:
  `Number of bytes read → Read data`
  *(The data which have been read is stored.)*

**Command / Subcommand**

| Item | ASCII code | Binary code |
|---|---|---|
| Command | 1828 → `31H 38H 32H 38H` | `28H 18H` |
| Subcommand | 0000 → `30H 30H 30H 30H` | `00H 00H` |

**Data specified by request data**

| Item | Description | Reference |
|---|---|---|
| Command | Fixed value: 1828H | — |
| Subcommand | 0000H | — |
| File pointer No. | Specify the file pointer No. obtained with the 'open file' (command: 1827). | Page 207 File pointer No. |
| Offset address | Specify the start address to start reading. | Page 207 Offset address |
| Number of bytes read | Specify the number of bytes to be read from file in the range of 0 to 1920. Specify it as one address/one byte. When the file size is 1921 bytes or more, use an offset address and read data divide into multiple steps. The file size can be checked by reading directory/file information (command: 1810). | Page 208 Number of bytes; Page 210 Read directory/file information (command: 1810) |

**Data stored in response data**

| Item | Description | Reference |
|---|---|---|
| Number of bytes read | The number of data bytes read from file is stored. | Page 208 Number of bytes |
| Read data | The contents of the read file are stored. | — |

**Communication example**

Read the file under the following conditions.

- File pointer No.: 0
- Number of bytes read: 1K bytes

Field order: `Command → Subcommand → File pointer No. → Offset address → Number of bytes read`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 38H` [Command 1828] `30H 30H 30H 30H` [Subcommand 0000] `30H 30H 30H 30H` [File pointer No. 0000] `30H 30H 30H 30H 30H 30H 30H 30H` [Offset address 00000000] `30H 34H 30H 30H` [Number of bytes read 0400]
  (Response data): `30H 34H 30H 30H` [Number of bytes read 0400] + Read data (contents of a file)
- ■Data communication in binary code (Request data): `28H 18H` [Command] `00H 00H` [Subcommand] `00H 00H` [File pointer No.] `00H 00H 00H 00H` [Offset address] `00H 04H` [Number of bytes read, LE = 0400H = 1024]
  (Response data): `00H 04H` [Number of bytes read, LE] + Read data (contents of a file)

#### Write to file (command: 1829)

Write content to a file.

Use the open/close command in order to prohibit access from other devices when this command is used.

> When this command is executed to files of parameter and a program being executed, place CPU module in the STOP status. (Page 464 Commands that cannot be executed during RUN)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1829H) → Subcommand → File pointer No. → Offset address → Number of bytes written → Write data`
- ■Response data:
  `Number of bytes written` — The number of data bytes written in the file is stored. (Page 208 Number of bytes)

**Command / Subcommand**

| Item | ASCII code | Binary code |
|---|---|---|
| Command | 1829 → `31H 38H 32H 39H` | `29H 18H` |
| Subcommand | 0000 → `30H 30H 30H 30H` | `00H 00H` |

**Data specified by request data**

| Item | Description | Reference |
|---|---|---|
| Command | Fixed value: 1829H | — |
| Subcommand | 0000H | — |
| File pointer No. | Specify the file pointer No. obtained with the 'open file' (command: 1827). | Page 207 File pointer No. |
| Offset address | Specify the start address to start writing. • Writing data to a file of which drive name is '00H' (program memory): Specify the address in multiples of 4 (0, 4, 8, ... in decimal notation). • Writing data to a file of which drive name is other than '00H': Specify the addresses in even numbered addresses (0, 2, 4, 6, 8, ... in decimal notation). | Page 207 Offset address |
| Number of bytes written | Specify the number of bytes to be written to a file in the range of 0 to 1920. Specify it as one address/one byte. Write data to the file within the file size reserved by creating new file. When the file size is 1921 bytes or more, use an offset address and write data divide into multiple steps. The file size can be checked by reading directory/file information (command: 1810). | Page 208 Number of bytes; Page 210 Read directory/file information (command: 1810) |
| Write data | Specify the data to be written to a file. | — |

**Communication example**

Write the file under the following conditions.

- File pointer No.: 0
- Offset address: 0
- Number of bytes written: 1K byte

Field order: `Command → Subcommand → File pointer No. → Offset address → Number of bytes written → Write data`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 39H` [Command 1829] `30H 30H 30H 30H` [Subcommand 0000] `30H 30H 30H 30H` [File pointer No. 0000] `30H 30H 30H 30H 30H 30H 30H 30H` [Offset address 00000000] `30H 34H 30H 30H` [Number of bytes written 0400] + Write data (contents of a file)
  (Response data): `30H 34H 30H 30H` [Number of bytes written 0400]
- ■Data communication in binary code (Request data): `29H 18H` [Command] `00H 00H` [Subcommand] `00H 00H` [File pointer No.] `00H 00H 00H 00H` [Offset address] `00H 04H` [Number of bytes written, LE = 0400H = 1024] + Write data (contents of a file)
  (Response data): `00H 04H` [Number of bytes written, LE]

#### Close file (command: 182A)

Close a file and unlock the file which has been locked by the 'open file' (command: 1827).

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(182AH) → Subcommand → File pointer No. → Close type`
- ■Response data: There is no response data for this command.

**Command / Subcommand**

| Item | ASCII code | Binary code |
|---|---|---|
| Command | 182A → `31H 38H 32H 41H` | `2AH 18H` |
| Subcommand | 0000 → `30H 30H 30H 30H` | `00H 00H` |

**Data specified by request data**

| Item | Description | Reference |
|---|---|---|
| Command | Fixed value: 182AH | — |
| Subcommand | 0000H | — |
| File pointer No. | Specify the file pointer No. obtained with the 'open file' (command: 1827). | Page 207 File pointer No. |
| Close type | Specify the file to be closed. When closing a file specified by file pointer, specify '0'. | Page 209 Close type |

Close type '0':

| | ASCII code | Binary code |
|---|---|---|
| 0000 | `30H 30H 30H 30H` | `00H 00H` |

> The locked files which have been opened by other devices cannot be unlocked. Executing the command to files which have been opened by the other external device results in abnormal completion of the command.
>
> Files are closed by restarting a module (such as resetting CPU module).

**Communication example**

Close the file under the following conditions.

- File pointer No.: 0
- Close type: 2

Field order: `Command → Subcommand → File pointer No. → Close type`

- ■Data communication in ASCII code (Request data): `31H 38H 32H 41H` [Command 182A] `30H 30H 30H 30H` [Subcommand 0000] `30H 30H 30H 30H` [File pointer No. 0000] `30H 30H 30H 32H` [Close type 0002]
- ■Data communication in binary code (Request data): `2AH 18H` [Command] `00H 00H` [Subcommand] `00H 00H` [File pointer No.] `02H 00H` [Close type, LE = 0002H]

---

## 13 SERIAL COMMUNICATION MODULE DEDICATED COMMANDS

This chapter explains the dedicated commands for serial communication module.

### 13.1 User Frame

A user frame is a data name which is used to send/receive data by registering the fixed format portion in a message to be communicated between an external device and serial communication module.

```
Control code → such as an access route → Arbitrary data → Control code → Sum check code
└──────────── User frame ────────────┘                    └─────── User frame ────────┘
```

Control code or sum check code in the message are registered as a default registration frame. The data such as an access route can be registered to the user frame.

By using the user frame, the following data communication can be performed.

- Transmission of on-demand data by MC protocol
- Data communication by nonprocedural protocol

This section explains the commands for an external device to register, delete, and read user frames to C24. For transmission and reception of data using a user frame, refer to the following manual.

- MELSEC-Q/L Serial Communication Module User's Manual (Application)
- MELSEC iQ-R Serial Communication Module User's Manual (Application)

> The command can only be used for C24 (including multidrop connection station) connected to the external device. It cannot be used via network.
>
> This command is processed by the connected C24/E71 without waiting for the END processing by CPU module.

#### Data to be specified in command

This section explains the contents and specification methods for data items which are set in each command related to the user frame.

##### Frame No.

Specify the number of target user frame.

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

| Type | Setting value | Registration destination | Remarks |
|---|---|---|---|
| Default registration frame | 1H to 3E7H | ROM for C24 operating system | Read only |
| User frame | 3E8H to 4AFH | Flash ROM of C24 | Read, write, delete |
| User frame | 8001H to 801FH | Buffer memory of C24 (addresses 1B00H to 1FF6H) | Read, write, delete |

**Ex.** For 3E8H

| | ASCII code | Binary code |
|---|---|---|
| 03E8 | `30H 33H 45H 38H` | `E8H 03H` |

##### Number of registration data byte, number of frame byte

Specify the number of bytes of registration data.

For the number of registration data byte, calculate the variable data as 2 bytes.
For the number of frame byte, calculate the variable data as 1 byte.

For the variable data, refer to the following manuals.

- MELSEC-Q/L Serial Communication Module User's Manual (Application)
- MELSEC iQ-R Serial Communication Module User's Manual (Application)

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

| Function | Setting value |
|---|---|
| Data registration, read data | 1H to 50H (1 to 80) |
| Deletion | 0H |

**Ex.** When the registration data is ETX + variable data (sum check code) + CR + LF

- Number of registration data byte = 1 byte (ETX) + 2 byte (variable data) + 1 byte (CR) + 1 byte (LF) = 5

| | ASCII code | Binary code |
|---|---|---|
| 0005 | `30H 30H 30H 35H` | `05H 00H` |

- Number of frame byte = 1 byte (ETX) + 1 byte (variable data) + 1 byte (CR) + 1 byte (LF) = 4

| | ASCII code | Binary code |
|---|---|---|
| 0004 | `30H 30H 30H 34H` | `04H 00H` |

##### Registration data

The following shows the content of data in a user frame.

- ■Data communication in ASCII code: Convert the data to 2-digit (hexadecimal) ASCII code, and send it from the upper digit.
- ■Data communication in binary code: Send data from the first frame.

**Ex.** When the registration data is ETX + Variable data (sum check code) + CR + LF

| Registration data | ETX | Variable data (Sum check code) | CR | LF |
|---|---|---|---|---|
| Binary code | `03H` | `FFH F0H` | `0DH` | `0AH` |
| ASCII code | `30H 33H` | `46H 46H 46H 31H` | `30H 44H` | `30H 41H` |

> **Note:** In the PDF, the variable data (sum check code) of this example is shown as `FFF1` (`46H 46H 46H 31H`) in the ASCII code example and as `FFH F0H` in the binary code example.

#### Read registered data (command: 0610)

Read the registered content of user frames.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(0610) → Subcommand(0000) → Frame No.`
- ■Response data:
  `Registration data byte count → Frame byte count → Registration data`
  *(The read registration data is stored.)*

**Data specified by request data**

■Command

| | ASCII code | Binary code[^13.1-3] |
|---|---|---|
| 0610 | `30H 36H 31H 30H` | `10H 06H` (sent as `10H 10H 06H` with additional code) |

[^13.1-3]: For C24, an additional code is added. (Page 35 Additional code (10H))

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| 0000 | `30H 30H 30H 30H` | `00H 00H` |

■Frame No.

Specify the number of user frame to be read. (Page 246 Frame No.)

When the frame No. whose user frame is not registered is specified, an error occurs and an abnormal response is returned.

| Type | Setting value | Registration destination |
|---|---|---|
| Default registration frame | 1H to 3E7H | ROM for C24 operating system |
| User frame | 3E8H to 4AFH | Flash ROM of C24 |
| User frame | 8001H to 801FH | Buffer memory of C24 (addresses 1B00H to 1FF6H) |

**Data to be stored by response data**

■Number of registration data byte, number of frame byte

The data byte count of the registration data is stored. (Page 246 Number of registration data byte, number of frame byte)

■Registration data

The data content of user frames to be registered is stored. (Page 247 Registration data)

**Communication example**

Read the following registration data from frame No. 3E8H.
Registration data: ETX + Variable data (sum check code) + CR + LF

■Data communication in ASCII code

Request data: `0610 0000 03E8` → `30H 36H 31H 30H 30H 30H 30H 30H 30H 33H 45H 38H`
(Command | Subcommand | Frame No.)

Response data: `0005 0004 [Registration data: 03,FF,F1,0D,0A]` →
`30H 30H 30H 35H` (0005, number of registration data byte) `30H 30H 30H 34H` (0004, number of frame byte) `30H 33H 46H 46H 46H 31H 30H 44H 30H 41H` (Registration data, ETX/variable data/CR/LF encoded as 2-digit ASCII per byte)
(Number of registration data byte | Number of frame byte | Registration data)

■Data communication in binary code

Request data: `10H 10H 06H 00H 00H E8H 03H`
(Command (with additional 10H) | Subcommand | Frame No.)

Response data: `05H 00H 04H 00H 03H FFH F0H 0DH 0AH`
(Number of registration data byte | Number of frame byte | Registration data: ETX, variable data (FFH F0H), CR, LF)

> **Note:** In the PDF, the variable data is shown as `FFF1` (`46H 46H 46H 31H`) in the ASCII code response and as `FFH F0H` in the binary code response. The PDF's ASCII code request diagram shows `38H` below the character '3' of Frame No. 03E8; the ASCII code of '3' is `33H`, as written above.

#### Register data (command: 1610, subcommand: 0000)

Register user frames to C24.

> For MELSEC iQ-R series, the user frame is replaced with the contents registered using module extended parameters by powering OFF → ON or switching the CPU module STOP → RUN. For more information on user frame registration using module extended parameters, refer to the following manual.
> MELSEC iQ-R Serial Communication Module User's Manual (Application)
>
> When registering a user frame with the same frame No. again, deleted the user frame first and registered again. If it is attempted to register a user frame by specifying an already registered frame No., an error occurs and an abnormal response is returned.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1610) → Subcommand(0000) → Frame No. → Number of registration data byte → Number of frame byte → Registration data`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code[^13.1-3] |
|---|---|---|
| 1610 | `31H 36H 31H 30H` | `10H 16H` (sent as `10H 10H 16H` with additional code) |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| 0000 | `30H 30H 30H 30H` | `00H 00H` |

■Frame No.

Specify the number of user frame to be registered. (Page 246 Frame No.)

| Type | Setting value | Registration destination |
|---|---|---|
| User frame | 3E8H to 4AFH | Flash ROM of C24 |
| User frame | 8001H to 801FH | Buffer memory of C24 (addresses 1B00H to 1FF6H) |

■Number of registration data byte, number of frame byte

Specify the byte count of registration data within the range of 1 to 80. (Page 246 Number of registration data byte, number of frame byte)

■Registration data

Store the content of user frame data to be registered. (Page 247 Registration data)

**Communication example**

Register the following data in frame No. 3E8H.
Registration data: ETX + Variable data (sum check code) + CR + LF

■Data communication in ASCII code

Request data: `1610 0000 03E8 0005 0004 [Registration data: 03,FF,F1,0D,0A]` →
`31H 36H 31H 30H` (1610) `30H 30H 30H 30H` (0000) `30H 33H 45H 38H` (03E8) `30H 30H 30H 35H` (0005) `30H 30H 30H 34H` (0004) `30H 33H 46H 46H 46H 31H 30H 44H 30H 41H` (Registration data, ETX/variable data/CR/LF encoded as 2-digit ASCII per byte)
(Command | Subcommand | Frame No. | Number of registration data byte | Number of frame byte | Registration data)

■Data communication in binary code

Request data: `10H 10H 16H 00H 00H E8H 03H 05H 00H 04H 00H 03H FFH F1H 0DH 0AH`
(Command (with additional 10H) | Subcommand | Frame No. | Number of registration data byte | Number of frame byte | Registration data: ETX, variable data, CR, LF)

> **Note:** In the PDF, the variable data is shown as `FFF1` (`46H 46H 46H 31H`) in the ASCII code request and as `FFH F1H` in the binary code request (the binary code response of the Read registered data example on Page 249 shows `FFH F0H`). The PDF's ASCII code request diagram shows `38H` below the character '3' of Frame No. 03E8; the ASCII code of '3' is `33H`, as written above.

#### Delete registered data (command: 1610, subcommand: 0001)

Delete the registered user frame.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1610) → Subcommand(0001) → Frame No. → Number of registration data byte(0) → Number of frame byte(0)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code[^13.1-3] |
|---|---|---|
| 1610 | `31H 36H 31H 30H` | `10H 16H` (sent as `10H 10H 16H` with additional code) |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| 0001 | `30H 30H 30H 31H` | `01H 00H` |

■Frame No.

Specify the number of user frame to be deleted. (Page 246 Frame No.)

When the frame No. whose user frame is not registered is specified, an error occurs and an abnormal response is returned.

| Type | Setting value | Registration destination |
|---|---|---|
| User frame | 3E8H to 4AFH | Flash ROM of C24 |
| User frame | 8001H to 801FH | Buffer memory of C24 (addresses 1B00H to 1FF6H) |

■Number of registration data byte, number of frame byte

Specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| 0000 | `30H 30H 30H 30H` | `00H 00H` |

**Communication example**

Delete the registration data of frame No. 3E8H.

■Data communication in ASCII code

Request data: `1610 0001 03E8 0000 0000` →
`31H 36H 31H 30H` (1610) `30H 30H 30H 31H` (0001) `30H 33H 45H 38H` (03E8) `30H 30H 30H 30H` (0000) `30H 30H 30H 30H` (0000)
(Command | Subcommand | Frame No. | Number of registration data byte | Number of frame byte)

■Data communication in binary code

Request data: `10H 10H 16H 01H 00H E8H 03H 00H 00H 00H 00H`
(Command (with additional 10H) | Subcommand | Frame No. | Number of registration data byte | Number of frame byte)

### 13.2 Global Function

The global function is a function to turn ON/OFF the global signals (input signals: X1A/X1B) of serial communication module connected to external devices with the multidrop communications.

This is used as interlock signals for emergency command, simultaneous startup, and applicability of data transmission/reception to CPU modules.

> The command can only be used for C24 (including multidrop connection station) connected to the external device. It cannot be used via network.
>
> The global signal is turned OFF by powering OFF, resetting, or switching the mode of CPU module.

**Compatibility with global function of Computer link module**

This function (command: 1618) is compatible with GW command for dedicated protocol of Computer link module.

■Operation of serial communication module when GW command is received

When GW command is executed from Computer link module connected with the multidrop connection, the global signal (input signal: X1A/X1B) at the side where the command was received turns ON/OFF.

- Received from CH1: X1A
- Received from CH2: X1B

■Command: Operation of computer link module when 1618 is received

When this function (command: 1618) is used to Computer link module, the global signal (input signal: X2) of Computer link module turns ON/OFF.

#### Global signal ON/OFF (command: 1618)

Turn ON/OFF the global signal from an external device.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1618) → Subcommand → Global signal specification`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| 1618 | `31H 36H 31H 38H` | `18H 16H` |

■Subcommand

| Item | Setting value | ASCII code | Binary code |
|---|---|---|---|
| OFF | 0000 | `30H 30H 30H 30H` | `00H 00H` |
| ON | 0001 | `30H 30H 30H 31H` | `01H 00H` |

■Global signal specification

Specify which global signal (X1A or X1B) is turned ON/OFF.

| Global signal to be turned ON/OFF | Setting value | ASCII code | Binary code |
|---|---|---|---|
| Global signal at the side where command was received (• Received from CH1: X1A • Received from CH2: X1B) | 0000 | `30H 30H 30H 30H` | `00H 00H` |
| X1A regardless of the interface | 0001 | `30H 30H 30H 31H` | `01H 00H` |
| X1B regardless of the interface | 0002 | `30H 30H 30H 32H` | `02H 00H` |

The target station (all stations/only 1 specified station) of which global signal is turned ON/OFF is specified by a station No. (Page 50 Station No.)

**Communication example (turn ON)**

Turn the global signal X1A ON.

■Data communications in ASCII code (Format 1)

Request data: `1618 0001 0001` → Command `31H 36H 31H 38H` | Subcommand `30H 30H 30H 31H` | Global signal specification `30H 30H 30H 31H`

■Data communications in binary code (Format 5)

Request data: `18H 16H 01H 00H 01H 00H`
(Command | Subcommand | Global signal specification)

**Communication example (turn OFF)**

Turn OFF the global signal X1A.

■Data communications in ASCII code (Format 1)

Request data: `1618 0000 0001` → Command `31H 36H 31H 38H` | Subcommand `30H 30H 30H 30H` | Global signal specification `30H 30H 30H 31H`

■Data communications in binary code (Format 5)

Request data: `18H 16H 00H 00H 01H 00H`
(Command | Subcommand | Global signal specification)

### 13.3 Transmission sequence initialization function

The transmission sequence initialization function is a function to initialize the transmission sequence of data communication using format 5 of 4C frame, and to place C24 into the state where it waits to receive commands from external devices.

> The command can only be used for C24 (including multidrop connection station) connected to the external device. It cannot be used via network.

#### Initialize transmission sequence (command: 1615)

Initialize the transmission sequence of data communication using format 5 of 4C frame, and make C24 wait to receive commands from external devices.

> This function is equivalent to EOT, CL during data communication in ASCII code. Use the control codes, EOT, CL during data communication in ASCII code. (Page 34 EOT(04H), CL(0CH))

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1615) → Subcommand(0000)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| | Binary code |
|---|---|
| 1615 | `15H 16H` |

■Subcommand

| | Binary code |
|---|---|
| 0000 | `00H 00H` |

**Communication example**

Initialize transmission sequence.

Request data (binary code): `15H 16H 00H 00H`
(Command | Subcommand)

### 13.4 Mode Switching Function

The mode switching function is a function to switch the current communication protocol (operation mode) or transmission specification for the specified interface from external devices after C24 starts up. For more details on the mode switching function, refer to the following manuals.

- MELSEC-Q/L Serial Communication Module User's Manual (Application)
- MELSEC iQ-R Serial Communication Module User's Manual (Application)

> The command can only be used for C24 (including multidrop connection station) connected to the external device. It cannot be used via network.

#### Data to be specified in command

This section explains the contents and specification method for data item which is set in the command to switch mode.

##### Channel No.

Specify the target interface.

| Target interface | ASCII code | Binary code |
|---|---|---|
| CH1 | `30H 31H` (01) | `01H` |
| CH2 | `30H 32H` (02) | `02H` |

##### Switching instruction

Select which specify the contents to be switched, by data in the command or by Engineering tool.

| Bit | Item | Setting of Engineering tool | OFF (0) | ON (1) |
|---|---|---|---|---|
| b0 | Mode No. | Communication protocol setting | Specified by Engineering tool | Specified by command |
| b1 | Transmission setting | Transmission setting | Specified by Engineering tool | Specified by command |
| b2 | Communication speed | Communication speed setting | Specified by Engineering tool | Specified by command |
| b3 to b7 | (Fixed to '0') | | | |

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value from lower byte (L: bits 0 to 7).

**Ex.** When the switching instruction is '1' (Only mode No. is specified by command)

ASCII `30H 31H` (01); Binary `01H`

##### Mode No.

Specify the communication protocol setting after switching the C24 mode.

| Mode No. | Operation mode |
|---|---|
| 01H | MC Protocol (Format 1) |
| 02H | MC Protocol (Format 2) |
| 03H | MC Protocol (Format 3) |
| 04H | MC Protocol (Format 4) |
| 05H | MC Protocol (Format 5) |
| 06H | Nonprocedural protocol |
| 07H | Bidirectional protocol |
| 09H | Predefined protocol |
| 0AH | MODBUS slave (RTU)*1 |
| 0BH | MODBUS slave (ASCII)*1 |
| FFH | MELSOFT connection, GX Developer connection |

*1 Can be specified only for MELSEC iQ-R series serial communication modules the firmware versions of which are '13' or later.

- ■Data communication in ASCII code: Convert the numerical value to 2-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value from lower byte (L: bits 0 to 7).

**Ex.** When mode No. is '1' (MC protocol (Format 1)): ASCII `30H 31H` (01); Binary `01H`

##### Transmission setting

Specify the transmission setting after switching the C24 mode.

| Bit | Item | OFF (0) | ON (1) |
|---|---|---|---|
| b0 | Operation setting | Independent | Interlink |
| b1 | Data bit | 7 | 8 |
| b2 | Parity bit | None | Yes |
| b3 | Odd/even parity | Odd | Even |
| b4 | Stop bit | 1 | 2 |
| b5 | Sum check code | None | Yes |
| b6 | Online change | Disable | Enable |
| b7 | Setting change | Disable | Enable |

For the transmission setting, refer to the following manuals.

- MELSEC iQ-R Serial Communication Module User's Manual (Application)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value from lower byte (L: bits 0 to 7).

> **Note:** The PDF text says "4-digit" here, but the example below (E6: `45H 36H`, binary `E6H`) shows the 1-byte value as a 2-character (2-digit) ASCII code.

When the transmission settings are as follows:

| Bit | Item | Setting | ON/OFF |
|---|---|---|---|
| b0 | Operation setting | Independent | OFF |
| b1 | Data bit | 8 | ON |
| b2 | Parity bit | Yes | ON |
| b3 | Odd/even parity | Odd | OFF |
| b4 | Stop bit | 1 | OFF |
| b5 | Sum check code | Yes | ON |
| b6 | Online change | Enable | ON |
| b7 | Setting change | Enable | ON |

ASCII: `45H 36H` (E6); Binary: `E6H`

##### Communication speed

Specify the communication speed after switching the C24 mode.

| Specified value | Communication speed |
|---|---|
| 0FH | 50 bps |
| 00H | 300 bps |
| 01H | 600 bps |
| 02H | 1200 bps |
| 03H | 2400 bps |
| 04H | 4800 bps |
| 05H | 9600 bps |
| 06H | 14400 bps |
| 07H | 19200 bps |
| 08H | 28800 bps |
| 09H | 38400 bps |
| 0AH | 57600 bps |
| 0BH | 115200 bps |
| 0CH | 230400 bps |

The communication speed that can be set differs depending on the module or channel.

For the communication speed setting, refer to the following manuals.

- MELSEC iQ-R Serial Communication Module User's Manual (Application)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send a 1-byte numerical value from lower byte (L: bits 0 to 7).

> **Note:** The PDF text says "4-digit" here, but the example below (05: `30H 35H`, binary `05H`) shows the 1-byte value as a 2-character (2-digit) ASCII code.

**Ex.** For communication speed 05H (9600 bps): ASCII `30H 35H` (05); Binary `05H`

#### Switch mode (command: 1612)

Switch the C24 mode from external devices.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(1612) → Subcommand(0000) → Channel No. → Switching instruction → Mode No. → Transmission setting → Communication speed`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| 1612 | `31H 36H 31H 32H` | `12H 16H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| 0000 | `30H 30H 30H 30H` | `00H 00H` |

■Channel No.

Specify the target interface (CH1/CH2). (Page 258 Channel No.)

■Switching instruction

Select which specify the contents to be switched, by data in the command or by Engineering tool. (Page 258 Switching instruction)

○: Specified by command, —: Specified by Engineering tool

| Switching instruction | Mode No. | Transmission setting | Communication speed |
|---|---|---|---|
| 0 | — | — | — |
| 1 | ○ | — | — |
| 2 | — | ○ | — |
| 3 | ○ | ○ | — |
| 4 | — | — | ○ |
| 5 | ○ | — | ○ |
| 6 | — | ○ | ○ |
| 7 | ○ | ○ | ○ |

■Mode No.

Specify the operation mode within the range of 1 to BH or FFH. (Page 259 Mode No.)

When the setting by command enabled (Switching instruction: 1, 3, 5, or 7), the operation mode is changed according to the specified value.

As for the setting by command is disabled (Switching instruction: 0, 2, 4, or 6), the operation mode is changed according to the Communication protocol setting set with Engineering tool

> Even when the setting by command is disabled, specify the value (1 to BH or FFH). (Do not specify '0'.)

■Transmission setting

Specify the transmission setting. (Page 260 Transmission setting)

When the setting by command is enabled (Switching instruction: 2, 3, 6, or 7), the transmission setting is changed according to the specified value.

As for the setting by command is disabled (Switching instruction: 0, 1, 4, or 5), the transmission setting is changed according to the transmission setting set with Engineering tool

When the setting is invalid, specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| 00 | `30H 30H` | `00H` |

■Communication speed

Specify the communication speed. (Page 261 Communication speed)

When the setting by command is enabled (Switching instruction: 4, 5, 6, or 7), the communication speed is changed according to the specified value.

When the setting by command is disabled (Switching instruction: 0, 1, 2, or 3), the communication speed is changed according to the communication speed setting set with Engineering tool.

When the setting is invalid, specify '0'.

| | ASCII code | Binary code |
|---|---|---|
| 00 | `30H 30H` | `00H` |

**Communication example**

Perform mode switching for CH1 interface.

- Operation mode: MC protocol (Format 1) (Specified value: 01H)
- Operation setting: Following settings (Specified value: B0H)
- Communication speed: 9600 bps (Specified value: 05H)

| Item | Setting | Bit | ON/OFF |
|---|---|---|---|
| Operation setting | Independent | b0 | OFF |
| Data bit | 7 | b1 | OFF |
| Parity bit | None | b2 | OFF |
| Odd/even parity | Odd | b3 | OFF |
| Stop bit | 2 | b4 | ON |
| Sum check code | Yes | b5 | ON |
| Online change | Disable | b6 | OFF |
| Setting change | Enable | b7 | ON |

■Data communication in ASCII code

Request data: `1612 0000 01 07 01 B0 05`
`31H 36H 31H 32H` (1612) `30H 30H 30H 30H` (0000) `30H 31H` (Channel No. 01=CH1) `30H 37H` (Switching instruction 07) `30H 30H` (Mode No. 01) `42H 30H` (Transmission setting B0) `30H 35H` (Communication speed 05)
(Command | Subcommand | Channel No. | Switching instruction | Mode No. | Transmission setting | Communication speed)

> **Note:** The ASCII-code diagram in the PDF prints `30H 30H` under the Mode No. characters "0 1". This is an evident typo in the PDF: the character "1" is `31H`, so Mode No. 01 is `30H 31H` (as in the Mode No. example on Page 259), and the binary-code example below uses `01H`. The request data with the correct Mode No. bytes is `31H 36H 31H 32H 30H 30H 30H 30H 30H 31H 30H 37H 30H 31H 42H 30H 30H 35H`.

■Data communication in binary code

Request data: `12H 16H 00H 00H 01H 07H 01H B0H 05H`
(Command | Subcommand | Channel No. | Switching instruction | Mode No. | Transmission setting | Communication speed)

---

### 13.5 Programmable controller CPU monitoring function

Programmable controller CPU monitoring function is a function that C24 monitors CPU module with the monitoring information which was registered in advance.

For the Programmable controller CPU monitoring function, refer to the following manuals.

- MELSEC iQ-R Serial Communication Module User's Manual (Application)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

> The command can only be used for C24 (including multidrop connection station) connected to the external device. It cannot be used via network.

**Data to be specified in command**

#### Cycle time units

Specify the unit of cycle time.

| Time unit | ASCII code | Binary code |
|---|---|---|
| 100 ms | `30H 30H` (00) | `00H` |
| 1 second | `30H 31H` (01) | `01H` |
| 1 minute | `30H 32H` (02) | `02H` |

#### Cycle time

Specify the time interval (period for 1 cycle) that C24 reads the monitoring information from CPU module.

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

#### Programmable controller CPU monitoring function

Specify the send timing (fixed cycle send/condition match send) of the monitoring result.

| Programmable controller CPU monitoring function | ASCII code | Binary code |
|---|---|---|
| Fixed cycle send (Sending information in the cycle time interval.) | `30H 31H` (01) | `01H` |
| Condition match send (Sending information when the specified condition matches.) | `30H 32H` (02) | `02H` |

#### CPU error monitoring, CPU status information

Specify whether to perform error monitoring for the host station CPU module.

| CPU error monitoring | ASCII code | Binary code |
|---|---|---|
| Do not perform error monitoring for the host station CPU module. | `30H 30H` (00) | `00H` |
| Perform error monitoring for the host station CPU module. | `30H 31H` (01) | `01H` |

When the error monitoring is performed, the CPU monitoring result is stored in the response data as a CPU status information.

The following values are stored in the CPU status information.

| Specified value | CPU status |
|---|---|
| 0000H | During normal operation |
| 0001H | Module warning occurring |
| 0002H | Module error/module system error occurring |

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal) and store it in the 'Device Data' in the following format from upper digits.
  Format: `30H 31H` (01) | `30H 30H 30H 30H 30H 30H` (000000) | `30H 30H 30H 31H` (0001) | Device data
- ■Data communication in binary code: Store the 2-byte numerical values in the 'Device Data' in the following format from lower byte (L: bits 0 to 7).
  Format: `01H` | `00H 00H 00H` | `01H 00H` | Device data

#### Monitoring condition

When 'fixed cycle send' is specified with the programmable controller CPU monitoring function, specify '0' (ASCII `30H 30H 30H 30H`, Binary `00H 00H`).

When 'condition match send' is specified with the Programmable controller CPU monitoring function, specify the value below. The timing to send the result can be selected.

- Edge trigger transmission: Send the result when the condition matches.
- Level trigger transmission: Send the result in the cycle time interval during the condition matches.

| Edge trigger transmission | Level trigger transmission | Monitoring condition | Device type that can be specified |
|---|---|---|---|
| 0001H | 0101H | ON/OFF status of device = ON/OFF status of monitoring condition | Bit device |
| 0001H | 0101H | Device value = Monitoring condition value | Word device |
| 0002H | 0102H | ON/OFF status of device ≠ ON/OFF status of monitoring condition | Bit device |
| 0002H | 0102H | Device value ≠ Monitoring condition value | Word device |
| 0003H | 0103H | Device value ≤ Monitoring condition value | Unsigned Word device |
| 0004H | 0104H | Device value < Monitoring condition value | Unsigned Word device |
| 0005H | 0105H | Device value ≥ Monitoring condition value | Unsigned Word device |
| 0006H | 0106H | Device value > Monitoring condition value | Unsigned Word device |
| 0007H | 0107H | Device value ≤ Monitoring condition value | Signed Word device |
| 0008H | 0108H | Device value < Monitoring condition value | Signed Word device |
| 0009H | 0109H | Device value ≥ Monitoring condition value | Signed Word device |
| 000AH | 010AH | Device value > Monitoring condition value | Signed Word device |

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

#### Monitoring condition value

When 'fixed cycle send' is specified with the Programmable controller CPU monitoring function, specify '0' (ASCII `30H 30H 30H 30H`, Binary `00H 00H`).

When 'condition match send' is specified with the Programmable controller CPU monitoring function, specify the value below.

| Monitoring condition value | Monitoring condition value | Device type |
|---|---|---|
| 0000H | OFF | Bit device |
| 0001H | ON | Bit device |
| 0000H to FFFFH | Numerical value | Word device |

- ■Data communication in ASCII code: Convert the numerical value to 4-digit ASCII code (hexadecimal), and send it from the upper digits.
- ■Data communication in binary code: Send 2-byte numerical values from lower byte (L: bits 0 to 7).

#### Register (command: 0630)

Register the target devices to be monitored with the Programmable controller CPU monitoring function and its monitoring conditions in CPU module.

Specify the monitoring target device for multiple blocks with the consecutive word devices and bit devices treated as one block. The host station CPU module can also be specified as error monitoring target.

The monitoring is started when executing the registration command.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data:
  `Command(0630H) → Subcommand(0000H) → Cycle time unit → Cycle time → Programmable controller CPU monitoring function → Transmission method(00) → Number of registered word blocks (m points) → Number of registered bit blocks (n points) → CPU error monitoring → [Word device registration: Block (first point) → ... → Block (mth point)] → [Bit device registration: Block (first point) → ... → Block (nth point)] → CPU error monitoring registration (fixed value, present only when CPU error monitoring = 1)`
  Each word/bit device registration block: `Monitoring start device → Number of registered points → Monitoring condition → Monitoring condition value`
- ■Response data:
  `Fixed value(2102H) → Programmable controller CPU monitoring function → Number of registered word blocks (n points) → Number of registered bit blocks (n points) → CPU error monitoring → [Word device information: Block (first point) → ... → Block (mth point)] → [Bit device information: Block (first point) → ... → Block (nth point)] → CPU status information (present only when CPU error monitoring = 1)`
  Each word/bit device information block: `Monitoring start device → Number of registered points → Device data`
  CPU status information block: `Fixed value → Number of registered points → Device data (CPU status value)`

The device information of the monitoring result and programmable controller CPU status information are stored.

The response data of this command will be sent as same as that of the on-demand function. For the transmission methods and its timing of monitoring result, refer to the following manuals.

- MELSEC iQ-R Serial Communication Module User's Manual (Application)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| Register | 0630 | `30H 06H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| Fixed | 0000 | `00H 00H` |

■Cycle time units, cycle time

Specify the following time interval (period for 1 cycle). (Page 265 Cycle time)

- Time interval that C24 reads monitoring information from CPU module
- Transmission interval of monitoring result when 'fixed cycle send' is specified with the Programmable controller CPU monitoring function

The time unit (100 ms/1 second/1 minute) can be selected. (Page 265 Cycle time units)

■Programmable controller CPU monitoring function

Specify the send timing (fixed cycle send/condition match send) of the monitoring result. (Page 265 Programmable controller CPU monitoring function)

■Transmission method

Fixed '0'.

| | ASCII code | Binary code |
|---|---|---|
| Fixed | 00 | `00H` |

■Number of registered word blocks, number of registered bit blocks

Specify the number of blocks of word device registration and bit device registration. (Page 71 Number of blocks)

■CPU error monitoring, CPU error monitoring registration

Specify whether to perform error monitoring (status monitoring) of host station CPU module together. (Page 266 CPU error monitoring, CPU status information)

- When do not perform CPU error monitoring (0): CPU error monitoring registration is not required.
- When perform CPU error monitoring (1): Register the following fixed values to CPU error monitoring registration.

| Field | ASCII code | Binary code |
|---|---|---|
| Monitoring start device (fixed) | 01000000 | `00H 00H 00H 01H` |
| Number of registered points (fixed) | 0001 | `01H 00H` |
| Monitoring condition (fixed) | 0005 | `05H 00H` |
| Monitoring condition value (fixed) | 0001 | `01H 00H` |

■Word device registration, bit device registration

Specify the monitoring target device for multiple blocks with the consecutive word devices and bit devices treated as one block.

Specify the following items for each block: `Monitoring start device → Number of registered points → Monitoring condition → Monitoring condition value` (Block (1 point))

- Monitoring start device: Specify the device code and device number. (Page 65 Devices)
- Number of registered points: Specify the device points from the start device in word units. (Page 70 Number of device points)
- Monitoring condition: Specify the monitoring condition and transmission method for the device to be registered. (Page 266 Monitoring condition)
- Monitoring condition value: Specify a value to judge the conditions have been matched or ON/OFF state of the bit. (Page 267 Monitoring condition value)

**Data to be stored by response data**

■Fixed value

| | ASCII code | Binary code |
|---|---|---|
| Fixed value | 2102 | `02H 21H` |

■Programmable controller CPU monitoring function

The send timing (fixed cycle send/condition match send) of the monitoring result is stored. (Page 265 Programmable controller CPU monitoring function)

■Number of registered word blocks, number of registered bit blocks

The number of blocks of word device information and bit device information are stored. (Page 71 Number of blocks)

■Word device information, bit device information

The information of device, whose monitoring condition is satisfied, is stored for multiple blocks with the consecutive word devices and bit devices treated as one block.

The following items are stored for each block: `Monitoring start device → Number of registered points → Device data` (Block (1 point))

- Monitoring start device: Device code and device number are stored. (Page 65 Devices)
- Number of registered points: Number of device data points is stored in word unit. (Page 70 Number of device points)
- Device data: The device value is stored.

The order of device data differ depending on the setting of "Word/byte units designation" in Engineering tool.

For byte units, handle the word data as 2-byte data and send the numerical values from the lower byte (L: bits 0 to 7).

**Ex.** 2 points data of word device (1234H, 5678H)

- Word unit (0):

  | | ASCII code | Binary code |
  |---|---|---|
  | Word 1 (1234H) | 1234 | `12H 34H` |
  | Word 2 (5678H) | 5678 | `56H 78H` |

- Byte unit (1):

  | | ASCII code | Binary code |
  |---|---|---|
  | Word 1 (1234H) | 3412 | `34H 12H` |
  | Word 2 (5678H) | 7856 | `78H 56H` |

■CPU error monitoring, CPU status information

Specify whether to include the result of error monitoring (state monitoring) of the host station CPU module. (Page 266 CPU error monitoring, CPU status information)

- When do not perform CPU error monitoring (0): CPU status information is not stored.
- When perform CPU error monitoring (1): CPU status information is stored.

**Communication example (for fixed cycle send)**

Perform the Programmable controller CPU monitoring registration with the following conditions.

- Cycle time units, cycle time: 30 seconds
- Programmable controller CPU monitoring function: Fixed cycle send
- Number of registered word blocks: 2 blocks
- Number of registered bit blocks: 1 block
- CPU error monitoring: Included

| Block | Monitoring start device | Registered points |
|---|---|---|
| Word device registration (1-point) | D0 | 4 points |
| Word device registration (2-point) | W100 | 8 points |
| Bit device registration (1-point) | M0 | 2 points |

Data communication in ASCII code and in binary code (Request data):

| Field | ASCII | Binary |
|---|---|---|
| Command | 0630 | `30H 06H` |
| Subcommand | 0000 | `00H 00H` |
| Cycle time unit (1 second) | 01 | `01H` |
| Cycle time (30) | 001E | `1EH 00H` |
| Programmable controller CPU monitoring function (fixed cycle send) | 01 | `01H` |
| Transmission method | 00 | `00H` |
| Number of registered word blocks | 02 | `02H` |
| Number of registered bit blocks | 01 | `01H` |
| CPU error monitoring (included) | 01 | `01H` |
| D0 block: Monitoring start device | D*000000 | `00H 00H 00H A8H` |
| D0 block: Number of registered points | 0004 | `04H 00H` |
| D0 block: Monitoring condition / value | 0000 / 0000 | `00H 00H` / `00H 00H` |
| W100 block: Monitoring start device | W*000100 | `00H 10H 00H B4H` |
| W100 block: Number of registered points | 0008 | `08H 00H` |
| W100 block: Monitoring condition / value | 0000 / 0000 | `00H 00H` / `00H 00H` |
| M0 block: Monitoring start device | M*000000 | `00H 00H 00H 90H` |
| M0 block: Number of registered points | 0002 | `02H 00H` |
| M0 block: Monitoring condition / value | 0000 / 0000 | `00H 00H` / `00H 00H` |
| CPU error monitoring registration (fixed) | 01000000 / 0001 / 0005 / 0001 | `00H 00H 00H 01H` / `01H 00H` / `05H 00H` / `01H 00H` |

> **Note:** The PDF prints the W100 start device in the binary code examples as `00H 10H 00H B4H`, while its ASCII code examples show `W*000100` (000100H would be `00H 01H 00H` in binary code).

The response data indicates the following data:

(1) Word device information (1-point)

| Device data | D0 | D1 | D2 | D3 |
|---|---|---|---|---|
| Device value (Decimal) | 99 | 4144 | 5445 | 10240 |
| Device value (Hexadecimal) | 0063H | 1030H | 1545H | 2800H |
| ASCII code, Word unit | 30303633 | 31303330 | 31353435 | 32383030 |
| ASCII code, Byte unit | 36333030 | 33303130 | 34353135 | 30303238 |
| Binary code, Word unit | 0063 | 101030[^add10] | 1545 | 2800 |
| Binary code, Byte unit | 6300 | 301010[^add10] | 4515 | 0028 |

[^add10]: The additional code is added. (Page 35 Additional code (10H))

(2) Bit device information (1-point)

| Device data | M15 to M8 | M7 to M0 | M31 to M24 | M23 to M16 |
|---|---|---|---|---|
| Device value (Bit) | 0 0 0 1 0 0 0 1 | 0 0 1 1 0 0 0 1 | 0 1 0 0 1 0 0 0 | 0 1 0 0 1 0 0 1 |
| Device value (Byte) | 11H | 31H | 48H | 49H |
| Device value (Word) | 1131H | | 4849H | |
| ASCII code, Word unit | 31313331 | | 34383439 | |
| ASCII code, Byte unit | 33313131 | | 34393438 | |
| Binary code, Word unit | 1131 | | 4849 | |
| Binary code, Byte unit | 3111 | | 4948 | |

(3) CPU status information

- CPU status information: Module warning occurring (0001)

Data communication in ASCII code and in binary code (Response data):

| Field | ASCII | Binary |
|---|---|---|
| Fixed value | 2102 | `02H 21H` |
| Programmable controller CPU monitoring function | 01 | `01H` |
| Number of registered word blocks | 02 | `02H` |
| Number of registered bit blocks | 01 | `01H` |
| CPU error monitoring | 01 | `01H` |
| Word device information (first point): Monitoring start device (D0), Number of registered points | D*000000 / 0004 | `00H 00H 00H A8H` / `04H 00H` |
| Word device information (first point): Device data | (1) | (1) |
| Word device information (second point) | ... | ... |
| Bit device information (first point): Monitoring start device (M0), Number of registered points | M*000000 / 0002 | `00H 00H 00H 90H` / `02H 00H` |
| Bit device information (first point): Device data | (2) | (2) |
| CPU status information: Fixed value / Number of registered points / Device data | 01000000 / 0001 / 0001 | `00H 00H 00H 01H` / `01H 00H` / `01H 00H 00H 00H` |

> **Note:** In the binary code example the PDF shows the device data of the CPU status information as four bytes (`01H 00H 00H 00H`), while the ASCII code example shows four characters (`0001`).

- Data communication in ASCII code: In the figure (1) and (2), set the value of "ASCII Code" in the table of "Device data" of each response data.
- Data communication in binary code: In the figure (1) and (2), set the value of "Binary code" in the tables of "Device data" of each response data.

The order of device data differ depending on the setting of "Word/byte units designation" in Engineering tool.

**Communication example (for condition match send)**

Perform the Programmable controller CPU monitoring registration function with the following conditions.

- Cycle time units, cycle time: 30 seconds
- Programmable controller CPU monitoring function: Condition match send
- Number of registered word blocks: 2 blocks
- Number of registered bit blocks: 1 block
- CPU error monitoring designation: Included

| Block | Monitoring start device | Number of registered points | Monitoring condition | Monitoring condition value |
|---|---|---|---|---|
| Word device registration (1-point) | D0 | 4 points | Edge triggered transmission: Device value = Monitoring condition value | 99 |
| Word device registration (2-point) | W100 | 8 points | Edge triggered transmission: Device value ≠ Monitoring condition value | 0 |
| Bit device registration (1-point) | M0 | 2 points | Edge trigger transmission: ON/OFF status of device ≠ ON/OFF status of monitoring condition | OFF |

The response data indicates the following data.

For the condition match send, the registration data information will be sent individually. (Device information will be sent per block.)

Data communication in ASCII code and in binary code (Request data):

| Field | ASCII | Binary |
|---|---|---|
| Command | 0630 | `30H 06H` |
| Subcommand | 0000 | `00H 00H` |
| Cycle time unit (1 second) | 01 | `01H` |
| Cycle time (30) | 001E | `1EH 00H` |
| Programmable controller CPU monitoring function (condition match send) | 02 | `02H` |
| Transmission method | 00 | `00H` |
| Number of registered word blocks | 02 | `02H` |
| Number of registered bit blocks | 01 | `01H` |
| CPU error monitoring (included) | 01 | `01H` |
| D0 block: Monitoring start device | D*000000 | `00H 00H 00H A8H` |
| D0 block: Number of registered points | 0004 | `04H 00H` |
| D0 block: Monitoring condition / value (D0=99) | 0001 / 0063 | `01H 00H` / `63H 00H` |
| W100 block: Monitoring start device | W*000100 | `00H 10H 00H B4H` |
| W100 block: Number of registered points | 0008 | `08H 00H` |
| W100 block: Monitoring condition / value (W100≠0) | 0002 / 0000 | `02H 00H` / `00H 00H` |
| M0 block: Monitoring start device | M*000000 | `00H 00H 00H 90H` |
| M0 block: Number of registered points | 0002 | `02H 00H` |
| M0 block: Monitoring condition / value (M0≠OFF) | 0002 / 0000 | `02H 00H` / `00H 00H` |
| CPU error monitoring registration (fixed) | 01000000 / 0001 / 0005 / 0001 | `00H 00H 00H 01H` / `01H 00H` / `05H 00H` / `01H 00H` |

> **Note:** As in the fixed cycle send example, the PDF prints the W100 start device in the binary code example as `00H 10H 00H B4H` (ASCII code example: `W*000100`).

(1) When the condition of D0 = 99 (word device registration (first point) of request data) is satisfied.

| Device data | D0 | D1 | D2 | D3 |
|---|---|---|---|---|
| Device value (Decimal) | 99 | 4144 | 5445 | 10240 |
| Device value (Hexadecimal) | 0063H | 1030H | 1545H | 2800H |
| ASCII code, Word unit | 30303633 | 31303330 | 31353435 | 32383030 |
| ASCII code, Byte unit | 36333030 | 33303130 | 34353135 | 30303238 |
| Binary code, Word unit | 0063 | 101030[^add10b] | 1545 | 2800 |
| Binary code, Byte unit | 6300 | 301010[^add10b] | 4515 | 0028 |

[^add10b]: The additional code is added. (Page 35 Additional code (10H))

Data communication in ASCII code and in binary code (Response data), when the condition (D0 = 99) of word device registration (first point) is established:

| Field | ASCII | Binary |
|---|---|---|
| Fixed value | 2102 | `02H 21H` |
| Programmable controller CPU monitoring function | 02 | `02H` |
| Number of registered word blocks | 01 | `01H` |
| Number of registered bit blocks | 00 | `00H` |
| CPU error monitoring | 00 | `00H` |
| Word device information: Monitoring start device (D0), Number of registered points | D*000000 / 0004 | `00H 00H 00H A8H` / `04H 00H` |
| Word device information: Device data | (1) | (1) |

(2) When the condition of M0 ≠ OFF (bit device registration (1 point) of request data) is satisfied.

| Device data | M15 to M8 | M7 to M0 | M31 to M24 | M23 to M16 |
|---|---|---|---|---|
| Device value (Bit) | 0 0 0 1 0 0 0 1 | 0 0 1 1 0 0 0 1 | 0 1 0 0 1 0 0 0 | 0 1 0 0 1 0 0 1 |
| Device value (Byte) | 11H | 31H | 48H | 49H |
| Device value (Word) | 1131H | | 4849H | |
| ASCII code, Word unit | 31313331 | | 34383439 | |
| ASCII code, Byte unit | 33313131 | | 34393438 | |
| Binary code, Word unit | 1131 | | 4849 | |
| Binary code, Byte unit | 3111 | | 4948 | |

Data communication in ASCII code and in binary code (Response data), when the condition (M0 ≠ OFF) of bit device registration (first point) is established:

| Field | ASCII | Binary |
|---|---|---|
| Fixed value | 2102 | `02H 21H` |
| Programmable controller CPU monitoring function | 02 | `02H` |
| Number of registered word blocks | 00 | `00H` |
| Number of registered bit blocks | 01 | `01H` |
| CPU error monitoring | 00 | `00H` |
| Bit device information: Monitoring start device (M0), Number of registered points | M*000000 / 0002 | `00H 00H 00H 90H` / `02H 00H` |
| Bit device information: Device data | (2) | (2) |

(3) When CPU error monitoring condition is satisfied

- CPU status information: Module warning occurring (0001)

When the error is detected at the first time, the monitoring result is sent with the equivalent method to the edge trigger transmission.

Data communication in ASCII code and in binary code (Response data), when the condition of CPU error monitoring is established (Module warning being generated):

| Field | ASCII | Binary |
|---|---|---|
| Fixed value | 2102 | `02H 21H` |
| Programmable controller CPU monitoring function | 02 | `02H` |
| Number of registered word blocks | 00 | `00H` |
| Number of registered bit blocks | 00 | `00H` |
| CPU error monitoring | 01 | `01H` |
| CPU status information: Fixed value / Number of registered points / Device data | 01000000 / 0001 / 0001 | `00H 00H 00H 01H` / `01H 00H` / `01H 00H 00H 00H` |

> **Note:** As in the fixed cycle send example, the PDF shows the device data of the CPU status information in the binary code example as four bytes (`01H 00H 00H 00H`), while the ASCII code example shows four characters (`0001`).

- Data communication in ASCII code: In the figure (1) and (2), set the value of "ASCII Code" in the table of "Device data" of each response data.
- Data communication in binary code: In the figure (1) and (2), set the value of "Binary code" in the tables of "Device data" of each response data.

The order of device data differ depending on the setting of "Word/byte units designation" in Engineering tool.

#### Deregister (command: 0631)

Deregister the Programmable controller CPU monitoring function. Execute the cancel command to end monitoring.

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: `Command(0631H) → Subcommand(0000H)`
- ■Response data: There is no response data for this command.

**Data specified by request data**

■Command

| | ASCII code | Binary code |
|---|---|---|
| Deregister | 0631 | `31H 06H` |

■Subcommand

| | ASCII code | Binary code |
|---|---|---|
| Fixed | 0000 | `00H 00H` |

**Communication example**

Deregister the Programmable controller CPU monitoring function.

- ■Data communication in ASCII code (Request data): `0631 0000` → `30H 36H 30H 31H 30H 30H 30H 30H`
- ■Data communication in binary code (Request data): `31H 06H 00H 00H`

> **Note:** The PDF prints the third byte of this ASCII code example as `30H`, although the third character is "3"; the ASCII code of `0631 0000` is `30H 36H 33H 31H 30H 30H 30H 30H` (see Command above).

---

### 13.6 On-demand function

On-demand function is a function that transmits data to external device using MC protocol after starting up the function from CPU module.

This function can be used to send the emergency data that is required to notify to external devices from the CPU module.

> The command can only be used for C24 connected to the external device on 1:1 basis. It cannot be used via network.
>
> If the on-demand function is used with multidrop connection of 1:n station or m:n station, the on-demand data will be collapsed and proper data is not sent.

#### Settings for using the on-demand function

The following shows the setting items for the on-demand function.

**Settings of serial communication module**

Set the following values in the parameter of serial communication module or the buffer memory.

| Setting item | Description | Corresponding buffer memory CH1 | Corresponding buffer memory CH2 |
|---|---|---|---|
| Communication protocol setting | Set the format of the message format to be used. • MC protocol (Format 1) to MC protocol (Format 4): Data is sent by 1C frame • MC protocol (Format 5): Data is sent by 4C frame. | — | — |
| Word/byte units designation | Set the unit of data length (number of data). | 150 (96H) | 310 (136H) |
| Buffer memory start address designation | Set the start address of the buffer memory to be used for the on-demand function. | 160 (A0H) | 320 (140H) |
| Data length designation | Set the data length of the area to which the transmitted data is stored by the on-demand function. | 161 (A1H) | 321 (141H) |

The execution result is stored in the following:

| Execution result | Description | Corresponding buffer memory CH1 | Corresponding buffer memory CH2 |
|---|---|---|---|
| On-demand execution results | • Normal completion: 0 • Abnormal completion (error code): Other than 0 | 598 (256H) | 614 (266H) |

The setting content can be checked and changed by using any of the following Engineering tools.

- GX Works3 (Module parameter)
- GX Works2, GX Configurator-SC

For details on the setting items and buffer memory, refer to the following manuals.

- MELSEC iQ-R Serial Communication Module User's Manual (Application)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

**Specification of interface to be sent and send data**

Specify the transmission channel (CH1/CH2) and data equivalent to the response data with the arguments of the following dedicated instruction. Set the arguments in the program.

- G(P).ONDEMAND

For the dedicated instruction "G(P).ONDEMAND", refer to the following manuals.

- MELSEC iQ-R Programming Manual (Module Dedicated Instructions)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

#### Execution procedure

The procedures for the on-demand function are as shown below.

**Procedure for CPU module and C24**

The following shows the procedure to send data using the on-demand function from CPU module.

1. Set the items for the on-demand function. (Page 279 Settings for using the on-demand function)
2. Execute the serial communication module dedicated instruction 'G(P).ONDEMAND'.
   - MELSEC iQ-R Programming Manual (Module Dedicated Instructions)
   - MELSEC-Q/L Serial Communication Module User's Manual (Application)

**Procedure for external device**

The following shows the procedure to receive the data (on-demand data) transmitted using the on-demand function from CPU module.

1. Receive the message.
2. Judge whether the received message is issued from the on-demand function by "PC No.". For the message of on-demand function, 'PC No.' will be 'FE'. (Page 52 Network No., PC No.)
3. Process the received on-demand data.

#### Execution timing

The following shows the execution timing of send/receive processing while C24 is sending/receiving other messages of MC protocol, when a data transmission instruction by the on-demand function is executed in the CPU module.

> When sending on-demand data or response data, the timeout check is performed by the send monitoring timer (timer 2). If a timeout error occurs, change the send monitoring time (timer 2).

**When C24 is receiving request message of other command from external device**

C24 sends on-demand data before the response message to the external device is sent.

- Full-duplex communication: C24 sends on-demand data during the request message from the external device is being received.
  (Timing diagram: while the C24 receives "Message of the other command (Request message)" from the external device, the C24 transmits "On-demand data (Response message)" and then "Message of the other command (Response message)" to the external device; "G(P).ONDEMAND" is executed in the CPU module.)
- Half-duplex communication: C24 sends on-demand data after the request message from the external device has been received.
  (Timing diagram: after the C24 has received "Message of the other command (Request message)" from the external device, the C24 transmits "On-demand data (Response message)" and then "Message of the other command (Response message)" to the external device; "G(P).ONDEMAND" is executed in the CPU module.)

**When C24 is sending response message of other command to external device**

C24 sends the on-demand data to the external device after the response message to the external device.
(Timing diagram: after "Message of the other command (Request message)" has been received from the external device, the C24 transmits "Message of the other command (Response message)" and then "On-demand data (Response message)" to the external device; "G(P).ONDEMAND" is executed in the CPU module.)

#### On-demand (command: 2101)

Send data using the on-demand function from CPU module. Any message other than send data is added automatically in the message format selected in the communication protocol setting. ('FE' is specified to the 'PC No.' for the access route.)

- Communication protocol setting is MC protocol (Format 1) to MC protocol (Format 4): Message format of 1C frame
- Communication protocol setting is MC protocol (Format 5): Message format of 4C frame

> When using 2C/3C/4C frame (Format 1 to 4), register the message format of each format in the user frame. (Page 29 Message Formats of Each Protocol)

For the user frames, refer to the following manuals.

- MELSEC iQ-R Serial Communication Module User's Manual (Application)
- MELSEC-Q/L Serial Communication Module User's Manual (Application)

**Message format**

The following shows the message format of the request data and response data of the command.

- ■Request data: There is no request data for this command.
- ■Response data: `Command(2101H, present only for MC protocol Format 5) → Transmission data`

**Data stored in response data**

■Command

It is added only when the message is sent with MC protocol format 5 specified in the communication protocol setting.

| | Binary code |
|---|---|
| On-demand | `01H 21H` |

■Transmission data

The transmission data specified in the dedicated instruction 'G(P).ONDEMAND' is stored.

The order of data differs depending on the setting of "Word/byte units designation" in Engineering tool. For byte units, handle the word data as 2-byte data and send the numerical values from the lower byte (L: bits 0 to 7).

**Ex.** When transmitting 2-word data (1234H, 5678H):

- Word unit (0):

  | | ASCII code | Binary code |
  |---|---|---|
  | Word 1 (1234H) | 1234 | `12H 34H` |
  | Word 2 (5678H) | 5678 | `56H 78H` |

- Byte unit (1):

  | | ASCII code | Binary code |
  |---|---|---|
  | Word 1 (1234H) | 3412 | `34H 12H` |
  | Word 2 (5678H) | 7856 | `78H 56H` |

**Communication example (communication protocol setting is format 1 to 4)**

Send 2-word data (1234H, 5678H) to the external device from the CPU module under the following settings.

- Communication protocol setting: MC protocol (Format 1 to 4)
- Word/byte units designation: Word unit

Response data (Transmission data only — no Command field in Format 1 to 4): ASCII `12345678` → `31H 32H 33H 34H 35H 36H 37H 48H`

> **Note:** The PDF prints the last byte of this example as `48H`, although the last character is "8"; the ASCII code of `12345678` is `31H 32H 33H 34H 35H 36H 37H 38H` (see the Word unit example above).

**Communication example (communication protocol is format 5)**

Send 2-word data (1234H, 5678H) to the external device from the CPU module under the following settings.

- Communication protocol setting: MC Protocol (Format 5)
- Word/byte units designation: Word unit

Response data: `Command(2101H) → Transmission data(1234H, 5678H)` → Binary `01H 21H 12H 34H 56H 78H`
