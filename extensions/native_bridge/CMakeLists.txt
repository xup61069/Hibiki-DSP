add_library(hibiki_tab_bridge_contract STATIC
  src/tab_bridge.cpp
  src/ws_transport.cpp
)

target_include_directories(hibiki_tab_bridge_contract
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(hibiki_tab_bridge_contract PUBLIC hibiki_hub)
target_compile_features(hibiki_tab_bridge_contract PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(hibiki_tab_bridge_contract PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_tab_bridge_contract PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(WIN32)
  target_link_libraries(hibiki_tab_bridge_contract PUBLIC ws2_32 bcrypt)
endif()

# Bounded offline self-test for the HIBT decoder and fixed-capacity tab queue.
# Keep this target in the extension lane so it can be run independently of the
# monolithic contract test without opening a socket or touching local audio.
if(HIBIKI_BUILD_TESTS)
  enable_testing()
  add_executable(hibiki_tab_bridge_selftest
    src/tab_bridge_selftest.cpp
  )
  target_link_libraries(hibiki_tab_bridge_selftest PRIVATE hibiki_tab_bridge_contract)
  target_compile_features(hibiki_tab_bridge_selftest PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(hibiki_tab_bridge_selftest PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_tab_bridge_selftest PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  add_test(NAME hibiki_tab_bridge_selftest COMMAND hibiki_tab_bridge_selftest)

  add_executable(hibiki_ws_transport_tests
    src/ws_transport_tests.cpp
  )
  target_link_libraries(hibiki_ws_transport_tests PRIVATE hibiki_tab_bridge_contract)
  target_compile_features(hibiki_ws_transport_tests PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(hibiki_ws_transport_tests PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hibiki_ws_transport_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  add_test(NAME hibiki_ws_transport_tests COMMAND hibiki_ws_transport_tests)
endif()
