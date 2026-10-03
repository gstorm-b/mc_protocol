#ifndef QPB_WIDGETS_P_H
#define QPB_WIDGETS_P_H

// Internal header, not part of the public API.

#include <qpb/qpbglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QModelIndex;
class QWidget;
QT_END_NAMESPACE

namespace qpb {

class Property;
class PropertyModel;

namespace detail {

// Carries EditorFactory::notifyCommit() to the delegates (and later form
// views) that own the editor.
class CommitNotifier : public QObject
{
    Q_OBJECT

public:
    static CommitNotifier* instance();

signals:
    void commitRequested(QWidget* editor);
};

// Editors opened by a PropertyDelegate carry this dynamic property (the
// delegate's address) so events from their child widgets can be mapped back.
inline constexpr char EditorOwnerProperty[] = "_qpb_editorOwner";

// The (persistent) index an editor was opened for (dynamic property).
inline constexpr char EditorIndexProperty[] = "_qpb_editorIndex";

// Number of EditorDialogScope objects alive for an editor (dynamic property).
inline constexpr char DialogDepthProperty[] = "_qpb_dialogDepth";

// Style sheet selectors (README.md, "Style sheets"; public: never rename).
// QWidget[qpbPart="groupTitle"]: the role of one of the views' widgets.
inline constexpr char StylePartProperty[] = "qpbPart";
// QLabel[qpbModified="true"]: the label of a modified property.
inline constexpr char StyleModifiedProperty[] = "qpbModified";

// True while an EditorDialogScope exists for editor or one of its ancestors.
bool isShowingDialog(const QWidget* editor);

// True for multi-line text widgets, where Enter inserts a line break.
bool isMultilineText(const QObject* object);

// The property behind a (possibly proxied) index, or nullptr.
const Property* propertyOf(const QModelIndex& index);

// True when a user reset of property (a group: of its leaves) would change
// something: modified, not read-only, enabled.
bool isResettableByUser(const Property* property);

// Resets property (a group: its leaves) to the default values as a user edit
// through model->setData(), in one batch, so read-only and disabled
// properties are left alone (D34).
void resetByUser(PropertyModel* model, const Property* property);

// Maps a (possibly proxied) index to its PropertyModel and source index.
// Returns nullptr if no PropertyModel is found at the bottom of the proxy chain.
PropertyModel* propertyModelOf(const QModelIndex& index, QModelIndex* sourceIndex = nullptr);

} // namespace detail
} // namespace qpb

#endif // QPB_WIDGETS_P_H
