// tests/hil/replay/replay_util.h: small helpers shared by replay.cpp and replay_session.cpp
// (string splitting, hex text, device and op names, decoding a request frame with a MockPlc).
// Test code only; not a public header.
#pragma once

#include "replay.h"

#include "mc/core/device.h"
#include "mc/core/request.h"
#include "mc/mock/mock_plc.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mc::replay::util {

/// Splits @p s on whitespace.
std::vector<std::string> words(const std::string& s);
/// Removes leading and trailing whitespace.
std::string trim(const std::string& s);
/// True when @p s starts with @p prefix.
bool startsWith(const std::string& s, const std::string& prefix);
/// "50 00 FF" for @p bytes; at most @p limit bytes, then " ...".
std::string hex(const std::vector<uint8_t>& bytes, size_t limit = 40);
/// "50 00 FF" for a view.
std::string hex(ByteView bytes, size_t limit = 40);
/// Parses @p s as an unsigned number in @p base; nullopt on any non-digit.
std::optional<uint32_t> number(const std::string& s, int base);
/// The device type whose symbol is @p symbol ("D", "TN"); nullopt when none.
std::optional<DeviceType> typeBySymbol(const std::string& symbol);
/// The canonical text of a device ("D100", "X1F").
std::string deviceText(const Device& d);
/// "ReadWords" -> Op::ReadWords; nullopt for anything else (e.g. "Raw").
std::optional<Op> opByName(const std::string& name);
/// True for ReadBits / WriteBits.
inline bool isBitOp(Op op) { return op == Op::ReadBits || op == Op::WriteBits; }
/// True for WriteBits / WriteWords.
inline bool isWriteOp(Op op) { return op == Op::WriteBits || op == Op::WriteWords; }
/// True when @p t is a bit device.
bool isBitDevice(DeviceType t);
/// First index at which @p a and @p b differ, or the shorter size when one is a prefix of the
/// other.
size_t firstDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
/// "at byte 11: captured 03, other 04 (captured N bytes, other M bytes)".
std::string describeDifference(const std::vector<uint8_t>& captured,
                               const std::vector<uint8_t>& other, const char* otherName);

/// One request the mock found in a frame, with the data of a write.
struct Decoded {
    Op op{Op::ReadWords};
    Device head{};
    uint16_t count{0};
    bool answeredOk{false};    ///< The mock executed it (no PLC error).
    std::vector<uint8_t> data; ///< Write payload in the normalized layout; empty for a read.
};

/// Feeds @p bytes to a fresh MockPlc speaking @p cfg and returns every request it decoded, in
/// order. The data of a write is read back from the mock's memory.
std::vector<Decoded> decode(const FrameConfig& cfg, const std::vector<uint8_t>& bytes);

/// The `Request` of a decoded request (its data view points into @p d).
Request toRequest(const Decoded& d);

/// The number of points a request touches on @p d.head's device: a word operation on a bit device
/// touches 16 points per word.
uint32_t pointsTouched(const Decoded& d);

/// A view over a byte vector.
inline ByteView view(const std::vector<uint8_t>& v) { return ByteView{v.data(), v.size()}; }

} // namespace mc::replay::util
