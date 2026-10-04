#include "mc_workbench/capture_export.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace mc::workbench {

namespace {

struct Resolved {
    bool ok{false};
    QString path;   // absolute, forward slashes, lower case, no trailing slash
    QString reason; // why not, when !ok
};

// What the operating system says an existing folder is: links, junctions, 8.3 names and case
// resolved. Empty on failure.
QString osResolve(const QString& existing) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(existing);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return QString();
    }
    QString out;
    QVector<wchar_t> buffer(1024);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const DWORD n = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0) {
            break;
        }
        if (n < static_cast<DWORD>(buffer.size())) {
            out = QString::fromWCharArray(buffer.data(), static_cast<int>(n));
            break;
        }
        buffer.resize(static_cast<int>(n) + 1);
    }
    CloseHandle(handle);
    if (out.startsWith(QLatin1String("\\\\?\\UNC\\"))) {
        out = QLatin1String("\\\\") + out.mid(8);
    } else if (out.startsWith(QLatin1String("\\\\?\\"))) {
        out = out.mid(4);
    }
    return out;
#else
    return QFileInfo(existing).canonicalFilePath();
#endif
}

bool isReservedDeviceName(const QString& segment) {
    // CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension.
    const QString base = segment.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    static const QStringList fixed = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                                      QStringLiteral("NUL"), QStringLiteral("CONIN$"), QStringLiteral("CONOUT$")};
    if (fixed.contains(base)) {
        return true;
    }
    return base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))) &&
           base.at(3).isDigit() && base.at(3) != QLatin1Char('0');
}

// A segment that does not exist yet, as Windows would create it: trailing dots and spaces are
// dropped. A segment that cannot be given a plain meaning is refused (empty, `:`, `~`, wildcards,
// control characters, reserved device names).
bool normaliseNewSegment(const QString& segment, QString* out, QString* why) {
    QString s = segment;
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' '))) {
        s.chop(1);
    }
    if (s.isEmpty()) {
        *why = QStringLiteral("the name \"%1\" is empty once trailing dots and spaces are dropped").arg(segment);
        return false;
    }
    static const QString bad = QStringLiteral(":~*?\"<>|");
    for (const QChar c : s) {
        if (bad.contains(c) || c.unicode() < 32) {
            *why = QStringLiteral("the name \"%1\" holds a character that is not a plain file name").arg(segment);
            return false;
        }
    }
    if (isReservedDeviceName(s)) {
        *why = QStringLiteral("the name \"%1\" is a reserved device name").arg(segment);
        return false;
    }
    *out = s;
    return true;
}

// The folder as the operating system will see it: the deepest existing ancestor resolved through the
// OS, the rest normalised as Windows would. Refuses when unsure.
Resolved resolve(const QString& input) {
    Resolved r;
    const QString trimmed = input; // not trimmed: a trailing space is part of the name (Windows drops it)
    if (input.trimmed().isEmpty()) {
        r.reason = QStringLiteral("no folder given");
        return r;
    }
    if (trimmed.startsWith(QLatin1String("\\\\?\\")) || trimmed.startsWith(QLatin1String("\\\\.\\")) ||
        trimmed.startsWith(QLatin1String("//?/")) || trimmed.startsWith(QLatin1String("//./"))) {
        r.reason = QStringLiteral("a device or extended-length path (\\\\?\\ or \\\\.\\) is not accepted");
        return r;
    }
    const QString absolute = QDir::cleanPath(QDir(trimmed).absolutePath());
    QStringList tail;
    QString head = absolute;
    while (!head.isEmpty() && !QFileInfo::exists(head)) {
        const QString parent = QFileInfo(head).absolutePath();
        if (parent == head) {
            break;
        }
        tail.prepend(QFileInfo(head).fileName());
        head = parent;
    }
    if (head.isEmpty() || !QFileInfo::exists(head)) {
        r.reason = QStringLiteral("no part of %1 exists, so it cannot be resolved").arg(input);
        return r;
    }
    const QString resolvedHead = osResolve(head);
    if (resolvedHead.isEmpty()) {
        r.reason = QStringLiteral("the operating system could not resolve %1").arg(head);
        return r;
    }
    QString out = QDir::cleanPath(resolvedHead);
    for (const QString& part : tail) {
        QString normal;
        QString why;
        if (!normaliseNewSegment(part, &normal, &why)) {
            r.reason = why;
            return r;
        }
        out += QLatin1Char('/') + normal;
    }
    r.path = QDir::cleanPath(out).toLower();
    r.ok = true;
    return r;
}

