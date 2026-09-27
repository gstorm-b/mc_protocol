/**
 * @file frame_config.h
 * @brief Frame configuration: which frame family, its wire format, and the parameters each
 * frame needs (spec `mc-protocol-frame-spec.md` §8.3).
 */
#pragma once

#include "mc/core/result.h"

#include <cstdint>

namespace mc {

/**
 * @enum FrameType
 * @brief Which frame family a FrameConfig describes.
 */
enum class FrameType : uint8_t {
    F3E, ///< 3E: Ethernet, QnA command family, ASCII or Binary.
    F1E, ///< 1E: Ethernet, 1E command family, ASCII or Binary.
    F3C, ///< 3C: serial, QnA command family, ASCII only, formats 1-4.
    F1C, ///< 1C: serial, 1C command family, ASCII only, formats 1-4.
    F4E, ///< 4E: reserved for v2 (3E plus a serial number); rejected by validate() in v1.
    F4C  ///< 4C: reserved for v2 (3C plus format 5 and a wider access route); rejected in v1.
};

/**
 * @enum DataCode
 * @brief Wire representation of numeric and device fields.
 */
enum class DataCode : uint8_t {
    Binary, ///< Raw binary fields (3E, 1E only).
    Ascii   ///< ASCII hex/decimal text fields; the only code 3C and 1C support.
};

/**
 * @enum SerialFormat
 * @brief Serial frame envelope (spec §5.4/§5.5/§5.6). Values equal the manual's format number.
 */
enum class SerialFormat : uint8_t {
    Format1 = 1, ///< `ENQ ... SUM`, no block number.
    Format2,     ///< `ENQ BLK ... SUM`, adds a block number.
    Format3,     ///< `STX ... ETX SUM`, no leading ENQ/trailing wait.
    Format4,     ///< Format 1 plus a trailing CR LF.
    Format5      ///< Binary (4C only); reserved, rejected by validate() in v1.
};

/**
 * @enum PlcSeries
 * @brief Chooses the QnA subcommand and device-code column a QnA frame (3E/3C) uses.
 */
enum class PlcSeries : uint8_t {
    QL, ///< Q/L series device codes and subcommands (spec §3.2 "QnA ... Q/L" columns).
    IqR ///< iQ-R series device codes and subcommands (spec §3.2 "QnA ... iQ-R" columns).
};

/**
 * @enum TargetFamily
 * @brief Chooses the point-limit column of spec §4.4 and, for `aSeriesTarget`, an access rule.
 */
enum class TargetFamily : uint8_t {
    IqR_Q_L, ///< iQ-R, Q or L CPU: the widest point limits.
    QnA,     ///< QnACPU: the intermediate point limits.
    A        ///< ACPU/A-series: the narrowest point limits.
};

/**
 * @enum C1CommandSet
 * @brief Which 1C command letters encode a request (spec §4.3).
 */
enum class C1CommandSet : uint8_t {
    ACPU, ///< BR/WR/BW/WW/BT/WT: shorter device fields (spec §3.3 "1C ACPU commands").
    AnA   ///< JR/QR/JW/QW/JT/QT: longer device fields (spec §3.3 "1C AnA/AnU commands").
};

/**
 * @struct FrameConfig
 * @brief One flat struct describing every frame family; a frame ignores the fields it does not
 * use, and so does validate(Request, FrameConfig) (`request.h`).
 *
 * Value type, trivially copyable. Construct through the named constructors (frame3E(), frame1E(),
 * frame3C(), frame1C()), which set spec §8.3's per-frame defaults; the raw member defaults below
 * already equal the 3E defaults, since 3E is the widest-used baseline.
 *
 * @see validate, effectiveTimeoutMs, isSerial
 */
struct FrameConfig {
    FrameType frame{FrameType::F3E}; ///< Which frame family this config describes.
    DataCode code{DataCode::Binary}; ///< Wire code; forced to Ascii for F3C/F1C by validate().

    // Ethernet frames (3E; 1E uses pc and monitoringTimer only; 4E reserved). 3E/4C/3C also use
    // network and pc as part of their access route (spec §5.1, §5.4, §5.5).
    uint8_t network{0x00};  ///< Network No. of the access route.
    uint8_t pc{0xFF};       ///< PC No. of the access route.
    uint16_t io{0x03FF};    ///< Request destination module I/O No. (3E/4E/4C only).
    uint8_t station{0x00};  ///< Request destination module station No. (3E/4E/4C only).
    uint16_t monitoringTimer{0x0010}; ///< ×250 ms; 0 = wait forever (then timeoutMs is mandatory).
                                      ///< 1E default is 0x000A (frame1E()).
    PlcSeries series{PlcSeries::QL}; ///< QnA subcommand/device-code column (3E/3C only).
    bool checkRoute{false}; ///< Compare response route with request route (spec §5.4). Ethernet
                            ///< default false; frame3C()/frame1C() set true.
    uint16_t serialStart{0}; ///< 4E only, reserved, unused in v1.

    // Serial frames (3C, 1C; 4C reserved).
    SerialFormat format{SerialFormat::Format1}; ///< Serial envelope; format 5 rejected in v1.
    uint8_t stationNo{0x00};    ///< Station No. of the access route (3C/1C; 4C: multidrop/global).
    uint8_t selfStation{0x00};  ///< Self-station No. of the access route (3C only).
    bool sumCheck{true};        ///< Append/require the sum check (spec §2.5).
    uint8_t blockNo{0x00};      ///< Block No.; Format 2 only.
    bool checkBlockNo{true};    ///< Compare response Block No. with request Block No. (Format 2).
    bool sendEotOnError{true};  ///< Send EOT after an error response (spec §6.3).
    bool f3ShortResponseHasSum{false}; ///< Format 3 "no data" response carries a sum check byte.

