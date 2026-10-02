/**
 * @file profile.h
 * @brief The profile of one bench PLC: identity, scratch area, device limits and the
 * McDeviceConfig of its connection (spec `SPEC-hil-capture.md`, "Profile").
 *
 * A profile is JSON:
 *
 *     { "schema": 1,
 *       "profile": { "id": ..., "plc": ..., "module": ..., "firmware": ..., "adapter": ...,
 *                    "plcState": ..., "scratch": ["D100-D2099", ...],
 *                    "deviceEnd": { "D": 12287, "W": "1FFF" },
 *                    "supports": ["D", "W", ...], "scanTimeDevice": "",
 *                    "specialBit": "SM0", "specialWord": "SD0", "families": ["qna-serial"] },
 *       "device": { an McDeviceConfig, read by McDeviceConfig::fromJson } }
 *
 * Required: schema, profile.id, profile.plc, profile.scratch, profile.deviceEnd,
 * profile.supports, profile.specialBit, profile.specialWord, device. The rest is optional.
 * Every other key is an error, also inside `device`. Scratch ranges and deviceEnd
 * numbers are written in each device type's own radix (D decimal, W/B/X/Y hexadecimal, from
 * the core device table); a deviceEnd of a hexadecimal type is a JSON string. `families` holds
 * "qna-ethernet", "a1e", "qna-serial" or "a1c". `device.subscriptions` must be empty: polling is
 * the business of the plan's poll steps.
 */
#pragma once

#include "hil_capture/json_reader.h"
#include "mc/core/device.h"
#include "mc/device/mc_device_config.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <cstdint>
#include <optional>

namespace mc::hil {

/// @brief One inclusive scratch range of one device type.
struct ScratchRange {
    DeviceType type{DeviceType::D}; ///< Device type of the range.
    uint32_t first{0};              ///< First device number, inclusive.
    uint32_t last{0};               ///< Last device number, inclusive.
};

/// @brief A loaded, validated profile.
struct Profile {
    QString id;                    ///< Folder-safe identity, e.g. "q03ude-c24-3c-f4".
    QString plc;                   ///< CPU model.
    QString module;                ///< Communication module.
    QString firmware;              ///< Firmware or version text.
    QString adapter;               ///< USB-serial adapter text.
    QString plcState;              ///< PLC state text, e.g. "RUN, online change enabled".
    QVector<ScratchRange> scratch; ///< Declared scratch ranges, in file order.
    std::array<std::optional<uint32_t>, static_cast<size_t>(DeviceType::Count)> deviceEnd{};
    ///< Last existing number of each device type; nullopt when not declared.
    QVector<DeviceType> supports;   ///< Device types to exercise.
    QString scanTimeDevice;         ///< Text of the scan time device; empty when none.
    std::optional<Device> scanTime; ///< The parsed scan time device.
    Device specialBit{};            ///< First special relay as addressed through this frame.
    Device specialWord{};           ///< First special register as addressed through this frame.
    QStringList families;           ///< Frame family names the profile belongs to.
    McDeviceConfig device;          ///< Connection, frame and session settings.

    /// @brief Whether @p t is in `supports`.
    bool supportsType(DeviceType t) const;
    /// @brief The first scratch range of type @p t in file order, or nullptr.
    const ScratchRange* firstScratch(DeviceType t) const;
    /// @brief Whether the points [@p head, @p head + @p points - 1] lie inside ONE scratch range.
    bool inScratch(DeviceType t, uint32_t head, uint64_t points) const;
    /// @brief The declared deviceEnd of @p t.
    std::optional<uint32_t> end(DeviceType t) const { return deviceEnd[static_cast<size_t>(t)]; }
};

/// @brief Outcome of loading a profile.
struct ProfileLoad {
    std::optional<Profile> profile; ///< Set on success.
    LoadError error;                ///< Set on failure.
    /// @brief Whether loading succeeded.
    bool ok() const { return profile.has_value(); }
};

/// @brief Loads a profile from parsed JSON.
/// @param[in] root The JSON document's top-level object.
ProfileLoad loadProfile(const QJsonObject& root);

/// @brief Reads, parses and loads a profile file.
/// @param[in] path File to read.
ProfileLoad loadProfileFile(const QString& path);

/// @brief Looks a device symbol up case-insensitively ("d", "TN", "STS").
std::optional<DeviceType> deviceTypeFromSymbol(const QString& symbol);

/// @brief The canonical symbol of a device type.
QString deviceSymbol(DeviceType t);

/// @brief The canonical text of a device, e.g. "D100", "X1F".
QString deviceText(const Device& d);

/// @brief Parses a device number written in the radix of @p t (no prefix, no sign).
/// @param[in] t Device type whose radix applies.
/// @param[in] text Digits only.
/// @param[out] out The number.
/// @return false when @p text is empty, has a digit outside the radix, or does not fit 32 bits.
bool parseDeviceNumber(DeviceType t, const QString& text, uint32_t& out);

/// @brief The device number as text in the radix of its type (upper-case hexadecimal).
QString formatDeviceNumber(DeviceType t, uint32_t number);

/// @brief Whether @p id can name a capture folder: letters, digits, '-', '_' and '.', starting
/// with a letter or digit, at most 80 characters, no "..".
bool folderSafe(const QString& id);

} // namespace mc::hil
