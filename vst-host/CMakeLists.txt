add_library(hibiki_vst_host STATIC
  src/plugin_host.cpp
  src/vst3_sandbox.cpp
  src/vst3_worker_protocol.cpp
  src/vst3_worker_pipe.cpp
  src/latency_compensation.cpp
  src/latency_graph_commit.cpp
  src/vst3_parameter_timeline.cpp
  src/vst3_timeline_editor.cpp
  src/vst3_timeline_persistence.cpp
  src/vst3_timeline_file_store.cpp
  src/vst3_supervisor_surface.cpp
  src/vst3_worker_lane.cpp
  src/vst3_scene_automation.cpp
  src/vst3_plugin_state.cpp
  src/vst3_scene_state.cpp
  src/vst3_scene_state_preflight.cpp
  src/vst3_bus_layout.cpp
  src/vst3_crash_report.cpp
)

add_executable(hibiki_vst_worker
  src/vst3_worker_main.cpp
)

target_link_libraries(hibiki_vst_worker PRIVATE hibiki_vst_host)

target_include_directories(hibiki_vst_host
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/hub/include
)

target_compile_features(hibiki_vst_host PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(hibiki_vst_host PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_vst_host PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

option(HIBIKI_ENABLE_VST3_SDK "Build optional VST3 SDK module catalog" OFF)
set(HIBIKI_VST3_SDK_ROOT "" CACHE PATH "Local pinned Steinberg VST3 SDK checkout")
if(HIBIKI_ENABLE_VST3_SDK)
  if(NOT EXISTS "${HIBIKI_VST3_SDK_ROOT}/pluginterfaces/vst/ivstcomponent.h")
    message(FATAL_ERROR "HIBIKI_VST3_SDK_ROOT must point to a populated VST3 SDK checkout")
  endif()
  add_library(hibiki_vst3_sdk_catalog STATIC
    src/vst3_sdk_catalog.cpp
    src/vst3_sdk_processor.cpp
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/pluginterfaces/base/conststringtable.cpp"
    "${HIBIKI_VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp"
  )
  target_include_directories(hibiki_vst3_sdk_catalog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${HIBIKI_VST3_SDK_ROOT}"
    "${HIBIKI_VST3_SDK_ROOT}/base/source"
    "${HIBIKI_VST3_SDK_ROOT}/pluginterfaces"
    "${HIBIKI_VST3_SDK_ROOT}/public.sdk/source"
  )
  target_compile_features(hibiki_vst3_sdk_catalog PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(hibiki_vst3_sdk_catalog PRIVATE /W4 /permissive- /EHsc /Zc:char8_t-)
    target_compile_definitions(hibiki_vst3_sdk_catalog PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  if(WIN32)
    target_link_libraries(hibiki_vst3_sdk_catalog PUBLIC shell32)
  endif()

  add_executable(hibiki_vst3_sdk_worker
    src/vst3_sdk_worker_main.cpp
  )
  target_compile_features(hibiki_vst3_sdk_worker PRIVATE cxx_std_20)
  target_link_libraries(hibiki_vst3_sdk_worker PRIVATE
    hibiki_vst3_sdk_catalog
    hibiki_vst_host
  )
  if(MSVC)
    target_compile_options(hibiki_vst3_sdk_worker PRIVATE /W4 /permissive- /EHsc /Zc:char8_t-)
  endif()
endif()
