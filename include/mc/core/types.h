/**
 * @file types.h
 * @brief Byte views (owning and non-owning) and the "no code" sentinel used across the library.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace mc {

/**
 * @struct ByteView
 * @brief Non-owning view of a read-only byte range.
 *
 * Value type, trivially copyable. The library never stores a view past the call it was passed
 * to, except where a function's documentation says so (`Session::submit` copies).
 *
 * @see MutableByteView
 */
struct ByteView {
    const uint8_t* data{nullptr}; ///< First byte of the range; may be nullptr when size == 0.
    size_t size{0};               ///< Number of bytes in the range.
};

/**
 * @struct MutableByteView
 * @brief Non-owning view of a writable byte range.
 *
 * Value type, trivially copyable. Same lifetime contract as ByteView.
 *
 * @see ByteView
 */
struct MutableByteView {
    uint8_t* data{nullptr}; ///< First byte of the range; may be nullptr when size == 0.
    size_t size{0};         ///< Number of bytes in the range.
};

static_assert(std::is_trivially_copyable_v<ByteView>, "ByteView must be trivially copyable");
static_assert(std::is_trivially_copyable_v<MutableByteView>,
              "MutableByteView must be trivially copyable");

/// Owning convenience type for callers that want one; the library's own functions never
/// allocate one unless documented (e.g. `mc::convert::from*`).
using ByteBuf = std::vector<uint8_t>;

/// Sentinel meaning "this device has no code for this frame family or command" (used by
/// `device.h`'s table and by `validate()`).
inline constexpr uint16_t kNoCode = 0xFFFF;

} // namespace mc
