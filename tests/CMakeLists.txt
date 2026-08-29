add_executable(hibiki_contract_tests
  unit/contract_tests.cpp
)

target_link_libraries(hibiki_contract_tests PRIVATE hibiki_hub hibiki_asio_bridge hibiki_vst_host hibiki_tab_bridge_contract hibiki_sdk hibiki_driver_contract)

if(MSVC)
  target_compile_options(hibiki_contract_tests PRIVATE /W4 /permissive- /EHsc)
  # contract_tests intentionally exercises every fixed-size wire fixture in one
  # process; keep the test harness from exhausting the Windows default stack
  # as new versioned payloads are added, and engine-control scene fixtures
  # stage full IR attachments on the stack. Runtime/RT code has no
  # dependency on this linker setting.
  target_link_options(hibiki_contract_tests PRIVATE /STACK:16777216)
else()
  target_compile_options(hibiki_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_contract_tests COMMAND hibiki_contract_tests)

add_executable(hibiki_driver_stream_tests
  unit/driver_stream_transport_tests.cpp
)

target_link_libraries(hibiki_driver_stream_tests PRIVATE hibiki_sdk)

if(MSVC)
  target_compile_options(hibiki_driver_stream_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_driver_stream_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_driver_stream_tests COMMAND hibiki_driver_stream_tests)

add_executable(hibiki_noise_suppressor_tests
  unit/noise_suppressor_tests.cpp
)

target_link_libraries(hibiki_noise_suppressor_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_noise_suppressor_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_noise_suppressor_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_noise_suppressor_tests COMMAND hibiki_noise_suppressor_tests)

add_executable(hibiki_lane_latency_tests
  unit/lane_latency_tests.cpp
)

target_link_libraries(hibiki_lane_latency_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_lane_latency_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_lane_latency_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_lane_latency_tests COMMAND hibiki_lane_latency_tests)
add_executable(hibiki_asio_transport_consumer_tests
  unit/asio_transport_consumer_tests.cpp
)

target_link_libraries(hibiki_asio_transport_consumer_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_asio_transport_consumer_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_asio_transport_consumer_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_asio_transport_consumer_tests COMMAND hibiki_asio_transport_consumer_tests)

add_executable(hibiki_output_crossfade_tests
  unit/output_crossfade_tests.cpp
)

target_link_libraries(hibiki_output_crossfade_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_output_crossfade_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_output_crossfade_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_output_crossfade_tests COMMAND hibiki_output_crossfade_tests)

add_executable(hibiki_control_status_tests
  unit/control_status_tests.cpp
)

target_link_libraries(hibiki_control_status_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_control_status_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_control_status_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_control_status_tests COMMAND hibiki_control_status_tests)

add_executable(hibiki_peq_dsp_tests
  unit/peq_dsp_tests.cpp
)

target_link_libraries(hibiki_peq_dsp_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_peq_dsp_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_peq_dsp_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_peq_dsp_tests COMMAND hibiki_peq_dsp_tests)

add_executable(hibiki_program_loudness_tests
  unit/program_loudness_tests.cpp
)

target_link_libraries(hibiki_program_loudness_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_program_loudness_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_program_loudness_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_program_loudness_tests COMMAND hibiki_program_loudness_tests)

add_executable(hibiki_wav_ir_tests
  unit/wav_ir_tests.cpp
)

target_link_libraries(hibiki_wav_ir_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_wav_ir_tests PRIVATE /W4 /permissive- /EHsc)
  # wav_ir fixtures stage bounded IrConvolverV1 objects (about 256 KiB each)
  # directly on the stack; keep the harness above the Windows default stack.
  # Runtime/RT code has no dependency on this linker setting.
  target_link_options(hibiki_wav_ir_tests PRIVATE /STACK:16777216)
else()
  target_compile_options(hibiki_wav_ir_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_wav_ir_tests COMMAND hibiki_wav_ir_tests)

add_executable(hibiki_true_peak_limiter_tests
  unit/true_peak_limiter_tests.cpp
)

target_link_libraries(hibiki_true_peak_limiter_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_true_peak_limiter_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_true_peak_limiter_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_true_peak_limiter_tests COMMAND hibiki_true_peak_limiter_tests)

add_executable(hibiki_equal_loudness_tests
  unit/equal_loudness_tests.cpp
  unit/audio_engine_loudness_geometry_tests.cpp
)

target_link_libraries(hibiki_equal_loudness_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_equal_loudness_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_equal_loudness_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_equal_loudness_tests COMMAND hibiki_equal_loudness_tests)

add_executable(hibiki_output_fanout_tests
  unit/output_fanout_tests.cpp
)

