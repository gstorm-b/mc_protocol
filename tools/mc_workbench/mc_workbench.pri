# mc_workbench.pri -- the sources of tools/mc_workbench other than workbench_main.cpp, for qmake. The twin of
# the mc_workbench_core and mc_workbench_ui static libraries in tools/mc_workbench/CMakeLists.txt;
# the program (tools/qmake/mc_workbench.pro) and its tests (tests/qmake/mc_workbench_tests.pro)
# both include it. Pulls in hil_capture.pri (the mc_hil_tool twin: RecordingTransport; it pulls in
# mc_device.pri and mc_mock.pri, which pull in mc_core.pri), components/qpb.pri and mc_gui_deps.pri
# (the docking library; include this file only when MC_GUI_ENABLED is 1).
!defined(MC_WORKBENCH_PRI_INCLUDED, var) {
MC_WORKBENCH_PRI_INCLUDED = 1

include($$PWD/../hil_capture/hil_capture.pri)
include($$PWD/../../components/qpb.pri)
include($$PWD/../../mc_gui_deps.pri)

QT += core network serialport widgets

# The headers are included as "mc_workbench/x.h", so tools/ is the include root.
INCLUDEPATH += $$PWD/..

HEADERS += \
    $$PWD/component_versions.h \
    $$PWD/qt_compat.h \
    $$PWD/runner_types.h \
    $$PWD/runner_base.h \
    $$PWD/runner_thread.h \
    $$PWD/queue_log_sink.h \
    $$PWD/device_runner.h \
    $$PWD/device_host.h \
    $$PWD/config_binding.h \
    $$PWD/console_model.h \
    $$PWD/point_table_model.h \
    $$PWD/trend_widget.h \
    $$PWD/device_tab.h \
    $$PWD/mock_runner.h \
    $$PWD/mock_host.h \
    $$PWD/mock_types.h \
    $$PWD/mock_request_log_model.h \
    $$PWD/mock_memory_editor.h \
    $$PWD/mock_fault_panel.h \
    $$PWD/mock_tab.h \
    $$PWD/flow_gate.h \
    $$PWD/frame_decoder.h \
    $$PWD/capture_types.h \
    $$PWD/capture_export.h \
    $$PWD/capture_builder.h \
    $$PWD/capture_controller.h \
    $$PWD/trace_model.h \
    $$PWD/log_model.h \
    $$PWD/async_file_writer.h \
    $$PWD/tab_telemetry.h \
    $$PWD/trace_view.h \
    $$PWD/log_view.h \
    $$PWD/capture_panel.h \
    $$PWD/trace_pane.h \
    $$PWD/hil_types.h \
    $$PWD/hil_prepare.h \
    $$PWD/hil_runner.h \
    $$PWD/hil_host.h \
    $$PWD/hil_confirm_dialog.h \
    $$PWD/hil_view.h \
    $$PWD/workspace.h \
    $$PWD/workspace_io.h \
    $$PWD/recent_files.h \
    $$PWD/workspace_controller.h \
    $$PWD/main_window.h

SOURCES += \
    $$PWD/component_versions.cpp \
    $$PWD/runner_types.cpp \
    $$PWD/runner_base.cpp \
    $$PWD/runner_thread.cpp \
    $$PWD/queue_log_sink.cpp \
    $$PWD/device_runner.cpp \
    $$PWD/device_host.cpp \
    $$PWD/config_binding.cpp \
    $$PWD/console_model.cpp \
    $$PWD/point_table_model.cpp \
    $$PWD/trend_widget.cpp \
    $$PWD/device_tab.cpp \
    $$PWD/mock_runner.cpp \
    $$PWD/mock_host.cpp \
    $$PWD/mock_types.cpp \
    $$PWD/mock_request_log_model.cpp \
    $$PWD/mock_memory_editor.cpp \
    $$PWD/mock_fault_panel.cpp \
    $$PWD/mock_tab.cpp \
    $$PWD/frame_decoder.cpp \
    $$PWD/capture_types.cpp \
    $$PWD/capture_export.cpp \
    $$PWD/capture_builder.cpp \
    $$PWD/capture_controller.cpp \
    $$PWD/trace_model.cpp \
    $$PWD/log_model.cpp \
    $$PWD/async_file_writer.cpp \
    $$PWD/tab_telemetry.cpp \
    $$PWD/trace_view.cpp \
    $$PWD/log_view.cpp \
    $$PWD/capture_panel.cpp \
    $$PWD/trace_pane.cpp \
    $$PWD/hil_types.cpp \
    $$PWD/hil_prepare.cpp \
    $$PWD/hil_runner.cpp \
    $$PWD/hil_host.cpp \
    $$PWD/hil_confirm_dialog.cpp \
    $$PWD/hil_view.cpp \
    $$PWD/workspace.cpp \
    $$PWD/workspace_io.cpp \
    $$PWD/recent_files.cpp \
    $$PWD/workspace_controller.cpp \
    $$PWD/main_window.cpp

}
