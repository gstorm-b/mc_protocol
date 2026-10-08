#ifndef QPB_ATTRIBUTES_H
#define QPB_ATTRIBUTES_H

#include <qpb/qpbglobal.h>

#include <QtCore/qlist.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

namespace qpb {

// Standard attribute keys (README.md, "Types and attributes").
//
// Attributes are stored per property in a QVariantMap and tune validation,
// display and editors. Keys a type does not know are ignored, so custom types
// may define their own keys (a prefix such as "myapp." is recommended).
namespace Attr {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
// Int, Double: smallest accepted value (int / double). Values are clamped.
inline constexpr QLatin1StringView Minimum {"minimum"};
// Int, Double: largest accepted value (int / double). Values are clamped.
inline constexpr QLatin1StringView Maximum {"maximum"};
// Int, Double: spin box step (int / double).
inline constexpr QLatin1StringView Step {"step"};
// Double: number of decimals (int). Values are rounded to it. Default 2.
inline constexpr QLatin1StringView Decimals {"decimals"};
// Int, Double: text shown before the number (QString).
inline constexpr QLatin1StringView Prefix {"prefix"};
// Int, Double: text shown after the number (QString).
inline constexpr QLatin1StringView Suffix {"suffix"};

// String: maximum number of characters (int).
inline constexpr QLatin1StringView MaxLength {"maxLength"};
// String: placeholder text of the editor (QString).
inline constexpr QLatin1StringView Placeholder {"placeholder"};
// String: pattern the whole value must match (QString, QRegularExpression syntax).
inline constexpr QLatin1StringView RegularExpression {"regularExpression"};
// String: the value may span several lines (bool). Editors use a multi-line
// text field; cells show the lines joined with a pilcrow. Since 1.1.
inline constexpr QLatin1StringView Multiline {"multiline"};

// Enum: the selectable options (QList<qpb::EnumOption>).
inline constexpr QLatin1StringView Options {"options"};

// FilePath: file dialog name filter, e.g. "Images (*.png *.jpg)" (QString).
inline constexpr QLatin1StringView Filter {"filter"};
// FilePath: open or save dialog (int, a qpb::FileMode value). Default FileMode::Open.
inline constexpr QLatin1StringView DialogMode {"dialogMode"};
// FilePath, DirPath: directory the dialog starts in when the value is empty (QString).
inline constexpr QLatin1StringView DefaultDir {"defaultDir"};
// FilePath, DirPath: a non-empty value must name an existing file/directory (bool).
inline constexpr QLatin1StringView MustExist {"mustExist"};

// Any type: ID of the editor to use for this property instead of the editor
// registered for its type (TypeId). See EditorFactory.
inline constexpr QLatin1StringView EditorId {"editorId"};

#else

// Qt 5 (since 1.7): the same constants as QLatin1String, which converts to
// QString like QLatin1StringView does.
inline constexpr QLatin1String Minimum {"minimum", 7};
inline constexpr QLatin1String Maximum {"maximum", 7};
inline constexpr QLatin1String Step {"step", 4};
inline constexpr QLatin1String Decimals {"decimals", 8};
inline constexpr QLatin1String Prefix {"prefix", 6};
inline constexpr QLatin1String Suffix {"suffix", 6};
inline constexpr QLatin1String MaxLength {"maxLength", 9};
inline constexpr QLatin1String Placeholder {"placeholder", 11};
inline constexpr QLatin1String RegularExpression {"regularExpression", 17};
inline constexpr QLatin1String Multiline {"multiline", 9};
inline constexpr QLatin1String Options {"options", 7};
inline constexpr QLatin1String Filter {"filter", 6};
inline constexpr QLatin1String DialogMode {"dialogMode", 10};
inline constexpr QLatin1String DefaultDir {"defaultDir", 10};
inline constexpr QLatin1String MustExist {"mustExist", 9};
inline constexpr QLatin1String EditorId {"editorId", 8};

#endif

} // namespace Attr

// One selectable option of an Enum property.
//
// Aggregate that may gain fields at the end in later versions: assign fields by
// name, or use the two-argument form shown in the examples.
struct EnumOption
{
    QString label; // text shown to the user
    QVariant value; // value stored in the property (int or QString)

    friend bool operator==(const EnumOption& a, const EnumOption& b)
    {
        return a.label == b.label && a.value == b.value;
    }
    friend bool operator!=(const EnumOption& a, const EnumOption& b)
    {
        return !(a == b);
    }
};

// Dialog mode of a FilePath property (value of Attr::DialogMode).
enum class FileMode {
    Open, // pick an existing file
    Save, // pick a file name to write to
};

} // namespace qpb

Q_DECLARE_METATYPE(qpb::EnumOption)

#endif // QPB_ATTRIBUTES_H
