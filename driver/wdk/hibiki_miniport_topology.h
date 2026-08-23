// SPDX-License-Identifier: MS-PL
//
// Hibiki Topology PortCls Miniport Header.
// Implements the minimal IMiniportTopology COM adapter required for each
// audio endpoint's topology filter half of the WaveRT/Topology pair.
// Free of GPL linkage, zero allocations in operation.

#pragma once

#if !defined(_NTDDK_)
#error "Include this header only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>

class HibikiMiniportTopologyV1 : public IMiniportTopology {
private:
    LONG m_RefCount;
    PPCFILTER_DESCRIPTOR m_FilterDescriptor;
    PPORTTOPOLOGY m_Port;

public:
    HibikiMiniportTopologyV1();
    ~HibikiMiniportTopologyV1();

    // IUnknown methods
    STDMETHODIMP QueryInterface(_In_ REFIID Interface, _Out_ PVOID* Object);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMiniport methods
    STDMETHODIMP GetDescription(
        _Out_ PPCFILTER_DESCRIPTOR* Description);
    STDMETHODIMP DataRangeIntersection(
        _In_        ULONG              PinId,
        _In_        PKSDATARANGE       DataRange,
        _In_        PKSDATARANGE       MatchingDataRange,
        _In_        ULONG              OutputBufferLength,
        _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
                    PVOID              ResultantFormat,
        _Out_       PULONG             ResultantFormatLength);

    // IMiniportTopology methods
    STDMETHODIMP Init(
        _In_ PUNKNOWN                  UnknownAdapter,
        _In_ PRESOURCELIST             ResourceList,
        _In_ PPORTTOPOLOGY             Port);

    // Custom initialization with filter descriptor
    void InitDescriptor(_In_ PPCFILTER_DESCRIPTOR FilterDescriptor);
};
