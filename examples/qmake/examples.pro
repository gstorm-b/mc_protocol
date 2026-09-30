# examples/qmake/examples.pro -- subdirs of the example programs, built through the .pri files.
# Sibling files use ".file" so qmake resolves the real project name instead of assuming
# "<entry>/<entry>.pro" (see mc_protocol.pro for why).
TEMPLATE = subdirs

SUBDIRS = session_loop virtual_plc qt_console_poller
session_loop.file = session_loop.pro
virtual_plc.file = virtual_plc.pro
qt_console_poller.file = qt_console_poller.pro
