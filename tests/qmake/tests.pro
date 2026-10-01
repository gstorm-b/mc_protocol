# tests/qmake/tests.pro — subdirs of the test .pro files that build the library's tests
# through the .pri files (BLD-03). Sibling files use ".file" so qmake resolves the real
# project name instead of assuming "<entry>/<entry>.pro" (see mc_protocol.pro for why).
TEMPLATE = subdirs

SUBDIRS = build_version mc_core_model_tests mc_core_protocol_tests mc_core_session_tests mc_mock_tests mc_integration_tests mc_tcp_transport_tests mc_config_json_tests mc_device_tests mc_device_thread_tests mc_serial_tests
build_version.file = build_version.pro
mc_core_model_tests.file = mc_core_model_tests.pro
mc_core_protocol_tests.file = mc_core_protocol_tests.pro
mc_core_session_tests.file = mc_core_session_tests.pro
mc_mock_tests.file = mc_mock_tests.pro
mc_integration_tests.file = mc_integration_tests.pro
mc_tcp_transport_tests.file = mc_tcp_transport_tests.pro
mc_config_json_tests.file = mc_config_json_tests.pro
mc_device_tests.file = mc_device_tests.pro
mc_device_thread_tests.file = mc_device_thread_tests.pro
mc_serial_tests.file = mc_serial_tests.pro
