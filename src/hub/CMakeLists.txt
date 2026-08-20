add_library(hibiki_hub STATIC
  src/contracts.cpp
  src/calibration.cpp
  src/audio_engine.cpp
  src/device_recovery.cpp
  src/device_switch.cpp
  src/exporters.cpp
  src/ipc.cpp
  src/iso226.cpp
  src/output_sink.cpp
  src/scene_graph.cpp
  src/scene_presets.cpp
  src/volume_state.cpp
)

target_include_directories(hibiki_hub
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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
  target_link_libraries(hibiki_hub PUBLIC ole32 uuid)
endif()
