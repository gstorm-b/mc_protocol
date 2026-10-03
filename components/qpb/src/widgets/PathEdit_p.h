#ifndef QPB_PATHEDIT_P_H
#define QPB_PATHEDIT_P_H

// Internal header, not part of the public API.

#include <qpb/Attributes.h>
#include <qpb/qpbglobal.h>

#include <QtWidgets/qwidget.h>

#include <functional>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QToolButton;
QT_END_NAMESPACE

namespace qpb::detail {

// Editor for FilePath and DirPath: a line edit plus a "..." button that opens
// a file or directory dialog (README.md, "Types and attributes").
class QPB_WIDGETS_EXPORT PathEdit : public QWidget
{
    Q_OBJECT

public:
    enum class Kind {
        File,
        Directory,
    };

    // Replaces the dialogs in tests: called instead of QFileDialog, returns
    // the chosen path or an empty string for "cancelled". Pass an empty
    // function to restore the real dialogs.
    using DialogProvider = std::function<QString(PathEdit* edit)>;
    static void setDialogProviderForTesting(DialogProvider provider);

    explicit PathEdit(Kind kind, QWidget* parent = nullptr);

    Kind kind() const
    {
        return m_kind;
    }

    QString path() const;
    void setPath(const QString& path);

    void setFileMode(FileMode mode)
    {
        m_fileMode = mode;
    }
    FileMode fileMode() const
    {
        return m_fileMode;
    }
    void setFilter(const QString& filter)
    {
        m_filter = filter;
    }
    QString filter() const
    {
        return m_filter;
    }
    void setDefaultDir(const QString& directory)
    {
        m_defaultDir = directory;
    }
    QString defaultDir() const
    {
        return m_defaultDir;
    }

    QLineEdit* lineEdit() const
    {
        return m_lineEdit;
    }
    QToolButton* browseButton() const
    {
        return m_button;
    }

    // Opens the dialog; on acceptance stores the path and requests a commit.
    void browse();

private:
    QString runDialog();

    Kind m_kind;
    FileMode m_fileMode = FileMode::Open;
    QString m_filter;
    QString m_defaultDir;
    QLineEdit* m_lineEdit;
    QToolButton* m_button;
};

} // namespace qpb::detail

#endif // QPB_PATHEDIT_P_H
