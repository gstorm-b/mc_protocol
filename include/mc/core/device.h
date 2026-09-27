/**
 * @file device.h
 * @brief PLC device addresses: the device table (spec `mc-protocol-frame-spec.md` §3.2),
 * parsing and formatting.
 */
#pragma once

#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstdint>
#include <string_view>

namespace mc {

/**
 * @enum DeviceKind
 * @brief Whether a device symbol addresses a single bit, one word, or a double word.
 */
enum class DeviceKind : uint8_t {
    Bit,  ///< Single-bit device (e.g. X, Y, M).
    Word, ///< Single 16-bit word device (e.g. D, W, TN).
    DWord ///< Double word device; no v1 symbol of spec §3.2 uses this kind.
};

/**
 * @enum Radix
 * @brief Numeral base a device's number is written and parsed in.
 */
enum class Radix : uint8_t {
    Dec, ///< Device number is decimal, e.g. D100.
    Hex  ///< Device number is hexadecimal, e.g. X1F.
};

/**
 * @enum DeviceType
 * @brief Every device symbol of spec §3.2, in table order.
 *
 * `Count` is not a device; it is the number of rows in the device table, used to size and index
 * it.
 *
 * @see deviceInfo, DeviceInfo
 */
enum class DeviceType : uint8_t {
    SM,  ///< Special relay.
    SD,  ///< Special register.
    X,   ///< Input.
    Y,   ///< Output.
    M,   ///< Internal relay.
    L,   ///< Latch relay.
    F,   ///< Annunciator.
    V,   ///< Edge relay.
    B,   ///< Link relay.
    D,   ///< Data register.
    W,   ///< Link register.
    TS,  ///< Timer, contact.
    TC,  ///< Timer, coil.
    TN,  ///< Timer, current value.
    STS, ///< Retentive timer, contact.
    STC, ///< Retentive timer, coil.
    STN, ///< Retentive timer, current value.
    CS,  ///< Counter, contact.
    CC,  ///< Counter, coil.
    CN,  ///< Counter, current value.
    SB,  ///< Link special relay.
    SW,  ///< Link special register.
    S,   ///< Step relay.
    DX,  ///< Direct access input.
    DY,  ///< Direct access output.
    Z,   ///< Index register.
    R,   ///< File register (block switching).
    ZR,  ///< File register (serial number access).
    RD,  ///< Refresh data register; iQ-R only.
    Count ///< Not a device; the number of rows in the device table.
};

/**
 * @struct DeviceInfo
 * @brief One row of spec §3.2: a device symbol's kind, radix and on-wire codes for every frame
 * family.
 *
 * Value type, trivially copyable. All codes are stored as data; `kNoCode` (the u16 fields) or an
 * empty string (the char array fields) mean "this frame family has no code for this device".
 * Rows are looked up through deviceInfo(); the table itself is private to
 * `src/core/model/device_table.cpp`.
 *
 * @see deviceInfo, DeviceType
 */
struct DeviceInfo {
    DeviceType type;    ///< This row's own type; equals its index in the table.
    char symbol[4];     ///< Canonical symbol, e.g. "D", "TN", "STS"; NUL-terminated.
    DeviceKind kind;    ///< Bit, Word or DWord.
    Radix radix;        ///< Radix the device number is parsed and formatted in.
    char qnaAsciiQL[3]; ///< QnA ASCII Q/L code, e.g. "D*"; empty if unsupported.
    uint16_t qnaBinQL;  ///< QnA Binary Q/L code, e.g. 0xA8; kNoCode if unsupported.
    char qnaAsciiIqr[5]; ///< QnA ASCII iQ-R code, e.g. "D***"; empty if unsupported.
    uint16_t qnaBinIqr;  ///< QnA Binary iQ-R code, e.g. 0x00A8; kNoCode if unsupported.
    uint16_t e1Code;     ///< 1E code, e.g. 0x4420; kNoCode if unsupported.
    char c1Code[3];      ///< 1C code, e.g. "D"; empty if unsupported.
};

/**
 * @brief Looks up the device table row for @p t.
 *
 * Not `constexpr`: the table itself is private to `src/core/model/device_table.cpp` (kept out of
 * this header so the public surface stays free of the raw row data); nothing in this module's
 * plan needs compile-time evaluation of this lookup.
 *
 * @param[in] t Device symbol to look up.
 * @return The table row for @p t.
 * @par Complexity
 * O(1); no allocation.
 * @see DeviceInfo, DeviceType
 */
const DeviceInfo& deviceInfo(DeviceType t) noexcept;

/**
 * @struct Device
 * @brief One PLC device address: a symbol such as D or X plus its number.
 *
 * Value type, trivially copyable, no invariants beyond what parseDevice() enforces; field width
 * for a frame family is not checked here (validate(), core-request).
 *
 * @see parseDevice, formatDevice
 */
struct Device {
    DeviceType type{DeviceType::D}; ///< Symbol, indexes the device table.
    uint32_t number{0};             ///< Device number, in the symbol's own radix.
};

/**
 * @brief Equality of two devices.
 * @param[in] a First device.
 * @param[in] b Second device.
 * @return true when @p a and @p b have the same type and the same number.
 * @par Complexity
 * O(1); no allocation.
 * @see operator!=, operator<
 */
constexpr bool operator==(const Device& a, const Device& b) noexcept {
    return a.type == b.type && a.number == b.number;
}

/**
 * @brief Inequality of two devices.
 * @param[in] a First device.
 * @param[in] b Second device.
 * @return The negation of `a == b`.
 * @par Complexity
 * O(1); no allocation.
 * @see operator==
 */
constexpr bool operator!=(const Device& a, const Device& b) noexcept { return !(a == b); }

/**
 * @brief Orders two devices by table order, then by number.
 *
 * Used by core-session to sort subscriptions into a stable, deterministic order.
 *
 * @param[in] a First device.
 * @param[in] b Second device.
 * @return true when @p a sorts before @p b: its type comes first in DeviceType's declaration
 * order, or the types are equal and its number is smaller.
 * @par Complexity
 * O(1); no allocation.
 * @see operator==
 */
constexpr bool operator<(const Device& a, const Device& b) noexcept {
    if (a.type != b.type) {
        return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
    }
    return a.number < b.number;
}

/**
 * @brief Parses device address text such as "D100", "x1F", "TN10" (spec §3.4).
 *
 * The symbol is matched case-insensitively and longest-first (e.g. "STS" is tried before "S"),
 * then the remaining text is parsed as the device number in the matched symbol's own radix
 * (decimal or hexadecimal). Field width for any frame family is not checked here; that is
 * validate() (core-request).
 *
 * @param[in] text Device text without surrounding spaces.
 * @return The parsed device.
 * @retval ErrorCode::InvalidDevice No symbol of the table is a prefix of @p text, no digit
 * follows the matched symbol, or a digit falls outside the matched symbol's radix.
 * @par Complexity
 * O(len); no allocation.
 * @see formatDevice
 */
Expected<Device> parseDevice(std::string_view text) noexcept;

/**
 * @brief Writes the canonical text form of @p d (e.g. "D100", "X1F") into @p out.
 *
 * Behaves like snprintf: writes at most `capacity - 1` characters plus a terminating NUL (writes
 * nothing when capacity == 0), and always returns the number of characters the full text needs,
 * whether or not it fit in @p capacity. The number is never zero-padded.
 *
 * @param[in] d Device to format.
 * @param[out] out Destination buffer; may be null when capacity == 0.
 * @param[in] capacity Number of bytes available at @p out, including the terminating NUL.
 * @return The number of characters the canonical text needs, excluding the terminating NUL.
 * @par Complexity
 * O(digits); no allocation.
 * @see parseDevice
 */
size_t formatDevice(const Device& d, char* out, size_t capacity) noexcept;

} // namespace mc
