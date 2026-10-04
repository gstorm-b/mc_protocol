/**
 * @file component_versions.h
 * @brief The versions of the components MC Workbench is built from, for its About box.
 */
#pragma once

#include <QString>

namespace mc::workbench {

/// @brief Version strings of the library and the third-party components compiled in.
struct ComponentVersions {
    QString mc;  ///< The mc_protocol library, `MC_VERSION_STRING`.
    QString qt;  ///< The Qt library the program is running against (`qVersion()`).
    QString qpb; ///< The qpb property browser (`qpb::version()`).
};

/**
 * @brief Reads the versions of the components the program is linked with.
 * @return The three version strings; none is empty.
 */
ComponentVersions componentVersions();

} // namespace mc::workbench
