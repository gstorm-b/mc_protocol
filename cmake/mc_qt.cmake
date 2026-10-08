# mc_qt.cmake -- finds Qt 5.15 or Qt 6.2+ (SPEC-build-packaging.md). Included once by the
# top-level CMakeLists.txt. Nothing here searches for Qt by itself: a core-only configure
# (-DMC_BUILD_DEVICE=OFF) never calls mc_find_qt(), so Qt is never looked for (BLD-02).
#
# MC_QT_MAJOR forces a major version (5 or 6) on a machine that has both. Left empty, the Qt
# found through CMAKE_PREFIX_PATH is used, Qt 6 first when one prefix holds both. Switching the
# major of an existing build tree needs a fresh configure (QT_DIR is cached).
#
# Link the versionless Qt::Core, Qt::Network, Qt::SerialPort and Qt::Test targets, which both
# majors provide. A generator expression that needs a file ($<TARGET_FILE_DIR:...>) names
# Qt${QT_VERSION_MAJOR}::Core instead: the versionless targets of Qt 5.15 are interface targets.
#
# Qt 5.15 with MSVC needs a toolset older than 14.50 (VS 2026): the Qt 5.15 headers use the
# stdext checked iterators, which the 14.50 standard library removed. On this machine every Qt 5
# build loads 14.44 (". scripts/vsdev.ps1 -VcVarsVer 14.44"); a newer cl gets a warning here.

set(MC_QT_MAJOR "" CACHE STRING
    "Force the Qt major version (5 or 6); empty uses the Qt that CMAKE_PREFIX_PATH points at")
set_property(CACHE MC_QT_MAJOR PROPERTY STRINGS "" 5 6)

# mc_find_qt(<component>...): finds Qt with the given components and sets QT_VERSION_MAJOR. A
# macro, not a function: imported Qt targets and the variables are directory-scoped, so every
# directory that links Qt calls it itself.
macro(mc_find_qt)
    if(MC_QT_MAJOR STREQUAL "")
        set(_mc_qt_names Qt6 Qt5)
    elseif(MC_QT_MAJOR STREQUAL "5" OR MC_QT_MAJOR STREQUAL "6")
        set(_mc_qt_names Qt${MC_QT_MAJOR})
    else()
        message(FATAL_ERROR "MC_QT_MAJOR is '${MC_QT_MAJOR}'; it must be empty, 5 or 6")
    endif()
    find_package(QT NAMES ${_mc_qt_names} REQUIRED COMPONENTS Core)
    # A forced major must be the one found, never silently the other (both kits on the prefix).
    if(NOT MC_QT_MAJOR STREQUAL "" AND NOT QT_VERSION_MAJOR EQUAL MC_QT_MAJOR)
        message(FATAL_ERROR "MC_QT_MAJOR is ${MC_QT_MAJOR} but Qt ${QT_VERSION} was found "
            "(QT_DIR=${QT_DIR}); configure a fresh build folder")
    endif()
    if(QT_VERSION_MAJOR EQUAL 5)
        set(_mc_qt_min 5.15)
    else()
        set(_mc_qt_min 6.2)
    endif()
    find_package(Qt${QT_VERSION_MAJOR} ${_mc_qt_min} REQUIRED COMPONENTS ${ARGN})

    get_property(_mc_qt_warned GLOBAL PROPERTY MC_QT5_MSVC_WARNED)
    if(QT_VERSION_MAJOR EQUAL 5 AND MSVC AND MSVC_VERSION GREATER_EQUAL 1950 AND
       NOT _mc_qt_warned)
        set_property(GLOBAL PROPERTY MC_QT5_MSVC_WARNED TRUE)
        message(WARNING "Qt ${QT_VERSION} with MSVC ${MSVC_VERSION}: the Qt 5.15 headers need an "
            "MSVC toolset older than 14.50 (VS 2026 removed the stdext checked iterators they "
            "use). Configure a fresh build folder from a shell with an older toolset, e.g. "
            "\". scripts/vsdev.ps1 -VcVarsVer 14.44\".")
    endif()
endmacro()

# mc_apply_qt5_msvc(<target> <PUBLIC|PRIVATE>): Qt 5 with MSVC only. The toolsets before 14.50 still
# have the stdext checked iterators the Qt 5.15 headers use, but deprecate them (STL4043, C4996),
# which /WX turns into an error in every file that instantiates QList/QVector comparisons. The
# standard library's own switch _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING silences exactly that
# deprecation. mc_device applies it PUBLIC, so everything that links mc::device has it; a Qt target
# that does not link mc::device (hil_fault_proxy) applies it itself.
function(mc_apply_qt5_msvc target scope)
    if(MSVC AND QT_VERSION_MAJOR EQUAL 5)
        target_compile_definitions(${target} ${scope} _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
    endif()
endfunction()
