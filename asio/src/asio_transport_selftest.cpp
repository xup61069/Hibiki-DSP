// SPDX-License-Identifier: Apache-2.0
//
// Bounded offline self-test for hibiki_asio_transport_v1 (fixed-layout SPSC
// shared-memory ring). Deterministic in-memory fixtures only: no COM launch,
// no file writes, no vendor ASIO SDK, no driver or hardware involvement.

#include "hibiki/asio_transport_v1.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void check(const char* name, const bool ok) {
    std::printf("[%s] %s\n", ok ? "pass" : "FAIL", name);
    if (!ok) ++g_failures;
}

constexpr uint32_t kChannels = 8U;
constexpr uint32_t kRate = 48000U;
constexpr uint32_t kFrames = 128U;

alignas(alignof(struct hibiki_asio_transport_region_v1))
unsigned char g_region_bytes[sizeof(struct hibiki_asio_transport_region_v1)];

float g_planar[kChannels][kFrames];
const float* g_planar_ptrs[kChannels];
float g_interleaved[kFrames * kChannels];

struct hibiki_asio_transport_region_v1* region() {
    return reinterpret_cast<struct hibiki_asio_transport_region_v1*>(g_region_bytes);
}

bool init_default() {
    return hibiki_asio_transport_init_v1(region(), sizeof(g_region_bytes), kChannels, kRate, kFrames) == 1;
}

void fill_block(const float base) {
    for (uint32_t frame = 0U; frame < kFrames; ++frame) {
        for (uint32_t channel = 0U; channel < kChannels; ++channel) {
            const float value =
                base + static_cast<float>(frame) * 0.5f + static_cast<float>(channel) * 0.25f - 64.0f;
            g_planar[channel][frame] = value;
        }
    }
    for (uint32_t channel = 0U; channel < kChannels; ++channel) {
        g_planar_ptrs[channel] = g_planar[channel];
    }
}

bool fixtures_finite(const uint32_t channels, const uint32_t frames) {
    for (uint32_t frame = 0U; frame < frames; ++frame) {
        for (uint32_t channel = 0U; channel < channels; ++channel) {
            if (!std::isfinite(g_planar[channel][frame])) return false;
        }
    }
    return true;
}

bool case_region_size() {
    return hibiki_asio_transport_region_size_v1() == sizeof(struct hibiki_asio_transport_region_v1);
}

bool case_init_valid() {
    if (!init_default()) return false;
    const struct hibiki_asio_transport_region_v1* r = region();
    return r->magic == HIBIKI_ASIO_TRANSPORT_MAGIC_V1 &&
           r->abi_version == HIBIKI_ASIO_TRANSPORT_ABI_V1 &&
           r->size_bytes == sizeof(struct hibiki_asio_transport_region_v1) &&
           r->channels == kChannels && r->sample_rate == kRate &&
           r->frames_per_buffer == kFrames && r->producer_sequence == 0U &&
           r->consumer_sequence == 0U && r->dropped_blocks == 0U;
}

bool case_init_rejections() {
    struct hibiki_asio_transport_region_v1 scratch;
    const size_t full = sizeof(scratch);
    bool ok = true;
    ok = ok && hibiki_asio_transport_init_v1(NULL, full, kChannels, kRate, kFrames) == 0;
    ok = ok && hibiki_asio_transport_init_v1(&scratch, full - 1U, kChannels, kRate, kFrames) == 0;
    ok = ok && hibiki_asio_transport_init_v1(&scratch, full, 3U, kRate, kFrames) == 0;
    ok = ok && hibiki_asio_transport_init_v1(&scratch, full, 6U, 22050U, kFrames) == 0;
    ok = ok && hibiki_asio_transport_init_v1(&scratch, full, kChannels, kRate, 0U) == 0;
    ok = ok && hibiki_asio_transport_init_v1(&scratch, full, kChannels, kRate,
                                             HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1 + 1U) == 0;
    return ok;
}