bool isInside(const QString& child, const QString& parent) {
    return child == parent || child.startsWith(parent + QLatin1Char('/'));
}

bool hasCapturedSegments(const QString& path) {
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i] == QLatin1String("vectors") && parts[i + 1] == QLatin1String("captured")) {
            return true;
        }
    }
    return false;
}

bool endsInDotOrSpace(const QString& path) {
    QString p = path;
    while (p.endsWith(QLatin1Char('/')) || p.endsWith(QLatin1Char('\\'))) {
        p.chop(1);
    }
    const QString last = p.section(QRegularExpression(QStringLiteral("[/\\\\]")), -1);
    if (last == QLatin1String(".") || last == QLatin1String("..")) {
        return false;
    }
    return last.endsWith(QLatin1Char('.')) || last.endsWith(QLatin1Char(' '));
}

} // namespace

QString checkOutputInputs(const QString& outputRoot, const QString& profileId) {
    if (endsInDotOrSpace(profileId)) {
        return QStringLiteral("the profile id \"%1\" ends in a dot or a space; Windows would drop it. Rename the profile.")
            .arg(profileId);
    }
    if (endsInDotOrSpace(outputRoot)) {
        return QStringLiteral("the output folder \"%1\" ends in a dot or a space; Windows would drop it. Choose another folder.")
            .arg(outputRoot);
    }
    return QString();
}

ExportDecision checkOutputTarget(const QString& outputRoot, CaptureSource source,
                                 const QString& capturedRoot) {
    ExportDecision decision;
    if (outputRoot.trimmed().isEmpty()) {
        decision.reason = QStringLiteral("no output folder given");
        return decision;
    }
    const Resolved target = resolve(outputRoot);
    if (!target.ok) {
        decision.reason = QStringLiteral("The output folder cannot be checked, so nothing is written: %1.")
                              .arg(target.reason);
        return decision;
    }
    bool protectedPlace = hasCapturedSegments(target.path);
    if (!capturedRoot.trimmed().isEmpty()) {
        const Resolved root = resolve(capturedRoot);
        if (!root.ok) {
            decision.reason = QStringLiteral("The protected folder tests/vectors/captured cannot be checked, so "
                                             "nothing is written: %1.")
                                  .arg(root.reason);
            return decision;
        }
        if (isInside(target.path, root.path)) {
            protectedPlace = true;
        }
    }
    if (protectedPlace && source != CaptureSource::RealPlc) {
        decision.reason =
            QStringLiteral("%1 may not be written under tests/vectors/captured/: that folder takes "
                           "captures of a real PLC only. Choose another folder.")
                .arg(source == CaptureSource::MockPlc ? QStringLiteral("A capture of a mock PLC")
                                                       : QStringLiteral("A capture of virtual_plc"));
        return decision;
    }
    decision.allowed = true;
    return decision;
}

bool isLoopbackHost(const QString& host) {
    QString text = host.trimmed();
    // "[::1]" (the URL spelling of an IPv6 literal) and a fully qualified "localhost." name too.
    if (text.size() > 2 && text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']'))) {
        text = text.mid(1, text.size() - 2);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    if (text.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    QHostAddress address;
    if (address.setAddress(text)) {
        return address.isLoopback();
    }
    return false;
}

QString findCapturedRoot(const QString& startDir) {
    QDir dir(startDir);
    for (int depth = 0; depth < 12; ++depth) {
        const QString candidate = dir.absoluteFilePath(QStringLiteral("tests/vectors/captured"));
        if (QFileInfo(candidate).isDir()) {
            return QDir::cleanPath(candidate);
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

} // namespace mc::workbench
