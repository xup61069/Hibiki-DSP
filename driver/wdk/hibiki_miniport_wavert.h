// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Miniport Header.
// Defines the IMiniportWaveRT and IMiniportWaveRTStreamNotification COM adapter
// classes for SYSVAD-derived WDK driver builds.
// Free of GPL linkage, allocation-free in streaming path.

#pragma once

#if !defined(_NTDDK_)
#error "Include this header only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>

#include "hibiki/endpoint_topology_v1.h"
#include "hibiki/wavert_endpoint_state_v1.h"
#include "hibiki/wavert_stream_v1.h"

// Forward declarations
class HibikiMiniportWaveRtV1;
void* __cdecl operator new(size_t size, POOL_TYPE pool_type);
void __cdecl operator delete(void* pointer) noexcept;
class HibikiMiniportWaveRtStreamV1;

#if !defined(HIBIKI_WDK_STRUCTS_DEFINED)
#define HIBIKI_WDK_STRUCTS_DEFINED
struct hibiki_wdk_endpoint_context_v1 {
    FAST_MUTEX property_lock;
    hibiki_wavert_endpoint_state_v1 state;
};

struct hibiki_wdk_stream_context_v1 {
    KSPIN_LOCK lock;
    hibiki_wavert_stream_v1 stream;
};
#endif

// Maximum notification events registered per stream (bounded table)
#define HIBIKI_MAX_NOTIFICATION_EVENTS_V1 4

//=============================================================================
// HibikiMiniportWaveRtStreamV1
// Implements IMiniportWaveRTStream + IMiniportWaveRTStreamNotification
// (vtable order matches WDK 10.0.28000.0 portcls.h exactly)
//=============================================================================
class HibikiMiniportWaveRtStreamV1 : public IMiniportWaveRTStreamNotification {
private:
    LONG                            m_RefCount;
    HibikiMiniportWaveRtV1*         m_Miniport;
    PPORTWAVERTSTREAM               m_PortStream;
    ULONG                           m_Pin;
    BOOLEAN                         m_Capture;
    KSSTATE                         m_State;
    ULONG                           m_EndpointIndex;

    // Buffer and stream state
    PVOID                           m_DmaBuffer;
    PMDL                            m_DmaBufferMdl;
    ULONG                           m_DmaBufferSize;
    ULONG                           m_AllocatedBytes;
    BOOLEAN                         m_StreamInitialized;
    hibiki_wdk_stream_context_v1    m_StreamContext;

    // Notification event handles
    KSPIN_LOCK                      m_NotificationLock;
    PKEVENT                         m_NotificationEvents[HIBIKI_MAX_NOTIFICATION_EVENTS_V1];
    ULONG                           m_NotificationEventCount;
    ULONG                           m_BufferNotificationCount;

    // Position tracking and software-clock scheduling
    KSPIN_LOCK                      m_PositionLock;
    KTIMER                          m_PositionTimer;
    KDPC                            m_PositionDpc;
    EX_RUNDOWN_REF                  m_PositionDpcRundown;
    volatile LONG                   m_PositionTimerActive;
    ULONGLONG                       m_RunStartTime100ns;
    ULONGLONG                       m_RunBaseBytes;
    ULONGLONG                       m_BytesPerSecond;
    ULONGLONG                       m_LastNotificationBoundary;
    ULONGLONG                       m_TotalBytesProcessed;

    NTSTATUS AllocateBufferCore(
        _In_  ULONG                   RequestedSize,
        _In_  ULONG                   NotificationCount,
        _Out_ PMDL*                   AudioBufferMdl,
        _Out_ ULONG*                 ActualSize,
        _Out_ ULONG*                 OffsetFromFirstPage,
        _Out_ MEMORY_CACHING_TYPE*   CacheType);

    static VOID PositionTimerDpc(
        _In_ PKDPC                     Dpc,
        _In_opt_ PVOID                 DeferredContext,
        _In_opt_ PVOID                 SystemArgument1,
        _In_opt_ PVOID                 SystemArgument2);
    VOID StartPositionTimer();
    VOID StopPositionTimer();
    ULONGLONG NotificationBoundaryLocked(
        _In_  ULONGLONG                TotalBytes) const;
    VOID UpdatePositionLocked(
        _In_  ULONGLONG                Now100ns,
        _Out_ BOOLEAN*                 Notify);
    VOID SignalNotificationEventsAtDpc();

public:
    HibikiMiniportWaveRtStreamV1();
    ~HibikiMiniportWaveRtStreamV1();

    // IUnknown methods
    STDMETHODIMP QueryInterface(_In_ REFIID Interface, _Out_ PVOID* Object);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMiniportWaveRTStream methods (kit vtable order)
    STDMETHOD_(NTSTATUS, SetFormat)(_In_ PKSDATAFORMAT DataFormat);
    STDMETHOD_(NTSTATUS, SetState)(_In_ KSSTATE State);
    STDMETHOD_(NTSTATUS, GetPosition)(_Out_ PKSAUDIO_POSITION Position);
    STDMETHOD_(NTSTATUS, AllocateAudioBuffer)(
        _In_  ULONG                   RequestedSize,
        _Out_ PMDL*                   AudioBufferMdl,
        _Out_ ULONG*                  ActualSize,
        _Out_ ULONG*                  OffsetFromFirstPage,
        _Out_ MEMORY_CACHING_TYPE*    CacheType);
    STDMETHOD_(VOID, FreeAudioBuffer)(
        _In_opt_ PMDL                 AudioBufferMdl,
        _In_     ULONG                BufferSize);
    STDMETHOD_(VOID, GetHWLatency)(
        _Out_ KSRTAUDIO_HWLATENCY*    HWLatency);
    STDMETHOD_(NTSTATUS, GetPositionRegister)(
        _Out_ KSRTAUDIO_HWREGISTER*   Register);
    STDMETHOD_(NTSTATUS, GetClockRegister)(
        _Out_ KSRTAUDIO_HWREGISTER*   Register);

