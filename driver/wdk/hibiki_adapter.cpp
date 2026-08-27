// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Adapter Implementation.
// Implements DriverEntry, AddDevice, StartDevice, and subdevice registration
// for all four virtual audio endpoints (Main, Low Latency, Surround 7.1, Virtual Mic).
// Free of GPL linkage, non-allocating in streaming paths.

#if !defined(_NTDDK_)
#error "Compile this file only inside a WDK PortCls driver project"
#endif

#include <initguid.h>
#include "hibiki_adapter.h"
#include "hibiki_miniport_wavert.h"
#include "hibiki_miniport_topology.h"
#include "hibiki_filter_tables.h"

// Endpoint name lookup table
static const PCWSTR EndpointSubdeviceNames[HIBIKI_MAX_SUBDEVICES_V1] = {
    HIBIKI_SUBDEVICE_NAME_MAIN_V1,
    HIBIKI_SUBDEVICE_NAME_LOW_LATENCY_V1,
    HIBIKI_SUBDEVICE_NAME_SURROUND_V1,
    HIBIKI_SUBDEVICE_NAME_VIRTUAL_MIC_V1
};

//=============================================================================
// Subdevice Registration
//=============================================================================

extern "C" NTSTATUS HibikiRegisterSingleSubdeviceV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_opt_ PRESOURCELIST ResourceList,
    _In_opt_ PIRP         Irp,
    _In_ ULONG            EndpointIndex,
    _In_ PCWSTR           SubdeviceName) {
    if (DeviceObject == nullptr || SubdeviceName == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: RegisterSingle null param\n");
        return STATUS_INVALID_PARAMETER;
    }

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(EndpointIndex, &topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&topology) == 0) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] topology invalid idx=%lu\n", SubdeviceName, EndpointIndex);
        return STATUS_INVALID_PARAMETER;
    }

    /* Build subdevice names for both halves of the endpoint pair. */
    WCHAR topoName[64];
    WCHAR waveName[64];

    /* Simple concatenation without CRT. */
    const SIZE_T nameLen = wcslen(SubdeviceName);
    if (nameLen == 0 || nameLen > 32) { return STATUS_INVALID_PARAMETER; }

    RtlCopyMemory(topoName, L"Topology", 8 * sizeof(WCHAR));
    RtlCopyMemory(&topoName[8], SubdeviceName, (nameLen + 1) * sizeof(WCHAR));
    RtlCopyMemory(waveName, L"Wave", 5 * sizeof(WCHAR));
    RtlCopyMemory(&waveName[5], SubdeviceName, (nameLen + 1) * sizeof(WCHAR));

    /* 1. Create the Topology port and miniport pair first. */
    PPORT topoPort = nullptr;
    NTSTATUS ntStatus = PcNewPort(&topoPort, CLSID_PortTopology);
    if (!NT_SUCCESS(ntStatus) || topoPort == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] PcNewPort(topo) failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }

    const PCFILTER_DESCRIPTOR* topoFilterDesc = nullptr;
    ntStatus = HibikiGetTopologyFilterDescriptorEndpointV1(EndpointIndex, &topoFilterDesc);
    if (!NT_SUCCESS(ntStatus) || topoFilterDesc == nullptr) {
        topoPort->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] GetTopologyFilter failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }

    auto* topoMiniport = new (NonPagedPoolNx) HibikiMiniportTopologyV1();
    if (topoMiniport == nullptr) {
        topoPort->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] alloc topo miniport\n", SubdeviceName);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    topoMiniport->InitDescriptor(const_cast<PCFILTER_DESCRIPTOR*>(topoFilterDesc));

    ntStatus = topoPort->Init(DeviceObject, Irp, topoMiniport, nullptr, ResourceList);
    if (!NT_SUCCESS(ntStatus)) {
        topoMiniport->Release();
        topoPort->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] topo Init failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }

    PUNKNOWN unknownTopology = nullptr;
    ntStatus = topoPort->QueryInterface(IID_IUnknown, reinterpret_cast<PVOID*>(&unknownTopology));
    if (!NT_SUCCESS(ntStatus) || unknownTopology == nullptr) {
        topoMiniport->Release();
        topoPort->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] topo QI failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }

    ntStatus = PcRegisterSubdevice(DeviceObject, topoName, topoPort);
    if (!NT_SUCCESS(ntStatus)) {
        unknownTopology->Release();
        topoMiniport->Release();
        topoPort->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] RegisterSubdevice(%ws) failed 0x%08X\n", SubdeviceName, topoName, ntStatus);
        return ntStatus;
    }
    topoMiniport->Release();
    topoPort->Release();

    /* 2. Create the WaveRT port and miniport pair. */
    PPORT wavePort = nullptr;
    ntStatus = PcNewPort(&wavePort, CLSID_PortWaveRT);
    if (!NT_SUCCESS(ntStatus) || wavePort == nullptr) {
        unknownTopology->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] PcNewPort(wave) failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }

    auto* waveMiniport = new (NonPagedPoolNx) HibikiMiniportWaveRtV1();
    if (waveMiniport == nullptr) {
        wavePort->Release();
        unknownTopology->Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    const ULONG default_actuator = 0;
    ntStatus = waveMiniport->InitEndpoint(EndpointIndex, default_actuator);
    if (!NT_SUCCESS(ntStatus)) {
        waveMiniport->Release();
        wavePort->Release();
        unknownTopology->Release();
        return ntStatus;
    }

    ntStatus = wavePort->Init(DeviceObject, Irp, waveMiniport, nullptr, ResourceList);
    if (!NT_SUCCESS(ntStatus)) {
        waveMiniport->Release();
        wavePort->Release();
        unknownTopology->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] wave Init failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }

    PUNKNOWN unknownWave = nullptr;
    ntStatus = wavePort->QueryInterface(IID_IUnknown, reinterpret_cast<PVOID*>(&unknownWave));
    if (!NT_SUCCESS(ntStatus) || unknownWave == nullptr) {
        waveMiniport->Release();
        wavePort->Release();
        unknownTopology->Release();
        return ntStatus;
    }

    ntStatus = PcRegisterSubdevice(DeviceObject, waveName, wavePort);
    if (!NT_SUCCESS(ntStatus)) {
        unknownWave->Release();
        waveMiniport->Release();
        wavePort->Release();
        unknownTopology->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] RegisterSubdevice(%ws) failed 0x%08X\n", SubdeviceName, waveName, ntStatus);
        return ntStatus;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: [%ws] PcRegisterSubdevice ok\n", SubdeviceName);
    waveMiniport->Release();
    wavePort->Release();

    /* 3. Connect bridge pins between WaveRT and Topology filters.
     * Render: Wave pin 1 -> Topology pin 0.
     * Capture: Topology pin 1 -> Wave pin 0. */
    const bool is_capture = (topology.direction == HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1);
    if (is_capture) {
        ntStatus = PcRegisterPhysicalConnection(DeviceObject, unknownTopology, 1U, unknownWave, 0U);
    } else {
        ntStatus = PcRegisterPhysicalConnection(DeviceObject, unknownWave, 1U, unknownTopology, 0U);
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: [%ws] PhysicalConnection -> 0x%08X\n", SubdeviceName, ntStatus);

    /* Release our local COM references; PortCls retains its own. */
    unknownWave->Release();
    unknownTopology->Release();
    return ntStatus;
}
extern "C" NTSTATUS HibikiRegisterSubdevicesV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_opt_ PRESOURCELIST ResourceList,
    _In_opt_ PIRP         Irp) {
    if (DeviceObject == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: RegisterSubdevices null device\n");
        return STATUS_INVALID_PARAMETER;
    }

    for (ULONG i = 0; i < HIBIKI_MAX_SUBDEVICES_V1; ++i) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: register begin idx=%lu [%ws]\n", i, EndpointSubdeviceNames[i]);
        const NTSTATUS ntStatus = HibikiRegisterSingleSubdeviceV1(
            DeviceObject, ResourceList, Irp, i, EndpointSubdeviceNames[i]);
        if (!NT_SUCCESS(ntStatus)) {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: register idx=%lu [%ws] failed 0x%08X\n", i, EndpointSubdeviceNames[i], ntStatus);
            return ntStatus;
        }
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: all %lu endpoints registered\n", (ULONG)HIBIKI_MAX_SUBDEVICES_V1);
    return STATUS_SUCCESS;
}

