// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Miniport Implementation.
// Bridges standard PortCls COM interfaces (IMiniportWaveRT,
// IMiniportWaveRTStreamNotification) to the portable stream/topology/property cores.
// Free of GPL linkage, non-allocating in streaming path.

#if !defined(_NTDDK_)
#error "Compile this adapter only inside a WDK PortCls driver project"
#endif

#include "hibiki_miniport_wavert.h"
#include "hibiki_filter_tables.h"

// Forward declaration from hibiki_stream_adapter.cpp
extern "C" NTSTATUS HibikiWaveRtPinInitializeV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG channels,
    _In_ ULONG sample_rate,
    _In_ ULONG frames_per_period,
    _In_ ULONG period_count);

extern "C" NTSTATUS HibikiWaveRtPinInitializeEndpointV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG endpoint_index,
    _In_ ULONG period_count);

extern "C" NTSTATUS HibikiWaveRtPinInitializeCaptureEndpointV1(
    _Out_ hibiki_wdk_stream_context_v1* context,
    _In_reads_bytes_(storage_bytes) uint8_t* storage,
    _In_ SIZE_T storage_bytes,
    _In_ ULONG endpoint_index,
    _In_ ULONG period_count);

extern "C" void HibikiWaveRtPinResetV1(
    _Inout_ hibiki_wdk_stream_context_v1* context);

extern "C" NTSTATUS HibikiPropertyContextInitializeEndpointV1(
    _Out_ hibiki_wdk_endpoint_context_v1* context,
    _In_ ULONG endpoint_index,
    _In_ ULONG actuator);

static bool HibikiWaveRtFormatMatchesEndpointV1(
    _In_ PKSDATAFORMAT                         DataFormat,
    _In_ const hibiki_endpoint_topology_v1*    Topology) {
    if (DataFormat == nullptr || Topology == nullptr ||
        DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE)) {
        return false;
    }

    if (!IsEqualGUIDAligned(DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataFormat->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
        !IsEqualGUIDAligned(DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) {
        return false;
    }

    const auto* format = reinterpret_cast<const KSDATAFORMAT_WAVEFORMATEXTENSIBLE*>(DataFormat);
    const ULONG bytes_per_frame = Topology->channel_count * sizeof(float);
    const ULONG expected_avg_bytes = Topology->sample_rate * bytes_per_frame;
    const ULONG required_cb_size = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

    return format->WaveFormatExt.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->WaveFormatExt.Format.nChannels == Topology->channel_count &&
        format->WaveFormatExt.Format.nSamplesPerSec == Topology->sample_rate &&
        format->WaveFormatExt.Format.wBitsPerSample == 32U &&
        format->WaveFormatExt.Format.nBlockAlign == bytes_per_frame &&
        format->WaveFormatExt.Format.nAvgBytesPerSec == expected_avg_bytes &&
        format->WaveFormatExt.Format.cbSize >= required_cb_size &&
        format->WaveFormatExt.Samples.wValidBitsPerSample == 32U &&
        format->WaveFormatExt.dwChannelMask == Topology->channel_mask &&
        IsEqualGUIDAligned(format->WaveFormatExt.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

static bool HibikiWaveRtStateTransitionAllowedV1(
    _In_ KSSTATE Current,
    _In_ KSSTATE Next) {
    if (Current == Next) {
        return true;
    }

    switch (Current) {
        case KSSTATE_STOP:
            return Next == KSSTATE_ACQUIRE;
        case KSSTATE_ACQUIRE:
            return Next == KSSTATE_PAUSE || Next == KSSTATE_STOP;
        case KSSTATE_PAUSE:
            return Next == KSSTATE_RUN || Next == KSSTATE_ACQUIRE;
        case KSSTATE_RUN:
            return Next == KSSTATE_PAUSE;
        default:
            return false;
    }
}

//=============================================================================
// HibikiMiniportWaveRtStreamV1 Implementation
//=============================================================================

HibikiMiniportWaveRtStreamV1::HibikiMiniportWaveRtStreamV1()
    : m_RefCount(1),
      m_Miniport(nullptr),
      m_PortStream(nullptr),
      m_Pin(0),
      m_Capture(FALSE),
      m_State(KSSTATE_STOP),
      m_EndpointIndex(0),
      m_DmaBuffer(nullptr),
      m_DmaBufferMdl(nullptr),
      m_DmaBufferSize(0),
      m_AllocatedBytes(0),
      m_StreamInitialized(FALSE),
      m_NotificationEventCount(0),
      m_BufferNotificationCount(0),
      m_PositionTimerActive(0),
      m_RunStartTime100ns(0),
      m_RunBaseBytes(0),
      m_BytesPerSecond(0),
      m_LastNotificationBoundary(0),
      m_TotalBytesProcessed(0) {
    KeInitializeSpinLock(&m_NotificationLock);
    KeInitializeSpinLock(&m_PositionLock);
    KeInitializeTimer(&m_PositionTimer);
    KeInitializeDpc(&m_PositionDpc, &HibikiMiniportWaveRtStreamV1::PositionTimerDpc, this);
    ExInitializeRundownProtection(&m_PositionDpcRundown);
    ExWaitForRundownProtectionRelease(&m_PositionDpcRundown);
    ExRundownCompleted(&m_PositionDpcRundown);
    RtlZeroMemory(&m_StreamContext, sizeof(m_StreamContext));
    RtlZeroMemory(m_NotificationEvents, sizeof(m_NotificationEvents));
}

HibikiMiniportWaveRtStreamV1::~HibikiMiniportWaveRtStreamV1() {
    StopPositionTimer();
    if (m_PortStream != nullptr) {
        m_PortStream->Release();
        m_PortStream = nullptr;
    }
    if (m_Miniport != nullptr) {
        m_Miniport->Release();
        m_Miniport = nullptr;
    }
}

STDMETHODIMP HibikiMiniportWaveRtStreamV1::QueryInterface(
    _In_ REFIID Interface,
    _Out_ PVOID* Object) {
    if (Object == nullptr) return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(Interface, IID_IUnknown) ||
        IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream) ||
        IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStreamNotification)) {
        *Object = static_cast<IMiniportWaveRTStreamNotification*>(this);
        AddRef();
        return STATUS_SUCCESS;
    }

    *Object = nullptr;
    return STATUS_NOINTERFACE;
}

