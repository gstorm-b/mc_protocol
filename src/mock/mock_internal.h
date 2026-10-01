// Private to mc_mock (mc::detail::mock): the types and functions shared by the server-direction
// sources of MockPlc. The independence rule (MCK-HYG) applies to every file under src/mock.
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/types.h"
#include "mc/mock/mock_plc.h"
#include "mock/memory_image.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mc::detail::mock {

/// The access route as it travels in a 3E header: network, PC, request destination module I/O,
/// request destination module station.
struct Route {
    uint8_t network{0};
    uint8_t pc{0};
    uint16_t io{0};
    uint8_t station{0};
};

/// What a decoded batch request asks of the memory image, whatever the frame family. `executable`
/// is false when the frame was well-framed but is something the mock does not execute (another
/// command, an unknown subcommand or device code, request data that does not fit the command's
/// layout); the other fields then keep their defaults.
struct Access {
    bool executable{false};
    Op op{Op::ReadWords};
    Device head{};
    uint16_t count{0};
    ByteBuf data; ///< Write payload: one byte per point for bits, little-endian words otherwise.
};

/// One framed QnA request, decoded as far as the mock can. `command` and `subcommand` hold
/// whatever was readable even when the request is not executable.
struct QnaRequest : Access {
    Route route;
    uint16_t command{0};
    uint16_t subcommand{0};
    PlcSeries series{PlcSeries::QL};
};

/// Spec §5.3: the one 1E end code that is followed by an abnormal code.
inline constexpr uint8_t kE1EndCodeWithAbnormal = 0x5B;

/// One framed 1E request (spec §5.3, §4.2). `command` is the subheader (00H-05H); `pc` is the PC
/// number of the request header.
struct E1Request : Access {
    uint8_t pc{0};
    uint8_t command{0};
};

enum class FrameStatus {
    NeedMore, ///< The buffer holds only part of a frame.
    Junk,     ///< The first byte cannot start a frame; `consumed` is 1.
    Complete, ///< A whole frame; `consumed` bytes belong to it.
    Eot       ///< Serial only: an EOT ended the partial request; `consumed` includes it.
};

struct DecodeResult {
    FrameStatus status{FrameStatus::NeedMore};
    size_t consumed{0};
    QnaRequest request; ///< Meaningful for Complete only.
};

struct DecodeResult1e {
    FrameStatus status{FrameStatus::NeedMore};
    size_t consumed{0};
    E1Request request; ///< Meaningful for Complete only.
};

/// One framed 3C or 1C request (spec §5.5, §5.6, §4.1, §4.3), decoded as far as the mock can. The
/// route fields hold what the request carried; `network` and `selfStation` exist in 3C only,
/// `block` in format 2 only.
struct SerialRequest : Access {
    uint8_t station{0};
    uint8_t network{0};
    uint8_t pc{0};
    uint8_t selfStation{0};
    uint8_t block{0};
    PlcSeries series{PlcSeries::QL}; ///< 3C: from the subcommand received; 1C: always QL.
    bool sumValid{true};             ///< false when a SUM was expected and does not match.
};

struct SerialDecodeResult {
    FrameStatus status{FrameStatus::NeedMore};
    size_t consumed{0};
    SerialRequest request; ///< Meaningful for Complete only.
};

/// Frames and decodes one 3C or 1C request from the start of `rx`, in the format of `cfg` (spec
/// §6.3 read from the server side). Formats 1, 2 and 4 have no terminator before the SUM, so the
/// end of the request is computed from the command and its point count (spec §4.1, §4.3); format
/// 3 ends at ETX. Bytes before the start byte (ENQ; STX in format 3) are Junk one at a time, and an
/// EOT anywhere ends the partial request (Eot). A request whose layout cannot be computed (an
/// unknown command in formats 1, 2 and 4, an access route that is not hexadecimal) is Junk too: its
/// start byte is dropped and the scan resumes. The station number and the SUM are reported in the
/// request, not judged here.
SerialDecodeResult decodeSerialRequest(const FrameConfig& cfg, ByteView rx);

/// Parses `digits` as a device number in `radix` (spec §3.2: leading zeros of an ASCII number may
/// be spaces). Fails on any other character.
bool parseDeviceNumber(ByteView digits, Radix radix, uint64_t& number);

/// Decodes a QnA ASCII device field: the text code and a number in the device's radix, 8
/// characters for Q/L and 12 for iQ-R (spec §3.3). The `*` of the code may be a space.
bool decodeQnaAsciiDevice(PlcSeries series, ByteView field, Device& out);

/// Characters of a QnA ASCII device field.
size_t qnaAsciiDeviceSize(PlcSeries series);