target_link_libraries(hibiki_output_fanout_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_output_fanout_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_output_fanout_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_output_fanout_tests COMMAND hibiki_output_fanout_tests)


add_executable(hibiki_volume_state_tests
  unit/volume_state_tests.cpp
)

target_link_libraries(hibiki_volume_state_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_volume_state_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_volume_state_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_volume_state_tests COMMAND hibiki_volume_state_tests)


add_executable(hibiki_device_recovery_tests
  unit/device_recovery_tests.cpp
)

target_link_libraries(hibiki_device_recovery_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_device_recovery_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_device_recovery_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_device_recovery_tests COMMAND hibiki_device_recovery_tests)

add_executable(hibiki_scene_safety_tests
  unit/scene_safety_tests.cpp
)

target_link_libraries(hibiki_scene_safety_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_scene_safety_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_scene_safety_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_scene_safety_tests COMMAND hibiki_scene_safety_tests)

add_executable(hibiki_session_route_rule_tests
  unit/session_route_rule_tests.cpp
)

target_link_libraries(hibiki_session_route_rule_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_session_route_rule_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_session_route_rule_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_session_route_rule_tests COMMAND hibiki_session_route_rule_tests)

add_executable(hibiki_device_switch_tests
  unit/device_switch_tests.cpp
)

target_link_libraries(hibiki_device_switch_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_device_switch_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_device_switch_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_device_switch_tests COMMAND hibiki_device_switch_tests)

add_executable(hibiki_ir_convolver_tests
  unit/ir_convolver_tests.cpp
)

target_link_libraries(hibiki_ir_convolver_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_ir_convolver_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_ir_convolver_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_ir_convolver_tests COMMAND hibiki_ir_convolver_tests)

add_executable(hibiki_ir_phase_kernel_tests
  unit/ir_phase_kernel_tests.cpp
)

target_link_libraries(hibiki_ir_phase_kernel_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_ir_phase_kernel_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_ir_phase_kernel_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_ir_phase_kernel_tests COMMAND hibiki_ir_phase_kernel_tests)


add_executable(hibiki_ir_phase_tests
  unit/ir_phase_tests.cpp
)

