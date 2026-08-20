cmake_minimum_required(VERSION 3.25)

project(HibikiDSP VERSION 0.1.0 LANGUAGES C CXX)

option(HIBIKI_BUILD_TESTS "Build contract and DSP tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/hub)
add_subdirectory(driver)
add_subdirectory(asio)
add_subdirectory(vst-host)
add_subdirectory(extensions/native_bridge)
add_subdirectory(sdk)

if(HIBIKI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
