/**
 * @file device_encode.h
 * @brief On-wire device address encoding for every frame family (spec
 * `mc-protocol-frame-spec.md` §3.3): `qnaDevice()` (3E/3C, ASCII or Binary, Q/L or iQ-R),
 * `e1Device()` (1E, ASCII or Binary) and `c1Device()` (1C, always ASCII, ACPU or AnA/AnU).
 *
 * Builds on core-model's device table (`deviceInfo()`, spec §3.2) for each family's code, and on
 * field_codec.h's `AsciiCodec`/`BinaryCodec` for every field these functions can express as a
 * plain u16/u32 (the 1E functions are entirely built that way). The one field kind field_codec.h
 * has no primitive for -- a device number in its *own* radix (decimal or hexadecimal, spec §3.2:
 * "D100" is decimal, "X1F" is hexadecimal), zero-padded to a fixed width -- is this file's own
 * (`putFixedWidthNumber`, device_encode.cpp): QnA ASCII and 1C are both this kind of field, 1E
 * ASCII is not (spec E4: its number is always 8 hexadecimal digits regardless of the device's own
 * radix, so `AsciiCodec::putU32` already produces exactly the right text). X and Y on an FX CPU
 * are the exception to "own radix": their ASCII digits are octal when the caller passes
 * `XyNumbering::Octal` (`FrameConfig::xyAsciiDigits`), through the same `putFixedWidthNumber`.
 *
 * This is the one place command code is allowed to branch on ASCII vs. Binary itself (module
 * spec "Internal design": "no ASCII/Binary branches inside command code except device
 * encoding").
 *
 * Every function here assumes `d`'s number already fits its target field's width: `validate()`
 * (core-model, already run by `McProtocol::encode()` before any codec sees the request) rejects
 * an oversized device number with `ErrorCode::InvalidDevice` first (see also field_codec.h's own
 * note on field overflow). What these functions still check is `d.type` having no code at all
 * for the requested family (spec §3.2's table has blank cells, e.g. `RD` on QnA Q/L, `SM`/`SD` on
 * 1E/1C) -- that is a property of `d.type` alone, not of `d.number`, so it is not validate()'s
 * job and must be checked here.
 *
 * Internal only (mc::detail); not part of mc_core's public surface.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstddef>

namespace mc::detail {

/// Wire size of a QnA device field (spec §3.3): ASCII 8 (Q/L) or 12 (iQ-R) characters; Binary 4
/// (Q/L) or 6 (iQ-R) bytes.
constexpr size_t qnaDeviceSize(DataCode code, PlcSeries series) noexcept {
    if (code == DataCode::Ascii) {
        return series == PlcSeries::QL ? 8 : 12;
    }
    return series == PlcSeries::QL ? 4 : 6;
}

/**
 * @brief Encodes `d` as a QnA device field (3E/3C; spec §3.3 "QnA ASCII Q/L", "QnA ASCII iQ-R",
 * "QnA Binary Q/L", "QnA Binary iQ-R" rows): ASCII is the family's own text code (spec §3.2's
 * `qnaAsciiQL`/`qnaAsciiIqr` column, e.g. `"D*"`, never a space or a hex-encoded number) followed
 * by the device number, zero-padded in `d`'s own radix; Binary is the number, little-endian,
 * followed by the family's own numeric code (`qnaBinQL`/`qnaBinIqr`).
 * @param[in] d Device to encode.
 * @param[in] code Ascii or Binary wire representation.
 * @param[in] series Q/L or iQ-R device-code column (`FrameConfig::series`).
 * @param[out] out Destination; must hold at least `qnaDeviceSize(code, series)` bytes.
 * @param[in] xyDigits Base of the digits of an X or Y number in ASCII
 * (`FrameConfig::xyAsciiDigits`); Binary always carries the point index.
 * @return Bytes/characters written (`qnaDeviceSize(code, series)`).
 * @retval ErrorCode::InvalidDevice `d.type` has no code for this `series` (spec §3.2: a blank
 * cell, e.g. `RD` on Q/L).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `qnaDeviceSize(code, series)`.
 * @par Complexity
 * O(1); no allocation.
 * @see e1Device, c1Device
 */