bool case_roundtrip_exact() {
    if (!init_default()) return false;
    fill_block(0.0f);
    if (!fixtures_finite(kChannels, kFrames)) return false;
    if (hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                             g_planar_ptrs, kChannels, kFrames) != 1) {
        return false;
    }
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    std::memset(g_interleaved, 0, sizeof(g_interleaved));
    if (hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes), g_interleaved,
                                                 kFrames, &out_frames, &out_channels,
                                                 &out_rate) != 1) {
        return false;
    }
    if (out_frames != kFrames || out_channels != kChannels || out_rate != kRate) return false;
    for (uint32_t frame = 0U; frame < kFrames; ++frame) {
        for (uint32_t channel = 0U; channel < kChannels; ++channel) {
            if (g_interleaved[frame * kChannels + channel] != g_planar[channel][frame]) {
                return false;
            }
        }
    }
    return region()->producer_sequence == 1U && region()->consumer_sequence == 1U &&
           region()->dropped_blocks == 0U;
}

bool case_mapping_mismatch() {
    if (!init_default()) return false;
    fill_block(1.0f);
    float two_by[2][kFrames] = {};
    const float* two_ptrs[2] = {two_by[0], two_by[1]};
    bool ok = hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                   two_ptrs, 2U, kFrames) == 0;
    ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                    g_planar_ptrs, kChannels, 64U) == 0;
    ok = ok && region()->producer_sequence == 0U;
    return ok;
}

bool case_truncated_payload() {
    if (!init_default()) return false;
    fill_block(2.0f);
    const size_t short_bytes = sizeof(g_region_bytes) - 1U;
    bool ok = hibiki_asio_transport_push_planar_v1(region(), short_bytes,
                                                   g_planar_ptrs, kChannels, kFrames) == 0;
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), short_bytes, g_interleaved,
                                                        kFrames, &out_frames, &out_channels,
                                                        &out_rate) == 0;
    ok = ok && out_frames == 0U && out_channels == 0U && out_rate == 0U;
    return ok;
}

bool case_corrupted_header() {
    bool ok = true;
    for (size_t i = 0; i < 3; ++i) {
        if (!init_default()) return false;
        struct hibiki_asio_transport_region_v1* r = region();
        if (i == 0U) {
            r->magic = 0xDEADBEEFU;
        } else if (i == 1U) {
            r->abi_version = HIBIKI_ASIO_TRANSPORT_ABI_V1 + 1U;
        } else {
            r->size_bytes += 4U;
        }
        fill_block(3.0f);
        ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                        g_planar_ptrs, kChannels, kFrames) == 0;
        uint32_t out_frames = 9U;
        uint32_t out_channels = 9U;
        uint32_t out_rate = 9U;
        ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                            g_interleaved, kFrames, &out_frames,
                                                            &out_channels, &out_rate) == 0;
        ok = ok && region()->producer_sequence == 0U && region()->consumer_sequence == 0U;
    }
    return ok;
}

bool case_overflow_and_fifo() {
    if (!init_default()) return false;
    bool ok = true;
    for (uint32_t block = 0U; block < HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1; ++block) {
        fill_block(static_cast<float>(block));
        ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                        g_planar_ptrs, kChannels, kFrames) == 1;
    }
    ok = ok && region()->producer_sequence == HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1;
    fill_block(99.0f);
    ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                    g_planar_ptrs, kChannels, kFrames) == 0;
    ok = ok && region()->dropped_blocks == 1U;
    for (uint32_t block = 0U; block < HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1; ++block) {
        uint32_t out_frames = 0U;
        uint32_t out_channels = 0U;
        uint32_t out_rate = 0U;
        std::memset(g_interleaved, 0, sizeof(g_interleaved));
        ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                            g_interleaved, kFrames, &out_frames,
                                                            &out_channels, &out_rate) == 1;
        ok = ok && out_frames == kFrames;
        const float expected_head = static_cast<float>(block) - 64.0f;
        ok = ok && g_interleaved[0] == expected_head;
        ok = ok && g_interleaved[1] == expected_head + 0.25f;
    }
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                        g_interleaved, kFrames, &out_frames,
                                                        &out_channels, &out_rate) == 0;
    ok = ok && region()->consumer_sequence == region()->producer_sequence;
    fill_block(7.0f);
    ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                    g_planar_ptrs, kChannels, kFrames) == 1;
    return ok;
}

bool case_capacity_overflow_nonconsuming() {
    if (!init_default()) return false;
    fill_block(4.0f);
    if (hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                             g_planar_ptrs, kChannels, kFrames) != 1) {
        return false;
    }
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    const bool rejected = hibiki_asio_transport_pop_interleaved_v1(
                              region(), sizeof(g_region_bytes), g_interleaved, kFrames - 1U,
                              &out_frames, &out_channels, &out_rate) == 0;
    return rejected && out_frames == 0U && out_channels == 0U && out_rate == 0U &&
           region()->consumer_sequence == 0U && region()->producer_sequence == 1U;
}

bool case_empty_pop_zeroes_outputs() {
    if (!init_default()) return false;
    uint32_t out_frames = 7U;
    uint32_t out_channels = 7U;
    uint32_t out_rate = 7U;
    const bool rejected = hibiki_asio_transport_pop_interleaved_v1(
                              region(), sizeof(g_region_bytes), g_interleaved, kFrames,
                              &out_frames, &out_channels, &out_rate) == 0;
    return rejected && out_frames == 0U && out_channels == 0U && out_rate == 0U;
}

bool case_null_arguments() {
    if (!init_default()) return false;
    fill_block(5.0f);
    bool ok = hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                   NULL, kChannels, kFrames) == 0;
    float* bad_channel_array[2] = {g_planar[0], NULL};
    ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                    bad_channel_array, 2U, kFrames) == 0;
    ok = ok && hibiki_asio_transport_push_planar_v1(NULL, sizeof(g_region_bytes),
                                                    g_planar_ptrs, kChannels, kFrames) == 0;
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes), NULL,
                                                        kFrames, &out_frames, &out_channels,
                                                        &out_rate) == 0;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                        g_interleaved, kFrames, NULL,
                                                        &out_channels, &out_rate) == 0;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                        g_interleaved, kFrames, &out_frames,
                                                        NULL, &out_rate) == 0;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                        g_interleaved, kFrames, &out_frames,
                                                        &out_channels, NULL) == 0;
    ok = ok && region()->producer_sequence == 0U && region()->consumer_sequence == 0U;
    return ok;
}

bool case_fixture_nonfinite_rejected() {
    fill_block(6.0f);
    if (!fixtures_finite(kChannels, kFrames)) return false;
    const float saved = g_planar[3][17U];
    g_planar[3][17U] = static_cast<float>(std::nan(""));
    const bool caught = !fixtures_finite(kChannels, kFrames);
    g_planar[3][17U] = saved;
    return caught && fixtures_finite(kChannels, kFrames);
}

}  // namespace

int main() {
    check("region_size_matches_struct", case_region_size());
    check("init_valid_sets_header", case_init_valid());
    check("init_rejects_invalid_formats", case_init_rejections());
    check("roundtrip_8ch_bit_exact", case_roundtrip_exact());
    check("push_rejects_mapping_mismatch", case_mapping_mismatch());
    check("truncated_payload_fail_closed", case_truncated_payload());
    check("corrupted_header_fail_closed", case_corrupted_header());
    check("overflow_drops_and_fifo_order", case_overflow_and_fifo());
    check("pop_capacity_overflow_nonconsuming", case_capacity_overflow_nonconsuming());
    check("empty_pop_zeroes_outputs", case_empty_pop_zeroes_outputs());
    check("null_arguments_fail_closed", case_null_arguments());
    check("fixture_layer_rejects_nonfinite", case_fixture_nonfinite_rejected());

    if (g_failures != 0) {
        std::printf("asio transport selftest FAILED: %d case(s)\n", g_failures);
        return 1;
    }
    std::printf("asio transport selftest passed (12 cases)\n");
    return 0;
}
