add_library(hibiki_vst_host STATIC
  src/plugin_host.cpp
)

target_include_directories(hibiki_vst_host
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(hibiki_vst_host PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(hibiki_vst_host PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_vst_host PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