STDMETHODIMP_(ULONG) HibikiMiniportWaveRtStreamV1::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_RefCount));
}

STDMETHODIMP_(ULONG) HibikiMiniportWaveRtStreamV1::Release() {
    const LONG count = InterlockedDecrement(&m_RefCount);
    if (count == 0) {
        delete this;
        return 0;
    }
    return static_cast<ULONG>(count);
}

VOID HibikiMiniportWaveRtStreamV1::PositionTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    auto* stream = static_cast<HibikiMiniportWaveRtStreamV1*>(DeferredContext);
    if (stream == nullptr ||
        !ExAcquireRundownProtection(&stream->m_PositionDpcRundown)) {
        return;
    }

    BOOLEAN notify = FALSE;
    KeAcquireSpinLockAtDpcLevel(&stream->m_PositionLock);
    stream->UpdatePositionLocked(KeQueryInterruptTime(), &notify);
    KeReleaseSpinLockFromDpcLevel(&stream->m_PositionLock);

    if (notify) {
        stream->SignalNotificationEventsAtDpc();
    }

    ExReleaseRundownProtection(&stream->m_PositionDpcRundown);
}

VOID HibikiMiniportWaveRtStreamV1::StartPositionTimer() {
    if (InterlockedCompareExchange(&m_PositionTimerActive, 0, 0) != 0) {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_PositionLock, &oldIrql);
    if (!m_StreamInitialized || m_DmaBufferSize == 0 || m_BytesPerSecond == 0) {
        KeReleaseSpinLock(&m_PositionLock, oldIrql);
        return;
    }

    const ULONGLONG now100ns = KeQueryInterruptTime();
    m_RunStartTime100ns = now100ns;
    m_RunBaseBytes = m_TotalBytesProcessed;
    m_LastNotificationBoundary =
        NotificationBoundaryLocked(m_TotalBytesProcessed);
    KeReleaseSpinLock(&m_PositionLock, oldIrql);

    ExReInitializeRundownProtection(&m_PositionDpcRundown);
    InterlockedExchange(&m_PositionTimerActive, 1);

    LARGE_INTEGER dueTime{};
    dueTime.QuadPart = -10000; // 1 ms in relative 100ns units.
    KeSetTimerEx(&m_PositionTimer, dueTime, 1, &m_PositionDpc);
}

