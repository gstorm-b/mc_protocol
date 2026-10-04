# mc_gui_deps.pri -- decides whether the GUI (tools/mc_workbench) can be built by qmake, and where
# the Qt Advanced Docking System (ADS) is. The twin of the MC_BUILD_GUI / MC_ADS_DIR logic in
# tools/CMakeLists.txt. Dev-only: not part of the consumer contract.
#
# Reads the git-ignored mc_local.pri (template: mc_local.pri.example), then the environment
# variable MC_ADS_DIR. Sets, for the kit in use:
#   MC_GUI_ENABLED   1 when the GUI can be built, else empty
#   MC_ADS_KIT_DIR   the ADS install prefix (bin/, lib/, include/)
#   MC_ADS_DLL       the ADS DLL of the current debug/release pass (to copy next to a program)
# and, when enabled, adds the ADS include path and library to the including project.
!defined(MC_GUI_DEPS_PRI_INCLUDED, var) {
MC_GUI_DEPS_PRI_INCLUDED = 1

exists($$PWD/mc_local.pri): include($$PWD/mc_local.pri)

win32-g++ {
    MC_ADS_KIT_DIR = $$MC_ADS_DIR_MINGW
} else {
    MC_ADS_KIT_DIR = $$MC_ADS_DIR
}
isEmpty(MC_ADS_KIT_DIR): MC_ADS_KIT_DIR = $$(MC_ADS_DIR)

MC_GUI_ENABLED =
MC_GUI_SKIP_REASON =
isEmpty(MC_ADS_KIT_DIR) {
    MC_GUI_SKIP_REASON = no docking library path: copy mc_local.pri.example to mc_local.pri
} else: !exists($$MC_ADS_KIT_DIR/include/qtadvanceddocking-qt6/DockManager.h) {
    MC_GUI_SKIP_REASON = no docking library headers in $$MC_ADS_KIT_DIR
} else: lessThan(QT_MAJOR_VERSION, 6) {
    MC_GUI_SKIP_REASON = Qt $$QT_VERSION is older than 6.5
} else: equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 5) {
    MC_GUI_SKIP_REASON = Qt $$QT_VERSION is older than 6.5
} else {
    MC_GUI_ENABLED = 1
}
isEmpty(MC_GUI_ENABLED): message(MC Workbench (GUI) skipped: $$MC_GUI_SKIP_REASON)

# The debug build of ADS carries a "d" postfix on the library and the DLL.
CONFIG(debug, debug|release) {
    MC_ADS_LIBNAME = qtadvanceddocking-qt6d
} else {
    MC_ADS_LIBNAME = qtadvanceddocking-qt6
}
# MinGW names the DLL with a "lib" prefix, MSVC does not.
win32-g++ {
    MC_ADS_DLL = $$MC_ADS_KIT_DIR/bin/lib$${MC_ADS_LIBNAME}.dll
} else {
    MC_ADS_DLL = $$MC_ADS_KIT_DIR/bin/$${MC_ADS_LIBNAME}.dll
}

equals(MC_GUI_ENABLED, 1) {
    INCLUDEPATH += $$MC_ADS_KIT_DIR/include/qtadvanceddocking-qt6
    LIBS += -L$$MC_ADS_KIT_DIR/lib -l$$MC_ADS_LIBNAME
    # The DLL goes next to the program (or test) after the link; $(DESTDIR) is the debug/ or
    # release/ folder of a debug_and_release build, empty otherwise.
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$MC_ADS_DLL)) $(DESTDIR)
}

}
