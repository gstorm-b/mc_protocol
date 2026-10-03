#include <qpb/PropertyBuilders.h>

namespace qpb {

BoolBuilder::BoolBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

// --- Int -----------------------------------------------------------------------

IntBuilder::IntBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

IntBuilder& IntBuilder::range(int minimum, int maximum)
{
    return this->minimum(minimum).maximum(maximum);
}

IntBuilder& IntBuilder::minimum(int minimum)
{
    return attribute(Attr::Minimum, minimum);
}

IntBuilder& IntBuilder::maximum(int maximum)
{
    return attribute(Attr::Maximum, maximum);
}

IntBuilder& IntBuilder::step(int step)
{
    return attribute(Attr::Step, step);
}

IntBuilder& IntBuilder::prefix(const QString& prefix)
{
    return attribute(Attr::Prefix, prefix);
}

IntBuilder& IntBuilder::suffix(const QString& suffix)
{
    return attribute(Attr::Suffix, suffix);
}

// --- Int64 ---------------------------------------------------------------------

Int64Builder::Int64Builder(Property& property)
    : PropertyBuilderBase(property)
{ }

Int64Builder& Int64Builder::range(qint64 minimum, qint64 maximum)
{
    return this->minimum(minimum).maximum(maximum);
}

Int64Builder& Int64Builder::minimum(qint64 minimum)
{
    return attribute(Attr::Minimum, minimum);
}

Int64Builder& Int64Builder::maximum(qint64 maximum)
{
    return attribute(Attr::Maximum, maximum);
}

Int64Builder& Int64Builder::step(qint64 step)
{
    return attribute(Attr::Step, step);
}

Int64Builder& Int64Builder::prefix(const QString& prefix)
{
    return attribute(Attr::Prefix, prefix);
}

Int64Builder& Int64Builder::suffix(const QString& suffix)
{
    return attribute(Attr::Suffix, suffix);
}

// --- Double --------------------------------------------------------------------

DoubleBuilder::DoubleBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

DoubleBuilder& DoubleBuilder::range(double minimum, double maximum)
{
    return this->minimum(minimum).maximum(maximum);
}

DoubleBuilder& DoubleBuilder::minimum(double minimum)
{
    return attribute(Attr::Minimum, minimum);
}

DoubleBuilder& DoubleBuilder::maximum(double maximum)
{
    return attribute(Attr::Maximum, maximum);
}

DoubleBuilder& DoubleBuilder::step(double step)
{
    return attribute(Attr::Step, step);
}

DoubleBuilder& DoubleBuilder::decimals(int decimals)
{
    return attribute(Attr::Decimals, decimals);
}

DoubleBuilder& DoubleBuilder::prefix(const QString& prefix)
{
    return attribute(Attr::Prefix, prefix);
}

DoubleBuilder& DoubleBuilder::suffix(const QString& suffix)
{
    return attribute(Attr::Suffix, suffix);
}

// --- String --------------------------------------------------------------------

StringBuilder::StringBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

StringBuilder& StringBuilder::maxLength(int length)
{
    return attribute(Attr::MaxLength, length);
}

StringBuilder& StringBuilder::placeholder(const QString& text)
{
    return attribute(Attr::Placeholder, text);
}

StringBuilder& StringBuilder::regularExpression(const QString& pattern)
{
    return attribute(Attr::RegularExpression, pattern);
}

StringBuilder& StringBuilder::multiline(bool multiline)
{
    return attribute(Attr::Multiline, multiline);
}

// --- Enum ----------------------------------------------------------------------

EnumBuilder::EnumBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

// --- FilePath ------------------------------------------------------------------

FilePathBuilder::FilePathBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

FilePathBuilder& FilePathBuilder::filter(const QString& filter)
{
    return attribute(Attr::Filter, filter);
}

FilePathBuilder& FilePathBuilder::dialogMode(FileMode mode)
{
    return attribute(Attr::DialogMode, int(mode));
}

FilePathBuilder& FilePathBuilder::defaultDir(const QString& directory)
{
    return attribute(Attr::DefaultDir, directory);
}

FilePathBuilder& FilePathBuilder::mustExist(bool mustExist)
{
    return attribute(Attr::MustExist, mustExist);
}

// --- DirPath -------------------------------------------------------------------

DirPathBuilder::DirPathBuilder(Property& property)
    : PropertyBuilderBase(property)
{ }

DirPathBuilder& DirPathBuilder::defaultDir(const QString& directory)
{
    return attribute(Attr::DefaultDir, directory);
}

DirPathBuilder& DirPathBuilder::mustExist(bool mustExist)
{
    return attribute(Attr::MustExist, mustExist);
}

} // namespace qpb
