// SPDX-License-Identifier: MS-PL
//
// WDK-only pin adapter. The portable stream core owns no synchronization;
// this boundary serializes the WaveRT callback and engine-side submit path at
// DISPATCH_LEVEL without allocation, COM or GPL/user-space linkage.

#if !defined(_NTDDK_)
#error "Compile this adapter only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>

#include "hibiki/endpoint_topology_v1.h"
#include "hibiki/wavert_stream_v1.h"

struct hibiki_wdk_stream_context_v1 {
    KSPIN_LOCK lock;
    hibiki_wavert_stream_v1 stream;
};

extern "C" NTSTATUS HibikiWaveRtPinInitializeV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG channels,
    _In_ ULONG sample_rate,
    _In_ ULONG frames_per_period,
    _In_ ULONG period_count);

static NTSTATUS hibiki_wdk_pin_initialize_endpoint_v1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG endpoint_index,
    _In_ ULONG period_count,
    _In_ ULONG expected_direction) {
    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(endpoint_index, &topology) == 0 ||
        (expected_direction != MAXULONG && topology.direction != expected_direction)) {
        return STATUS_INVALID_PARAMETER;
    }
    return HibikiWaveRtPinInitializeV1(
        context, storage, storage_bytes, topology.channel_count, topology.sample_rate,
        topology.frames_per_buffer, period_count);
}

extern "C" NTSTATUS HibikiWaveRtPinInitializeV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG channels,
    _In_ ULONG sample_rate,
    _In_ ULONG frames_per_period,
    _In_ ULONG period_count) {
    if (context == nullptr || storage == nullptr || storage_bytes > MAXSIZE_T) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(context, sizeof(*context));
    KeInitializeSpinLock(&context->lock);
    if (hibiki_wavert_stream_init_v1(
            &context->stream, storage, static_cast<size_t>(storage_bytes), channels,
            sample_rate, frames_per_period, period_count) != HIBIKI_WAVERT_STREAM_OK_V1) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS HibikiWaveRtPinInitializeEndpointV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG endpoint_index,
    _In_ ULONG period_count) {
    return hibiki_wdk_pin_initialize_endpoint_v1(
        context, storage, storage_bytes, endpoint_index, period_count,
        HIBIKI_ENDPOINT_DIRECTION_RENDER_V1);
}

extern "C" NTSTATUS HibikiWaveRtPinInitializeCaptureEndpointV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG endpoint_index,
    _In_ ULONG period_count) {
    return hibiki_wdk_pin_initialize_endpoint_v1(
        context, storage, storage_bytes, endpoint_index, period_count,
        HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1);
}

extern "C" NTSTATUS HibikiWaveRtBuildFormatEndpointV1(
    _In_ ULONG endpoint_index,
    _Out_ WAVEFORMATEXTENSIBLE* format) {
    if (format == nullptr) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(format, sizeof(*format));
    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(endpoint_index, &topology) == 0 ||
        topology.channel_mask > MAXULONG) {
        return STATUS_INVALID_PARAMETER;
    }
    format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format->Format.nChannels = static_cast<WORD>(topology.channel_count);
    format->Format.nSamplesPerSec = topology.sample_rate;
    format->Format.wBitsPerSample = 32U;
    format->Format.nBlockAlign = static_cast<WORD>(topology.channel_count * 4U);
    format->Format.nAvgBytesPerSec = topology.sample_rate * format->Format.nBlockAlign;
    format->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format->Samples.wValidBitsPerSample = 32U;
    format->dwChannelMask = static_cast<DWORD>(topology.channel_mask);
    format->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS HibikiWaveRtBuildFormatV1(
    _In_ ULONG endpoint_index,
    _Out_ WAVEFORMATEXTENSIBLE* format) {
    if (format == nullptr) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(format, sizeof(*format));
    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(endpoint_index, &topology) == 0 ||
        topology.direction != HIBIKI_ENDPOINT_DIRECTION_RENDER_V1) {
        return STATUS_INVALID_PARAMETER;
    }
    return HibikiWaveRtBuildFormatEndpointV1(endpoint_index, format);
}

extern "C" NTSTATUS HibikiWaveRtPinSubmitRenderV1(
    _Inout_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(frames * context->stream.channels * 4) const uint8_t* samples,
    _In_ ULONG frames) {
    if (context == nullptr || samples == nullptr || frames == 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    KIRQL old_irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&context->lock, &old_irql);
    const int result = hibiki_wavert_stream_push_v1(&context->stream, samples, frames);
    KeReleaseSpinLock(&context->lock, old_irql);
    return result == HIBIKI_WAVERT_STREAM_OK_V1 ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

extern "C" NTSTATUS HibikiWaveRtPinReadRenderV1(
    _Inout_ hibiki_wdk_stream_context_v1* context,
    _Out_writes_bytes_(frames * context->stream.channels * 4) uint8_t* samples,
    _In_ ULONG frames) {
    if (context == nullptr || samples == nullptr || frames == 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    KIRQL old_irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&context->lock, &old_irql);
    const int result = hibiki_wavert_stream_pop_or_silence_v1(&context->stream, samples, frames);
    KeReleaseSpinLock(&context->lock, old_irql);
    return result == HIBIKI_WAVERT_STREAM_REJECTED_V1 ? STATUS_INVALID_DEVICE_STATE
                                                       : STATUS_SUCCESS;
}

extern "C" void HibikiWaveRtPinResetV1(
    _Inout_ hibiki_wdk_stream_context_v1* context) {
    if (context == nullptr) return;
    KIRQL old_irql = PASSIVE_LEVEL;
    KeAcquireSpinLock(&context->lock, &old_irql);
    hibiki_wavert_stream_reset_v1(&context->stream);
    KeReleaseSpinLock(&context->lock, old_irql);
}
