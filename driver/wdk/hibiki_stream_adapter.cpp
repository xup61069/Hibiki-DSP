// SPDX-License-Identifier: MS-PL
//
// WDK-only pin adapter. The portable stream core owns no synchronization;
// this boundary serializes the WaveRT callback and engine-side submit path at
// DISPATCH_LEVEL without allocation, COM or GPL/user-space linkage.

#if !defined(_NTDDK_)
#error "Compile this adapter only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>

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