VOID HibikiMiniportWaveRtStreamV1::StopPositionTimer() {
    const LONG wasActive = InterlockedExchange(&m_PositionTimerActive, 0);
    KeCancelTimer(&m_PositionTimer);
    KeRemoveQueueDpc(&m_PositionDpc);

    if (wasActive != 0) {
        // The wait prevents new DPC callbacks from acquiring the stream and
        // drains any callback already in flight before the buffer is released.
        ExWaitForRundownProtectionRelease(&m_PositionDpcRundown);
        ExRundownCompleted(&m_PositionDpcRundown);
    }
}

ULONGLONG HibikiMiniportWaveRtStreamV1::NotificationBoundaryLocked(
    _In_ ULONGLONG TotalBytes) const {
    if (m_BufferNotificationCount == 0 || m_DmaBufferSize == 0) {
        return 0;
    }

    const ULONGLONG maxValue = static_cast<ULONGLONG>(-1);
    const ULONGLONG bufferSize = static_cast<ULONGLONG>(m_DmaBufferSize);
    const ULONGLONG notificationCount =
        static_cast<ULONGLONG>(m_BufferNotificationCount);
    const ULONGLONG bufferNumber = TotalBytes / bufferSize;
    ULONGLONG boundary = bufferNumber > maxValue / notificationCount
        ? maxValue
        : bufferNumber * notificationCount;

    // The WDK contract admits one or two notifications.  For two, use the
    // first byte at or past the halfway point; the full-buffer boundary is
    // represented by the next cycle's base ordinal.
    if (notificationCount == 2 &&
        (TotalBytes % bufferSize) >= (bufferSize + 1U) / 2U &&
        boundary != maxValue) {
        ++boundary;
    }
    return boundary;
}

VOID HibikiMiniportWaveRtStreamV1::UpdatePositionLocked(
    _In_ ULONGLONG Now100ns,
    _Out_ BOOLEAN* Notify) {
    if (Notify == nullptr) {
        return;
    }

    *Notify = FALSE;
    if (InterlockedCompareExchange(&m_PositionTimerActive, 0, 0) == 0 ||
        !m_StreamInitialized || m_DmaBufferSize == 0 || m_BytesPerSecond == 0 ||
        Now100ns < m_RunStartTime100ns) {
        return;
    }

    const ULONGLONG ticksPerSecond = 10000000ULL;
    const ULONGLONG maxValue = static_cast<ULONGLONG>(-1);
    const ULONGLONG elapsed100ns = Now100ns - m_RunStartTime100ns;
    const ULONGLONG wholeSeconds = elapsed100ns / ticksPerSecond;
    const ULONGLONG remainder100ns = elapsed100ns % ticksPerSecond;
    const ULONGLONG bytesPerSecond = m_BytesPerSecond;

    const ULONGLONG wholeBytes = wholeSeconds > maxValue / bytesPerSecond
        ? maxValue
        : wholeSeconds * bytesPerSecond;
    const ULONGLONG partialProduct = remainder100ns > maxValue / bytesPerSecond
        ? maxValue
        : remainder100ns * bytesPerSecond;
    const ULONGLONG partialBytes = partialProduct / ticksPerSecond;
    const ULONGLONG elapsedBytes = wholeBytes > maxValue - partialBytes
        ? maxValue
        : wholeBytes + partialBytes;
    const ULONGLONG totalBytes = m_RunBaseBytes > maxValue - elapsedBytes
        ? maxValue
        : m_RunBaseBytes + elapsedBytes;

    if (totalBytes <= m_TotalBytesProcessed) {
        return;
    }

    m_TotalBytesProcessed = totalBytes;
    const ULONGLONG currentBoundary = NotificationBoundaryLocked(totalBytes);
    if (currentBoundary > m_LastNotificationBoundary) {
        m_LastNotificationBoundary = currentBoundary;
        *Notify = TRUE;
    }
}

