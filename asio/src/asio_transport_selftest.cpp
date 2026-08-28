// SPDX-License-Identifier: Apache-2.0
//
// Bounded offline self-test for hibiki_asio_transport_v1 (fixed-layout SPSC
// shared-memory ring). Deterministic in-memory fixtures only: no COM launch,
// no file writes, no vendor ASIO SDK, no driver or hardware involvement.

#include "hibiki/asio_transport_v1.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

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

bool case_push_rejects_nonfinite() {
    struct nonfinite_case {
        float value;
        uint32_t channel;
        uint32_t frame;
    };
    const nonfinite_case cases[] = {
        {static_cast<float>(std::nan("")), 3U, 17U},
        {std::numeric_limits<float>::infinity(), 0U, 0U},
        {-std::numeric_limits<float>::infinity(), kChannels - 1U, kFrames - 1U},
    };

    for (const nonfinite_case& test_case : cases) {
        if (!init_default()) return false;
        fill_block(6.0f);
        if (!fixtures_finite(kChannels, kFrames)) return false;
        const float saved = g_planar[test_case.channel][test_case.frame];
        g_planar[test_case.channel][test_case.frame] = test_case.value;

        struct hibiki_asio_transport_region_v1* r = region();
        struct hibiki_asio_transport_slot_v1* slot = &r->slots[0];
        slot->ready_sequence = 0xA5A5A5A5U;
        slot->frames = 77U;
        slot->channels = 4U;
        slot->sample_rate = 44100U;
        const uint32_t sample_index = test_case.frame * kChannels + test_case.channel;
        const uint32_t sentinel_index = sample_index == 0U ? 1U : 0U;
        slot->samples[sentinel_index] = 12.5F;
        slot->samples[sample_index] = -9.5F;

        bool ok = hibiki_asio_transport_push_planar_v1(
                      r, sizeof(g_region_bytes), g_planar_ptrs, kChannels, kFrames) == 0;
        ok = ok && r->producer_sequence == 0U && r->consumer_sequence == 0U &&
             r->dropped_blocks == 0U && slot->ready_sequence == 0xA5A5A5A5U &&
             slot->frames == 77U && slot->channels == 4U && slot->sample_rate == 44100U &&
             slot->samples[sentinel_index] == 12.5F && slot->samples[sample_index] == -9.5F;

        g_planar[test_case.channel][test_case.frame] = saved;
        if (!ok || hibiki_asio_transport_push_planar_v1(r, sizeof(g_region_bytes),
                                                        g_planar_ptrs, kChannels, kFrames) != 1) {
            return false;
        }

        uint32_t out_frames = 0U;
        uint32_t out_channels = 0U;
        uint32_t out_rate = 0U;
        std::memset(g_interleaved, 0, sizeof(g_interleaved));
        if (hibiki_asio_transport_pop_interleaved_v1(
                r, sizeof(g_region_bytes), g_interleaved, kFrames, &out_frames, &out_channels,
                &out_rate) != 1 || out_frames != kFrames || out_channels != kChannels ||
            out_rate != kRate || g_interleaved[sample_index] != saved) {
            return false;
        }
    }
    return true;
}

// Fabricate one occupied slot as a long-running ring would have left it.
void stamp_slot(const uint32_t sequence, const float tag) {
    struct hibiki_asio_transport_region_v1* r = region();
    struct hibiki_asio_transport_slot_v1* slot =
        &r->slots[sequence % HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1];
    for (uint32_t frame = 0U; frame < kFrames; ++frame) {
        for (uint32_t channel = 0U; channel < kChannels; ++channel) {
            slot->samples[frame * kChannels + channel] =
                tag + static_cast<float>(frame) * 0.5f + static_cast<float>(channel) * 0.25f;
        }
    }
    slot->frames = kFrames;
    slot->channels = kChannels;
    slot->sample_rate = kRate;
    slot->ready_sequence = sequence + 1U;
}

bool pop_and_verify_head(const float expected_tag) {
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    std::memset(g_interleaved, 0, sizeof(g_interleaved));
    if (hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes), g_interleaved,
                                                 kFrames, &out_frames, &out_channels,
                                                 &out_rate) != 1) {
        return false;
    }
    return out_frames == kFrames && out_channels == kChannels && out_rate == kRate &&
           g_interleaved[0] == expected_tag &&
           g_interleaved[1] == expected_tag + 0.25f;
}

