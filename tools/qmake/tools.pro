# tools/qmake/tools.pro -- subdirs of the developer tools, built through the .pri files. Sibling
# files use ".file" so qmake resolves the real project name instead of assuming
# "<entry>/<entry>.pro" (see mc_protocol.pro for why).
TEMPLATE = subdirs

SUBDIRS = hil_capture
hil_capture.file = hil_capture.pro

# MC Workbench: only when mc_gui_deps.pri finds a docking library for this kit (mc_local.pri,
# template mc_local.pri.example); otherwise it is skipped with a message and the rest is unchanged.
include(../../mc_gui_deps.pri)
equals(MC_GUI_ENABLED, 1) {
    SUBDIRS += mc_workbench
    mc_workbench.file = mc_workbench.pro
}