Expected<size_t> qnaDevice(const Device& d, DataCode code, PlcSeries series, MutableByteView out,
                           XyNumbering xyDigits = XyNumbering::Hex) noexcept;

/// Wire size of a 1E device field: ASCII 12 characters; Binary 6 bytes.
constexpr size_t e1DeviceSize(DataCode code) noexcept { return code == DataCode::Ascii ? 12 : 6; }

/**
 * @brief Encodes `d` as a 1E device field (spec §3.3 "1E ASCII"/"1E Binary"): the family's own
 * code (spec §3.2's `e1Code` column) followed by the device number. Unlike QnA ASCII, 1E ASCII's
 * number is always 8 hexadecimal digits regardless of `d`'s own radix (spec E4).
 *
 * `DeviceType::L` and `DeviceType::S` already carry `DeviceType::M`'s own `e1Code` in the device
 * table (spec §3.2 footnote 2: "1E has no separate code for L and S; perform accessing by
 * specifying 'M'"); `validate()` is what gates whether an `L`/`S` device reaches this function at
 * all (`FrameConfig::e1AliasLS`), so this function itself needs no alias parameter.
 *
 * @param[in] d Device to encode.
 * @param[in] code Ascii or Binary wire representation.
 * @param[out] out Destination; must hold at least `e1DeviceSize(code)` bytes.
 * @param[in] xyDigits Base of the 8 ASCII digits of an X or Y number
 * (`FrameConfig::xyAsciiDigits`); every other device is written in hexadecimal, Binary always
 * carries the point index.
 * @return Bytes/characters written (`e1DeviceSize(code)`).
 * @retval ErrorCode::InvalidDevice `d.type` has no 1E code (spec §3.2: a blank `e1Code` cell,
 * e.g. `SM`, `SD`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `e1DeviceSize(code)`.
 * @par Complexity
 * O(1); no allocation.
 * @see qnaDevice, c1Device
 */
Expected<size_t> e1Device(const Device& d, DataCode code, MutableByteView out,
                          XyNumbering xyDigits = XyNumbering::Hex) noexcept;

/// Wire size of a 1C device field (spec §3.3): ACPU commands 5 characters (Timer/Counter
/// devices: 4); AnA/AnU commands 7 characters (Timer/Counter: 6). Not `constexpr`: reads
/// `deviceInfo(d.type)` to tell a Timer/Counter device from any other (device.h's own
/// `deviceInfo()` is not `constexpr` either, by design).
size_t c1DeviceSize(const Device& d, C1CommandSet commandSet) noexcept;

/**
 * @brief Encodes `d` as a 1C device field (spec §3.3 "1C ACPU commands"/"1C AnA/AnU commands"),
 * always ASCII: the family's own code (spec §3.2's `c1Code` column, 1 character, or 2 for the six
 * Timer/Counter symbols) followed by the device number, zero-padded in `d`'s own radix, to
 * `c1DeviceSize(d, commandSet) - <code width>` digits.
 * @param[in] d Device to encode.
 * @param[in] commandSet ACPU (`BR`/`WR`/`BW`/`WW`) or AnA/AnU (`JR`/`QR`/`JW`/`QW`) field widths
 * (`FrameConfig::commandSet`).
 * @param[out] out Destination; must hold at least `c1DeviceSize(d, commandSet)` characters.
 * @param[in] xyDigits Base of the digits of an X or Y number (`FrameConfig::xyAsciiDigits`).
 * @return Characters written (`c1DeviceSize(d, commandSet)`).
 * @retval ErrorCode::InvalidDevice `d.type` has no 1C code (spec §3.2: a blank `c1Code` cell,
 * e.g. `V`).
 * @retval ErrorCode::BufferTooSmall `out` is smaller than `c1DeviceSize(d, commandSet)`.
 * @par Complexity
 * O(1); no allocation.
 * @see qnaDevice, e1Device
 */
Expected<size_t> c1Device(const Device& d, C1CommandSet commandSet, MutableByteView out,
                          XyNumbering xyDigits = XyNumbering::Hex) noexcept;

} // namespace mc::detail
