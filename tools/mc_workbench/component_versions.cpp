#include "mc_workbench/component_versions.h"

#include <mc/version.h>

#include <qpb/qpbcore.h>

#include <QtGlobal>

namespace mc::workbench {

ComponentVersions componentVersions() {
    ComponentVersions versions;
    versions.mc = QStringLiteral(MC_VERSION_STRING);
    versions.qt = QString::fromLatin1(qVersion());
    versions.qpb = QString::fromLatin1(qpb::version());
    return versions;
}

} // namespace mc::workbench
