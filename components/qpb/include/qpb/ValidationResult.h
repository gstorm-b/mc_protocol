#ifndef QPB_VALIDATIONRESULT_H
#define QPB_VALIDATIONRESULT_H

#include <qpb/qpbglobal.h>

#include <QtCore/qstring.h>

#include <utility>

namespace qpb {

// Outcome of validating a candidate property value (README.md, "Values and the value pipeline").
//
// Aggregate that may gain fields at the end in later versions; create it with
// valid() / error() rather than positional initialization.
struct ValidationResult
{
    bool ok = true;
    QString message; // human-readable reason when !ok

    static ValidationResult valid()
    {
        return {};
    }
    static ValidationResult error(QString message)
    {
        return {false, std::move(message)};
    }

    explicit operator bool() const
    {
        return ok;
    }
};

} // namespace qpb

#endif // QPB_VALIDATIONRESULT_H
