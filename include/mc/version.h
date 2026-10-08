/**
 * @file version.h
 * @brief Compiled-in library version: the `MC_VERSION_*` macros and mc::version().
 */
#pragma once

/// Major version component. Bumped for a non-additive change to `include/mc/` once 1.0 is tagged.
#define MC_VERSION_MAJOR 0
/// Minor version component.
#define MC_VERSION_MINOR 1
/// Patch version component.
#define MC_VERSION_PATCH 1
/// Dotted "MAJOR.MINOR.PATCH" form of the three macros above; must always agree with them.
#define MC_VERSION_STRING "0.1.1"

/**
 * @namespace mc
 * @brief Public namespace of the mc_protocol library.
 *
 * Everything a consumer links against lives flat in `mc`; internals live in `mc::detail`.
 */
namespace mc {

/**
 * @struct Version
 * @brief The three numeric components of the compiled-in library version.
 *
 * Value type, trivially copyable, no invariants beyond what version() produces.
 */
struct Version {
    int major; ///< Major version component.
    int minor; ///< Minor version component.
    int patch; ///< Patch version component.
};

/**
 * @brief Returns the compiled-in library version, for a consumer to log at startup.
 *
 * @return The version this library was built as, taken from the `MC_VERSION_*` macros.
 * @par Complexity
 * O(1); no allocation.
 */
constexpr Version version() noexcept {
    return {MC_VERSION_MAJOR, MC_VERSION_MINOR, MC_VERSION_PATCH};
}

} // namespace mc