VOID HibikiMiniportWaveRtStreamV1::SignalNotificationEventsAtDpc() {
    KeAcquireSpinLockAtDpcLevel(&m_NotificationLock);
    for (ULONG i = 0; i < m_NotificationEventCount; ++i) {
        PKEVENT notificationEvent = m_NotificationEvents[i];
        if (notificationEvent == nullptr) {
            continue;
        }

        // PortCls supplies kernel event objects, but the registration API
        // explicitly permits defensive exception handling around signaling.
        __try {
            KeSetEvent(notificationEvent, IO_NO_INCREMENT, FALSE);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    KeReleaseSpinLockFromDpcLevel(&m_NotificationLock);
}

NTSTATUS HibikiMiniportWaveRtStreamV1::Init(
    _In_ HibikiMiniportWaveRtV1*   Miniport,
    _In_ PPORTWAVERTSTREAM         PortStream,
    _In_ ULONG                     Pin,
    _In_ BOOLEAN                   Capture,
    _In_ PKSDATAFORMAT             DataFormat,
    _In_ ULONG                     EndpointIndex) {
    if (Miniport == nullptr || PortStream == nullptr || DataFormat == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(EndpointIndex, &topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&topology) == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Verify direction matching
    const ULONG expected_direction = Capture ? HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1
                                             : HIBIKI_ENDPOINT_DIRECTION_RENDER_V1;
    const ULONG expected_pin = Capture ? 1U : 0U;
    if (topology.direction != expected_direction || Pin != expected_pin) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!HibikiWaveRtFormatMatchesEndpointV1(DataFormat, &topology)) {
        return STATUS_INVALID_PARAMETER;
    }

    m_Miniport = Miniport;
    m_Miniport->AddRef();

    m_PortStream = PortStream;
    m_PortStream->AddRef();

    m_Pin = Pin;
    m_Capture = Capture;
    m_EndpointIndex = EndpointIndex;
    m_State = KSSTATE_STOP;

    return STATUS_SUCCESS;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::AllocateAudioBuffer(
    _In_  ULONG                   RequestedSize,
    _Out_ PMDL*                   AudioBufferMdl,
    _Out_ ULONG*                  ActualSize,
    _Out_ ULONG*                  OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE*    CacheType) {
    return AllocateBufferCore(
        RequestedSize, 0U, AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
}

VOID HibikiMiniportWaveRtStreamV1::FreeAudioBuffer(
    _In_opt_ PMDL                 AudioBufferMdl,
    _In_     ULONG                BufferSize) {
    FreeBufferWithNotification(AudioBufferMdl, BufferSize);
}

NTSTATUS HibikiMiniportWaveRtStreamV1::AllocateBufferWithNotification(
    _In_     ULONG                NotificationCount,
    _In_     ULONG                RequestedSize,
    _Out_    PMDL*                AudioBufferMdl,
    _Out_    ULONG*               ActualSize,
    _Out_    ULONG*               OffsetFromFirstPage,
    _Out_    MEMORY_CACHING_TYPE* CacheType) {
    if (NotificationCount != 1U && NotificationCount != 2U) {
        return STATUS_INVALID_PARAMETER;
    }

    return AllocateBufferCore(
        RequestedSize, NotificationCount, AudioBufferMdl, ActualSize,
        OffsetFromFirstPage, CacheType);
}

NTSTATUS HibikiMiniportWaveRtStreamV1::AllocateBufferCore(
    _In_  ULONG                   RequestedSize,
    _In_  ULONG                   NotificationCount,
    _Out_ PMDL*                   AudioBufferMdl,
    _Out_ ULONG*                  ActualSize,
    _Out_ ULONG*                  OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE*    CacheType) {
    if (AudioBufferMdl == nullptr || ActualSize == nullptr ||
        OffsetFromFirstPage == nullptr || CacheType == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    if (m_StreamInitialized || m_DmaBuffer != nullptr || m_DmaBufferMdl != nullptr) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    *AudioBufferMdl = nullptr;
    *ActualSize = 0;
    *OffsetFromFirstPage = 0;
    *CacheType = MmCached;

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(m_EndpointIndex, &topology) == 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    const ULONG period_count = 2U; // Default double-buffered period count
    const SIZE_T bytes_per_frame = static_cast<SIZE_T>(topology.channel_count) * sizeof(float);
    const SIZE_T period_bytes = static_cast<SIZE_T>(topology.frames_per_buffer) * bytes_per_frame;
    const SIZE_T maximum_size =
        static_cast<SIZE_T>(HIBIKI_WAVERT_STREAM_MAX_PERIOD_FRAMES_V1) *
        HIBIKI_WAVERT_STREAM_MAX_PERIOD_COUNT_V1 * bytes_per_frame;
    if (period_bytes > maximum_size / period_count ||
        static_cast<SIZE_T>(RequestedSize) > maximum_size) {
        return STATUS_INVALID_PARAMETER;
    }

    const SIZE_T required_size = period_bytes * period_count;

    ULONG actual_size = RequestedSize;
    if (actual_size < required_size) {
        actual_size = static_cast<ULONG>(required_size);
    }

    // Allocate continuous memory through port stream
    PMDL mdl = m_PortStream->AllocatePagesForMdl(PHYSICAL_ADDRESS{0}, actual_size);
    if (mdl == nullptr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PVOID buffer = m_PortStream->MapAllocatedPages(mdl, MmCached);
    if (buffer == nullptr) {
        m_PortStream->FreePagesFromMdl(mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buffer, actual_size);

    m_DmaBuffer = buffer;
    m_DmaBufferMdl = mdl;
    KIRQL positionIrql;
    KeAcquireSpinLock(&m_PositionLock, &positionIrql);
    m_DmaBufferSize = actual_size;
    m_AllocatedBytes = actual_size;
    KeReleaseSpinLock(&m_PositionLock, positionIrql);

    // Initialize the stream ring context
    NTSTATUS ntStatus = m_Capture
        ? HibikiWaveRtPinInitializeCaptureEndpointV1(&m_StreamContext, static_cast<uint8_t*>(buffer), actual_size, m_EndpointIndex, period_count)
        : HibikiWaveRtPinInitializeEndpointV1(&m_StreamContext, static_cast<uint8_t*>(buffer), actual_size, m_EndpointIndex, period_count);

    if (!NT_SUCCESS(ntStatus)) {
        m_PortStream->UnmapAllocatedPages(m_DmaBuffer, m_DmaBufferMdl);
        m_PortStream->FreePagesFromMdl(m_DmaBufferMdl);
        m_DmaBuffer = nullptr;
        m_DmaBufferMdl = nullptr;
        KeAcquireSpinLock(&m_PositionLock, &positionIrql);
        m_DmaBufferSize = 0;
        m_AllocatedBytes = 0;
        KeReleaseSpinLock(&m_PositionLock, positionIrql);
        m_StreamInitialized = FALSE;
        return ntStatus;
    }

    m_StreamInitialized = TRUE;
    KeAcquireSpinLock(&m_PositionLock, &positionIrql);
    const ULONGLONG bytes_per_second =
        static_cast<ULONGLONG>(topology.sample_rate) *
        static_cast<ULONGLONG>(bytes_per_frame);
    m_BufferNotificationCount = NotificationCount;
    m_BytesPerSecond = bytes_per_second;
    m_RunStartTime100ns = 0;
    m_RunBaseBytes = 0;
    m_LastNotificationBoundary = 0;
    m_TotalBytesProcessed = 0;
    KeReleaseSpinLock(&m_PositionLock, positionIrql);
    *AudioBufferMdl = m_DmaBufferMdl;
    *ActualSize = m_DmaBufferSize;
    *OffsetFromFirstPage = 0;
    *CacheType = MmCached;

    return STATUS_SUCCESS;
}

VOID HibikiMiniportWaveRtStreamV1::FreeBufferWithNotification(
    _In_opt_ PMDL                 AudioBufferMdl,
    _In_     ULONG                BufferSize) {
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(AudioBufferMdl);

    StopPositionTimer();

    // The stream owns the MDL paired with its mapped buffer.  Release that
    // stored allocation even when a compatibility caller passes a null MDL;
    // the WDK free callback is the ownership boundary for this buffer.
    PMDL allocated_mdl = m_DmaBufferMdl;

    if (m_StreamInitialized) {
        HibikiWaveRtPinResetV1(&m_StreamContext);
        m_StreamInitialized = FALSE;
    }

    if (m_DmaBuffer != nullptr) {
        m_PortStream->UnmapAllocatedPages(m_DmaBuffer, m_DmaBufferMdl);
        m_DmaBuffer = nullptr;
    }

    if (allocated_mdl != nullptr) {
        m_PortStream->FreePagesFromMdl(allocated_mdl);
        m_DmaBufferMdl = nullptr;
    }

    KIRQL positionIrql;
    KeAcquireSpinLock(&m_PositionLock, &positionIrql);
    m_DmaBufferSize = 0;
    m_AllocatedBytes = 0;
    m_BufferNotificationCount = 0;
    m_BytesPerSecond = 0;
    m_RunStartTime100ns = 0;
    m_RunBaseBytes = 0;
    m_LastNotificationBoundary = 0;
    m_TotalBytesProcessed = 0;
    KeReleaseSpinLock(&m_PositionLock, positionIrql);
}

NTSTATUS HibikiMiniportWaveRtStreamV1::AllocateAudioBufferWithNotification(
    _In_     ULONG                NotificationCount,
    _In_     ULONG                RequestedSize,
    _Out_    PMDL*                AudioBufferMdl,
    _Out_    ULONG*               ActualSize,
    _Out_    ULONG*               OffsetFromFirstPage,
    _Out_    MEMORY_CACHING_TYPE* CacheType) {
    return AllocateBufferWithNotification(NotificationCount, RequestedSize,
        AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
}

VOID HibikiMiniportWaveRtStreamV1::FreeAudioBufferWithNotification(
    _In_opt_ PMDL                 AudioBufferMdl,
    _In_     ULONG                BufferSize) {
    FreeBufferWithNotification(AudioBufferMdl, BufferSize);
}

NTSTATUS HibikiMiniportWaveRtStreamV1::GetClockRegister(
    _Out_ KSRTAUDIO_HWREGISTER*   Register) {
    if (Register == nullptr) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Register, sizeof(*Register));
    // This software-backed endpoint has no hardware clock register to map.
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::GetPositionRegister(
    _Out_ KSRTAUDIO_HWREGISTER*   Register) {
    if (Register == nullptr) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Register, sizeof(*Register));
    // Keep clients on the GetPosition/KSPROPERTY_AUDIO_POSITION path when
    // this software-backed endpoint has no position register.
    return STATUS_NOT_SUPPORTED;
}

VOID HibikiMiniportWaveRtStreamV1::GetHWLatency(
    _Out_ KSRTAUDIO_HWLATENCY*    HWLatency) {
    if (HWLatency == nullptr) return;

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(m_EndpointIndex, &topology) != 0) {
        HWLatency->FifoSize = 0;

        HWLatency->CodecDelay = 0;
        return;
    }

    // 100ns units latency estimation based on frame buffer size
    const ULONG latency_100ns = static_cast<ULONG>((topology.frames_per_buffer * 10000000ULL) / topology.sample_rate);
    HWLatency->FifoSize = topology.frames_per_buffer * topology.channel_count * sizeof(float);
    HWLatency->CodecDelay = latency_100ns;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::SetFormat(
    _In_ PKSDATAFORMAT            DataFormat) {
    if (DataFormat == nullptr ||
        DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE)) {
        return STATUS_INVALID_PARAMETER;
    }
    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(m_EndpointIndex, &topology) == 0 ||
        !HibikiWaveRtFormatMatchesEndpointV1(DataFormat, &topology)) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::SetState(
    _In_ KSSTATE                  State) {
    if (!HibikiWaveRtStateTransitionAllowedV1(m_State, State)) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (State) {
        case KSSTATE_STOP:
            StopPositionTimer();
            if (m_StreamInitialized) {
                HibikiWaveRtPinResetV1(&m_StreamContext);
            }
            {
                KIRQL positionIrql;
                KeAcquireSpinLock(&m_PositionLock, &positionIrql);
                m_TotalBytesProcessed = 0;
                m_RunStartTime100ns = 0;
                m_RunBaseBytes = 0;
                m_LastNotificationBoundary = 0;
                KeReleaseSpinLock(&m_PositionLock, positionIrql);
            }
            m_State = KSSTATE_STOP;
            break;
        case KSSTATE_ACQUIRE:
        case KSSTATE_PAUSE:
            if (m_State == KSSTATE_RUN) {
                StopPositionTimer();
            }
            m_State = State;
            break;
        case KSSTATE_RUN:
            if (!m_StreamInitialized || m_DmaBufferSize == 0) {
                return STATUS_INVALID_DEVICE_STATE;
            }
            if (m_State != KSSTATE_RUN) {
                m_State = KSSTATE_RUN;
                StartPositionTimer();
            }
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) HibikiMiniportWaveRtStreamV1::GetPosition(
    _Out_ PKSAUDIO_POSITION       Position) {
    if (Position == nullptr) return STATUS_INVALID_PARAMETER;

    BOOLEAN ignoredNotify = FALSE;
    KIRQL positionIrql;
    KeAcquireSpinLock(&m_PositionLock, &positionIrql);
    if (InterlockedCompareExchange(&m_PositionTimerActive, 0, 0) != 0) {
        UpdatePositionLocked(KeQueryInterruptTime(), &ignoredNotify);
    }
    const ULONG dmaBufferSize = m_DmaBufferSize;
    const ULONGLONG totalBytesProcessed = m_TotalBytesProcessed;
    KeReleaseSpinLock(&m_PositionLock, positionIrql);

    if (dmaBufferSize == 0) {
        Position->PlayOffset = 0;
        Position->WriteOffset = 0;
        return STATUS_SUCCESS;
    }

    const ULONGLONG current_offset = totalBytesProcessed % dmaBufferSize;
    Position->PlayOffset = current_offset;
    Position->WriteOffset = current_offset;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) HibikiMiniportWaveRtStreamV1::RegisterNotificationEvent(
    _In_ PKEVENT                  NotificationEvent) {
    if (NotificationEvent == nullptr) return STATUS_INVALID_PARAMETER;

    KIRQL notificationIrql;
    KeAcquireSpinLock(&m_NotificationLock, &notificationIrql);
    if (m_NotificationEventCount >= HIBIKI_MAX_NOTIFICATION_EVENTS_V1) {
        KeReleaseSpinLock(&m_NotificationLock, notificationIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Check for duplicates
    for (ULONG i = 0; i < m_NotificationEventCount; ++i) {
        if (m_NotificationEvents[i] == NotificationEvent) {
            KeReleaseSpinLock(&m_NotificationLock, notificationIrql);
            return STATUS_SUCCESS;
        }
    }

    m_NotificationEvents[m_NotificationEventCount] = NotificationEvent;
    ++m_NotificationEventCount;
    KeReleaseSpinLock(&m_NotificationLock, notificationIrql);
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) HibikiMiniportWaveRtStreamV1::UnregisterNotificationEvent(
    _In_ PKEVENT                  NotificationEvent) {
    if (NotificationEvent == nullptr) return STATUS_INVALID_PARAMETER;

    KIRQL notificationIrql;
    KeAcquireSpinLock(&m_NotificationLock, &notificationIrql);
    for (ULONG i = 0; i < m_NotificationEventCount; ++i) {
        if (m_NotificationEvents[i] == NotificationEvent) {
            for (ULONG j = i; j + 1 < m_NotificationEventCount; ++j) {
                m_NotificationEvents[j] = m_NotificationEvents[j + 1];
            }
            m_NotificationEvents[m_NotificationEventCount - 1] = nullptr;
            --m_NotificationEventCount;
            KeReleaseSpinLock(&m_NotificationLock, notificationIrql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&m_NotificationLock, notificationIrql);
    return STATUS_NOT_FOUND;
}

//=============================================================================
// HibikiMiniportWaveRtV1 Implementation
//=============================================================================

HibikiMiniportWaveRtV1::HibikiMiniportWaveRtV1()
    : m_RefCount(1),
      m_Port(nullptr),
      m_EndpointIndex(0),
      m_Initialized(FALSE) {
    RtlZeroMemory(&m_PropertyContext, sizeof(m_PropertyContext));
    RtlZeroMemory(&m_Topology, sizeof(m_Topology));
}

HibikiMiniportWaveRtV1::~HibikiMiniportWaveRtV1() {
    if (m_Port != nullptr) {
        m_Port->Release();
        m_Port = nullptr;
    }
}

STDMETHODIMP HibikiMiniportWaveRtV1::QueryInterface(
    _In_ REFIID Interface,
    _Out_ PVOID* Object) {
    if (Object == nullptr) return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(Interface, IID_IUnknown) ||
        IsEqualGUIDAligned(Interface, IID_IMiniport) ||
        IsEqualGUIDAligned(Interface, IID_IMiniportWaveRT)) {
        *Object = static_cast<IMiniportWaveRT*>(this);
        AddRef();
        return STATUS_SUCCESS;
    }

    *Object = nullptr;
    return STATUS_NOINTERFACE;
}

STDMETHODIMP_(ULONG) HibikiMiniportWaveRtV1::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_RefCount));
}

STDMETHODIMP_(ULONG) HibikiMiniportWaveRtV1::Release() {
    const LONG count = InterlockedDecrement(&m_RefCount);
    if (count == 0) {
        delete this;
        return 0;
    }
    return static_cast<ULONG>(count);
}

STDMETHODIMP HibikiMiniportWaveRtV1::Init(
    _In_ PUNKNOWN                  UnknownAdapter,
    _In_ PRESOURCELIST             ResourceList,
    _In_ PPORTWAVERT               Port) {
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    if (Port == nullptr) return STATUS_INVALID_PARAMETER;

    m_Port = Port;
    m_Port->AddRef();
    return STATUS_SUCCESS;
}

NTSTATUS HibikiMiniportWaveRtV1::InitEndpoint(
    _In_ ULONG                     EndpointIndex,
    _In_ ULONG                     Actuator) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
               "HIBIKI: miniport InitEndpoint idx=%lu actuator=%lu enter\n",
               EndpointIndex, Actuator);
    if (hibiki_endpoint_topology_get_v1(EndpointIndex, &m_Topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&m_Topology) == 0) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "HIBIKI: miniport InitEndpoint idx=%lu topology invalid\n", EndpointIndex);
        return STATUS_INVALID_PARAMETER;
    }

    const NTSTATUS ntStatus = HibikiPropertyContextInitializeEndpointV1(
        &m_PropertyContext, EndpointIndex, Actuator);
    if (!NT_SUCCESS(ntStatus)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "HIBIKI: miniport InitEndpoint idx=%lu property ctx failed 0x%08X\n",
                   EndpointIndex, ntStatus);
        return ntStatus;
    }

    m_EndpointIndex = EndpointIndex;
    m_Initialized = TRUE;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
               "HIBIKI: miniport InitEndpoint idx=%lu ok\n", EndpointIndex);
    return STATUS_SUCCESS;
}

STDMETHODIMP HibikiMiniportWaveRtV1::GetDescription(
    _Out_ PPCFILTER_DESCRIPTOR*    Description) {
    if (Description == nullptr) return STATUS_INVALID_PARAMETER;
    if (!m_Initialized) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "HIBIKI: GetDescription before init\n");
        return STATUS_INVALID_DEVICE_STATE;
    }

    const PCFILTER_DESCRIPTOR* filterDescriptor = nullptr;
    const NTSTATUS ntStatus = HibikiGetFilterDescriptorEndpointV1(
        m_EndpointIndex, &filterDescriptor);
    if (!NT_SUCCESS(ntStatus)) {
        *Description = nullptr;
        return ntStatus;
    }

    *Description = const_cast<PPCFILTER_DESCRIPTOR>(filterDescriptor);
    return STATUS_SUCCESS;
}

