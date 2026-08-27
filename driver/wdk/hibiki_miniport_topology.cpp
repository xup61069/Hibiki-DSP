// SPDX-License-Identifier: MS-PL
//
// Hibiki Topology PortCls Miniport Implementation.
// Minimal IMiniportTopology adapter: returns the static filter descriptor
// and rejects data range intersections (bridge pins are analog).

#include "hibiki_miniport_topology.h"

HibikiMiniportTopologyV1::HibikiMiniportTopologyV1()
    : m_RefCount(1), m_FilterDescriptor(nullptr), m_Port(nullptr) {
}

HibikiMiniportTopologyV1::~HibikiMiniportTopologyV1() {
    if (m_Port != nullptr) {
        m_Port->Release();
        m_Port = nullptr;
    }
}

STDMETHODIMP HibikiMiniportTopologyV1::QueryInterface(
    _In_ REFIID Interface, _Out_ PVOID* Object) {
    if (Object == nullptr) return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
        *Object = PVOID(PUNKNOWN(this));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniport)) {
        *Object = PVOID(PMINIPORT(this));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniportTopology)) {
        *Object = PVOID(PMINIPORTTOPOLOGY(this));
    } else {
        *Object = nullptr;
        return STATUS_NOINTERFACE;
    }

    if (*Object != nullptr) {
        reinterpret_cast<PUNKNOWN>(*Object)->AddRef();
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_(ULONG) HibikiMiniportTopologyV1::AddRef() {
    return InterlockedIncrement(&m_RefCount);
}

STDMETHODIMP_(ULONG) HibikiMiniportTopologyV1::Release() {
    const LONG count = InterlockedDecrement(&m_RefCount);
    if (count == 0) { delete this; }
    return count;
}

void HibikiMiniportTopologyV1::InitDescriptor(
    _In_ PPCFILTER_DESCRIPTOR FilterDescriptor) {
    m_FilterDescriptor = FilterDescriptor;
}

STDMETHODIMP HibikiMiniportTopologyV1::GetDescription(
    _Out_ PPCFILTER_DESCRIPTOR* Description) {
    if (Description == nullptr || m_FilterDescriptor == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    *Description = m_FilterDescriptor;
    return STATUS_SUCCESS;
}

STDMETHODIMP HibikiMiniportTopologyV1::DataRangeIntersection(
    _In_        ULONG              PinId,
    _In_        PKSDATARANGE       DataRange,
    _In_        PKSDATARANGE       MatchingDataRange,
    _In_        ULONG              OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
                PVOID              ResultantFormat,
    _Out_       PULONG             ResultantFormatLength) {
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    return STATUS_NOT_IMPLEMENTED;
}

STDMETHODIMP HibikiMiniportTopologyV1::Init(
    _In_ PUNKNOWN                  UnknownAdapter,
    _In_ PRESOURCELIST             ResourceList,
    _In_ PPORTTOPOLOGY             Port) {
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    if (Port == nullptr || m_FilterDescriptor == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    if (m_Port != nullptr) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    m_Port = Port;
    m_Port->AddRef();
    return STATUS_SUCCESS;
}
