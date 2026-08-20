add_library(hibiki_asio_bridge STATIC
  src/asio_bridge.cpp
)

target_include_directories(hibiki_asio_bridge
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hibiki_asio_bridge PUBLIC hibiki_hub)
target_compile_features(hibiki_asio_bridge PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(hibiki_asio_bridge PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_asio_bridge PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

# The SDK is intentionally not vendored. A developer may opt in to this
# source-only COM transport by pointing CMake at a locally obtained ASIO SDK.
option(HIBIKI_ENABLE_NATIVE_ASIO "Build the optional native ASIO COM DLL" OFF)
if(HIBIKI_ENABLE_NATIVE_ASIO)
  if(NOT WIN32)
    message(FATAL_ERROR "HIBIKI_ENABLE_NATIVE_ASIO requires Windows")
  endif()
  if(NOT HIBIKI_ASIO_SDK_ROOT)
    message(FATAL_ERROR "Set HIBIKI_ASIO_SDK_ROOT to a local ASIO SDK checkout")
  endif()
  if(NOT EXISTS "${HIBIKI_ASIO_SDK_ROOT}/common/iasiodrv.h")
    message(FATAL_ERROR "ASIO SDK common/iasiodrv.h was not found")
  endif()

  add_library(hibiki_asio_native SHARED
    src/native_asio_driver.cpp
    src/native_asio_driver.def
  )
  target_include_directories(hibiki_asio_native PRIVATE
    "${HIBIKI_ASIO_SDK_ROOT}/common"
  )
  target_compile_definitions(hibiki_asio_native PRIVATE
    HIBIKI_ENABLE_NATIVE_ASIO=1
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
  )
  target_compile_features(hibiki_asio_native PRIVATE cxx_std_20)
  target_link_libraries(hibiki_asio_native PRIVATE ole32 advapi32)
  set_target_properties(hibiki_asio_native PROPERTIES
    OUTPUT_NAME "HibikiDSPASIO"
    PREFIX ""
  )
  if(MSVC)
    target_compile_options(hibiki_asio_native PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_asio_native PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
endif()
