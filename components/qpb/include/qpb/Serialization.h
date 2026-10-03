#ifndef QPB_SERIALIZATION_H
#define QPB_SERIALIZATION_H

#include <qpb/qpbglobal.h>

#include <QtCore/qjsonobject.h>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

namespace qpb {

class PropertyGroup;

// Saving and restoring property values (README.md, "Saving and loading"). Since 1.2.
//
// The functions live in their own namespace: argument-dependent lookup does not
// search it, so they can never make an unqualified call to an application's own
// save(group, settings) ambiguous. Call them qualified:
// qpb::serialization::save(*model.root(), settings).
//
// Only values are stored, never the structure: the tree is built by the
// application, then values are written into it. Read-only and (since 1.3) live
// properties are neither written nor read, since the application maintains them. Values are
// restored with Property::setValue(), so they are converted and validated like
// any value set by the application. To get one PropertyModel::batchValueChanged
// signal instead of many, call these between beginBatch() and endBatch().

namespace serialization {

// The values of group's descendants: every group becomes a nested object keyed
// by its id, every other property its value (TypeHandler::toJson). Groups with
// nothing to store (empty, or read-only properties only) are left out.
QPB_CORE_EXPORT QJsonObject toJson(const PropertyGroup& group);

// Sets the values found in json on group's descendants. Keys without a
// matching property are ignored. Returns false if a value was rejected
// (the property keeps its value); the other values are still applied.
QPB_CORE_EXPORT bool fromJson(PropertyGroup& group, const QJsonObject& json);

// The values of group's descendants in settings, under the settings' current
// group, with keys that are paths relative to group ("General/language").
QPB_CORE_EXPORT void save(const PropertyGroup& group, QSettings& settings);

// Sets the values stored in settings (see save()) on group's descendants.
// Missing keys leave the property unchanged. Returns false if a value was
// rejected; the other values are still applied.
QPB_CORE_EXPORT bool load(PropertyGroup& group, const QSettings& settings);

} // namespace serialization
} // namespace qpb

#endif // QPB_SERIALIZATION_H
