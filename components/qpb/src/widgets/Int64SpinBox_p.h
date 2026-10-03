#ifndef QPB_INT64SPINBOX_P_H
#define QPB_INT64SPINBOX_P_H

// Internal header, not part of the public API.

#include <qpb/qpbglobal.h>

#include <QtWidgets/qabstractspinbox.h>

namespace qpb::detail {

// Spin box for qint64 values (QSpinBox is limited to int), used as the editor
// of Types::Int64. Mirrors the parts of the QSpinBox API the editor needs.
class QPB_WIDGETS_EXPORT Int64SpinBox : public QAbstractSpinBox
{
    Q_OBJECT

public:
    explicit Int64SpinBox(QWidget* parent = nullptr);

    qint64 value() const
    {
        return m_value;
    }
    void setValue(qint64 value);

    qint64 minimum() const
    {
        return m_minimum;
    }
    qint64 maximum() const
    {
        return m_maximum;
    }
    void setRange(qint64 minimum, qint64 maximum);

    qint64 singleStep() const
    {
        return m_step;
    }
    void setSingleStep(qint64 step);

    QString prefix() const
    {
        return m_prefix;
    }
    void setPrefix(const QString& prefix);
    QString suffix() const
    {
        return m_suffix;
    }
    void setSuffix(const QString& suffix);

    // Parses the text typed so far into the value (bounded). Also done when
    // editing finishes. (QAbstractSpinBox::interpretText() is not virtual.)
    void interpretInput();

    void stepBy(int steps) override;
    QValidator::State validate(QString& input, int& position) const override;
    void fixup(QString& input) const override;

signals:
    void valueChanged(qint64 value);

protected:
    StepEnabled stepEnabled() const override;

private:
    // The number part of text (prefix and suffix removed), trimmed.
    QString numberText(const QString& text) const;
    void updateText();

    qint64 m_value = 0;
    qint64 m_minimum = 0;
    qint64 m_maximum = 99;
    qint64 m_step = 1;
    QString m_prefix;
    QString m_suffix;
};

} // namespace qpb::detail

#endif // QPB_INT64SPINBOX_P_H
