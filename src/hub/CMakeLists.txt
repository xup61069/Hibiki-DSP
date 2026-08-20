add_library(hibiki_hub STATIC
  src/contracts.cpp
  src/control_payloads.cpp
  src/control_queue.cpp
  src/control_service.cpp
  src/calibration.cpp
  src/audio_session_registry.cpp
  src/audio_engine.cpp
  src/asio_transport_consumer.cpp
  src/device_recovery.cpp
  src/device_switch.cpp
  src/exporters.cpp
  src/ir_phase.cpp
  src/ipc.cpp
  src/ipc_pipe.cpp
  src/iso226.cpp
  src/output_sink.cpp
  src/output_crossfade.cpp
  src/scene_graph.cpp
  src/scene_presets.cpp
  src/scene_safety.cpp
  src/volume_state.cpp
  src/virtual_mic.cpp
  src/windows_wasapi_output.cpp
)

target_include_directories(hibiki_hub
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/sdk/include
)

target_link_libraries(hibiki_hub PUBLIC hibiki_sdk)

target_compile_features(hibiki_hub PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(hibiki_hub PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_hub PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

set_target_properties(hibiki_hub PROPERTIES
  OUTPUT_NAME hibiki_hub
  POSITION_INDEPENDENT_CODE ON
)

if(WIN32)
  target_sources(hibiki_hub PRIVATE src/windows_volume_broker.cpp)
  target_sources(hibiki_hub PRIVATE src/windows_device_watcher.cpp)
  target_sources(hibiki_hub PRIVATE src/windows_audio_session_watcher.cpp)
  target_link_libraries(hibiki_hub PUBLIC ole32 uuid)
endif()
