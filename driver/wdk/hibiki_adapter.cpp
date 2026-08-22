// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Adapter Implementation.
// Implements DriverEntry, AddDevice, StartDevice, and subdevice registration
// for all four virtual audio endpoints (Main, Low Latency, Surround 7.1, Virtual Mic).
// Free of GPL linkage, non-allocating in streaming paths.

#if !defined(_NTDDK_)
#error "Compile this file only inside a WDK PortCls driver project"
#endif

#include "hibiki_adapter.h"
#include "hibiki_miniport_wavert.h"
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
    _In_ PRESOURCELIST    ResourceList,
    _In_ ULONG            EndpointIndex,
    _In_ PCWSTR           SubdeviceName) {
    if (DeviceObject == nullptr || SubdeviceName == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(EndpointIndex, &topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&topology) == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // 1. Create PortWaveRT object
    PPORT port = nullptr;
    NTSTATUS ntStatus = PcNewPort(&port, CLSID_PortWaveRT);
    if (!NT_SUCCESS(ntStatus) || port == nullptr) {
        return ntStatus;
    }

    // 2. Create and initialize Miniport WaveRT
    auto* miniport = new (NonPagedPoolNx) HibikiMiniportWaveRtV1();
    if (miniport == nullptr) {
        port->Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    const ULONG default_actuator = 0; // Default actuator identity
    ntStatus = miniport->InitEndpoint(EndpointIndex, default_actuator);
    if (!NT_SUCCESS(ntStatus)) {
        miniport->Release();
        port->Release();
        return ntStatus;
    }

    // 3. Initialize Port with Miniport
    ntStatus = port->Init(DeviceObject, nullptr, miniport, nullptr, ResourceList);
    if (!NT_SUCCESS(ntStatus)) {
        miniport->Release();
        port->Release();
        return ntStatus;
    }

    // 4. Register Subdevice with PortCls
    ntStatus = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(SubdeviceName), port);

    // Release local COM references (PortCls retains registered references)
    miniport->Release();
    port->Release();

    return ntStatus;
}

extern "C" NTSTATUS HibikiRegisterSubdevicesV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_ PRESOURCELIST    ResourceList) {
    if (DeviceObject == nullptr) return STATUS_INVALID_PARAMETER;

    for (ULONG i = 0; i < HIBIKI_MAX_SUBDEVICES_V1; ++i) {
        const NTSTATUS ntStatus = HibikiRegisterSingleSubdeviceV1(
            DeviceObject, ResourceList, i, EndpointSubdeviceNames[i]);
        if (!NT_SUCCESS(ntStatus)) {
            return ntStatus;
        }
    }

    return STATUS_SUCCESS;
}

//=============================================================================
// StartDevice Callback
//=============================================================================

extern "C" NTSTATUS HibikiStartDevice(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_ PIRP             Irp,
    _In_ PRESOURCELIST    ResourceList) {
    UNREFERENCED_PARAMETER(Irp);

    if (DeviceObject == nullptr) return STATUS_INVALID_PARAMETER;

    return HibikiRegisterSubdevicesV1(DeviceObject, ResourceList);
}

//=============================================================================
// AddDevice Callback
//=============================================================================

extern "C" NTSTATUS HibikiAddDevice(
    _In_ PDRIVER_OBJECT   DriverObject,
    _In_ PDEVICE_OBJECT   PhysicalDeviceObject) {
    if (DriverObject == nullptr || PhysicalDeviceObject == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    return PcAddAdapterDevice(
        DriverObject,
        PhysicalDeviceObject,
        HibikiStartDevice,
        HIBIKI_MAX_SUBDEVICES_V1,
        0);
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

    return PcInitializeAdapterDriver(
        DriverObject,
        RegistryPath,
        (PDRIVER_ADD_DEVICE)HibikiAddDevice);
}


void* __cdecl operator new(size_t size, POOL_TYPE pool_type) {
    if (pool_type == NonPagedPoolNx) {
        return ExAllocatePoolWithTag(NonPagedPoolNx, size, 'ibiH');
    }
    return ExAllocatePoolWithTag(pool_type, size, 'ibiH');
}

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