    // 1C
    uint8_t messageWait{0}; ///< 0-15, ×10 ms; validate() rejects values above 15.
    C1CommandSet commandSet{C1CommandSet::ACPU}; ///< Selects the 1C command letters and field
                                                  ///< widths (spec §3.3).

    // 1E
    bool e1AliasLS{false}; ///< Accept L/S on 1E and encode them as M, keeping the number (spec
                           ///< §3.2 footnote 2). Off by default: the manual has no code of its
                           ///< own for L/S on 1E.

    // Common
    TargetFamily targetFamily{TargetFamily::IqR_Q_L}; ///< Point-limit column (spec §4.4).
    bool highPerformanceQcpu{false}; ///< 0403: ZR counts double (v1.1; unused by validate() in v1).
    bool aSeriesTarget{false}; ///< Enforce the multiple-of-16 head rule for QnA word access to
                               ///< bit devices, as an A-series target would require.
    bool splitWrites{false};   ///< Allow chunk() to split a write across several commands (v1.1);
                               ///< unused by validate() in v1.
    uint32_t timeoutMs{0}; ///< 0 = derived: see effectiveTimeoutMs().
    uint8_t readRetries{0}; ///< Serial links only: an Ethernet timeout faults the link (spec
                            ///< §6.1); unused by validate() in v1.

    /**
     * @brief Builds a 3E FrameConfig with spec §8.3's 3E defaults.
     * @param[in] code Wire code; Binary by default.
     * @return A FrameConfig with `frame == FrameType::F3E` and every other field at its spec
     * §8.3 3E default.
     * @par Complexity
     * O(1); no allocation.
     * @see frame1E, frame3C, frame1C
     */
    static FrameConfig frame3E(DataCode code = DataCode::Binary) noexcept;

    /**
     * @brief Builds a 1E FrameConfig with spec §8.3's 1E defaults.
     * @param[in] code Wire code; Binary by default.
     * @return A FrameConfig with `frame == FrameType::F1E`, `monitoringTimer == 0x000A`, and
     * every other field at its spec §8.3 1E default.
     * @par Complexity
     * O(1); no allocation.
     * @see frame3E
     */
    static FrameConfig frame1E(DataCode code = DataCode::Binary) noexcept;

    /**
     * @brief Builds a 3C FrameConfig with spec §8.3's 3C and serial defaults.
     * @param[in] f Serial envelope; Format1 by default.
     * @return A FrameConfig with `frame == FrameType::F3C`, `code == DataCode::Ascii`,
     * `checkRoute == true`, and every other field at its spec §8.3 3C/serial default.
     * @par Complexity
     * O(1); no allocation.
     * @see frame1C
     */
    static FrameConfig frame3C(SerialFormat f = SerialFormat::Format1) noexcept;

    /**
     * @brief Builds a 1C FrameConfig with spec §8.3's 1C and serial defaults.
     * @param[in] f Serial envelope; Format1 by default.
     * @return A FrameConfig with `frame == FrameType::F1C`, `code == DataCode::Ascii`,
     * `checkRoute == true`, and every other field at its spec §8.3 1C/serial default.
     * @par Complexity
     * O(1); no allocation.
     * @see frame3C
     */
    static FrameConfig frame1C(SerialFormat f = SerialFormat::Format1) noexcept;

    /**
     * @brief Checks this configuration for values that are wrong before anything is sent.
     *
     * Spec §8.3 last bullet: out-of-range values are a Config error at construction time, not at
     * send time.
     *
     * @return Success when every check below passes.
     * @retval ErrorCode::InvalidConfig `frame` is `F4E` or `F4C` (reserved for v2).
     * @retval ErrorCode::InvalidConfig `format == SerialFormat::Format5` (reserved for v2; no
     * v1 frame accepts it).
     * @retval ErrorCode::InvalidConfig `messageWait > 15`.
     * @retval ErrorCode::InvalidConfig `monitoringTimer == 0` and `timeoutMs == 0` (an explicit
     * timeout is mandatory when the PLC is told to wait forever).
     * @retval ErrorCode::InvalidConfig `frame` is `F3C` or `F1C` and `code != DataCode::Ascii`
     * (3C/1C are ASCII-only frames).
     * @par Complexity
     * O(1); no allocation.
     */
    Expected<void> validate() const noexcept;

    /**
     * @brief The response timeout this configuration implies.
     *
     * Returns `timeoutMs` directly when it is nonzero; otherwise derives it: for a serial frame,
     * a flat 3000 ms; otherwise (3E/4E/1E), `monitoringTimer * 250 + 1000` (spec §8.3).
     *
     * @return The effective timeout, in milliseconds.
     * @par Complexity
     * O(1); no allocation.
     * @see isSerial
     */
    uint32_t effectiveTimeoutMs() const noexcept;

    /**
     * @brief Whether this configuration is a serial frame.
     * @return true when `frame` is `F3C`, `F1C` or `F4C`.
     * @par Complexity
     * O(1); no allocation.
     */
    bool isSerial() const noexcept;
};

} // namespace mc
