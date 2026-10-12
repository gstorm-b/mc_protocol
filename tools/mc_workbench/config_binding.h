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
 * a property, and every edit is checked with `fromJson()` and `McDeviceConfig::validate()`.
 *
 * The tree is built from `toJson()`'s own key names, so a field the library adds shows up as a
 * missing property in the GUI-06 test. A malformed edit (wrong type, out of the field's own range:
 * what `fromJson()` refuses) is refused by qpb: the old value stays, `rejected()` carries the
 * library's message. An edit that parses but fails `validate()` as a whole (Serial selected with an
 * empty port name) is kept as a **draft**: `draft()` holds it, the grid shows it, the failing field
 * is marked (`draftWhere()`), `draftChanged()` is emitted, and `config()` keeps the last valid
 * configuration. An edit that makes the draft valid updates `config()` and emits `configChanged()`.
 *
 * The failing field is marked with what qpb offers (it has no invalid state): the property gets the
 * attribute `invalidAttribute()` carrying the message, its tool tip becomes the message, and its
 * display name gets the suffix " (!)".
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

    /// @brief The last configuration that passed `validate()`; the only one a runner may get.
    /// @return The last valid configuration.
    const mc::McDeviceConfig& config() const noexcept { return m_config; }

    /// @brief The configuration the tree shows right now (equal to `config()` while the draft is valid).
    /// @return The draft.
    const mc::McDeviceConfig& draft() const noexcept { return m_draft; }

    /// @brief Whether the draft passes `validate()`.
    /// @return true when `draft()` equals `config()`.
    bool draftValid() const noexcept { return m_draftValid; }

    /// @brief The library's message for the draft, with the field path it names.
    /// @return Human-readable text; empty while the draft is valid.
    QString draftMessage() const { return m_draftMessage; }

    /// @brief The property path of the field `validate()` names for the draft.
    /// @return For example "transport/serial/portName"; empty while the draft is valid or when the
    /// library's path matches no field.
    QString draftWhere() const { return m_draftWhere; }

    /// @brief Property attribute set on the field that fails validation; its value is the message.
    static QString invalidAttribute();

    /**
     * @brief Replaces the whole configuration.
     * @param[in] cfg The new configuration. One that fails `validate()` is loaded as a draft.
     * @param[out] message Optional. The library's message and path when @p cfg fails `validate()`.
     * @return true when @p cfg passed `validate()` (then `config()` equals it); false when it was
     * loaded as a draft only (`config()` keeps the last valid one).
     * @note Does not emit `configChanged()` or `draftChanged()`: the caller already knows the value.
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
    /// @brief An edit made the draft valid and `config()` changed.
    /// @param[out] cfg The new configuration (value copy).
    void configChanged(const mc::McDeviceConfig& cfg);

    /// @brief An edit changed the draft: it became invalid, stayed invalid with another message, or
    /// became valid again. Emitted after `configChanged()` when both apply.
    void draftChanged();

    /// @brief A malformed edit was refused.
    /// @param[out] path Property path of the edit.
    /// @param[out] message The library's message, with the field path it names.
    void rejected(const QString& path, const QString& message);

private:
    struct Candidate;
    Candidate candidate(const QString& path, const QVariant& value) const;
    void syncModel();
    void buildTree();
    void applyDraftMark();

    qpb::PropertyModel* m_model{nullptr};
    mc::McDeviceConfig m_config;
    mc::McDeviceConfig m_draft;
    bool m_draftValid{true};
    QString m_draftMessage;
    QString m_draftWhere;
    QString m_markedPath;
    QString m_lastMessage;
    bool m_syncing{false};
};

} // namespace mc::workbench
