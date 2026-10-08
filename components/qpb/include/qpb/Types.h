#ifndef QPB_TYPES_H
#define QPB_TYPES_H

#include <qpb/qpbglobal.h>

#include <QtCore/qstring.h>

namespace qpb {

// Logical type of a property (README.md, "Types and attributes").
//
// A type is identified by a string rather than by a QMetaType because several
// logical types share one storage type: String, FilePath and DirPath all store
// a QString. Types registered by applications use IDs of their choosing; a
// prefix such as "myapp.color" is recommended. IDs starting with "qpb." and the
// IDs below are reserved.
using TypeId = QString;

// Built-in type IDs. The value types are registered automatically in
// TypeRegistry::global() (and their editors in EditorFactory::global()).
namespace Types {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
// bool
inline constexpr QLatin1StringView Bool {"bool"};
// int. See Attr::Minimum, Maximum, Step, Prefix, Suffix.
inline constexpr QLatin1StringView Int {"int"};
// double. See Attr::Minimum, Maximum, Step, Decimals, Prefix, Suffix.
inline constexpr QLatin1StringView Double {"double"};
// QString. See Attr::MaxLength, Placeholder, RegularExpression.
inline constexpr QLatin1StringView String {"string"};
// One value out of Attr::Options; the stored value is the option's value
// (int or QString).
inline constexpr QLatin1StringView Enum {"enum"};
// QString holding a file path. See Attr::Filter, DialogMode, DefaultDir, MustExist.
inline constexpr QLatin1StringView FilePath {"filepath"};
// QString holding a directory path. See Attr::DefaultDir, MustExist.
inline constexpr QLatin1StringView DirPath {"dirpath"};
// A PropertyGroup. Groups have no value.
inline constexpr QLatin1StringView Group {"group"};
// qint64. See Attr::Minimum, Maximum, Step, Prefix, Suffix (qint64 values).
// Since 1.2; registered after the seven types of 1.0.
inline constexpr QLatin1StringView Int64 {"int64"};

#else

// Qt 5 (since 1.7): the same constants as QLatin1String, which converts to
// QString like QLatin1StringView does.
inline constexpr QLatin1String Bool {"bool", 4};
inline constexpr QLatin1String Int {"int", 3};
inline constexpr QLatin1String Double {"double", 6};
inline constexpr QLatin1String String {"string", 6};
inline constexpr QLatin1String Enum {"enum", 4};
inline constexpr QLatin1String FilePath {"filepath", 8};
inline constexpr QLatin1String DirPath {"dirpath", 7};
inline constexpr QLatin1String Group {"group", 5};
inline constexpr QLatin1String Int64 {"int64", 5};

#endif

} // namespace Types

} // namespace qpb

#endif // QPB_TYPES_H
