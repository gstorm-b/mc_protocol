/**
 * @file capture_types.h
 * @brief The value types of the capture feature that cross between a runner thread and the GUI
 * thread: settings, status, save request and save result.
 *
 * Plain values only; registered as Qt meta types (`registerCaptureMetaTypes()`, called by
 * `registerRunnerMetaTypes()`).
 */
#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace mc::workbench {

/// @brief What a capture was taken from. Decides where it may be exported.
enum class CaptureSource : quint8 {
    RealPlc,    ///< A real PLC: may be exported as replay test data (`tests/vectors/captured/`).
    MockPlc,    ///< A mock PLC of the GUI (or any other mock): a user folder only.
    VirtualPlc  ///< `examples/virtual_plc`: a user folder only.
};

/// @brief The `source:` word of a capture record and the `capture_source` key of `run.meta`.
/// @param[in] source The source.
/// @return "plc", "mock" or "virtual_plc".
QString captureSourceName(CaptureSource source);

/// @brief Settings of one capture.
struct CaptureSettings {
    QString profileId{QStringLiteral("gui-capture")}; ///< Folder-safe name of the capture folder.
    CaptureSource source{CaptureSource::MockPlc};     ///< What is being captured.
    QString note;                                     ///< Free text for `run.meta` (operator note).
    quint32 maxChunks{200000};                        ///< Recording stops (full) after this many chunks.
    quint64 maxBytes{64ull * 1024 * 1024};            ///< ... or this many bytes.
};

/// @brief What a runner's capture holds right now.
struct CaptureStatus {
    bool active{false};   ///< Recording.
    bool hasData{false};  ///< Something is recorded and can be saved.
    bool full{false};     ///< Recording stopped because a limit was reached.
    quint64 chunks{0};    ///< Chunks recorded.
    quint64 bytes{0};     ///< Bytes recorded.
    QString profileId;    ///< The capture's profile id.
    CaptureSource source{CaptureSource::MockPlc}; ///< The capture's source.
};

/// @brief A request to write the recorded capture to disk.
struct CaptureSaveRequest {
    QString outputRoot;   ///< The capture goes to `<outputRoot>/<profile id>/`.
    QString capturedRoot; ///< `tests/vectors/captured/` of the repository; protected (may be empty).
    bool overwrite{false}; ///< Replace an existing folder of the same name.
};

/// @brief How a save ended (the files are written on a short-lived worker thread).
struct CaptureSaveResult {
    quint64 token{0};     ///< The token of the save command.
    bool ok{false};       ///< The files were written.
    QString message;      ///< Why not; or a one-line summary on success.
    QString folder;       ///< The capture folder.
    QStringList files;    ///< File names written ("run.meta", "steps.vec", "session.vec").
    int records{0};       ///< Exchanges written to steps.vec.
};

/// @brief Registers the types of this header as Qt meta types; safe to call repeatedly.
void registerCaptureMetaTypes();

} // namespace mc::workbench

Q_DECLARE_METATYPE(mc::workbench::CaptureSettings)
Q_DECLARE_METATYPE(mc::workbench::CaptureStatus)
Q_DECLARE_METATYPE(mc::workbench::CaptureSaveRequest)
Q_DECLARE_METATYPE(mc::workbench::CaptureSaveResult)
