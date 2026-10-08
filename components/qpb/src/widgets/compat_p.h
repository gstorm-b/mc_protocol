#ifndef QPB_WIDGETS_COMPAT_P_H
#define QPB_WIDGETS_COMPAT_P_H

// The QtGui / QtWidgets API qpb's widgets use that differs between Qt 6 and
// Qt 5.15 (since 1.7); core/compat_p.h has the QtCore part.

#include <QtCore/qglobal.h>
#include <QtGui/qevent.h>
#include <QtWidgets/qformlayout.h>

namespace qpb::detail {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

inline QPoint eventPosition(const QMouseEvent* event)
{
    return event->position().toPoint();
}

inline void setFormRowVisible(QFormLayout* form, QWidget* field, bool visible)
{
    form->setRowVisible(field, visible);
}

#else

inline QPoint eventPosition(const QMouseEvent* event)
{
    return event->pos();
}

// QFormLayout::setRowVisible() is new in Qt 6.4: hide the row's widgets.
inline void setFormRowVisible(QFormLayout* form, QWidget* field, bool visible)
{
    if (QWidget* label = form->labelForField(field))
        label->setVisible(visible);
    field->setVisible(visible);
}

#endif

} // namespace qpb::detail

#endif // QPB_WIDGETS_COMPAT_P_H