bool case_wraparound_push_pop_fifo() {
    if (!init_default()) return false;
    struct hibiki_asio_transport_region_v1* r = region();
    // Simulate a long-lived ring paused three blocks before the uint32 wrap:
    // consumer=0xFFFFFFFC, producer=0xFFFFFFFF, three outstanding slots (0,1,2).
    r->consumer_sequence = 0xFFFFFFFCU;
    r->producer_sequence = 0xFFFFFFFFU;
    stamp_slot(0xFFFFFFFCU, -900.0f);
    stamp_slot(0xFFFFFFFDU, -800.0f);
    stamp_slot(0xFFFFFFFEU, -700.0f);
    bool ok = pop_and_verify_head(-900.0f);
    ok = ok && r->consumer_sequence == 0xFFFFFFFDU;
    ok = ok && pop_and_verify_head(-800.0f);
    ok = ok && r->consumer_sequence == 0xFFFFFFFEU;
    ok = ok && pop_and_verify_head(-700.0f);
    ok = ok && r->consumer_sequence == 0xFFFFFFFFU;
    // Producer crosses the wrap: 0xFFFFFFFF + 1 lands in slot 3 with sequence 0.
    fill_block(8.0f);
    if (!fixtures_finite(kChannels, kFrames)) return false;
    ok = ok && hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                    g_planar_ptrs, kChannels, kFrames) == 1;
    ok = ok && r->producer_sequence == 0U;
    // ready_sequence wraps too: 0xFFFFFFFF + 1 truncates to 0, and the next
    // pop validates it against consumer + 1 which wraps identically.
    ok = ok && r->slots[3].ready_sequence == 0U;
    ok = ok && r->slots[3].frames == kFrames && r->slots[3].channels == kChannels;
    ok = ok && pop_and_verify_head(-56.0f);  // fill_block(8.0f) head sample
    ok = ok && r->consumer_sequence == 0U && r->dropped_blocks == 0U;
    return ok;
}

bool case_wraparound_overflow_accounting() {
    if (!init_default()) return false;
    struct hibiki_asio_transport_region_v1* r = region();
    // Wrapped and full: consumer=0xFFFFFFFC, producer=0x00000000 (4 outstanding).
    r->consumer_sequence = 0xFFFFFFFCU;
    r->producer_sequence = 0x00000000U;
    stamp_slot(0xFFFFFFFCU, -600.0f);
    stamp_slot(0xFFFFFFFDU, -500.0f);
    stamp_slot(0xFFFFFFFEU, -400.0f);
    stamp_slot(0xFFFFFFFFU, -300.0f);
    fill_block(9.0f);
    if (!fixtures_finite(kChannels, kFrames)) return false;
    bool ok = hibiki_asio_transport_push_planar_v1(region(), sizeof(g_region_bytes),
                                                   g_planar_ptrs, kChannels, kFrames) == 0;
    ok = ok && r->dropped_blocks == 1U;
    ok = ok && r->producer_sequence == 0U && r->consumer_sequence == 0xFFFFFFFCU;
    // Non-consuming capacity overflow at the wrapped state leaves sequences alone.
    uint32_t out_frames = 0U;
    uint32_t out_channels = 0U;
    uint32_t out_rate = 0U;
    ok = ok && hibiki_asio_transport_pop_interleaved_v1(region(), sizeof(g_region_bytes),
                                                        g_interleaved, kFrames - 1U, &out_frames,
                                                        &out_channels, &out_rate) == 0;
    ok = ok && r->consumer_sequence == 0xFFFFFFFCU && r->producer_sequence == 0U;
    ok = ok && pop_and_verify_head(-600.0f);  // stamp_slot(-600.0f) head sample
    ok = ok && r->consumer_sequence == 0xFFFFFFFDU;
    return ok;
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
    check("push_rejects_nonfinite", case_push_rejects_nonfinite());
    check("wraparound_push_pop_fifo", case_wraparound_push_pop_fifo());
    check("wraparound_overflow_accounting", case_wraparound_overflow_accounting());

    if (g_failures != 0) {
        std::printf("asio transport selftest FAILED: %d case(s)\n", g_failures);
        return 1;
    }
    std::printf("asio transport selftest passed (14 cases)\n");
    return 0;
}
