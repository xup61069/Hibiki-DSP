add_library(hibiki_driver_contract STATIC
  src/control_validation.c
)

target_include_directories(hibiki_driver_contract
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include
)

set_target_properties(hibiki_driver_contract PROPERTIES
  OUTPUT_NAME hibiki_driver_contract
)

if(MSVC)
  target_compile_options(hibiki_driver_contract PRIVATE /W4)
else()
  target_compile_options(hibiki_driver_contract PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
