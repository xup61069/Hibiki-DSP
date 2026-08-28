// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/asio_transport_consumer.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace hibiki {

namespace {

bool format_matches(const hibiki_asio_transport_region_v1* region,
                    const std::uint32_t channels,
                    const std::uint32_t sample_rate,
                    const std::uint32_t frames_per_buffer) noexcept {
  return region != nullptr && region->magic == HIBIKI_ASIO_TRANSPORT_MAGIC_V1 &&
         region->abi_version == HIBIKI_ASIO_TRANSPORT_ABI_V1 &&
         region->size_bytes == hibiki_asio_transport_region_size_v1() &&
         region->channels == channels && region->sample_rate == sample_rate &&
         region->frames_per_buffer == frames_per_buffer;
}

}  // namespace

AsioTransportConsumerV1::~AsioTransportConsumerV1() { unbind(); }

bool AsioTransportConsumerV1::bind(const std::wstring_view mapping_name,
                                   const std::uint32_t channels,
                                   const std::uint32_t sample_rate,
                                   const std::uint32_t frames_per_buffer) noexcept {
  unbind();
  if (mapping_name.empty()) return false;
  const auto bytes = hibiki_asio_transport_region_size_v1();

#if defined(_WIN32)
  SetLastError(ERROR_SUCCESS);
  HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      static_cast<DWORD>(bytes),
                                      std::wstring(mapping_name).c_str());
  if (mapping == nullptr) return false;
  const bool newly_created = GetLastError() != ERROR_ALREADY_EXISTS;
  auto* region = static_cast<hibiki_asio_transport_region_v1*>(
      MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes));
  if (region == nullptr) {
    CloseHandle(mapping);
    return false;
  }
  if (newly_created) {
    if (hibiki_asio_transport_init_v1(region, bytes, channels, sample_rate, frames_per_buffer) == 0) {
      UnmapViewOfFile(region);
      CloseHandle(mapping);
      return false;
    }
  }
  if (!format_matches(region, channels, sample_rate, frames_per_buffer)) {
    UnmapViewOfFile(region);
    CloseHandle(mapping);
    return false;
  }
  auto staging_storage = std::unique_ptr<float[]>(new (std::nothrow) float[
      static_cast<std::size_t>(HIBIKI_ASIO_TRANSPORT_MAX_CHANNELS_V1) *
      HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1]);
  if (staging_storage == nullptr) {
    UnmapViewOfFile(region);
    CloseHandle(mapping);
    return false;
  }
  mapping_ = mapping;
  region_ = region;
  region_bytes_ = bytes;
  staging_storage_ = std::move(staging_storage);
#else
  (void)channels;
  (void)sample_rate;
  (void)frames_per_buffer;
  (void)bytes;
#endif

  channels_ = channels;
  sample_rate_ = sample_rate;
  frames_per_buffer_ = frames_per_buffer;
  return region_ != nullptr;
}

void AsioTransportConsumerV1::unbind() noexcept {
#if defined(_WIN32)
  if (region_ != nullptr) UnmapViewOfFile(region_);
  if (mapping_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_));
  mapping_ = nullptr;
#endif
  region_ = nullptr;
  region_bytes_ = 0;
  channels_ = 0;
  sample_rate_ = 0;
  frames_per_buffer_ = 0;
  staging_storage_.reset();
}

bool AsioTransportConsumerV1::pop(float* const interleaved,
                                  const std::uint32_t output_capacity_frames,
                                  AsioTransportBlockV1& block) noexcept {
  block = {};
  if (region_ == nullptr || interleaved == nullptr || staging_storage_ == nullptr) return false;
  AsioTransportBlockV1 candidate{};
  // The C ABI only receives a frame capacity. Stage into a private buffer
  // whose size covers the full versioned channel/frame contract, then copy to
  // the caller only after the shared slot metadata matches this bind.
  const auto bounded_capacity =
      output_capacity_frames < HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1
          ? output_capacity_frames
          : static_cast<std::uint32_t>(HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1);
  if (hibiki_asio_transport_pop_interleaved_v1(
          region_, region_bytes_, staging_storage_.get(), bounded_capacity, &candidate.frames,
          &candidate.channels, &candidate.sample_rate) == 0) {
    return false;
  }
  // The C ABI validates the shared region header, but slot metadata is copied
  // from shared memory. Re-check it against this consumer's bind contract so
  // an inconsistent slot cannot enter an engine lane.
  if (candidate.frames != frames_per_buffer_ || candidate.channels != channels_ ||
      candidate.sample_rate != sample_rate_) {
    return false;
  }
  const auto sample_count = static_cast<std::size_t>(candidate.frames) * candidate.channels;
  std::copy_n(staging_storage_.get(), sample_count, interleaved);
  block = candidate;
  return true;
}

std::uint32_t AsioTransportConsumerV1::dropped_blocks() const noexcept {
  if (region_ == nullptr) return 0;
#if defined(_WIN32)
  return static_cast<std::uint32_t>(InterlockedCompareExchange(
      reinterpret_cast<volatile LONG*>(&region_->dropped_blocks), 0, 0));
#else
  return region_->dropped_blocks;
#endif
}

}  // namespace hibiki
