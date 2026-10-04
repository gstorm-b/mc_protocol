#include "mc_workbench/capture_controller.h"

#include "hil_capture/profile.h"
#include "mc_workbench/capture_builder.h"
#include "mc_workbench/capture_export.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace mc::workbench {

void CaptureController::setTarget(const mc::McDeviceConfig& device, bool localTarget) {
    m_device = device;
    m_local = localTarget;
}

bool CaptureController::start(const CaptureSettings& settings, QString* error) {
    const auto refuse = [&](const QString& why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    if (!mc::hil::folderSafe(settings.profileId)) {
        return refuse(QStringLiteral("'%1' is not a folder name (letters, digits, - _ . ; at most "
                                     "80 characters)")
                          .arg(settings.profileId));
    }
    if (settings.source == CaptureSource::RealPlc && m_local) {
        return refuse(QStringLiteral("this link goes to this computer or to a mock, not to a real "
                                     "PLC: choose the source 'mock PLC' or 'virtual_plc'"));
    }
    if (settings.maxChunks == 0 || settings.maxBytes == 0) {
        return refuse(QStringLiteral("the capture limits must be above zero"));
    }
    discard();
    m_settings = settings;
    m_captureDevice = m_device;
    m_status = CaptureStatus{};
    m_status.active = true;
    m_status.profileId = settings.profileId;
    m_status.source = settings.source;
    touch();
    return true;
}

void CaptureController::stop() {
    if (m_status.active) {
        m_status.active = false;
        m_status.hasData = !m_chunks.isEmpty();
        touch();
    }
}

void CaptureController::discard() {
    m_chunks.clear();
    m_chunks.squeeze();
    m_status = CaptureStatus{};
    touch();
}

void CaptureController::add(const FrameRecord& record) {
    if (!m_status.active) {
        return;
    }
    if (m_status.chunks >= m_settings.maxChunks ||
        m_status.bytes + static_cast<quint64>(record.bytes.size()) > m_settings.maxBytes) {
        m_status.active = false;
        m_status.full = true;
        m_status.hasData = !m_chunks.isEmpty();
        touch();
        return;
    }
    FrameRecord copy;
    copy.tNs = record.tNs;
    copy.tx = record.tx;
    copy.bytes = record.bytes; // the decoder's text is not part of a capture
    m_chunks.push_back(std::move(copy));
    ++m_status.chunks;
    m_status.bytes += static_cast<quint64>(record.bytes.size());
    m_status.hasData = true;
    touch();
}

CaptureController::SaveJob CaptureController::prepareSave(quint64 token,
                                                          const CaptureSaveRequest& request,
                                                          QString* refusal) {
    const auto refuse = [&](const QString& why) {
        if (refusal != nullptr) {
            *refusal = why;
        }
        return SaveJob();
    };
    if (m_status.active) {
        return refuse(QStringLiteral("stop the capture before saving it"));
    }
    if (!m_status.hasData || m_chunks.isEmpty()) {
        return refuse(QStringLiteral("nothing is captured"));
    }
    // The rule is applied to the folder the capture is written to, not to the root: a profile id
    // such as "captured" turns the root `tests/vectors` into the protected folder.
    if (!request.outputRoot.trimmed().isEmpty()) {
        const QString inputProblem = checkOutputInputs(request.outputRoot, m_settings.profileId);
        if (!inputProblem.isEmpty()) {
            return refuse(inputProblem);
        }
    }
    const QString folder = QDir(request.outputRoot).absoluteFilePath(m_settings.profileId);
    const ExportDecision decision =
        checkOutputTarget(request.outputRoot.trimmed().isEmpty() ? QString() : folder, m_settings.source,
                          request.capturedRoot);
    if (!decision.allowed) {
        return refuse(decision.reason);
    }
    if (QFileInfo::exists(folder) && !request.overwrite) {
        return refuse(QStringLiteral("%1 already exists; saving would replace it").arg(folder));
    }

    // The job owns copies (QVector shares its data, the controller does not change it while it
    // is not recording, and a new start() detaches).
    const QVector<FrameRecord> chunks = m_chunks;
    const CaptureSettings settings = m_settings;
    const mc::McDeviceConfig device = m_captureDevice;
    const bool truncated = m_status.full;
    const QString outputRoot = request.outputRoot;
    return [=]() {
        CaptureSaveResult result;
        result.token = token;
        result.folder = QDir(outputRoot).absoluteFilePath(settings.profileId);
        try {
            const BuiltCapture built = buildCapture(chunks, settings, device, truncated);
            QString error;
            if (!writeCapture(built, outputRoot, &error, &result.files)) {
                result.message = error;
                return result;
            }
            result.ok = true;
            result.records = static_cast<int>(built.steps.size());
            result.message = QStringLiteral("%1 exchanges (%2 re-encodable, %3 literal) written to %4")
                                 .arg(built.steps.size())
                                 .arg(built.apiRecords)
                                 .arg(built.rawRecords)
                                 .arg(result.folder);
        } catch (const std::exception& e) {
            result.ok = false;
            result.message = QString::fromUtf8(e.what());
        }
        return result;
    };
}

} // namespace mc::workbench
