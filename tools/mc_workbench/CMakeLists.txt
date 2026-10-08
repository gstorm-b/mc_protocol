# MC Workbench (SPEC-gui-tool.md), added by tools/CMakeLists.txt once Qt (5.15, or 6.5 and newer)
# and the docking library are known to be there. Three targets, so the QtTest binary (tests/gui) and
# the program compile the very same sources once:
#   mc_workbench_core  STATIC  non-UI part: runner threads (DeviceRunner, MockRunner and their hosts),
#                              later the models; Qt Core and Network only, no widgets
#   mc_workbench_ui    STATIC  everything with widgets: main window, docks (ADS, qpb)
#   mc_workbench       WIN32   the program: workbench_main.cpp only
# Headers are included as "mc_workbench/x.h"; tools/ is the include root. The sources are mirrored
# in mc_workbench.pri for qmake.

# qpb (MIT, vendored unmodified in components/qpb; its own CMake file is used as it is).
add_subdirectory("${PROJECT_SOURCE_DIR}/components/qpb" "${CMAKE_CURRENT_BINARY_DIR}/qpb")
if(MSVC)
    # Release only: qpb's Int64SpinBox.cpp inlines Qt's qnumeric.h (qMulOverflow path) and cl reports
    # C4702 inside that Qt header. Third-party code on both sides, so silenced for this one vendored
    # target, not globally.
    target_compile_options(qpb_widgets PRIVATE /wd4702)
endif()

add_library(mc_workbench_core STATIC
    component_versions.h
    component_versions.cpp
    qt_compat.h
    runner_types.h
    runner_types.cpp
    runner_base.h
    runner_base.cpp
    runner_thread.h
    runner_thread.cpp
    queue_log_sink.h
    queue_log_sink.cpp
    device_runner.h
    device_runner.cpp
    device_host.h
    device_host.cpp
    config_binding.h
    config_binding.cpp
    console_model.h
    console_model.cpp
    mock_runner.h
    mock_runner.cpp
    mock_host.h
    mock_host.cpp
    mock_types.h
    mock_types.cpp
    mock_request_log_model.h
    mock_request_log_model.cpp
    flow_gate.h
    frame_decoder.h
    frame_decoder.cpp
    capture_types.h
    capture_types.cpp
    capture_export.h
    capture_export.cpp
    capture_builder.h
    capture_builder.cpp
    capture_controller.h
    capture_controller.cpp
    trace_model.h
    trace_model.cpp
    log_model.h
    log_model.cpp
    async_file_writer.h
    async_file_writer.cpp
    tab_telemetry.h
    tab_telemetry.cpp
    hil_types.h
    hil_types.cpp
    hil_prepare.h
    hil_prepare.cpp
    hil_runner.h
    hil_runner.cpp
    hil_host.h
    hil_host.cpp
    workspace.h
    workspace.cpp
    workspace_io.h
    workspace_io.cpp
    recent_files.h
    recent_files.cpp
)
set_target_properties(mc_workbench_core PROPERTIES AUTOMOC ON)
target_include_directories(mc_workbench_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
# mc_hil_tool brings RecordingTransport (and mc::mock, mc::device); the runners use its public API.
target_link_libraries(mc_workbench_core PUBLIC mc_hil_tool mc::device mc::mock qpb::core
    Qt::Core Qt::Network)
mc_apply_warnings(mc_workbench_core)
mc_apply_coverage(mc_workbench_core)

add_library(mc_workbench_ui STATIC
    main_window.h
    main_window.cpp
    point_table_model.h
    point_table_model.cpp
    trend_widget.h
    trend_widget.cpp
    device_tab.h
    device_tab.cpp
    mock_memory_editor.h
    mock_memory_editor.cpp
    mock_fault_panel.h
    mock_fault_panel.cpp
    mock_tab.h
    mock_tab.cpp
    trace_view.h
    trace_view.cpp
    log_view.h
    log_view.cpp
    capture_panel.h
    capture_panel.cpp
    trace_pane.h
    trace_pane.cpp
    hil_confirm_dialog.h
    hil_confirm_dialog.cpp
    hil_view.h
    hil_view.cpp
    workspace_controller.h
    workspace_controller.cpp
)
set_target_properties(mc_workbench_ui PROPERTIES AUTOMOC ON)
target_link_libraries(mc_workbench_ui PUBLIC
    mc_workbench_core
    qpb::widgets
    ads::${MC_ADS_PACKAGE}
    Qt::Widgets)
mc_apply_warnings(mc_workbench_ui)
mc_apply_coverage(mc_workbench_ui)

add_executable(mc_workbench WIN32 workbench_main.cpp)
target_link_libraries(mc_workbench PRIVATE mc_workbench_ui)
mc_apply_warnings(mc_workbench)
mc_apply_coverage(mc_workbench)

# The docking library is a DLL: next to the program after the build, as the Qt platform plugins
# are (windeployqt is not used). Tests get it through PATH instead (tests/CMakeLists.txt).
add_custom_command(TARGET mc_workbench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ads::${MC_ADS_PACKAGE}>" "$<TARGET_FILE_DIR:mc_workbench>"
    VERBATIM)
