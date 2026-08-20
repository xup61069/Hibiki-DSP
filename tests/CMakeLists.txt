add_executable(hibiki_contract_tests
  unit/contract_tests.cpp
)

target_link_libraries(hibiki_contract_tests PRIVATE hibiki_hub hibiki_asio_bridge hibiki_vst_host hibiki_sdk hibiki_driver_contract)

if(MSVC)
  target_compile_options(hibiki_contract_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hibiki_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_test(NAME hibiki_contract_tests COMMAND hibiki_contract_tests)