target_link_libraries(hibiki_ir_phase_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_ir_phase_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_ir_phase_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_ir_phase_tests COMMAND hibiki_ir_phase_tests)
add_executable(hibiki_process_loopback_plan_tests
  unit/process_loopback_plan_tests.cpp
)
target_link_libraries(hibiki_process_loopback_plan_tests PRIVATE hibiki_hub)
if(MSVC)
  target_compile_options(hibiki_process_loopback_plan_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_process_loopback_plan_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
add_test(NAME hibiki_process_loopback_plan_tests COMMAND hibiki_process_loopback_plan_tests)

add_executable(hibiki_session_command_queue_tests
  unit/session_command_queue_tests.cpp
)
target_link_libraries(hibiki_session_command_queue_tests PRIVATE hibiki_hub)
if(MSVC)
  target_compile_options(hibiki_session_command_queue_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_session_command_queue_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
add_test(NAME hibiki_session_command_queue_tests COMMAND hibiki_session_command_queue_tests)

add_executable(hibiki_output_sink_tests
  unit/output_sink_tests.cpp
)

target_link_libraries(hibiki_output_sink_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_output_sink_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_output_sink_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_output_sink_tests COMMAND hibiki_output_sink_tests)

add_executable(hibiki_virtual_mic_tests
  unit/virtual_mic_tests.cpp
)

target_link_libraries(hibiki_virtual_mic_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_virtual_mic_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_virtual_mic_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_virtual_mic_tests COMMAND hibiki_virtual_mic_tests)

add_executable(hibiki_ipc_codec_tests
  unit/hibiki_ipc_codec_tests.cpp
)

target_link_libraries(hibiki_ipc_codec_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_ipc_codec_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_ipc_codec_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_ipc_codec_tests COMMAND hibiki_ipc_codec_tests)

add_executable(hibiki_engine_control_tests
  unit/engine_control_dispatch_tests.cpp
)

target_link_libraries(hibiki_engine_control_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_engine_control_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_engine_control_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_engine_control_tests COMMAND hibiki_engine_control_tests)

add_executable(hibiki_calibration_compiler_tests
  unit/calibration_compiler_tests.cpp
)

target_link_libraries(hibiki_calibration_compiler_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_calibration_compiler_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_calibration_compiler_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_calibration_compiler_tests COMMAND hibiki_calibration_compiler_tests)

add_executable(hibiki_calibration_anchor_tests
  unit/calibration_anchor_tests.cpp
)

target_link_libraries(hibiki_calibration_anchor_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_calibration_anchor_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_calibration_anchor_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_calibration_anchor_tests COMMAND hibiki_calibration_anchor_tests)


add_executable(hibiki_vst3_sandbox_launch_tests
  unit/vst3_sandbox_launch_tests.cpp
)

target_link_libraries(hibiki_vst3_sandbox_launch_tests PRIVATE hibiki_hub hibiki_vst_host)

if(MSVC)
  target_compile_options(hibiki_vst3_sandbox_launch_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_vst3_sandbox_launch_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_vst3_sandbox_launch_tests COMMAND hibiki_vst3_sandbox_launch_tests)


add_executable(hibiki_exporters_tests
  unit/exporters_tests.cpp
)
target_link_libraries(hibiki_exporters_tests PRIVATE hibiki_hub)
if(MSVC)
  target_compile_options(hibiki_exporters_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_exporters_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
add_test(NAME hibiki_exporters_tests COMMAND hibiki_exporters_tests)
add_executable(hibiki_scene_presets_tests
  unit/scene_presets_tests.cpp
)

target_link_libraries(hibiki_scene_presets_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_scene_presets_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_scene_presets_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_scene_presets_tests COMMAND hibiki_scene_presets_tests)

add_executable(hibiki_device_catalog_snapshot_tests
  unit/device_catalog_snapshot_tests.cpp
)

target_link_libraries(hibiki_device_catalog_snapshot_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_device_catalog_snapshot_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_device_catalog_snapshot_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_device_catalog_snapshot_tests COMMAND hibiki_device_catalog_snapshot_tests)


add_executable(hibiki_session_catalog_snapshot_tests
  unit/session_catalog_snapshot_tests.cpp
)

target_link_libraries(hibiki_session_catalog_snapshot_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_session_catalog_snapshot_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_session_catalog_snapshot_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_session_catalog_snapshot_tests COMMAND hibiki_session_catalog_snapshot_tests)

add_executable(hibiki_control_plane_service_tests
  unit/control_plane_service_tests.cpp
)

target_link_libraries(hibiki_control_plane_service_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_control_plane_service_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_control_plane_service_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_control_plane_service_tests COMMAND hibiki_control_plane_service_tests)

add_executable(hibiki_ipc_pipe_tests
  unit/ipc_pipe_server_tests.cpp
)

target_link_libraries(hibiki_ipc_pipe_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_ipc_pipe_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_ipc_pipe_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_ipc_pipe_tests COMMAND hibiki_ipc_pipe_tests)
add_executable(hibiki_scene_catalog_tests
  unit/scene_catalog_tests.cpp
)

target_link_libraries(hibiki_scene_catalog_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_scene_catalog_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_scene_catalog_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_scene_catalog_tests COMMAND hibiki_scene_catalog_tests)

add_executable(hibiki_scene_catalog_id_tests
  unit/scene_catalog_id_tests.cpp
)

target_link_libraries(hibiki_scene_catalog_id_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_scene_catalog_id_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_scene_catalog_id_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_scene_catalog_id_tests COMMAND hibiki_scene_catalog_id_tests)

add_executable(hibiki_scene_graph_tests
  unit/scene_graph_tests.cpp
)

target_link_libraries(hibiki_scene_graph_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_scene_graph_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_scene_graph_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_scene_graph_tests COMMAND hibiki_scene_graph_tests)


add_executable(hibiki_audio_session_registry_tests
  unit/audio_session_registry_tests.cpp
)

target_link_libraries(hibiki_audio_session_registry_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_audio_session_registry_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_audio_session_registry_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_audio_session_registry_tests COMMAND hibiki_audio_session_registry_tests)


add_executable(hibiki_vst3_lane_bridge_tests
  unit/vst3_lane_bridge_tests.cpp
)

target_link_libraries(hibiki_vst3_lane_bridge_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_vst3_lane_bridge_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_vst3_lane_bridge_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_vst3_lane_bridge_tests COMMAND hibiki_vst3_lane_bridge_tests)


add_executable(hibiki_vst3_engine_integration_tests
  unit/vst3_engine_integration_tests.cpp
)

target_link_libraries(hibiki_vst3_engine_integration_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_vst3_engine_integration_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_vst3_engine_integration_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_vst3_engine_integration_tests COMMAND hibiki_vst3_engine_integration_tests)


add_executable(hibiki_output_handoff_tests
  unit/output_handoff_tests.cpp
)

target_link_libraries(hibiki_output_handoff_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_output_handoff_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_output_handoff_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_output_handoff_tests COMMAND hibiki_output_handoff_tests)

add_executable(hibiki_session_route_graph_tests
  unit/session_route_graph_tests.cpp
)

target_link_libraries(hibiki_session_route_graph_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_session_route_graph_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_session_route_graph_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_session_route_graph_tests COMMAND hibiki_session_route_graph_tests)


if(WIN32)
  add_executable(hibiki_windows_device_watcher_tests
    unit/windows_device_watcher_tests.cpp
  )

  target_link_libraries(hibiki_windows_device_watcher_tests PRIVATE hibiki_hub)

  if(MSVC)
    target_compile_options(hibiki_windows_device_watcher_tests PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_windows_device_watcher_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_test(NAME hibiki_windows_device_watcher_tests COMMAND hibiki_windows_device_watcher_tests)

  add_executable(hibiki_windows_volume_callback_tests
    unit/windows_volume_callback_tests.cpp
  )

  target_link_libraries(hibiki_windows_volume_callback_tests PRIVATE hibiki_hub)

  if(MSVC)
    target_compile_options(hibiki_windows_volume_callback_tests PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_windows_volume_callback_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_test(NAME hibiki_windows_volume_callback_tests COMMAND hibiki_windows_volume_callback_tests)
endif()


add_executable(hibiki_driver_volume_link_tests
  unit/driver_volume_link_tests.cpp
)

target_link_libraries(hibiki_driver_volume_link_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_driver_volume_link_tests PRIVATE /W4 /permissive- /EHsc)
  # DriverVolumeLinkV1 tests construct AudioEngineModel instances whose
  # fixed-capacity IR/loudness attachments exceed the default Windows stack;
  # match the contract_tests harness reservation.
  target_link_options(hibiki_driver_volume_link_tests PRIVATE /STACK:16777216)
else()
  target_compile_options(hibiki_driver_volume_link_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_driver_volume_link_tests COMMAND hibiki_driver_volume_link_tests)


add_executable(hibiki_control_plane_tests
  unit/control_plane_routing_tests.cpp
)

target_link_libraries(hibiki_control_plane_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_control_plane_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_control_plane_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_control_plane_tests COMMAND hibiki_control_plane_tests)


add_executable(hibiki_control_volume_dispatch_tests
  unit/control_volume_dispatch_tests.cpp
)

target_link_libraries(hibiki_control_volume_dispatch_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_control_volume_dispatch_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_control_volume_dispatch_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_control_volume_dispatch_tests COMMAND hibiki_control_volume_dispatch_tests)


add_executable(hibiki_control_payload_wire_tests
  unit/control_payload_wire_tests.cpp
)

target_link_libraries(hibiki_control_payload_wire_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_control_payload_wire_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_control_payload_wire_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_control_payload_wire_tests COMMAND hibiki_control_payload_wire_tests)


if(HIBIKI_BUILD_LIVE_PROBES AND WIN32)
  add_executable(hibiki_live_device_catalog_probe
    live_device_catalog_probe.cpp
  )
  target_link_libraries(hibiki_live_device_catalog_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_device_catalog_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_device_catalog_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_executable(hibiki_live_wasapi_handoff_probe
    live_wasapi_handoff_probe.cpp
  )
  target_link_libraries(hibiki_live_wasapi_handoff_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_wasapi_handoff_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_wasapi_handoff_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()

  add_executable(hibiki_live_audio_session_probe
    live_audio_session_probe.cpp
  )
  target_link_libraries(hibiki_live_audio_session_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_audio_session_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_audio_session_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_executable(hibiki_live_session_volume_probe
    live_session_volume_probe.cpp
  )
  target_link_libraries(hibiki_live_session_volume_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_session_volume_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_session_volume_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_executable(hibiki_live_engine_session_volume_probe
    live_engine_session_volume_probe.cpp
  )
  target_link_libraries(hibiki_live_engine_session_volume_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_engine_session_volume_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_engine_session_volume_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_executable(hibiki_live_process_loopback_probe
    live_process_loopback_probe.cpp
  )
  target_link_libraries(hibiki_live_process_loopback_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_process_loopback_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_process_loopback_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_executable(hibiki_live_system_volume_probe
    live_system_volume_probe.cpp
  )
  target_link_libraries(hibiki_live_system_volume_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_system_volume_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_system_volume_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()

  add_executable(hibiki_live_engine_system_volume_probe
    live_engine_system_volume_probe.cpp
  )
  target_link_libraries(hibiki_live_engine_system_volume_probe PRIVATE hibiki_hub hibiki_sdk)
  if(MSVC)
    target_compile_options(hibiki_live_engine_system_volume_probe PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_live_engine_system_volume_probe PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
endif()
