#include <qpb/widgets/EditorFactory.h>

#include <QtCore/qdir.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qfiledialog.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qtoolbutton.h>

#include "PathEdit_p.h"
#include "widgets_p.h"

namespace qpb::detail {

namespace {

PathEdit::DialogProvider& dialogProvider()
{
    static PathEdit::DialogProvider provider;
    return provider;
}

} // namespace

void PathEdit::setDialogProviderForTesting(DialogProvider provider)
{
    dialogProvider() = std::move(provider);
}

PathEdit::PathEdit(Kind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_lineEdit(new QLineEdit(this))
    , m_button(new QToolButton(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_lineEdit);
    layout->addWidget(m_button);

    m_lineEdit->setFrame(false);
    m_button->setText(QString(QChar(0x2026))); // horizontal ellipsis
    m_button->setToolTip(kind == Kind::File ? tr("Choose a file") : tr("Choose a directory"));
    m_button->setFocusPolicy(Qt::NoFocus);
    m_button->setProperty(StylePartProperty, QStringLiteral("browse")); // style sheet selector
    setFocusProxy(m_lineEdit);
    setFocusPolicy(Qt::StrongFocus);

    connect(m_button, &QToolButton::clicked, this, &PathEdit::browse);
}

QString PathEdit::path() const
{
    return QDir::fromNativeSeparators(m_lineEdit->text());
}

void PathEdit::setPath(const QString& path)
{
    m_lineEdit->setText(QDir::toNativeSeparators(path));
}

void PathEdit::browse()
{
    QString chosen;
    {
        EditorDialogScope scope(this);
        chosen = runDialog();
    }
    if (chosen.isEmpty())
        return;
    setPath(chosen);
    m_lineEdit->setFocus();
    EditorFactory::notifyCommit(this);
}

QString PathEdit::runDialog()
{
    if (dialogProvider())
        return dialogProvider()(this);

    const QString current = path();
    const QString start = current.isEmpty() ? m_defaultDir : current;
    if (m_kind == Kind::Directory)
        return QFileDialog::getExistingDirectory(this, tr("Choose a directory"), start);
    if (m_fileMode == FileMode::Save)
        return QFileDialog::getSaveFileName(this, tr("Choose a file"), start, m_filter);
    return QFileDialog::getOpenFileName(this, tr("Choose a file"), start, m_filter);
}

} // namespace qpb::detail
