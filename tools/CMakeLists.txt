# Developer tools (SPEC-hil-capture.md, SPEC-build-packaging.md "Who may link mc::mock").
# Added from the top-level CMakeLists.txt under MC_BUILD_TOOLS only, which is ON when this folder
# is the top-level project and both the device layer and the mock are built.

# Imported Qt targets are directory-scoped, so this directory finds Qt itself.
mc_find_qt(Core Network SerialPort)

# The tool's logic is a static library of its own so the program (hil_capture) and the QtTest
# binary mc_hil_tool_tests (tests/hil) compile the very same sources once. The test binary links
# this library instead of repeating a source list. Headers are included as "hil_capture/x.h";
# this directory is the include root.
set(MC_HIL_TOOL_SOURCES
    hil_capture/json_reader.h
    hil_capture/profile.h
    hil_capture/profile.cpp
    hil_capture/plan.h
    hil_capture/plan.cpp
    hil_capture/resolve.h
    hil_capture/resolve.cpp
    hil_capture/safety_gate.h
    hil_capture/safety_gate.cpp
    hil_capture/dry_run.h
    hil_capture/dry_run.cpp
    hil_capture/recording_transport.h
    hil_capture/recording_transport.cpp
    hil_capture/capture_writer.h
    hil_capture/capture_writer.cpp
    hil_capture/bench_report.h
    hil_capture/bench_report.cpp
    hil_capture/runner.h
    hil_capture/runner.cpp
    hil_capture/options.h
    hil_capture/options.cpp
    hil_capture/tool.h
    hil_capture/tool.cpp
)

add_library(mc_hil_tool STATIC ${MC_HIL_TOOL_SOURCES})
set_target_properties(mc_hil_tool PROPERTIES AUTOMOC ON)
target_include_directories(mc_hil_tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mc_hil_tool PUBLIC mc::device mc::mock Qt::Core)
mc_apply_warnings(mc_hil_tool)
mc_apply_coverage(mc_hil_tool)

add_executable(hil_capture hil_capture/main.cpp)
target_link_libraries(hil_capture PRIVATE mc_hil_tool)
mc_apply_warnings(hil_capture)
mc_apply_coverage(hil_capture)

# MC Workbench (SPEC-gui-tool.md). Needs Qt 5.15 or Qt >= 6.5 with Widgets (qpb's minimums; the
# library and the other tools build with Qt 5.15 or 6.2+), CMake >= 3.22 and the Qt Advanced
# Docking System built for the same Qt major (package qtadvanceddocking-qt5 or -qt6). The docking
# system is never part of this repository: MC_ADS_DIR is the install prefix of a build made for
# the compiler and Qt major in use (the prebuilt MSVC Qt 6 package, or scripts/build-ads.ps1 for
# MinGW and for Qt 5). When anything is missing the GUI is skipped and the rest of the build is
# unchanged.
if(DEFINED ENV{MC_ADS_DIR})
    set(_mc_ads_default "$ENV{MC_ADS_DIR}")
else()
    set(_mc_ads_default "C:/build_packages/qtadvanceddocking-5.1.1")
endif()
set(MC_ADS_DIR "${_mc_ads_default}" CACHE PATH
    "Install prefix of the Qt Advanced Docking System (qtadvanceddocking-qt5 or -qt6) for this compiler and Qt")

if(MC_BUILD_GUI)
    # The docking library's package and target carry the Qt major: qtadvanceddocking-qt5 /
    # ads::qtadvanceddocking-qt5 for Qt 5, -qt6 for Qt 6. tests/ derives the same names.
    set(MC_ADS_PACKAGE qtadvanceddocking-qt${QT_VERSION_MAJOR})
    set(MC_GUI_SKIP_REASON "")
    if(QT_VERSION_MAJOR EQUAL 5)
        set(_mc_gui_qt_min 5.15)
    else()
        set(_mc_gui_qt_min 6.5)
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.22)
        set(MC_GUI_SKIP_REASON "CMake ${CMAKE_VERSION} is older than 3.22")
    elseif(QT_VERSION VERSION_LESS _mc_gui_qt_min)
        set(MC_GUI_SKIP_REASON "Qt ${QT_VERSION} is older than ${_mc_gui_qt_min}")
    else()
        find_package(Qt${QT_VERSION_MAJOR} ${_mc_gui_qt_min} QUIET COMPONENTS Widgets)
        if(NOT Qt${QT_VERSION_MAJOR}Widgets_FOUND)
            set(MC_GUI_SKIP_REASON "Qt${QT_VERSION_MAJOR} Widgets was not found")
        endif()
    endif()

    if(NOT MC_GUI_SKIP_REASON)
        # The package is looked up under MC_ADS_DIR only; a stale result of an earlier configure
        # (different MC_ADS_DIR) must not survive.
        unset(${MC_ADS_PACKAGE}_DIR CACHE)
        find_package(${MC_ADS_PACKAGE} QUIET CONFIG PATHS "${MC_ADS_DIR}" NO_DEFAULT_PATH)
        if(NOT ${MC_ADS_PACKAGE}_FOUND)
            set(MC_GUI_SKIP_REASON "the docking library for Qt ${QT_VERSION_MAJOR} (${MC_ADS_PACKAGE}) was not found in MC_ADS_DIR=\"${MC_ADS_DIR}\"")
        endif()
    endif()

    if(NOT MC_GUI_SKIP_REASON)
        # An ADS built for the other compiler cannot be linked: MSVC import libraries are .lib,
        # MinGW ones .dll.a.
        get_target_property(_mc_ads_implib ads::${MC_ADS_PACKAGE} IMPORTED_IMPLIB_DEBUG)
        if(NOT _mc_ads_implib)
            get_target_property(_mc_ads_implib ads::${MC_ADS_PACKAGE} IMPORTED_IMPLIB_RELEASE)
        endif()
        if(MSVC AND NOT _mc_ads_implib MATCHES "[.]lib$")
            set(MC_GUI_SKIP_REASON "the docking library in MC_ADS_DIR was not built for MSVC")
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT _mc_ads_implib MATCHES "[.]dll[.]a$")
            set(MC_GUI_SKIP_REASON "the docking library in MC_ADS_DIR was not built for MinGW; run scripts/build-ads.ps1 and point MC_ADS_DIR at its install folder")
        endif()
    endif()

    if(MC_GUI_SKIP_REASON)
        message(STATUS "MC Workbench (GUI) skipped: ${MC_GUI_SKIP_REASON}")
    else()
        # Imported targets are directory-scoped; tests/ links the GUI libraries and needs this one.
        # RelWithDebInfo / MinSizeRel: ADS ships only Debug and Release; without this mapping CMake
        # picks the first one listed (Debug), linking the debug ADS next to release Qt (a
        # "Must construct a QApplication" abort at the first widget).
        set_target_properties(ads::${MC_ADS_PACKAGE} PROPERTIES IMPORTED_GLOBAL TRUE
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RELEASE;DEBUG"
            MAP_IMPORTED_CONFIG_MINSIZEREL "RELEASE;DEBUG")
        message(STATUS "MC Workbench (GUI) enabled: Qt ${QT_VERSION}, ADS ${${MC_ADS_PACKAGE}_VERSION} (${MC_ADS_PACKAGE}) from ${MC_ADS_DIR}")
        add_subdirectory(mc_workbench)
    endif()
endif()
