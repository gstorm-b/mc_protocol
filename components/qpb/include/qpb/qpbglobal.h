#ifndef QPB_QPBGLOBAL_H
#define QPB_QPBGLOBAL_H

#include <qpb/qpbversion.h>

#include <QtCore/qglobal.h>

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

// Packs a version into one integer so it can be compared in #if directives:
//   #if QPB_VERSION >= QPB_VERSION_CHECK(1, 1, 0)
#define QPB_VERSION_CHECK(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))

// Version of the qpb headers being compiled against.
#define QPB_VERSION QPB_VERSION_CHECK(QPB_VERSION_MAJOR, QPB_VERSION_MINOR, QPB_VERSION_PATCH)

// ---------------------------------------------------------------------------
// Symbol export
// ---------------------------------------------------------------------------

#if defined(QPB_STATIC)
#    define QPB_CORE_EXPORT
#    define QPB_WIDGETS_EXPORT
#else
#    if defined(QPB_BUILD_CORE_LIB)
#        define QPB_CORE_EXPORT Q_DECL_EXPORT
#    else
#        define QPB_CORE_EXPORT Q_DECL_IMPORT
#    endif
#    if defined(QPB_BUILD_WIDGETS_LIB)
#        define QPB_WIDGETS_EXPORT Q_DECL_EXPORT
#    else
#        define QPB_WIDGETS_EXPORT Q_DECL_IMPORT
#    endif
#endif

// ---------------------------------------------------------------------------
// Deprecation
// ---------------------------------------------------------------------------
//
// Superseded API is kept until the end of the major version and marked with
// QPB_DEPRECATED / QPB_DEPRECATED_X("use X instead"). Every deprecated
// declaration is additionally wrapped in
//
//   #if !defined(QPB_DISABLE_DEPRECATED)
//   ...
//   #endif
//
// so consumers can define QPB_DISABLE_DEPRECATED to turn remaining uses of
// deprecated API into compile errors.

#define QPB_DEPRECATED [[deprecated]]
#define QPB_DEPRECATED_X(text) [[deprecated(text)]]

namespace qpb {

// Version of the qpb library linked at run time, e.g. "1.2.0".
// Compare with QPB_VERSION_STR to detect header/library mismatches.
QPB_CORE_EXPORT const char* version() noexcept;

} // namespace qpb

#endif // QPB_QPBGLOBAL_H
