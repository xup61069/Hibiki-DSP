add_library(hibiki_tab_bridge_contract STATIC
  src/tab_bridge.cpp
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
