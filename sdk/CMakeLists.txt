add_library(hibiki_sdk STATIC
  src/asio_transport_v1.c
  src/driver_control_transport_v1.c
  src/driver_stream_ring_v1.c
  src/driver_stream_transport_v1.c
)
target_include_directories(hibiki_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
