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

    // 1. Create PortWaveRT object
    PPORT port = nullptr;
    NTSTATUS ntStatus = PcNewPort(&port, CLSID_PortWaveRT);
    if (!NT_SUCCESS(ntStatus) || port == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] PcNewPort failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: [%ws] PcNewPort ok\n", SubdeviceName);

    // 2. Create and initialize Miniport WaveRT
    auto* miniport = new (NonPagedPoolNx) HibikiMiniportWaveRtV1();
    if (miniport == nullptr) {
        port->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] alloc miniport failed\n", SubdeviceName);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    const ULONG default_actuator = 0; // Default actuator identity
    ntStatus = miniport->InitEndpoint(EndpointIndex, default_actuator);
    if (!NT_SUCCESS(ntStatus)) {
        miniport->Release();
        port->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] InitEndpoint failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: [%ws] InitEndpoint ok\n", SubdeviceName);

    // 3. Initialize Port with Miniport. Pass the start-device IRP through;
    // PortCls requires the initiating IRP during subdevice installation.
    ntStatus = port->Init(DeviceObject, Irp, miniport, nullptr, ResourceList);
    if (!NT_SUCCESS(ntStatus)) {
        miniport->Release();
        port->Release();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] port->Init failed 0x%08X\n", SubdeviceName, ntStatus);
        return ntStatus;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: [%ws] port->Init ok\n", SubdeviceName);

    // 4. Register Subdevice with PortCls
    ntStatus = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(SubdeviceName), port);
    if (NT_SUCCESS(ntStatus)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: [%ws] PcRegisterSubdevice ok\n", SubdeviceName);
    }
    else {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: [%ws] PcRegisterSubdevice failed 0x%08X\n", SubdeviceName, ntStatus);
    }

    // Release local COM references (PortCls retains registered references)
    miniport->Release();
    port->Release();

    return ntStatus;
}

extern "C" NTSTATUS HibikiRegisterSubdevicesV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_ PRESOURCELIST    ResourceList,
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
    _In_ PRESOURCELIST    ResourceList) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "HIBIKI: StartDevice enter irp=%p\n", Irp);
    if (DeviceObject == nullptr || ResourceList == nullptr) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HIBIKI: StartDevice null param dev=%p res=%p\n", DeviceObject, ResourceList);
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
        HIBIKI_MAX_SUBDEVICES_V1,
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

