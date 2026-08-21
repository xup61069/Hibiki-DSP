#ifndef HIBIKI_WAVERT_STREAM_V1_H
#define HIBIKI_WAVERT_STREAM_V1_H

// SPDX-License-Identifier: MS-PL

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HIBIKI_WAVERT_STREAM_ABI_V1 1u
#define HIBIKI_WAVERT_STREAM_BYTES_PER_SAMPLE_V1 4u
#define HIBIKI_WAVERT_STREAM_MAX_CHANNELS_V1 8u
#define HIBIKI_WAVERT_STREAM_MAX_PERIOD_FRAMES_V1 4096u
#define HIBIKI_WAVERT_STREAM_MAX_PERIOD_COUNT_V1 16u

enum hibiki_wavert_stream_result_v1 {
    HIBIKI_WAVERT_STREAM_OK_V1 = 1,
    HIBIKI_WAVERT_STREAM_REJECTED_V1 = 0,
    HIBIKI_WAVERT_STREAM_UNDERRUN_V1 = -1
};

// Single-producer/single-consumer byte ring. Synchronization between the
// WaveRT callback and the engine worker is supplied by the embedding driver;
// this core itself never allocates, blocks or takes a lock.
struct hibiki_wavert_stream_v1 {
    uint8_t* storage;
    size_t storage_bytes;
    size_t capacity_frames;
    size_t read_frame;
    size_t write_frame;
    size_t available_frames;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t frames_per_period;
    uint32_t period_count;
    uint64_t dropped_frames;
    uint64_t underrun_frames;
};

int hibiki_wavert_stream_init_v1(
    struct hibiki_wavert_stream_v1* stream,
    uint8_t* storage,
    size_t storage_bytes,
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t frames_per_period,
    uint32_t period_count);

void hibiki_wavert_stream_reset_v1(struct hibiki_wavert_stream_v1* stream);

int hibiki_wavert_stream_push_v1(
    struct hibiki_wavert_stream_v1* stream,
    const uint8_t* interleaved_pcm,
    uint32_t frames);

int hibiki_wavert_stream_pop_v1(
    struct hibiki_wavert_stream_v1* stream,
    uint8_t* interleaved_pcm,
    uint32_t frames);

// Always writes the requested number of frames. Missing frames are zero-filled
// and counted as underrun; this is the safe render-callback fallback.
int hibiki_wavert_stream_pop_or_silence_v1(
    struct hibiki_wavert_stream_v1* stream,
    uint8_t* interleaved_pcm,
    uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif
