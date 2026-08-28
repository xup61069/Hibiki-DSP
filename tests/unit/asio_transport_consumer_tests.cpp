// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/asio_transport_consumer.hpp"

#include <array>
#include <cstdio>
#include <vector>

#include "hibiki/asio_transport_v1.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::fputs("FAILED: " #expr "\n", stderr);                          \
            return 1;                                                           \
        }                                                                       \
    } while (false)

int main() {
    // Empty mapping name is rejected fail-closed.
    {
        hibiki::AsioTransportConsumerV1 consumer;
        CHECK(!consumer.bind(L"", 2U, 48000U, 64U));
        CHECK(!consumer.bound());
    }

    // Successful bind creates a shared region with matching format.
    // dropped_blocks() initial value is zero.
    {
        hibiki::AsioTransportConsumerV1 consumer;
        CHECK(consumer.bind(L"HibikiTest1663A", 2U, 48000U, 64U));
        CHECK(consumer.bound());
        CHECK(consumer.dropped_blocks() == 0U);
    }

    // Pop fail-closed: null output buffer on a bound consumer.
    {
        hibiki::AsioTransportConsumerV1 consumer;
        CHECK(consumer.bind(L"HibikiTest1663B", 2U, 48000U, 64U));
        hibiki::AsioTransportBlockV1 block{};
        CHECK(!consumer.pop(nullptr, 64U, block));
    }

    // Rebind same mapping name and matching format: both consumers succeed.
    {
        hibiki::AsioTransportConsumerV1 first;
        CHECK(first.bind(L"HibikiTest1663C", 2U, 48000U, 64U));
        CHECK(first.bound());
        hibiki::AsioTransportConsumerV1 second;
        CHECK(second.bind(L"HibikiTest1663C", 2U, 48000U, 64U));
        CHECK(second.bound());
    }

    // Format mismatch: same name but different sample_rate rejected.
    {
        hibiki::AsioTransportConsumerV1 first;
        CHECK(first.bind(L"HibikiTest1663D", 2U, 48000U, 64U));
        CHECK(first.bound());
        hibiki::AsioTransportConsumerV1 second;
        CHECK(!second.bind(L"HibikiTest1663D", 2U, 44100U, 64U));
        CHECK(!second.bound());
    }

    // Format mismatch: same name but different channels rejected.
    {
        hibiki::AsioTransportConsumerV1 first;
        CHECK(first.bind(L"HibikiTest1663E", 2U, 48000U, 64U));
        CHECK(first.bound());
        hibiki::AsioTransportConsumerV1 second;
        CHECK(!second.bind(L"HibikiTest1663E", 8U, 48000U, 64U));
        CHECK(!second.bound());
    }

    // Format mismatch: same name but different frames_per_buffer rejected.
    {
        hibiki::AsioTransportConsumerV1 first;
        CHECK(first.bind(L"HibikiTest1663F", 2U, 48000U, 64U));
        CHECK(first.bound());
        hibiki::AsioTransportConsumerV1 second;
        CHECK(!second.bind(L"HibikiTest1663F", 2U, 48000U, 128U));
        CHECK(!second.bound());
    }

    // Normal data path: push planar then pop interleaved through the C API.
    {
        const auto region_size = hibiki_asio_transport_region_size_v1();
        std::vector<std::uint8_t> region_storage(region_size);
        auto* region = reinterpret_cast<hibiki_asio_transport_region_v1*>(
            region_storage.data());
        CHECK(hibiki_asio_transport_init_v1(region, region_size, 2U, 48000U, 4U) != 0);

        const float left[4] = {1.0F, -1.0F, 0.5F, -0.5F};
        const float right[4] = {0.25F, -0.25F, 0.75F, -0.75F};
        const float* planar[2] = {left, right};
        CHECK(hibiki_asio_transport_push_planar_v1(
            region, region_size, planar, 2U, 4U) != 0);

        float interleaved[8] = {};
        std::uint32_t out_frames = 0U;
        std::uint32_t out_channels = 0U;
        std::uint32_t out_rate = 0U;
        CHECK(hibiki_asio_transport_pop_interleaved_v1(
            region, region_size, interleaved, 4U,
            &out_frames, &out_channels, &out_rate) != 0);
        CHECK(out_frames == 4U);
        CHECK(out_channels == 2U);
        CHECK(out_rate == 48000U);
        CHECK(interleaved[0] == 1.0F);
        CHECK(interleaved[1] == 0.25F);
        CHECK(interleaved[6] == -0.5F);
        CHECK(interleaved[7] == -0.75F);
    }

    // Unbind lifecycle: bound becomes false, pop fails.
    {
        hibiki::AsioTransportConsumerV1 consumer;
        CHECK(consumer.bind(L"HibikiTest1663H", 2U, 48000U, 64U));
        CHECK(consumer.bound());
        consumer.unbind();
        CHECK(!consumer.bound());
        hibiki::AsioTransportBlockV1 block{};
        std::array<float, 128> buffer{};
        CHECK(!consumer.pop(buffer.data(), 64U, block));
    }

#if defined(_WIN32)
    // Slot metadata is a shared-memory field, so the engine-side consumer
    // stages the C ABI result before re-checking it. Each rejected item is
    // consumed by the bounded SPSC ring without touching caller output, and
    // the next valid item remains usable. Structurally invalid heads are
    // discarded without asking the C ABI to copy beyond the fixed staging
    // bound.
    {
        constexpr wchar_t kMappingName[] = L"HibikiTest1663I";
        constexpr std::uint32_t kChannels = 2U;
        constexpr std::uint32_t kRate = 48000U;
        constexpr std::uint32_t kFrames = 4U;
        hibiki::AsioTransportConsumerV1 consumer;
        CHECK(consumer.bind(kMappingName, kChannels, kRate, kFrames));

        const auto region_bytes = hibiki_asio_transport_region_size_v1();
        const auto mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
                                              kMappingName);
        CHECK(mapping != nullptr);
        auto* region = static_cast<hibiki_asio_transport_region_v1*>(
            MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, region_bytes));
        CHECK(region != nullptr);

        const float left[kFrames] = {1.0F, -1.0F, 0.5F, -0.5F};
        const float right[kFrames] = {0.25F, -0.25F, 0.75F, -0.75F};
        const float* planar[kChannels] = {left, right};
        const auto push = [&]() {
            return hibiki_asio_transport_push_planar_v1(
                       region, region_bytes, planar, kChannels, kFrames) != 0;
        };
        constexpr float kCanary = -12345.0F;
        std::array<float, kChannels * kFrames + 2U> guarded_output{};
        guarded_output.front() = kCanary;
        guarded_output.back() = kCanary;
        auto* const output = guarded_output.data() + 1U;
        hibiki::AsioTransportBlockV1 block{99U, 99U, 99U};

        CHECK(push());
        region->slots[0].sample_rate = 44100U;
        CHECK(!consumer.pop(output, kFrames, block));
        CHECK(block.frames == 0U && block.channels == 0U && block.sample_rate == 0U);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        region->slots[1].channels = 1U;
        CHECK(!consumer.pop(output, kFrames, block));
        CHECK(block.frames == 0U && block.channels == 0U && block.sample_rate == 0U);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        region->slots[2].frames = 2U;
        CHECK(!consumer.pop(output, kFrames, block));
        CHECK(block.frames == 0U && block.channels == 0U && block.sample_rate == 0U);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        region->slots[3].channels = 8U;
        CHECK(!consumer.pop(output, kFrames, block));
        CHECK(block.frames == 0U && block.channels == 0U && block.sample_rate == 0U);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        CHECK(consumer.pop(output, kFrames, block));
        CHECK(block.frames == kFrames && block.channels == kChannels &&
              block.sample_rate == kRate);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        region->slots[1].frames = HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1 + 1U;
        CHECK(!consumer.pop(output, UINT32_MAX, block));
        CHECK(block.frames == 0U && block.channels == 0U && block.sample_rate == 0U);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        CHECK(consumer.pop(output, kFrames, block));
        CHECK(block.frames == kFrames && block.channels == kChannels &&
              block.sample_rate == kRate);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        region->slots[3].channels = HIBIKI_ASIO_TRANSPORT_MAX_CHANNELS_V1 + 1U;
        CHECK(!consumer.pop(output, kFrames, block));
        CHECK(block.frames == 0U && block.channels == 0U && block.sample_rate == 0U);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(push());
        CHECK(consumer.pop(output, kFrames, block));
        CHECK(block.frames == kFrames && block.channels == kChannels &&
              block.sample_rate == kRate);
        CHECK(guarded_output.front() == kCanary && guarded_output.back() == kCanary);

        CHECK(UnmapViewOfFile(region) != 0);
        CHECK(CloseHandle(mapping) != 0);
    }
#endif

    return 0;
}
