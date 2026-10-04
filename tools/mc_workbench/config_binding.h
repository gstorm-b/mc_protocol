/**
 * @file config_binding.h
 * @brief `ConfigBinding`: a qpb property tree bound to an `McDeviceConfig`, validated with the
 * library's own `validate()`.
 */
#pragma once

#include "mc/device/mc_device_config.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace qpb {
class PropertyModel;
} // namespace qpb

namespace mc::workbench {

/// @brief How one configuration field is shown and edited.
enum class FieldKind {
    Bool,          ///< A check box.
    Int,           ///< A spin box (32-bit range).
    Int64,         ///< A 64-bit spin box (values up to 4294967295).
    Enum,          ///< A combo box over `FieldInfo::options` (the JSON enumerator names).
    Text,          ///< A line edit.
    MaxGap,        ///< A line edit: "auto" or a number (`session/maxGap`).
    Subscriptions  ///< A line edit: "D100:4; M0:16" (the `subscriptions` array).
};

/// @brief One bound field: its place in the property tree and the rules of its editor.
struct FieldInfo {
    QString path;         ///< Property path, equal to the JSON path with '/' ("frame/timeoutMs").
    QString label;        ///< Name shown in the grid.
    FieldKind kind{FieldKind::Text};
    QStringList options;  ///< Enumerator names (Enum only); the stored value is the name.
    qint64 minimum{0};    ///< Lowest value of an Int or Int64 field.
    qint64 maximum{0};    ///< Highest value of an Int or Int64 field.
    QString toolTip;      ///< One line of help.
};

/**
 * @brief Binds a `qpb::PropertyModel` to an `McDeviceConfig`: every field of the configuration has
 * a property, and every edit is checked with `McDeviceConfig::validate()` (and `fromJson()`) before
 * it is accepted.
 *
 * The tree is built from `toJson()`'s own key names, so a field the library adds shows up as a
 * missing property in the GUI-06 test. An edit that the library rejects is refused by qpb: the old
 * value stays, `rejected()` carries the library's message together with the path the library names
 * (for example "TCP port is 0 (transport.tcp.port)"). An accepted edit updates `config()` and emits
 * `configChanged()`.
 *
 * Choosing another frame family loads that family's defaults (`FrameConfig::frame3C()` and so on)
 * when the current frame values do not fit the new family.
 *
 * @note GUI thread only (qpb rule); holds a value copy of the configuration, never a runner object.
 */
class ConfigBinding : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Builds the tree for @p cfg.
     * @param[in] cfg Initial configuration; need not be valid (an invalid one is shown as it is).
     * @param[in] parent Qt parent.
     */
    explicit ConfigBinding(const mc::McDeviceConfig& cfg = {}, QObject* parent = nullptr);

    /// @brief Destroys the tree.
    ~ConfigBinding() override;

    /// @brief The model to show in a `qpb::PropertyTreeView`.
    /// @return Never null; owned by this object.
    qpb::PropertyModel* model() const noexcept { return m_model; }

    /// @brief The configuration the tree shows right now.
    /// @return The last accepted configuration.
    const mc::McDeviceConfig& config() const noexcept { return m_config; }

    /**
     * @brief Replaces the whole configuration.
     * @param[in] cfg The new configuration.
     * @param[out] message Optional. The library's message and path when @p cfg is refused.
     * @return true when @p cfg passed `validate()` and was loaded; false leaves everything as it was.
     * @note Does not emit `configChanged()`: the caller already knows the value.
     */
    bool setConfig(const mc::McDeviceConfig& cfg, QString* message = nullptr);

    /// @brief Allows or forbids editing from the view; `setConfig()` and code edits still work.
    /// @param[in] editable false greys the whole tree out for the user.
    void setEditable(bool editable);

    /// @brief The message of the last refused edit; empty after the next accepted one.
    /// @return Human-readable text.
    QString lastMessage() const { return m_lastMessage; }

    /// @brief Every bound field, in tree order.
    /// @return The field list; the same on every call.
    static const QVector<FieldInfo>& fields();

signals:
    /// @brief An edit passed validation and `config()` changed.
    /// @param[out] cfg The new configuration (value copy).
    void configChanged(const mc::McDeviceConfig& cfg);

    /// @brief An edit was refused.
    /// @param[out] path Property path of the edit.
    /// @param[out] message The library's message, with the field path it names.
    void rejected(const QString& path, const QString& message);

private:
    struct Candidate;
    Candidate candidate(const QString& path, const QVariant& value) const;
    void syncModel();
    void buildTree();

    qpb::PropertyModel* m_model{nullptr};
    mc::McDeviceConfig m_config;
    QString m_lastMessage;
    bool m_syncing{false};
};

} // namespace mc::workbench
