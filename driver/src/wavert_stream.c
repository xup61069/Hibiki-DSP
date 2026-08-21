// SPDX-License-Identifier: MS-PL

#include "hibiki/wavert_stream_v1.h"

#include <string.h>

static int valid_channels(const uint32_t channels) {
    return channels == 2U || channels == 6U || channels == 8U;
}

static int valid_rate(const uint32_t rate) {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

static size_t frame_bytes(const uint32_t channels) {
    return (size_t)channels * HIBIKI_WAVERT_STREAM_BYTES_PER_SAMPLE_V1;
}

static int valid_stream(const struct hibiki_wavert_stream_v1* const stream) {
    if (stream == NULL || stream->storage == NULL || !valid_channels(stream->channels) ||
        !valid_rate(stream->sample_rate) || stream->frames_per_period == 0U ||
        stream->frames_per_period > HIBIKI_WAVERT_STREAM_MAX_PERIOD_FRAMES_V1 ||
        stream->period_count < 2U || stream->period_count > HIBIKI_WAVERT_STREAM_MAX_PERIOD_COUNT_V1 ||
        stream->capacity_frames < (size_t)stream->frames_per_period * 2U ||
        stream->capacity_frames > (size_t)HIBIKI_WAVERT_STREAM_MAX_PERIOD_FRAMES_V1 *
                                      HIBIKI_WAVERT_STREAM_MAX_PERIOD_COUNT_V1 ||
        stream->read_frame >= stream->capacity_frames ||
        stream->write_frame >= stream->capacity_frames ||
        stream->available_frames > stream->capacity_frames ||
        stream->storage_bytes < stream->capacity_frames * frame_bytes(stream->channels)) {
        return 0;
    }
    return 1;
}

static void add_counter(uint64_t* const counter, const uint64_t amount) {
    if (counter == NULL) return;
    if (UINT64_MAX - *counter < amount) {
        *counter = UINT64_MAX;
    } else {
        *counter += amount;
    }
}

static void copy_into_ring(struct hibiki_wavert_stream_v1* const stream,
                           const uint8_t* const source,
                           const uint32_t frames) {
    const size_t bytes = frame_bytes(stream->channels);
    for (uint32_t frame = 0U; frame < frames; ++frame) {
        const size_t slot = (stream->write_frame + frame) % stream->capacity_frames;
        memcpy(stream->storage + slot * bytes, source + (size_t)frame * bytes, bytes);
    }
    stream->write_frame = (stream->write_frame + frames) % stream->capacity_frames;
    stream->available_frames += frames;
}

static void copy_from_ring(struct hibiki_wavert_stream_v1* const stream,
                           uint8_t* const destination,
                           const uint32_t frames) {
    const size_t bytes = frame_bytes(stream->channels);
    for (uint32_t frame = 0U; frame < frames; ++frame) {
        const size_t slot = (stream->read_frame + frame) % stream->capacity_frames;
        memcpy(destination + (size_t)frame * bytes, stream->storage + slot * bytes, bytes);
    }
    stream->read_frame = (stream->read_frame + frames) % stream->capacity_frames;
    stream->available_frames -= frames;
}

int hibiki_wavert_stream_init_v1(
    struct hibiki_wavert_stream_v1* const stream,
    uint8_t* const storage,
    const size_t storage_bytes,
    const uint32_t channels,
    const uint32_t sample_rate,
    const uint32_t frames_per_period,
    const uint32_t period_count) {
    if (stream == NULL || storage == NULL || !valid_channels(channels) ||
        !valid_rate(sample_rate) || frames_per_period == 0U ||
        frames_per_period > HIBIKI_WAVERT_STREAM_MAX_PERIOD_FRAMES_V1 ||
        period_count < 2U || period_count > HIBIKI_WAVERT_STREAM_MAX_PERIOD_COUNT_V1) {
        return HIBIKI_WAVERT_STREAM_REJECTED_V1;
    }
    const size_t bytes = frame_bytes(channels);
    const size_t capacity = storage_bytes / bytes;
    const size_t minimum_capacity = (size_t)frames_per_period * 2U;
    const size_t requested_capacity = (size_t)frames_per_period * period_count;
    if (capacity < minimum_capacity || capacity < requested_capacity ||
        capacity > (size_t)HIBIKI_WAVERT_STREAM_MAX_PERIOD_FRAMES_V1 *
                       HIBIKI_WAVERT_STREAM_MAX_PERIOD_COUNT_V1) {
        return HIBIKI_WAVERT_STREAM_REJECTED_V1;
    }
    memset(stream, 0, sizeof(*stream));
    stream->storage = storage;
    stream->storage_bytes = storage_bytes;
    stream->capacity_frames = capacity;
    stream->channels = channels;
    stream->sample_rate = sample_rate;
    stream->frames_per_period = frames_per_period;
    stream->period_count = period_count;
    return HIBIKI_WAVERT_STREAM_OK_V1;
}

void hibiki_wavert_stream_reset_v1(struct hibiki_wavert_stream_v1* const stream) {
    if (stream == NULL) return;
    stream->read_frame = 0U;
    stream->write_frame = 0U;
    stream->available_frames = 0U;
    stream->dropped_frames = 0U;
    stream->underrun_frames = 0U;
}

int hibiki_wavert_stream_push_v1(
    struct hibiki_wavert_stream_v1* const stream,
    const uint8_t* const interleaved_pcm,
    const uint32_t frames) {
    if (!valid_stream(stream) || interleaved_pcm == NULL || frames == 0U ||
        (size_t)frames > stream->capacity_frames - stream->available_frames) {
        if (stream != NULL && frames != 0U) add_counter(&stream->dropped_frames, frames);
        return HIBIKI_WAVERT_STREAM_REJECTED_V1;
    }
    copy_into_ring(stream, interleaved_pcm, frames);
    return HIBIKI_WAVERT_STREAM_OK_V1;
}

int hibiki_wavert_stream_pop_v1(
    struct hibiki_wavert_stream_v1* const stream,
    uint8_t* const interleaved_pcm,
    const uint32_t frames) {
    if (!valid_stream(stream) || interleaved_pcm == NULL || frames == 0U ||
        (size_t)frames > stream->available_frames) {
        return HIBIKI_WAVERT_STREAM_REJECTED_V1;
    }
    copy_from_ring(stream, interleaved_pcm, frames);
    return HIBIKI_WAVERT_STREAM_OK_V1;
}

int hibiki_wavert_stream_pop_or_silence_v1(
    struct hibiki_wavert_stream_v1* const stream,
    uint8_t* const interleaved_pcm,
    const uint32_t frames) {
    if (!valid_stream(stream) || interleaved_pcm == NULL || frames == 0U) {
        return HIBIKI_WAVERT_STREAM_REJECTED_V1;
    }
    const size_t bytes = frame_bytes(stream->channels);
    const size_t available = stream->available_frames;
    const uint32_t copied = available >= frames ? frames : (uint32_t)available;
    if (copied > 0U) copy_from_ring(stream, interleaved_pcm, copied);
    if (copied < frames) {
        memset(interleaved_pcm + (size_t)copied * bytes, 0,
               ((size_t)frames - copied) * bytes);
        add_counter(&stream->underrun_frames, (uint64_t)frames - copied);
        return HIBIKI_WAVERT_STREAM_UNDERRUN_V1;
    }
    return HIBIKI_WAVERT_STREAM_OK_V1;
}
