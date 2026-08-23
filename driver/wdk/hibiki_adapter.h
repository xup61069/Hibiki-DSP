// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Adapter Header.
// Declares DriverEntry, AddDevice, StartDevice, and subdevice registration
// entry points for SYSVAD-derived WDK driver builds.
// Free of GPL linkage, zero allocations in audio path.

#pragma once

#if !defined(_NTDDK_)
#error "Include this header only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>

#include "hibiki/endpoint_topology_v1.h"

// Maximum subdevices registered by this adapter
#define HIBIKI_MAX_SUBDEVICES_V1 4

// Endpoint subdevice symbolic names
#define HIBIKI_SUBDEVICE_NAME_MAIN_V1          L"Main"
#define HIBIKI_SUBDEVICE_NAME_LOW_LATENCY_V1   L"LowLatency"
#define HIBIKI_SUBDEVICE_NAME_SURROUND_V1      L"Surround"
#define HIBIKI_SUBDEVICE_NAME_VIRTUAL_MIC_V1   L"VirtualMic"

// Standard driver entry point
extern "C" DRIVER_INITIALIZE DriverEntry;

// AddDevice callback
extern "C" NTSTATUS HibikiAddDevice(
    _In_ PDRIVER_OBJECT   DriverObject,
    _In_ PDEVICE_OBJECT   PhysicalDeviceObject);

// StartDevice callback called by PortCls during PNP start
extern "C" NTSTATUS HibikiStartDevice(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_ PIRP             Irp,
    _In_ PRESOURCELIST    ResourceList);

// Subdevice registration helper
extern "C" NTSTATUS HibikiRegisterSubdevicesV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _In_ PRESOURCELIST    ResourceList,
    _In_opt_ PIRP         Irp);

// PNP and Power handlers
extern "C" NTSTATUS HibikiPnpDispatchV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _Inout_ PIRP          Irp);

extern "C" NTSTATUS HibikiPowerDispatchV1(
    _In_ PDEVICE_OBJECT   DeviceObject,
    _Inout_ PIRP          Irp);
// Kernel-mode C++ allocation for PortCls adapter classes (Issue #394).
void* __cdecl operator new(size_t size, POOL_TYPE pool_type);
void* __cdecl operator new[](size_t size, POOL_TYPE pool_type);
void __cdecl operator delete(void* pointer) noexcept;
void __cdecl operator delete[](void* pointer) noexcept;
void __cdecl operator delete(void* pointer, size_t) noexcept;
