# qpb - property browser for Qt 6 Widgets.
#
# This folder is a self-contained component. Consuming projects copy it (e.g. to
# components/qpb/) and use it with:
#
#   add_subdirectory(components/qpb)
#   target_link_libraries(my_app PRIVATE qpb::widgets)
#
# Rules for this file: never touch global variables or flags,
# configure only qpb's own targets, and never refer to CMAKE_SOURCE_DIR.

cmake_minimum_required(VERSION 3.21)

# VERSION holds "MAJOR.MINOR.PATCH" with an optional pre-release suffix such as
# "-rc1". project() only accepts the numeric part.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" QPB_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${QPB_VERSION_STRING}" QPB_VERSION_STRING)
if(NOT QPB_VERSION_STRING MATCHES "^(([0-9]+)\\.([0-9]+)\\.([0-9]+))(-[0-9A-Za-z.]+)?$")
    message(FATAL_ERROR "qpb: invalid VERSION \"${QPB_VERSION_STRING}\"")
endif()
set(QPB_VERSION "${CMAKE_MATCH_1}")
set(QPB_VERSION_MAJOR "${CMAKE_MATCH_2}")
set(QPB_VERSION_MINOR "${CMAKE_MATCH_3}")
set(QPB_VERSION_PATCH "${CMAKE_MATCH_4}")
# No VERSION here: project(VERSION) in a subdirectory would set the host's
# CMAKE_PROJECT_VERSION cache entry when the host project has no version.
project(qpb LANGUAGES CXX)
# Re-run CMake when VERSION changes (e.g. after replacing the component folder).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/VERSION")

option(QPB_BUILD_SHARED "Build qpb as shared libraries instead of static ones" OFF)

set(QPB_MIN_QT6_VERSION 6.5)
set(QPB_MIN_QT5_VERSION 5.15) # since 1.7

# Reuse the host project's Qt if it has already been found (Qt 6 before Qt 5);
# otherwise find Qt 6 here, or Qt 5 when there is no Qt 6.
if(TARGET Qt6::Widgets)
    set(QPB_QT_MAJOR 6)
elseif(TARGET Qt5::Widgets)
    set(QPB_QT_MAJOR 5)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
    set(QPB_QT_MAJOR ${QT_VERSION_MAJOR})
    find_package(Qt${QPB_QT_MAJOR} ${QPB_MIN_QT${QPB_QT_MAJOR}_VERSION} REQUIRED COMPONENTS Core Widgets)
endif()
set(_qpb_qt_version "${Qt${QPB_QT_MAJOR}_VERSION}")
if(_qpb_qt_version AND _qpb_qt_version VERSION_LESS QPB_MIN_QT${QPB_QT_MAJOR}_VERSION)
    message(FATAL_ERROR
        "qpb requires Qt >= ${QPB_MIN_QT${QPB_QT_MAJOR}_VERSION}, found ${_qpb_qt_version}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/qpbTargetHelpers.cmake")

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/qpbversion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/qpb/qpbversion.h"
    @ONLY
)

# --- qpb::core (QtCore only) ------------------------------------------------

qpb_add_library(qpb_core
    EXPORT_DEFINE QPB_BUILD_CORE_LIB
    OUTPUT_NAME qpbcore
    SOURCES
        include/qpb/Attributes.h
        include/qpb/Property.h
        include/qpb/PropertyBuilders.h
        include/qpb/PropertyFilterProxyModel.h
        include/qpb/PropertyGroup.h
        include/qpb/PropertyModel.h
        include/qpb/QObjectPropertySource.h
        include/qpb/Serialization.h
        include/qpb/TypeRegistry.h
        include/qpb/Types.h
        include/qpb/ValidationResult.h
        include/qpb/qpbcore.h
        include/qpb/qpbglobal.h
        "${CMAKE_CURRENT_BINARY_DIR}/include/qpb/qpbversion.h"
        src/core/Property.cpp
        src/core/PropertyBuilders.cpp
        src/core/compat_p.h
        src/core/PropertyFilterProxyModel.cpp
        src/core/PropertyGroup.cpp
        src/core/PropertyModel.cpp
        src/core/QObjectPropertySource.cpp
        src/core/QObjectPropertySource_p.h
        src/core/Serialization.cpp
        src/core/Property_p.h
        src/core/TypeRegistry.cpp
        src/core/qpbglobal.cpp
)
target_link_libraries(qpb_core PUBLIC Qt${QPB_QT_MAJOR}::Core)
add_library(qpb::core ALIAS qpb_core)

# --- qpb::widgets (QtWidgets) -----------------------------------------------

qpb_add_library(qpb_widgets
    EXPORT_DEFINE QPB_BUILD_WIDGETS_LIB
    OUTPUT_NAME qpbwidgets
    SOURCES
        include/qpb/widgets/EditorFactory.h
        include/qpb/widgets/PropertyDelegate.h
        include/qpb/widgets/PropertyFormView.h
        include/qpb/widgets/PropertyTreeView.h
        include/qpb/qpb.h
        src/widgets/EditorFactory.cpp
        src/widgets/compat_p.h
        src/widgets/Int64SpinBox.cpp
        src/widgets/Int64SpinBox_p.h
        src/widgets/PathEdit.cpp
        src/widgets/PathEdit_p.h
        src/widgets/PropertyDelegate.cpp
        src/widgets/PropertyFormView.cpp
        src/widgets/PropertyTreeView.cpp
        src/widgets/widgets_p.cpp
        src/widgets/widgets_p.h
)
target_link_libraries(qpb_widgets PUBLIC qpb_core Qt${QPB_QT_MAJOR}::Widgets)
add_library(qpb::widgets ALIAS qpb_widgets)
