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

/// One framed QnA request, decoded as far as the mock can. `executable` is false when the frame
/// was well-framed but is something the mock does not execute (another command, an unknown
/// subcommand or device code, request data that does not fit the command's layout); `command`
/// and `subcommand` then hold whatever was readable, and the remaining fields keep their
/// defaults.
struct QnaRequest {
    Route route;
    uint16_t command{0};
    uint16_t subcommand{0};
    bool executable{false};
    Op op{Op::ReadWords};
    Device head{};
    uint16_t count{0};
    PlcSeries series{PlcSeries::QL};
    ByteBuf data; ///< Write payload: one byte per point for bits, little-endian words otherwise.
};

enum class FrameStatus {
    NeedMore, ///< The buffer holds only part of a frame.
    Junk,     ///< The first byte cannot start a frame; `consumed` is 1.
    Complete  ///< A whole frame; `consumed` bytes belong to it.
};

struct DecodeResult {
    FrameStatus status{FrameStatus::NeedMore};
    size_t consumed{0};
    QnaRequest request; ///< Meaningful for Complete only.
};

/// Frames and decodes one 3E request from the start of `rx` (spec §5.1, §4.1.1, §4.1.2).
DecodeResult decode3eRequest(DataCode code, ByteView rx);

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

/// Builds the 3E response frame for `request` (spec §5.1): the request's route is echoed, and an
/// error carries the error information (route, command, subcommand).
ByteBuf build3eResponse(DataCode code, const QnaRequest& request, const Outcome& outcome);

/// Damages a finished 3E response as `mode` says (mc/mock/mock_plc.h, Corruption). The serial-only
/// modes leave it unchanged.
void corrupt3eResponse(Corruption mode, DataCode code, ByteBuf& response);

} // namespace mc::detail::mock
