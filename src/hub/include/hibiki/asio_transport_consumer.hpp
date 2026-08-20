// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_ASIO_TRANSPORT_CONSUMER_HPP
#define HIBIKI_ASIO_TRANSPORT_CONSUMER_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hibiki/asio_transport_v1.h"

namespace hibiki {

struct AsioTransportBlockV1 {
  std::uint32_t frames{0};
  std::uint32_t channels{0};
  std::uint32_t sample_rate{0};
};

// Engine-side owner of the named ASIO data boundary. Binding and unbinding
// happen on a control thread; pop() is intentionally allocation-free so it
// can be called by an RT lane after the mapping has been prepared.
class AsioTransportConsumerV1 final {
public:
  AsioTransportConsumerV1() = default;
  ~AsioTransportConsumerV1();

  AsioTransportConsumerV1(const AsioTransportConsumerV1&) = delete;
  AsioTransportConsumerV1& operator=(const AsioTransportConsumerV1&) = delete;

  bool bind(std::wstring_view mapping_name,
            std::uint32_t channels,
            std::uint32_t sample_rate,
            std::uint32_t frames_per_buffer) noexcept;
  void unbind() noexcept;

  bool bound() const noexcept { return region_ != nullptr; }
  bool pop(float* interleaved,
           std::uint32_t output_capacity_frames,
           AsioTransportBlockV1& block) noexcept;
  std::uint32_t dropped_blocks() const noexcept;

private:
#if defined(_WIN32)
  void* mapping_{nullptr};
#endif
  hibiki_asio_transport_region_v1* region_{nullptr};
  std::size_t region_bytes_{0};
  std::uint32_t channels_{0};
  std::uint32_t sample_rate_{0};
  std::uint32_t frames_per_buffer_{0};
};

}  // namespace hibiki

#endif
