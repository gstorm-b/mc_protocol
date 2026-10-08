# qpb - property browser for Qt 6 Widgets

qpb shows and edits a tree of typed properties ("name | value") in Qt Widgets applications: object
inspectors, settings dialogs, plugin and device configuration. You describe the properties in code; qpb provides the
model, the views and the editors, validation, reset to default, change notification, saving and loading.

This folder is the complete library. Copy it into your project, add two lines of CMake, and build. Updating means
replacing the folder: within a major version (1.x) your code and CMake never need to change (see
[Compatibility](#compatibility)).

Contents: [Requirements](#requirements) - [Integration](#integration) - [Features](#features) -
[Quick start](#quick-start) - [API](#api) - [Developer guide](#developer-guide) - [Style sheets](#style-sheets) -
[Qt 5](#qt-5) - [Compatibility](#compatibility) - [Version history](#version-history)

## Requirements

- C++17 or newer, Qt 6.5 or newer or (since 1.7) Qt 5.15 (Core, Widgets), CMake 3.21 or newer.
- Any compiler and platform supported by that Qt (MSVC, GCC, Clang; Windows, Linux, macOS); see [Qt 5](#qt-5) for
  the compilers Qt 5.15 accepts.

## Integration

Put this folder in your project, for example as `components/qpb/`, and add:

```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
add_subdirectory(components/qpb)
target_link_libraries(my_app PRIVATE qpb::widgets)   # or qpb::core without widgets
```

```cpp
#include <qpb/qpb.h>       // everything; <qpb/qpbcore.h> for qpb::core only
```

| Target | Contains | Depends on |
|---|---|---|
| `qpb::core` | properties, model, types, validation, serialization, QObject source, filter proxy | Qt6::Core |
| `qpb::widgets` | tree view, form view, delegate, editors | `qpb::core`, Qt6::Widgets |

- Static libraries by default. `-DQPB_BUILD_SHARED=ON` builds shared ones; then deploy the qpb libraries next to your
  executable.
- The folder changes none of your project's CMake settings (standard, flags, output directories, AUTOMOC are set on
  qpb's own targets only). If your project has already found Qt (6 or 5), that Qt is used; otherwise Qt 6 is preferred to Qt 5.
- To update: delete the folder, put the new version in its place, rebuild, and read the new version's *Upgrade notes*
  in `CHANGELOG.md`. Never extract a new version over the old folder: files removed by the new version would remain.
- The version of the folder is in `VERSION`, at compile time in `QPB_VERSION` and at run time from `qpb::version()`.

## Features

- **Property tree**: groups and properties with an id, a path (`"Transform/x"`), a display name, a tool tip, a value
  and a default value. Built in code with fluent builders.
- **Types**: bool, int, 64-bit int, double, string (single or multiple lines), enum, file path, directory path, and
  your own types (e.g. `QColor`) through a type registry.
- **Attributes**: ranges, steps, decimals, prefix and suffix, maximum length, placeholder, regular expression, file
  filter, dialog mode, default directory, "must exist", enum options.
- **Validation**: conversion, normalization (clamping, rounding) and type checks, plus a validator per property.
  Rejected values keep the old value and report a message.
- **State**: read-only, disabled, hidden and live (a value maintained by the application), inherited by children.
  Conditions enable or show a property depending on another one's value.
- **Model**: a two-column `QAbstractItemModel` for any Qt view; change signals per value and per batch, callbacks per
  path; properties can be added and removed while the model is shown; reset of one property, a group or everything.
- **Views**: `PropertyTreeView` (collapsible tree or flat list with section headers) and `PropertyFormView` (a form
  with persistent editors and collapsible sections). Bold names for modified values, "Reset to default" context menu,
  full keyboard use (Enter, Esc, Tab), validation messages as tool tips.
- **Editors**: one per type, replaceable per type or per property; custom editors may open dialogs.
- **Search**: a filter proxy model by display name for both views.
- **QObject properties**: show the `Q_PROPERTY`s of your objects and keep both sides in sync.
- **Saving**: values to and from JSON and `QSettings`.
- **Theming**: Qt style sheets, with hooks for what qpb paints itself.

## Quick start

```cpp
#include <qpb/qpb.h>
#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    auto root = qpb::PropertyGroup::create("Camera");
    root->addString("name", "Main camera");
    root->addBool("visible", true);
    auto& transform = root->addGroup("Transform");
    transform.addDouble("x", 0.0).range(-100.0, 100.0).suffix(" m");
    transform.addInt("fov", 60).range(10, 170).displayName("Field of view");
    root->addEnum("projection", {"Perspective", "Orthographic"}, 0);
    root->addFilePath("lut", {}).filter("LUT files (*.cube)");

    qpb::PropertyModel model(std::move(root));
    model.onValueChanged("Transform/fov", &app, [](const QVariant& v) { qDebug() << "fov" << v; });

    qpb::PropertyTreeView view;   // or qpb::PropertyFormView
    view.setModel(&model);
    view.show();
    return app.exec();
}
```

## API

All names are in namespace `qpb`. Header per class: `<qpb/Name.h>`, widgets in `<qpb/widgets/Name.h>`.

### Types and attributes (`Types.h`, `Attributes.h`)

| Type id (`Types::...`) | Stored as | Builder | Default editor |
|---|---|---|---|
| `Bool` | `bool` | `addBool` | check box (in the tree: the cell's check indicator) |
| `Int` | `int` | `addInt` -> `IntBuilder` | spin box |
| `Int64` | `qint64` | `addInt64` -> `Int64Builder` | 64-bit spin box |
| `Double` | `double` | `addDouble` -> `DoubleBuilder` | double spin box |
| `String` | `QString` | `addString` -> `StringBuilder` | line edit; multi-line text edit with `multiline()` |
| `Enum` | `int` index, or the option's value | `addEnum` -> `EnumBuilder` | combo box |
| `FilePath`, `DirPath` | `QString` | `addFilePath`, `addDirPath` | line edit with a "..." button opening a dialog |
| `Group` | - | `addGroup` | - |

`TypeId` is a `QString`; your own types use their own ids. Attribute keys are in `Attr::` (`Minimum`, `Maximum`,
`Step`, `Decimals`, `Prefix`, `Suffix`, `MaxLength`, `Placeholder`, `RegularExpression`, `Multiline`, `Options`,
`Filter`, `DialogMode`, `DefaultDir`, `MustExist`, `EditorId`). Enum options are `EnumOption { label, value }`;
`addEnum(id, QStringList labels, index)` stores the index, `addEnum(id, QList<EnumOption>, value)` stores the value.

### Properties (`Property.h`, `PropertyGroup.h`, `PropertyBuilders.h`)

- `PropertyGroup::create(id)` makes a root. `add*()` add children and return a builder (or the group for
  `addGroup`). `add(typeId, id, value)` and `add(std::unique_ptr<Property>)` add any type. `remove(id)`, `child(id)`,
  `child(index)`, `children()`, `childCount()`, `indexOf()`, `find("A/b")`.
- Builders (all chainable): `displayName`, `toolTip`, `readOnly`, `enabled`, `visible`, `live`, `validator`,
  `attribute`, `editor(editorId)`, `enabledWhen`, `visibleWhen`, plus the typed ones (`range`, `minimum`, `maximum`,
  `step`, `decimals`, `prefix`, `suffix`, `maxLength`, `multiline`, `placeholder`, `regularExpression`, `filter`,
  `dialogMode`, `defaultDir`, `mustExist`). A builder converts to `Property&`.
- `Property`: `id()`, `path()`, `parent()`, `isGroup()`, `toGroup()`, `typeId()`, `displayName()` / `toolTip()` and
  setters, `value()`, `setValue()`, `defaultValue()`, `setDefaultValue()`, `isModified()`, `resetToDefault()`,
  `attribute()` / `setAttribute()` / `removeAttribute()`, `flags()` / `setFlag()` (`ReadOnly`, `Disabled`, `Hidden`,
  `Live`), `isReadOnly()`, `isEnabled()`, `isVisible()`, `isLive()` (effective: own flag and ancestors), `setValidator()`,
  `setEnabledWhen()` / `setVisibleWhen()` / `clear...When()`.
- `Property::create(typeId, id, value)` makes a detached property of any type, to attach with `add()`.

### Model (`PropertyModel.h`)

- `PropertyModel(std::unique_ptr<PropertyGroup> root)`; `setRoot()` replaces the tree; `root()`, `find(path)`,
  `propertyAt(index)`, `indexOf(property, column)`.
- Columns `NameColumn`, `ValueColumn`. Roles `PropertyRole`, `TypeIdRole`, `PathRole`, `IsGroupRole`,
  `IsModifiedRole`, `AttributesRole`, `IsVisibleRole`; your own roles start at `PropertyModel::UserRole`.
- `setValue(path, value)` (application code), `setData()` (views: refuses read-only and disabled properties),
  `resetToDefault(index)`, `resetAllToDefault()`.
- `beginBatch()` / `endBatch()` (nestable): `batchValueChanged(paths)` once at the end.
- Signals `valueChanged(path, new, old)`, `validationFailed(path, rejected, message)`, `batchValueChanged(paths)`.
- `onValueChanged(path, context, handler)`: a callback for one path, or for a group and its descendants (the handler
  then also gets the path). Ends with `context`; follows the path, so it survives `setRoot()`.

### Other core classes

- `ValidationResult`: `ValidationResult::valid()`, `ValidationResult::error(message)`, `ok`, `message`.
- `TypeRegistry::global()`: `registerType(id, handler)`, `registerType<T>(id, handler)` (storage type `T`),
  `replaceType()`, `handler()`, `contains()`, `types()`. `TypeHandler` fields (all optional): `storageType`,
  `displayText`, `normalize`, `validate`, `toJson`, `fromJson`.
- `PropertyFilterProxyModel`: filters by display name (`setFilterFixedString()`), case-insensitive; a group is shown
  when a descendant matches, and all its content when its own name matches.
- `QObjectPropertySource(model)`: `addObject(object, parentGroup, id)`, `removeObject()`, `groupOf()`, `objects()`,
  `refresh()`, `setTitleProperty()`, `setLiveReadOnlyProperties()`.
- `qpb::serialization`: `toJson(group)`, `fromJson(group, json)`, `save(group, settings)`, `load(group, settings)`.
- `qpbglobal.h`: `QPB_VERSION`, `QPB_VERSION_CHECK(major, minor, patch)`, `QPB_VERSION_STR`, `qpb::version()`,
  `QPB_DEPRECATED_X(text)`.

### Widgets

- `PropertyTreeView` (a `QTreeView`): `setModel()` (a `PropertyModel` or a proxy of one), `setMode(Mode::Tree |
  Mode::List)`, `setNameColumnWidth()`, `propertyDelegate()`, `setTabStopsOnCheckBoxes()`, colour properties for style
  sheets (`groupBackground`, `groupForeground`, `modifiedForeground`, `readOnlyForeground`).
- `PropertyFormView` (a `QScrollArea`): `setModel()`, `model()`, `editor(path)`, `isExpanded(groupPath)`,
  `setExpanded()`.
- `PropertyDelegate`: the delegate both views use; set it on any `QAbstractItemView` showing a `PropertyModel`.
- `EditorFactory::global()`: `registerEditor(id, handler)`, `replaceEditor()`, `handler()`, `handlerFor(property)`,
  `createEditor()`, `contains()`, `editors()`, `notifyCommit(editor)`. `EditorHandler` fields: `createEditor`,
  `setEditorData`, `editorData` (required), `applyAttributes`, `paint` (optional).
- `EditorDialogScope`: keeps an editor open while it shows a dialog.

## Developer guide

### Values and the value pipeline

Every change, from a view or from code, goes through the same steps: conversion to the type's storage type,
normalization (e.g. clamping to the range), the type's validation, the property's validator. A rejected value keeps
the old one and emits `validationFailed` (views show the message as a tool tip). An equal value changes nothing and
emits nothing. Read-only and disabled only stop the user: application code (`setValue`, `resetToDefault`) can still
change such properties.

```cpp
root->addString("name", "Main").validator([&scene](const QVariant& v, const qpb::Property&) {
    return scene.contains(v.toString()) ? qpb::ValidationResult::error("Name already used")
                                        : qpb::ValidationResult::valid();
});
```

### Reacting to changes

Prefer `onValueChanged()` over comparing paths in `valueChanged`. Wrap bulk updates in `beginBatch()` /
`endBatch()` to get one `batchValueChanged`:

```cpp
model.onValueChanged("Transform/x", this, [this](const QVariant& v) { m_object.x = v.toDouble(); });
model.onValueChanged("Output", this, [](const QString& path, const QVariant& v) { /* any child */ });
```

### Dependencies between properties

```cpp
general.addBool("autosave", true);
general.addInt("autosaveMinutes", 5).enabledWhen("General/autosave");              // true / non-empty
camera.addDouble("orthoScale", 1.0).visibleWhen("Camera/projection", 1);            // equals a value
limits.addInt64("quota", 0).enabledWhen("Limits/mode", [](const QVariant& v) { return v != "unlimited"; });
```

Conditions are evaluated by the model and combined with the property's own flags. A condition whose source path does
not exist (or a property outside a model) counts as met.

### Live values

A property maintained by the application (a counter, a status) should be `live()`: it is never shown as modified,
group resets skip it, and serialization ignores it.

### Saving and loading

```cpp
qpb::serialization::save(*model.root(), settings);   // keys are paths: "General/language"
model.beginBatch();
qpb::serialization::load(*model.root(), settings);   // values go through validation
model.endBatch();
```

Only values are stored; the application builds the tree first. Read-only and live properties are neither written nor
read. Call the functions qualified (`qpb::serialization::save`).

### Your own type

```cpp
qpb::TypeHandler color;
color.displayText = [](const QVariant& v, const qpb::Property&) { return v.value<QColor>().name(); };
color.toJson = [](const QVariant& v, const qpb::Property&) { return QJsonValue(v.value<QColor>().name()); };
qpb::TypeRegistry::global().registerType<QColor>("color", color);
root->add("color", "tint", QColor(Qt::red));
```

Give it an editor with `EditorFactory::global().registerEditor("color", handler)`. To use another editor for one
property of a built-in type, register it under its own id and set it with the builder's `editor("slider")`.

### Custom editors

`createEditor` builds the widget, `setEditorData` shows a value, `editorData` returns the entered value,
`applyAttributes` applies ranges and the like, `paint` draws the cell when not editing. Call
`EditorFactory::notifyCommit(editor)` when the user has picked a value (no need to wait for focus out). While an
editor shows a dialog, keep a `qpb::EditorDialogScope scope(this);` alive so the view does not close the editor.

### QObjects

```cpp
class Light : public QObject {
    Q_OBJECT
    Q_CLASSINFO("qpb:title", "name")                         // group title follows this Q_PROPERTY
    Q_CLASSINFO("qpb:intensity", "min=0;max=100;suffix= %")
    Q_CLASSINFO("qpb:used", "live")
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    ...
};
qpb::QObjectPropertySource source(&model);
source.addObject(&light);
```

Class info keys per property: `type`, `displayName`, `toolTip`, `readOnly`, `hidden`, `disabled`, `exclude`, `live`,
`enabledWhen`, `visibleWhen`, and any attribute (`min`, `max` stand for minimum and maximum); `qpb:properties` lists
and orders the properties. Changes of the object are read back when the `Q_PROPERTY` has a NOTIFY signal (otherwise
call `refresh()`). A destroyed object's group is removed.

### Views and keyboard

- Tree view: Enter commits, Esc cancels, Tab / Shift+Tab move to the next / previous editable value (skipping groups,
  read-only and hidden rows, and check boxes unless `setTabStopsOnCheckBoxes(true)`); Space toggles a check box. The
  context menu offers "Reset to default" and "Reset group".
- Form view: editors stay open and commit on focus out, on Enter (Ctrl+Enter in multi-line text) or at once for check
  boxes, combo boxes and paths; Esc restores the model's value.
- Both work through a proxy model, e.g. `PropertyFilterProxyModel` for a search box.

### Threads

Use the model, the views and the registries from the GUI thread.

## Style sheets

Standard selectors cover the views and every editor: `qpb--PropertyTreeView`, `qpb--PropertyFormView`, `::item`
(`:hover`, `:selected`, `:alternate`), `QHeaderView::section`, and the editors by their Qt classes
(`qpb--PropertyTreeView QLineEdit` for editors in cells, `qpb--PropertyFormView QLineEdit` in the form). For what qpb
paints or builds itself:

**Tree view**: colour properties, settable with `qproperty-`. Invalid values keep the defaults; a matching `::item`
rule with a `color` or `background` takes precedence. Fonts are fixed (group and modified names stay bold).

| Property | Default |
|---|---|
| `groupBackground` (brush) | the palette's Button |
| `groupForeground` | the item text colour |
| `modifiedForeground` | the item text colour |
| `readOnlyForeground` | the palette's PlaceholderText |

**Form view and path editors**: dynamic properties on their widgets.

| Widget | Selector |
|---|---|
| a group's section, title button, body | `QWidget[qpbPart="group"]`, `QToolButton[qpbPart="groupTitle"]`, `QWidget[qpbPart="groupBody"]` |
| a property's label | `QLabel[qpbPart="label"]`, and `[qpbModified="true"]` while modified |
| read-only text of a type without editor | `QLabel[qpbPart="value"]` |
| "..." button of file and directory editors | `QToolButton[qpbPart="browse"]` |

```css
qpb--PropertyTreeView { qproperty-groupBackground: #2c3038; qproperty-modifiedForeground: #e87c00; }
QToolButton[qpbPart="groupTitle"], QToolButton[qpbPart="groupTitle"]:checked {
    background: #2c3038; color: #e87c00; border: none; border-bottom: 1px solid #e87c00;
}
QLabel[qpbPart="label"][qpbModified="true"] { color: #e87c00; }
```

Notes: the section title is a checkable tool button (checked while expanded), so cover `:checked` if your sheet styles
checked tool buttons. `qproperty-` values stay when a sheet is removed; set them back to invalid values when switching
to a theme without them. A `::branch` rule replaces the tree's expand arrows and then needs `image:` for `:closed` and
`:open`. Only these property names and values are part of the API, not the class names of internal widgets.

## Qt 5

Since 1.7 the folder also builds with Qt 5.15. CMake uses the Qt your project has already found (Qt 6 first), and
otherwise looks for Qt 6, then Qt 5.15. Everything in this README applies to both, with two differences where Qt 5
has no equivalent:

- `Types::*` and `Attr::*` are `QLatin1String` constants (with Qt 6: `QLatin1StringView`). Both convert to `QString`,
  so code that uses them as type ids or attribute keys is the same.
- `TypeHandler::storageType` is an `int` type id, `QMetaType::UnknownType` by default (with Qt 6: a `QMetaType`).
  `registerType<T>(id, handler)` sets it in both; with Qt 5 you can also write `handler.storageType =
  qMetaTypeId<T>();`.

Compilers: Qt 5.15's own headers do not compile with MSVC 2026 (they use `stdext::checked_array_iterator`, which its
standard library no longer has). Use MSVC 2019 or 2022, GCC or Clang with Qt 5.15.

The compatibility promise below holds for each Qt major: an application built with Qt 5 keeps building with Qt 5 when
the folder is updated, and the same for Qt 6.

## Compatibility

Within 1.x, updating the folder never requires changes to your code or CMake (with the same Qt major):

- Nothing public is removed or changed, only added. Enum values and model roles keep their numbers; new ones are
  appended.
- Superseded API is marked deprecated (`QPB_DEPRECATED_X`) and kept until 2.0; define `QPB_DISABLE_DEPRECATED` to find
  remaining uses at compile time.
- Documented behaviour and defaults stay; new behaviour is opt-in.
- The minimum C++, Qt and CMake versions only rise in a new major version.
- Free functions are in nested namespaces (`qpb::serialization`), so they never clash with your own unqualified calls.
- Configuration structs (`TypeHandler`, `EditorHandler`, `EnumOption`, `ValidationResult`) may gain fields at the end:
  assign fields by name, never with positional `{a, b, c}` initialization.

## Version history

Details and upgrade notes for every version: `CHANGELOG.md`.

- **1.7.0** - Qt 5.15 support (see [Qt 5](#qt-5)); Qt 6 builds unchanged. Fixed: MSVC 2019 warning C4267 when
  building the library.
- **1.6.1** - Documentation only: this README; the changelog and header comments refer to it instead of documents
  outside the folder.
- **1.6.0** - Theming with style sheets: tree view colour properties (`groupBackground`, `groupForeground`,
  `modifiedForeground`, `readOnlyForeground`) and form view selectors (`qpbPart`, `qpbModified`). Fixed: modified
  labels of the form view lost their bold font under application style sheets.
- **1.5.0** - `PropertyModel::resetAllToDefault()`; `PropertyTreeView::setTabStopsOnCheckBoxes()` (off by default).
  Fixed: MSVC warning C4458 when building the library.
- **1.4.0** - Conditions between properties (`enabledWhen`, `visibleWhen`, also as `QObjectPropertySource` class
  info); `PropertyModel::onValueChanged()` callbacks per path.
- **1.3.0** - Live properties (`Property::Flag::Live`, `live()`); `QObjectPropertySource` group titles from a
  `Q_PROPERTY` and live read-only values (`setLiveReadOnlyProperties()`).
- **1.2.0** - `QObjectPropertySource`; `qpb::serialization` (JSON, `QSettings`); the 64-bit integer type
  (`Types::Int64`, `addInt64()`).
- **1.1.0** - `PropertyFormView`; `PropertyFilterProxyModel`; multi-line strings (`multiline()`).
- **1.0.0** - First stable release: property tree and builders, seven built-in types and custom types, validation,
  `PropertyModel`, `PropertyTreeView` (tree and list modes), `PropertyDelegate`, `EditorFactory` and custom editors,
  reset to default, the component folder. Read-only and disabled only restrict the user; application code can still
  set such values (changed during the release candidates).
