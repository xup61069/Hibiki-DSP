cmake_minimum_required(VERSION 3.25)

project(HibikiDSP VERSION 0.1.0 LANGUAGES C CXX)

option(HIBIKI_BUILD_TESTS "Build contract and DSP tests" ON)
option(HIBIKI_BUILD_LIVE_PROBES "Build opt-in probes that touch local Windows audio" OFF)
option(HIBIKI_BUILD_ENGINE_PREVIEW "Build the local named-pipe engine preview" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Repository documentation and source comments are UTF-8, including the
# Traditional Chinese canonical specs. Keep MSVC's source decoding explicit so
# a locale/code-page change cannot silently alter literals or warnings.
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_subdirectory(sdk)
add_subdirectory(src/hub)
add_subdirectory(driver)
add_subdirectory(asio)
add_subdirectory(vst-host)
add_subdirectory(extensions/native_bridge)

if(WIN32 AND HIBIKI_BUILD_ENGINE_PREVIEW)
  add_executable(hibiki_engine_preview apps/engine-preview/engine_preview.cpp)
  target_link_libraries(hibiki_engine_preview PRIVATE hibiki_hub)
  target_compile_features(hibiki_engine_preview PRIVATE cxx_std_20)
  # The control-plane transaction path (graph/IR prepare+commit) runs on the
  # preview main thread and uses several fixed-capacity structures worth
  # hundreds of KB per frame. Keep the default 1 MB stack from overflowing
  # when an IR prepare is followed immediately by a SceneApply transaction;
  # this mirrors the hibiki_contract_tests harness reserve.
  if(MSVC)
    target_link_options(hibiki_engine_preview PRIVATE /STACK:8388608)
  endif()
endif()

if(HIBIKI_BUILD_TESTS)
  enable_testing()
add_subdirectory(tests)
endif()
