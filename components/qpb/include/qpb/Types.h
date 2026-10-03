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

} // namespace Types

} // namespace qpb

#endif // QPB_TYPES_H