//=============================================================================
// StartDevice Callback
//=============================================================================

extern "C" NTSTATUS HibikiStartDevice(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_ PIRP             Irp,
    _In_opt_ PRESOURCELIST ResourceList) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: StartDevice enter irp=%p\n", Irp);
    if (DeviceObject == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: StartDevice null device\n");
        return STATUS_INVALID_PARAMETER;
    }

    const NTSTATUS ntStatus = HibikiRegisterSubdevicesV1(DeviceObject, ResourceList, Irp);
    if (NT_SUCCESS(ntStatus)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: StartDevice exit ok\n");
    }
    else {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: StartDevice exit failed 0x%08X\n", ntStatus);
    }
    return ntStatus;
}

//=============================================================================
// AddDevice Callback
//=============================================================================

extern "C" NTSTATUS HibikiAddDevice(
    _In_ PDRIVER_OBJECT   DriverObject,
    _In_ PDEVICE_OBJECT   PhysicalDeviceObject) {
    if (DriverObject == nullptr || PhysicalDeviceObject == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: AddDevice null param\n");
        return STATUS_INVALID_PARAMETER;
    }

    const NTSTATUS ntStatus = PcAddAdapterDevice(
        DriverObject,
        PhysicalDeviceObject,
        HibikiStartDevice,
        HIBIKI_MAX_SUBDEVICES_V1 * 2, /* 4 subdevices x 2 filters each */
        0);
    if (NT_SUCCESS(ntStatus)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: AddDevice ok\n");
    }
    else {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: AddDevice failed 0x%08X\n", ntStatus);
    }
    return ntStatus;
}

