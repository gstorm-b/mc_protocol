/**
 * @file capture_builder.h
 * @brief Turns the wire chunks of a tab into the capture files of `hil-capture` (`steps.vec`,
 * `session.vec`, `run.meta`), written with `mc::hil::CaptureWriter` so that `mc_replay_tests` reads
 * them unchanged.
 */
#pragma once

#include "hil_capture/capture_writer.h"
#include "mc/device/mc_device_config.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/runner_types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace mc::workbench {

/// @brief A capture ready to be written.
struct BuiltCapture {
    QString profileId;                      ///< Capture folder name.
    QVector<mc::hil::StepRecord> steps;     ///< One record per request chunk.
    mc::hil::RunMeta meta;                  ///< Content of `run.meta`.
    int apiRecords{0};                      ///< Records that replay re-encodes from their metadata.
    int rawRecords{0};                      ///< Records kept as literal frames (`via: raw`).
};

/**
 * @brief Pairs the chunks into request / answer exchanges and builds the records.
 *
 * Each tx chunk starts an exchange; the rx chunks up to the next tx chunk are its answer. A read
 * request the mock can decode becomes a `via: api` record that replay re-encodes from its
 * metadata. Everything else (a write, which a GUI session cannot read back as a plan would; an EOT;
 * bytes that are no request) is kept as a literal frame, `via: raw`, `op: Raw`. An answer cut short
 * by the next request is `response-partial`; a request with no answer is `timeout` (`noResponse`
 * for a literal frame), except the last request of the recording, which is left out when nothing
 * came back (the recording ended, nobody timed out). Rx chunks before the first request are counted
 * in `run.meta` (`stray_rx`).
 * `device_end` is the highest point each device type was successfully accessed at.
 *
 * @param[in] chunks The recorded chunks, oldest first; tx is the request direction.
 * @param[in] settings The capture's settings.
 * @param[in] device The link's configuration (frame, session, transport; the address is scrubbed).
 * @param[in] truncated The recording stopped because a limit was reached.
 * @return The records and the run metadata.
 */
BuiltCapture buildCapture(const QVector<FrameRecord>& chunks, const CaptureSettings& settings,
                          const mc::McDeviceConfig& device, bool truncated);

/**
 * @brief Writes @p capture to `<outputRoot>/<profile id>/`, replacing that folder.
 * @param[in] capture What to write.
 * @param[in] outputRoot The folder the capture folder is created in.
 * @param[out] error What went wrong.
 * @param[out] files The names of the files written.
 * @return true when all three files were written.
 */
bool writeCapture(const BuiltCapture& capture, const QString& outputRoot, QString* error,
                  QStringList* files);

} // namespace mc::workbench
