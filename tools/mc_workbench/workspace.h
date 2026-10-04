/**
 * @file workspace.h
 * @brief The workspace of MC Workbench as a value, and its JSON file (SPEC-gui-tool.md,
 * "Workspace"): the set of tabs with their configurations, the HIL view inputs and the dock layout.
 *
 * No widgets here: the window turns itself into a `Workspace` and back (`WorkspaceController`),
 * this file only knows the data and the file format. The reader is strict like the `hil-capture`
 * loaders: a key nobody reads is an error, and every problem carries its JSON path.
 */
#pragma once

#include "mc/device/mc_device_config.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace mc::workbench {

/// @brief The value of the `format` key of every workspace file.
inline constexpr const char* kWorkspaceFormat = "mc-workbench-workspace";

/// @brief The `version` this build writes and reads.
inline constexpr int kWorkspaceVersion = 1;

/// @brief Largest workspace file the loader reads (bytes); a bigger file is refused unread.
inline constexpr qint64 kWorkspaceMaxBytes = 16 * 1024 * 1024;

/// @brief Most points of one memory preset.
inline constexpr int kMaxPresetPoints = 65536;

/// @brief Most tabs of one kind in a workspace file.
inline constexpr int kMaxWorkspaceTabs = 64;

/// @brief A block of the memory image of a mock that is written back whenever the mock is built.
struct MemoryPreset {
    QString head;             ///< Head device text, in the numbering of the mock's frame ("D100").
    bool bits{false};         ///< true: `values` are 0 or 1 per bit; false: one word each.
    QVector<quint16> values;  ///< The points, in device order.

    /// @brief Memberwise equality.
    /// @param[in] other The other preset.
    /// @return true when head, kind and values are equal.
    bool operator==(const MemoryPreset& other) const {
        return head == other.head && bits == other.bits && values == other.values;
    }
};

/// @brief One device tab: its name and the `McDeviceConfig` (never validated here, see the parser).
struct WorkspaceDevice {
    QString name;               ///< Tab title.
    mc::McDeviceConfig config;  ///< The configuration, as `McDeviceConfig` JSON in the file.
};

/// @brief One mock tab: its name, the settings grid and its memory presets.
struct WorkspaceMock {
    QString name;                    ///< Tab title.
    QJsonObject settings;            ///< The values of the settings grid (qpb tree, nested by group).
    QVector<MemoryPreset> presets;   ///< Memory written whenever the mock is built.
};

/// @brief The inputs of the HIL runner view. Nothing here is a secret; no state of a run.
struct WorkspaceHil {
    QString profile;             ///< Profile file.
    QString plan;                ///< Plan file.
    QString groups;              ///< Step groups to run, comma separated; empty: every step.
    QString plcState{QStringLiteral("RUN")}; ///< "RUN" or "STOP".
    QString outputRoot;          ///< Folder that receives `<profile id>/`.
    QString source{QStringLiteral("mock")};  ///< "plc", "mock" or "virtual_plc" (the capture source).
    QString note;                ///< Operator note for `run.meta`.
    int benchReps{1};            ///< Repetitions of every bench step.
    bool overwrite{false};       ///< Replace an existing capture folder.
};

/// @brief The window arrangement.
struct WorkspaceLayout {
    QByteArray windowGeometry;  ///< `QWidget::saveGeometry()`; empty when not saved.
    QByteArray dockState;       ///< `ads::CDockManager::saveState()`; empty when not saved.
    int currentDevice{-1};      ///< Index of the current device tab; -1: not saved.
    int currentMock{-1};        ///< Index of the current mock tab; -1: not saved.
};

/// @brief Everything a workspace file holds.
struct Workspace {
    QVector<WorkspaceDevice> devices;  ///< Device tabs, in tab order.
    QVector<WorkspaceMock> mocks;      ///< Mock tabs, in tab order.
    bool hasHil{false};                ///< Whether the file has a `hil` section.
    WorkspaceHil hil;                  ///< The HIL view inputs (meaningful when `hasHil`).
    bool hasLayout{false};             ///< Whether the file has a `layout` section.
    WorkspaceLayout layout;            ///< The dock layout (meaningful when `hasLayout`).
};

/// @brief One thing wrong with a file: where, and what.
struct WorkspaceProblem {
    QString path;     ///< JSON path of the offending key, e.g. "devices[1].config.frame.timeoutMS".
    QString message;  ///< What is wrong, e.g. "unknown key".

    /// @brief "path: message", or the bare message when there is no path.
    /// @return One line.
    QString text() const {
        return path.isEmpty() ? message : path + QStringLiteral(": ") + message;
    }
};

/**
 * @brief Writes a workspace as a JSON object.
 *
 * Device configurations are written with `McDeviceConfig::toJson()`.
 *
 * @param[in] workspace The workspace.
 * @return The object (keys `format`, `version`, `devices`, `mocks`, `hil`, `layout`).
 */
QJsonObject workspaceToJson(const Workspace& workspace);

/**
 * @brief Writes a workspace as the bytes of its file (indented JSON, UTF-8).
 * @param[in] workspace The workspace.
 * @return The file content.
 */
QByteArray workspaceToBytes(const Workspace& workspace);

/**
 * @brief Reads a workspace from a JSON object, strictly.
 *
 * Every problem is collected, not only the first: a wrong type, a value out of range, a missing
 * `format`, a `version` this build does not know, and every key nobody reads ("unknown key", with
 * its path, also inside a device configuration, where `McDeviceConfig::fromJson()` itself would
 * ignore it). A device configuration must pass `McDeviceConfig::validate()` as well.
 *
 * @param[in] json The object.
 * @param[out] out The workspace; left empty when @p problems is not.
 * @param[out] problems Everything found; empty when the file is good.
 * @return true when the object is a good workspace.
 */
bool workspaceFromJson(const QJsonObject& json, Workspace& out, QVector<WorkspaceProblem>& problems);

/**
 * @brief Parses the bytes of a workspace file.
 * @param[in] bytes The file content.
 * @param[out] out The workspace; left empty on failure.
 * @param[out] problems Why the file is refused; empty on success.
 * @return true on success.
 */
bool workspaceFromBytes(const QByteArray& bytes, Workspace& out, QVector<WorkspaceProblem>& problems);

/**
 * @brief The problems as text for a message box: one line each, at most @p maxLines of them.
 * @param[in] problems The problems.
 * @param[in] maxLines Lines shown; the rest is summarised ("... and N more").
 * @return Multi-line plain text; empty when there are none.
 */
QString workspaceProblemsText(const QVector<WorkspaceProblem>& problems, int maxLines = 12);

/**
 * @brief The keys of @p json that the JSON of a default `McDeviceConfig` does not have.
 *
 * `McDeviceConfig::fromJson()` ignores unknown keys; a workspace must not (a misspelled
 * `timeoutMS` would silently leave the default in a configuration that talks to a PLC).
 *
 * @param[in] json A configuration object.
 * @param[in] basePath Path of @p json inside the file, e.g. "devices[0].config".
 * @return One problem per unknown key, with its full path.
 */
QVector<WorkspaceProblem> unknownConfigKeys(const QJsonObject& json, const QString& basePath);

} // namespace mc::workbench

Q_DECLARE_METATYPE(mc::workbench::Workspace)
Q_DECLARE_METATYPE(mc::workbench::WorkspaceProblem)