    // IMiniportWaveRTStreamNotification methods (kit vtable order)
    STDMETHOD_(NTSTATUS, AllocateBufferWithNotification)(
        _In_     ULONG                NotificationCount,
        _In_     ULONG                RequestedSize,
        _Out_    PMDL*                AudioBufferMdl,
        _Out_    ULONG*               ActualSize,
        _Out_    ULONG*               OffsetFromFirstPage,
        _Out_    MEMORY_CACHING_TYPE* CacheType);
   STDMETHOD_(VOID, FreeBufferWithNotification)(
       _In_opt_ PMDL                 AudioBufferMdl,
       _In_     ULONG                BufferSize);

    // Source-boundary compatibility names retained for the driver policy gate;
    // these forward to the exact WDK 26100 PortCls notification methods above.
    STDMETHOD_(NTSTATUS, AllocateAudioBufferWithNotification)(
        _In_     ULONG                NotificationCount,
        _In_     ULONG                RequestedSize,
        _Out_    PMDL*                AudioBufferMdl,
        _Out_    ULONG*               ActualSize,
        _Out_    ULONG*               OffsetFromFirstPage,
        _Out_    MEMORY_CACHING_TYPE* CacheType);
    STDMETHOD_(VOID, FreeAudioBufferWithNotification)(
        _In_opt_ PMDL                 AudioBufferMdl,
        _In_     ULONG                BufferSize);
   STDMETHOD_(NTSTATUS, RegisterNotificationEvent)(
       _In_     PKEVENT              NotificationEvent);
    STDMETHOD_(NTSTATUS, UnregisterNotificationEvent)(
        _In_     PKEVENT              NotificationEvent);

    // Internal initialization
    NTSTATUS Init(
        _In_ HibikiMiniportWaveRtV1*   Miniport,
        _In_ PPORTWAVERTSTREAM         PortStream,
        _In_ ULONG                     Pin,
        _In_ BOOLEAN                   Capture,
        _In_ PKSDATAFORMAT             DataFormat,
        _In_ ULONG                     EndpointIndex);

    // Stream context access
    hibiki_wdk_stream_context_v1* GetStreamContext() { return &m_StreamContext; }
    KSSTATE GetState() const { return m_State; }
};

//=============================================================================
// HibikiMiniportWaveRtV1
// Implements IMiniportWaveRT
//=============================================================================
class HibikiMiniportWaveRtV1 : public IMiniportWaveRT {
private:
    LONG                            m_RefCount;
    PPORTWAVERT                     m_Port;
    ULONG                           m_EndpointIndex;
    hibiki_wdk_endpoint_context_v1  m_PropertyContext;
    hibiki_endpoint_topology_v1     m_Topology;
    BOOLEAN                         m_Initialized;

public:
    HibikiMiniportWaveRtV1();
    ~HibikiMiniportWaveRtV1();

    // IUnknown methods
    STDMETHODIMP QueryInterface(_In_ REFIID Interface, _Out_ PVOID* Object);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMiniport methods
    STDMETHODIMP GetDescription(
        _Out_ PPCFILTER_DESCRIPTOR*    Description);

    STDMETHODIMP DataRangeIntersection(
        _In_        ULONG              PinId,
        _In_        PKSDATARANGE       DataRange,
        _In_        PKSDATARANGE       MatchingDataRange,
        _In_        ULONG              OutputBufferLength,
        _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
                    PVOID              ResultantFormat,
        _Out_       PULONG             ResultantFormatLength);

    // IMiniportWaveRT methods
    STDMETHODIMP Init(
        _In_ PUNKNOWN                  UnknownAdapter,
        _In_ PRESOURCELIST             ResourceList,
        _In_ PPORTWAVERT               Port);

    STDMETHODIMP NewStream(
        _Out_ PMINIPORTWAVERTSTREAM*   Stream,
        _In_  PPORTWAVERTSTREAM        PortStream,
        _In_  ULONG                    Pin,
        _In_  BOOLEAN                  Capture,
        _In_  PKSDATAFORMAT            DataFormat);

    STDMETHODIMP GetDeviceDescription(
        _Out_ PDEVICE_DESCRIPTION      DeviceDescription);

    // Custom initialization with endpoint index
    NTSTATUS InitEndpoint(
        _In_ ULONG                     EndpointIndex,
        _In_ ULONG                     Actuator);

    // Property context accessor
    hibiki_wdk_endpoint_context_v1* GetPropertyContext() { return &m_PropertyContext; }
    const hibiki_endpoint_topology_v1* GetTopology() const { return &m_Topology; }
};

// Factory function
extern "C" NTSTATUS CreateHibikiMiniportWaveRtV1(
    _Out_    PUNKNOWN*                 Unknown,
    _In_     REFCLSID                  ClassId,
    _In_opt_ PUNKNOWN                  UnknownOuter,
    _In_     POOL_FLAGS                PoolFlags);

