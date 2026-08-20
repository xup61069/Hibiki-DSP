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
