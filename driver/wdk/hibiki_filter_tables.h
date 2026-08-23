// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Filter and Topology Tables Header.
// Declares filter descriptors, pin descriptors, node descriptors, connection
// graphs, and data range validation for all four virtual audio endpoints.
// Free of GPL linkage, static/const tables.

#pragma once

#if !defined(_NTDDK_)
#error "Include this header only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>

#include "hibiki/endpoint_topology_v1.h"

// Retrieve the PortCls filter descriptor for a specific endpoint index
extern "C" NTSTATUS HibikiGetFilterDescriptorEndpointV1(
    _In_  ULONG                     EndpointIndex,
    _Out_ const PCFILTER_DESCRIPTOR** Description);

// Retrieve the topology-half filter descriptor paired with each endpoint.
extern "C" NTSTATUS HibikiGetTopologyFilterDescriptorEndpointV1(
    _In_  ULONG                     EndpointIndex,
    _Out_ const PCFILTER_DESCRIPTOR** Description);

// Validate data range intersection for a specific endpoint pin
extern "C" NTSTATUS HibikiDataRangeIntersectionEndpointV1(
    _In_        ULONG              EndpointIndex,
    _In_        ULONG              PinId,
    _In_        PKSDATARANGE       DataRange,
    _In_        PKSDATARANGE       MatchingDataRange,
    _In_        ULONG              OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
                PVOID              ResultantFormat,
    _Out_       PULONG             ResultantFormatLength);
