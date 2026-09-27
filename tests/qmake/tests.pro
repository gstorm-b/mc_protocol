# tests/qmake/tests.pro — subdirs of the test .pro files that build the library's tests
# through the .pri files (BLD-03). Sibling files use ".file" so qmake resolves the real
# project name instead of assuming "<entry>/<entry>.pro" (see mc_protocol.pro for why).
TEMPLATE = subdirs

SUBDIRS = build_version mc_core_model_tests mc_core_protocol_tests
build_version.file = build_version.pro
mc_core_model_tests.file = mc_core_model_tests.pro
mc_core_protocol_tests.file = mc_core_protocol_tests.pro
