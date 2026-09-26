# tests/qmake/tests.pro — subdirs of the test .pro files that build the library's tests
# through the .pri files (BLD-03). Sibling files use ".file" so qmake resolves the real
# project name instead of assuming "<entry>/<entry>.pro" (see mc_protocol.pro for why).
TEMPLATE = subdirs

SUBDIRS = build_version
build_version.file = build_version.pro
