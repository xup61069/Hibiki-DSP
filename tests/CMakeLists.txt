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

add_executable(hibiki_session_route_rules_tests
  unit/session_route_rules_tests.cpp
)

target_link_libraries(hibiki_session_route_rules_tests PRIVATE hibiki_hub)

if(MSVC)
  target_compile_options(hibiki_session_route_rules_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_session_route_rules_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_session_route_rules_tests COMMAND hibiki_session_route_rules_tests)

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
