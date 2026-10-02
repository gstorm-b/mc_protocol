# tools/qmake/tools.pro -- subdirs of the developer tools, built through the .pri files. Sibling
# files use ".file" so qmake resolves the real project name instead of assuming
# "<entry>/<entry>.pro" (see mc_protocol.pro for why).
TEMPLATE = subdirs

SUBDIRS = hil_capture
hil_capture.file = hil_capture.pro
