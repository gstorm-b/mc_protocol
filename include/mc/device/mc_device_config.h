/**
 * @file mc_device_config.h
 * @brief TransportKind, SubscriptionSpec and McDeviceConfig: everything needed to build an
 * McDevice as plain data, with validation and a JSON mapping.
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/result.h"
#include "mc/core/session.h"
#include "mc/device/serial_transport.h"
#include "mc/device/tcp_transport.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <cstdint>

namespace mc {

/**
 * @enum TransportKind
 * @brief Which Transport a McDeviceConfig asks for.
 */
enum class TransportKind : uint8_t {
    Tcp,   ///< A TcpTransport configured by McDeviceConfig::tcp.
    Serial ///< A SerialTransport configured by McDeviceConfig::serial.
};

/**
 * @struct SubscriptionSpec
 * @brief One subscription as text: a head device such as "D2000" and a point count.
 */
struct SubscriptionSpec {
    QString device;   ///< Head device, in the form parseDevice() accepts ("D2000", "x1F"); X/Y read
                      ///< in `frame.xyNotation`.
    quint32 count{1}; ///< Number of consecutive points starting at the head device.
};

/**
 * @struct McDeviceConfig
 * @brief Everything needed to build an McDevice, as plain data.
 *
 * Every field of FrameConfig and SessionConfig has a JSON key of the same name (see toJson()).
 * The struct is not a Q_GADGET; applications that want property bindings wrap it themselves.
 *
 * @see FrameConfig, SessionConfig
 */
struct McDeviceConfig {
    FrameConfig frame{FrameConfig::frame3E()}; ///< Frame family and its parameters.
    SessionConfig session{}; ///< Polling engine settings; `session.log` is ignored.
    TransportKind transport{TransportKind::Tcp}; ///< Which of `tcp` / `serial` is used.
    TcpSettings tcp{};                           ///< Used when `transport == TransportKind::Tcp`.
    SerialSettings serial{}; ///< Used when `transport == TransportKind::Serial`.
    QVector<SubscriptionSpec> subscriptions{}; ///< Subscribed at construction, in this order.

    /**
     * @brief Checks the configuration; the first failure wins, in the order listed here.
     *
     * The checks are: `frame.validate()`; `session.validate(frame)`; every subscription parses
     * with parseDevice() and fits the frame (a read plan for it can be built with
     * `session.plan`); the selected transport's settings are usable (non-empty host, non-zero
     * port, non-empty port name, positive baud rate). The settings of the transport that is not
     * selected are not checked.
     *
     * @param[out] where Optional. On failure receives the JSON-style path of the offending
     *             field, e.g. "subscriptions[2].device", "session.heartbeat.device",
     *             "transport.tcp.host"; cleared on success. May be null.
     * @return Success when every check passes.
     * @retval ErrorCode::InvalidConfig The frame, the session or a transport setting is
     *         unusable; `where` names the field ("frame", "session.heartbeat.device",
     *         "transport.tcp.host", "transport.tcp.port", "transport.serial.portName",
     *         "transport.serial.baudRate").
     * @retval ErrorCode::InvalidDevice A subscription's device does not parse, or is not
     *         supported by the frame (unknown symbol, field width, alignment); `where` is
     *         "subscriptions[i].device".
     * @retval ErrorCode::PointCount A subscription's count is zero or does not fit one command
     *         plan; `where` is "subscriptions[i].count".
     */
    Expected<void> validate(QString* where = nullptr) const;

    /**
     * @brief Serialises this configuration to the version 1 JSON schema.
     *
     * Enums are strings, `session.maxGap` is "auto" or a number, the device of the heartbeat is
     * its canonical text (X/Y written in `frame.xyNotation`), and `session.log` is not written.
     * The object always carries `"schema": 1`.
     *
     * @return The JSON object.
     * @see fromJson
     */
    QJsonObject toJson() const;

    /**
     * @brief Reads a configuration from JSON.
     *
     * A missing key takes the default of the field, an unknown key is ignored, `session.log`
     * stays null. A missing `"schema"` counts as 1. The result is not passed through
     * validate(): fromJson() checks types, enumerator names and numeric ranges only.
     *
     * @param[in] obj JSON object in the form toJson() writes.
     * @param[out] where Optional. On failure receives the JSON path of the offending key, e.g.
     *             "frame.timeoutMs" or "subscriptions[1]"; cleared on success. May be null.
     * @return The configuration.
     * @retval ErrorCode::InvalidConfig A value has the wrong JSON type, is not an integer where
     *         one is required, is out of range, names an unknown enumerator, is not a device
     *         (`session.heartbeat.device`), or `schema` is not 1 (`where` is "schema").
     * @see toJson, validate
     */
    static Expected<McDeviceConfig> fromJson(const QJsonObject& obj, QString* where = nullptr);
};

} // namespace mc
