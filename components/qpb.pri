# qpb.pri -- builds the vendored qpb 1.6.1 (components/qpb, MIT, unmodified) through qmake.
#
# qpb ships CMake files only; this wrapper sits OUTSIDE the vendored folder (which is replaced
# wholesale on an update, see components/qpb/README.md) and lists qpb's sources. The sources are
# compiled into the including project as a static part (QPB_STATIC), with the Qt modules widgets
# needs. After a qpb update, compare the source lists below with the SOURCES of
# components/qpb/CMakeLists.txt. qpbversion.h, which CMake generates from the VERSION file, is
# written under the including project's build folder at qmake time.
!defined(MC_QPB_PRI_INCLUDED, var) {
MC_QPB_PRI_INCLUDED = 1

QT += core widgets
CONFIG += c++17
DEFINES += QPB_STATIC

QPB_DIR = $$PWD/qpb

# qpbversion.h from VERSION ("MAJOR.MINOR.PATCH" with an optional "-suffix"), as
# cmake/qpbversion.h.in does.
QPB_VERSION_STRING = $$cat($$QPB_DIR/VERSION, singleline)
QPB_VERSION_NUMBERS = $$section(QPB_VERSION_STRING, -, 0, 0)
QPB_GENERATED_DIR = $$OUT_PWD/qpb_generated
QPB_VERSION_HEADER = \
    "$${LITERAL_HASH}ifndef QPB_QPBVERSION_H" \
    "$${LITERAL_HASH}define QPB_QPBVERSION_H" \
    "$${LITERAL_HASH}define QPB_VERSION_MAJOR $$section(QPB_VERSION_NUMBERS, ., 0, 0)" \
    "$${LITERAL_HASH}define QPB_VERSION_MINOR $$section(QPB_VERSION_NUMBERS, ., 1, 1)" \
    "$${LITERAL_HASH}define QPB_VERSION_PATCH $$section(QPB_VERSION_NUMBERS, ., 2, 2)" \
    "$${LITERAL_HASH}define QPB_VERSION_STR \"$$QPB_VERSION_STRING\"" \
    "$${LITERAL_HASH}endif"
write_file($$QPB_GENERATED_DIR/qpb/qpbversion.h, QPB_VERSION_HEADER)

INCLUDEPATH += $$QPB_DIR/include $$QPB_GENERATED_DIR $$QPB_DIR/src

HEADERS += \
    $$QPB_DIR/include/qpb/Attributes.h \
    $$QPB_DIR/include/qpb/Property.h \
    $$QPB_DIR/include/qpb/PropertyBuilders.h \
    $$QPB_DIR/include/qpb/PropertyFilterProxyModel.h \
    $$QPB_DIR/include/qpb/PropertyGroup.h \
    $$QPB_DIR/include/qpb/PropertyModel.h \
    $$QPB_DIR/include/qpb/QObjectPropertySource.h \
    $$QPB_DIR/include/qpb/Serialization.h \
    $$QPB_DIR/include/qpb/TypeRegistry.h \
    $$QPB_DIR/include/qpb/Types.h \
    $$QPB_DIR/include/qpb/ValidationResult.h \
    $$QPB_DIR/include/qpb/qpbcore.h \
    $$QPB_DIR/include/qpb/qpbglobal.h \
    $$QPB_DIR/src/core/QObjectPropertySource_p.h \
    $$QPB_DIR/src/core/Property_p.h \
    $$QPB_DIR/include/qpb/widgets/EditorFactory.h \
    $$QPB_DIR/include/qpb/widgets/PropertyDelegate.h \
    $$QPB_DIR/include/qpb/widgets/PropertyFormView.h \
    $$QPB_DIR/include/qpb/widgets/PropertyTreeView.h \
    $$QPB_DIR/include/qpb/qpb.h \
    $$QPB_DIR/src/widgets/Int64SpinBox_p.h \
    $$QPB_DIR/src/widgets/PathEdit_p.h \
    $$QPB_DIR/src/widgets/widgets_p.h

SOURCES += \
    $$QPB_DIR/src/core/Property.cpp \
    $$QPB_DIR/src/core/PropertyBuilders.cpp \
    $$QPB_DIR/src/core/PropertyFilterProxyModel.cpp \
    $$QPB_DIR/src/core/PropertyGroup.cpp \
    $$QPB_DIR/src/core/PropertyModel.cpp \
    $$QPB_DIR/src/core/QObjectPropertySource.cpp \
    $$QPB_DIR/src/core/Serialization.cpp \
    $$QPB_DIR/src/core/TypeRegistry.cpp \
    $$QPB_DIR/src/core/qpbglobal.cpp \
    $$QPB_DIR/src/widgets/EditorFactory.cpp \
    $$QPB_DIR/src/widgets/Int64SpinBox.cpp \
    $$QPB_DIR/src/widgets/PathEdit.cpp \
    $$QPB_DIR/src/widgets/PropertyDelegate.cpp \
    $$QPB_DIR/src/widgets/PropertyFormView.cpp \
    $$QPB_DIR/src/widgets/PropertyTreeView.cpp \
    $$QPB_DIR/src/widgets/widgets_p.cpp

}
