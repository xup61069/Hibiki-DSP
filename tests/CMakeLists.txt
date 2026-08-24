add_executable(hibiki_contract_tests
  unit/contract_tests.cpp
)

target_link_libraries(hibiki_contract_tests PRIVATE hibiki_hub hibiki_asio_bridge hibiki_vst_host hibiki_tab_bridge_contract hibiki_sdk hibiki_driver_contract)

if(MSVC)
  target_compile_options(hibiki_contract_tests PRIVATE /W4 /permissive- /EHsc)
  # contract_tests intentionally exercises every fixed-size wire fixture in one
  # process; keep the test harness from exhausting the Windows default stack
  # as new versioned payloads are added. Runtime/RT code has no dependency on
  # this linker setting.
  target_link_options(hibiki_contract_tests PRIVATE /STACK:8388608)
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
