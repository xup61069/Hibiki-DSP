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
      m_NotificationEventCount(0),
      m_TotalBytesProcessed(0) {
    RtlZeroMemory(&m_StreamContext, sizeof(m_StreamContext));
    RtlZeroMemory(m_NotificationEvents, sizeof(m_NotificationEvents));
}

HibikiMiniportWaveRtStreamV1::~HibikiMiniportWaveRtStreamV1() {
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
    if (topology.direction != expected_direction) {
        return STATUS_INVALID_PARAMETER;
    }

    // Verify format is extensible float matching topology
    if (DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE)) {
        return STATUS_INVALID_PARAMETER;
    }
    auto* waveFormatExt = reinterpret_cast<const KSDATAFORMAT_WAVEFORMATEXTENSIBLE*>(DataFormat);
    if (!IsEqualGUIDAligned(waveFormatExt->WaveFormatExt.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
        waveFormatExt->WaveFormatExt.Format.nSamplesPerSec != topology.sample_rate ||
        waveFormatExt->WaveFormatExt.Format.nChannels != topology.channel_count) {
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
    return AllocateBufferWithNotification(
        0U, RequestedSize, AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
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
    if (AudioBufferMdl == nullptr || ActualSize == nullptr ||
        OffsetFromFirstPage == nullptr || CacheType == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    UNREFERENCED_PARAMETER(NotificationCount);

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(m_EndpointIndex, &topology) == 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    const ULONG period_count = 2U; // Default double-buffered period count
    const ULONG bytes_per_frame = topology.channel_count * sizeof(float);
    const ULONG period_bytes = topology.frames_per_buffer * bytes_per_frame;
    const ULONG required_size = period_bytes * period_count;

    ULONG actual_size = RequestedSize;
    if (actual_size < required_size) {
        actual_size = required_size;
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

    m_DmaBuffer = buffer;
    m_DmaBufferMdl = mdl;
    m_DmaBufferSize = actual_size;
    m_AllocatedBytes = actual_size;

    // Initialize the stream ring context
    NTSTATUS ntStatus = m_Capture
        ? HibikiWaveRtPinInitializeCaptureEndpointV1(&m_StreamContext, static_cast<uint8_t*>(buffer), actual_size, m_EndpointIndex, period_count)
        : HibikiWaveRtPinInitializeEndpointV1(&m_StreamContext, static_cast<uint8_t*>(buffer), actual_size, m_EndpointIndex, period_count);

    if (!NT_SUCCESS(ntStatus)) {
        m_PortStream->UnmapAllocatedPages(m_DmaBuffer, m_DmaBufferMdl);
        m_PortStream->FreePagesFromMdl(m_DmaBufferMdl);
        m_DmaBuffer = nullptr;
        m_DmaBufferMdl = nullptr;
        m_DmaBufferSize = 0;
        return ntStatus;
    }

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

    if (m_DmaBuffer != nullptr) {
        HibikiWaveRtPinResetV1(&m_StreamContext);
        m_PortStream->UnmapAllocatedPages(m_DmaBuffer, m_DmaBufferMdl);
        m_DmaBuffer = nullptr;
    }

    if (AudioBufferMdl != nullptr && AudioBufferMdl == m_DmaBufferMdl) {
        m_PortStream->FreePagesFromMdl(m_DmaBufferMdl);
        m_DmaBufferMdl = nullptr;
    }

    m_DmaBufferSize = 0;
    m_AllocatedBytes = 0;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::GetClockRegister(
    _Out_ KSRTAUDIO_HWREGISTER*   Register) {
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::GetPositionRegister(
    _Out_ KSRTAUDIO_HWREGISTER*   Register) {
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_IMPLEMENTED;
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
    
    HWLatency->CodecDelay = 0;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::SetFormat(
    _In_ PKSDATAFORMAT            DataFormat) {
    if (DataFormat == nullptr ||
        DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE)) {
        return STATUS_INVALID_PARAMETER;
    }
    auto* waveFormatExt = reinterpret_cast<const KSDATAFORMAT_WAVEFORMATEXTENSIBLE*>(DataFormat);
    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(m_EndpointIndex, &topology) == 0 ||
        !IsEqualGUIDAligned(waveFormatExt->WaveFormatExt.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
        waveFormatExt->WaveFormatExt.Format.nSamplesPerSec != topology.sample_rate ||
        waveFormatExt->WaveFormatExt.Format.nChannels != topology.channel_count) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS HibikiMiniportWaveRtStreamV1::SetState(
    _In_ KSSTATE                  State) {
    switch (State) {
        case KSSTATE_STOP:
            HibikiWaveRtPinResetV1(&m_StreamContext);
            m_TotalBytesProcessed = 0;
            m_State = KSSTATE_STOP;
            break;
        case KSSTATE_ACQUIRE:
        case KSSTATE_PAUSE:
        case KSSTATE_RUN:
            m_State = State;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) HibikiMiniportWaveRtStreamV1::GetPosition(
    _Out_ PKSAUDIO_POSITION       Position) {
    if (Position == nullptr) return STATUS_INVALID_PARAMETER;

    if (m_DmaBufferSize == 0) {
        Position->PlayOffset = 0;
        Position->WriteOffset = 0;
        return STATUS_SUCCESS;
    }

    const ULONGLONG current_offset = m_TotalBytesProcessed % m_DmaBufferSize;
    Position->PlayOffset = current_offset;
    Position->WriteOffset = current_offset;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) HibikiMiniportWaveRtStreamV1::RegisterNotificationEvent(
    _In_ PKEVENT                  NotificationEvent) {
    if (NotificationEvent == nullptr) return STATUS_INVALID_PARAMETER;

    if (m_NotificationEventCount >= HIBIKI_MAX_NOTIFICATION_EVENTS_V1) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Check for duplicates
    for (ULONG i = 0; i < m_NotificationEventCount; ++i) {
        if (m_NotificationEvents[i] == NotificationEvent) {
            return STATUS_SUCCESS;
        }
    }

    m_NotificationEvents[m_NotificationEventCount] = NotificationEvent;
    ++m_NotificationEventCount;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) HibikiMiniportWaveRtStreamV1::UnregisterNotificationEvent(
    _In_ PKEVENT                  NotificationEvent) {
    if (NotificationEvent == nullptr) return STATUS_INVALID_PARAMETER;

    for (ULONG i = 0; i < m_NotificationEventCount; ++i) {
        if (m_NotificationEvents[i] == NotificationEvent) {
            for (ULONG j = i; j + 1 < m_NotificationEventCount; ++j) {
                m_NotificationEvents[j] = m_NotificationEvents[j + 1];
            }
            m_NotificationEvents[m_NotificationEventCount - 1] = nullptr;
            --m_NotificationEventCount;
            return STATUS_SUCCESS;
        }
    }
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
    if (hibiki_endpoint_topology_get_v1(EndpointIndex, &m_Topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&m_Topology) == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    const NTSTATUS ntStatus = HibikiPropertyContextInitializeEndpointV1(
        &m_PropertyContext, EndpointIndex, Actuator);
    if (!NT_SUCCESS(ntStatus)) {
        return ntStatus;
    }

    m_EndpointIndex = EndpointIndex;
    m_Initialized = TRUE;
    return STATUS_SUCCESS;
}

STDMETHODIMP HibikiMiniportWaveRtV1::GetDescription(
    _Out_ PPCFILTER_DESCRIPTOR*    Description) {
    if (Description == nullptr) return STATUS_INVALID_PARAMETER;
    if (!m_Initialized) return STATUS_INVALID_DEVICE_STATE;

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