//=============================================================================
// PNP & Power Dispatch
//=============================================================================

extern "C" NTSTATUS HibikiPnpDispatchV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _Inout_ PIRP          Irp) {
    if (DeviceObject == nullptr || Irp == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    return PcDispatchIrp(DeviceObject, Irp);
}

extern "C" NTSTATUS HibikiPowerDispatchV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _Inout_ PIRP          Irp) {
    if (DeviceObject == nullptr || Irp == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    return PcDispatchIrp(DeviceObject, Irp);
}

//=============================================================================
// DriverEntry
//=============================================================================

extern "C" NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT   DriverObject,
    _In_ PUNICODE_STRING  RegistryPath) {
    if (DriverObject == nullptr || RegistryPath == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: DriverEntry null param\n");
        return STATUS_INVALID_PARAMETER;
    }

    // Set standard major dispatch functions
    DriverObject->DriverExtension->AddDevice = HibikiAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_POWER]          = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PcDispatchIrp;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: DriverEntry entering PcInitializeAdapterDriver\n");
    const NTSTATUS ntStatus = PcInitializeAdapterDriver(
        DriverObject,
        RegistryPath,
        (PDRIVER_ADD_DEVICE)HibikiAddDevice);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               NT_SUCCESS(ntStatus) ? DPFLTR_TRACE_LEVEL : DPFLTR_ERROR_LEVEL,
               "HIBIKI: DriverEntry -> 0x%08X (%s)\n",
               ntStatus, NT_SUCCESS(ntStatus) ? "ok" : "FAILED");
    return ntStatus;
}


// ExAllocatePool2/3 require an NTDDI_VERSION bump above the current Win10 RTM
// target; until that coordinated change lands, keep the tagged allocator and
// silence its deprecation annotation at this call site.
#pragma warning(push)
#pragma warning(disable : 4996)
void* __cdecl operator new(size_t size, POOL_TYPE pool_type) {
    return ExAllocatePoolWithTag(pool_type, size, 'ibiH');
}
#pragma warning(pop)

void* __cdecl operator new[](size_t size, POOL_TYPE pool_type) {
    return operator new(size, pool_type);
}

void __cdecl operator delete(void* pointer) noexcept {
    if (pointer != nullptr) ExFreePoolWithTag(pointer, 'ibiH');
}

void __cdecl operator delete[](void* pointer) noexcept {
    operator delete(pointer);
}

void __cdecl operator delete(void* pointer, size_t) noexcept {
    operator delete(pointer);
}

