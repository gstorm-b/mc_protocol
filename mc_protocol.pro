# mc_protocol.pro — dev-only: proves the .pri files build the library's own tests and examples
# through qmake alone (SPEC-build-packaging.md, BLD-03). Not part of the consumer contract; a
# consumer includes mc_protocol.pri (or mc_core.pri) directly, never this file.
#
# SUBDIRS entries use an explicit ".file" mapping rather than a bare directory name: qmake's
# default subdirs resolution looks for "<dir>/<basename-of-dir>.pro" (e.g. "tests/qmake/qmake.pro"),
# which does not match this tree's tests/qmake/tests.pro. ".file" points at the real project file
# without emitting the "Cannot assign both file and subdir" warning that combining it with
# ".subdir" would produce (verified empirically against qmake 3.1 / Qt 6.11.1).
TEMPLATE = subdirs

SUBDIRS = tests_qmake examples_qmake
tests_qmake.file = tests/qmake/tests.pro
examples_qmake.file = examples/qmake/examples.pro