STDMETHODIMP HibikiMiniportWaveRtV1::DataRangeIntersection(
    _In_        ULONG              PinId,
    _In_        PKSDATARANGE       DataRange,
    _In_        PKSDATARANGE       MatchingDataRange,
    _In_        ULONG              OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
                PVOID              ResultantFormat,
    _Out_       PULONG             ResultantFormatLength) {
    if (!m_Initialized) return STATUS_INVALID_DEVICE_STATE;

    return HibikiDataRangeIntersectionEndpointV1(
        m_EndpointIndex, PinId, DataRange, MatchingDataRange,
        OutputBufferLength, ResultantFormat, ResultantFormatLength);
}

STDMETHODIMP HibikiMiniportWaveRtV1::NewStream(
    _Out_ PMINIPORTWAVERTSTREAM*   Stream,
    _In_  PPORTWAVERTSTREAM        PortStream,
    _In_  ULONG                    Pin,
    _In_  BOOLEAN                  Capture,
    _In_  PKSDATAFORMAT            DataFormat) {
    if (Stream == nullptr || PortStream == nullptr || DataFormat == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!m_Initialized) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    auto* newStream = new (NonPagedPoolNx) HibikiMiniportWaveRtStreamV1();
    if (newStream == nullptr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    const NTSTATUS ntStatus = newStream->Init(
        this, PortStream, Pin, Capture, DataFormat, m_EndpointIndex);
    if (!NT_SUCCESS(ntStatus)) {
        newStream->Release();
        *Stream = nullptr;
        return ntStatus;
    }

    *Stream = static_cast<IMiniportWaveRTStream*>(newStream);
    return STATUS_SUCCESS;
}

STDMETHODIMP HibikiMiniportWaveRtV1::GetDeviceDescription(
    _Out_ PDEVICE_DESCRIPTION      DeviceDescription) {
    if (DeviceDescription == nullptr) return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = TRUE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->InterfaceType = Internal;
    DeviceDescription->MaximumLength = 0xFFFFFFFF;
    return STATUS_SUCCESS;
}




