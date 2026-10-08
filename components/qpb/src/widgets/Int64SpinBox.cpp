#include <QtCore/qlocale.h>
#include <QtWidgets/qlineedit.h>

#include <limits>

#include "Int64SpinBox_p.h"
#include "core/compat_p.h"

namespace qpb::detail {

namespace {

// a + b, saturated at the limits of qint64.
qint64 saturatedAdd(qint64 a, qint64 b)
{
    qint64 result = 0;
    if (!addOverflow(a, b, &result))
        return result;
    return b > 0 ? std::numeric_limits<qint64>::max() : std::numeric_limits<qint64>::min();
}

qint64 saturatedMultiply(qint64 a, qint64 b)
{
    qint64 result = 0;
    if (!mulOverflow(a, b, &result))
        return result;
    return (a > 0) == (b > 0) ? std::numeric_limits<qint64>::max()
                              : std::numeric_limits<qint64>::min();
}

} // namespace

Int64SpinBox::Int64SpinBox(QWidget* parent)
    : QAbstractSpinBox(parent)
{
    connect(this, &QAbstractSpinBox::editingFinished, this, &Int64SpinBox::interpretInput);
    updateText();
}

void Int64SpinBox::setValue(qint64 value)
{
    value = qBound(m_minimum, value, m_maximum);
    const bool changed = value != m_value;
    m_value = value;
    updateText();
    if (changed)
        emit valueChanged(m_value);
}

void Int64SpinBox::setRange(qint64 minimum, qint64 maximum)
{
    m_minimum = minimum;
    m_maximum = qMax(minimum, maximum);
    setValue(m_value);
}

void Int64SpinBox::setSingleStep(qint64 step)
{
    m_step = qMax<qint64>(0, step);
}

void Int64SpinBox::setPrefix(const QString& prefix)
{
    m_prefix = prefix;
    updateText();
}

void Int64SpinBox::setSuffix(const QString& suffix)
{
    m_suffix = suffix;
    updateText();
}

void Int64SpinBox::stepBy(int steps)
{
    interpretInput();
    setValue(saturatedAdd(m_value, saturatedMultiply(m_step, steps)));
    selectAll();
}

QString Int64SpinBox::numberText(const QString& text) const
{
    QString number = text;
    if (!m_prefix.isEmpty() && number.startsWith(m_prefix))
        number.remove(0, m_prefix.size());
    if (!m_suffix.isEmpty() && number.endsWith(m_suffix))
        number.chop(m_suffix.size());
    return number.trimmed();
}

QValidator::State Int64SpinBox::validate(QString& input, int&) const
{
    const QString number = numberText(input);
    if (number.isEmpty() || number == QLatin1String("-") || number == QLatin1String("+"))
        return QValidator::Intermediate;
    bool ok = false;
    const qint64 value = locale().toLongLong(number, &ok);
    if (!ok)
        return QValidator::Invalid;
    return value < m_minimum || value > m_maximum ? QValidator::Intermediate
                                                  : QValidator::Acceptable;
}

void Int64SpinBox::fixup(QString& input) const
{
    bool ok = false;
    const qint64 value = locale().toLongLong(numberText(input), &ok);
    const qint64 fixed = ok ? qBound(m_minimum, value, m_maximum) : m_value;
    input = m_prefix + locale().toString(fixed) + m_suffix;
}

QAbstractSpinBox::StepEnabled Int64SpinBox::stepEnabled() const
{
    if (isReadOnly())
        return StepNone;
    StepEnabled enabled = StepNone;
    if (wrapping() || m_value < m_maximum)
        enabled |= StepUpEnabled;
    if (wrapping() || m_value > m_minimum)
        enabled |= StepDownEnabled;
    return enabled;
}

void Int64SpinBox::updateText()
{
    lineEdit()->setText(m_prefix + locale().toString(m_value) + m_suffix);
}

void Int64SpinBox::interpretInput()
{
    QString text = lineEdit()->text();
    fixup(text);
    bool ok = false;
    const qint64 value = locale().toLongLong(numberText(text), &ok);
    setValue(ok ? value : m_value);
}

} // namespace qpb::detail