/// Spec §4.1: subcommand 0000 / 0001 are Q/L word / bit units, 0002 / 0003 iQ-R word / bit units.
/// Fails for any other value.
bool decodeQnaSubcommand(uint16_t subcommand, PlcSeries& series, bool& bitUnit);

/// Decodes the request data of a QnA command in ASCII with no monitoring timer in front (3C: spec
/// §4.1 after the access route). The route fields of the result are left at zero.
QnaRequest decodeQnaAsciiRequestData(ByteView data);

/// Frames and decodes one 3E request from the start of `rx` (spec §5.1, §4.1.1, §4.1.2).
DecodeResult decode3eRequest(DataCode code, ByteView rx);

/// Frames and decodes one 1E request from the start of `rx` (spec §5.3, §4.2). There is no length
/// field: the frame ends where the command's fixed layout plus its point count say (00H-03H batch
/// access; 04H/05H by their n x item layout). A first byte that is not a command code 00H-05H is
/// Junk.
DecodeResult1e decode1eRequest(DataCode code, ByteView rx);

/// A failRange() fault.
struct Fault {
    DeviceType type;
    uint32_t first;
    uint32_t last;
    uint16_t code;
    uint8_t abnormal;
};

/// Result of executing one request: success (read data in normalized form: one byte per point
/// for bits, little-endian words otherwise) or the PLC error to answer with.
struct Outcome {
    bool ok{true};
    uint16_t plcCode{0};
    uint8_t abnormal{0};
    ByteBuf data;
};

/// Executes a QnA request on `memory`. Checks, in order: executable, bit op on a bit device,
/// failRange faults, device limits, response size. A failed request changes no memory.
Outcome executeQna(const QnaRequest& request, DataCode code, MemoryImage& memory,
                   const std::vector<Fault>& faults, const MockOptions& options);

/// Executes a 1E request on `memory`, with the same checks as executeQna() except the response
/// size (a 1E response has no length field). The error codes are MockOptions::unsupported1e and
/// outOfRange1e (+ outOfRange1eAbnormal); a failRange() fault answers with its code's low byte.
Outcome executeE1(const E1Request& request, MemoryImage& memory, const std::vector<Fault>& faults,
                  const MockOptions& options);

/// Executes a 3C or 1C request (`frame` is F3C or F1C) on `memory`, with the same checks as
/// executeQna() except the response size (a serial response has no length field). The error codes
/// are MockOptions::unsupportedQna / outOfRangeQna for 3C and unsupported1c / outOfRange1c for 1C;
/// a failRange() fault answers with its code, cut to its low byte on 1C.
Outcome executeSerial(const SerialRequest& request, FrameType frame, MemoryImage& memory,
                      const std::vector<Fault>& faults, const MockOptions& options);

/// Builds the 3E response frame for `request` (spec §5.1): the request's route is echoed, and an
/// error carries the error information (route, command, subcommand).
ByteBuf build3eResponse(DataCode code, const QnaRequest& request, const Outcome& outcome);

/// Builds the 1E response frame for `request` (spec §5.3): subheader `command | 80H`, end code,
/// the abnormal code only after end code 5BH, then the read data (a trailing dummy character on
/// an odd ASCII bit read, a zero low nibble on an odd binary one).
ByteBuf build1eResponse(DataCode code, const E1Request& request, const Outcome& outcome);

/// Builds the 3C or 1C response frame for `request` in the format of `cfg` (spec §5.4-§5.6): ACK /
/// NAK, or QACK / QNAK (GG / NN on 1C) in format 3, the access route and block number of the
/// request echoed, the SUM when `cfg.sumCheck` (F3 short responses only with
/// `cfg.f3ShortResponseHasSum`), CR LF in format 4. A failed `outcome` carries its code as 4
/// characters on 3C and 2 on 1C.
ByteBuf buildSerialResponse(const FrameConfig& cfg, const SerialRequest& request,
                            const Outcome& outcome);

/// Damages a finished 3E response as `mode` says (mc/mock/mock_plc.h, Corruption). The serial-only
/// modes leave it unchanged.
void corrupt3eResponse(Corruption mode, DataCode code, ByteBuf& response);

/// Damages a finished 1E response as `mode` says. A 1E response carries no route, so WrongRoute
/// leaves it unchanged, like the serial-only modes.
void corrupt1eResponse(Corruption mode, DataCode code, ByteBuf& response);

/// Damages a finished 3C or 1C response (built by buildSerialResponse() for `cfg`) as `mode` says.
/// WrongSubheader acts on Ethernet frames only; a mode that finds nothing to change (a SUM that
/// is not there, a block number outside format 2) leaves the response unchanged.
void corruptSerialResponse(const FrameConfig& cfg, Corruption mode, ByteBuf& response);

} // namespace mc::detail::mock
