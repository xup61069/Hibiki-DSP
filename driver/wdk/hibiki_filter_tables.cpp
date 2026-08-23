// SPDX-License-Identifier: MS-PL
//
// Hibiki WaveRT PortCls Filter and Topology Tables Implementation.
// Constructs static PortCls PCFILTER_DESCRIPTOR, PCPIN_DESCRIPTOR, PCNODE_DESCRIPTOR,
// PCCONNECTION_DESCRIPTOR, and PCAUTOMATION_TABLE graphs for each endpoint index.
// Free of GPL linkage, zero allocations.

#if !defined(_NTDDK_)
#error "Compile this file only inside a WDK PortCls driver project"
#endif

#include "hibiki_filter_tables.h"
#include "hibiki_miniport_wavert.h"

#ifndef KSAUDFNAME_VOLUME
#define KSAUDFNAME_VOLUME KSAUDFNAME_VOLUME_CONTROL
#endif
#ifndef KSAUDFNAME_MUTE
#define KSAUDFNAME_MUTE nullptr
#endif

// Forward declaration from hibiki_property_adapter.cpp
extern "C" NTSTATUS HibikiPropertyHandlerVolumeV1(
    _In_ PPCPROPERTY_REQUEST request,
    _Inout_ hibiki_wdk_endpoint_context_v1* context);

extern "C" NTSTATUS HibikiPropertyHandlerMuteV1(
    _In_ PPCPROPERTY_REQUEST request,
    _Inout_ hibiki_wdk_endpoint_context_v1* context);

extern "C" NTSTATUS HibikiWaveRtBuildFormatEndpointV1(
    _In_ ULONG endpoint_index,
    _Out_ WAVEFORMATEXTENSIBLE* format);

//=============================================================================
// Property Automation Items
//=============================================================================

static NTSTATUS PropertyHandler_Volume(
    _In_ PPCPROPERTY_REQUEST PropertyRequest) {
    if (PropertyRequest == nullptr || PropertyRequest->MajorTarget == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    auto* miniport = static_cast<HibikiMiniportWaveRtV1*>(PropertyRequest->MajorTarget);
    return HibikiPropertyHandlerVolumeV1(PropertyRequest, miniport->GetPropertyContext());
}

static NTSTATUS PropertyHandler_Mute(
    _In_ PPCPROPERTY_REQUEST PropertyRequest) {
    if (PropertyRequest == nullptr || PropertyRequest->MajorTarget == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    auto* miniport = static_cast<HibikiMiniportWaveRtV1*>(PropertyRequest->MajorTarget);
    return HibikiPropertyHandlerMuteV1(PropertyRequest, miniport->GetPropertyContext());
}

static const PCPROPERTY_ITEM VolumeProperties[] = {
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_VOLUMELEVEL,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_Volume
    }
};

static const PCPROPERTY_ITEM MuteProperties[] = {
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_MUTE,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_Mute
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationVolume, VolumeProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(AutomationMute, MuteProperties);

//=============================================================================
// Node Descriptors
//=============================================================================

enum {
    NODE_MUTE = 0,
    NODE_VOLUME = 1,
    TOTAL_NODES = 2
};

static const PCNODE_DESCRIPTOR EndpointNodes[] = {
    // NODE_MUTE
    {
        0,                      // Flags
        &AutomationMute,        // AutomationTable
        &KSNODETYPE_MUTE,       // Type
        nullptr        // Name
    },
    // NODE_VOLUME
    {
        0,                      // Flags
        &AutomationVolume,      // AutomationTable
        &KSNODETYPE_VOLUME,     // Type
        &KSAUDFNAME_VOLUME      // Name
    }
};

//=============================================================================
// Connection Descriptors
//=============================================================================

// Render Connection Graph: Pin 0 (Wave In) -> Mute -> Volume -> Pin 1 (Bridge Out)
static const PCCONNECTION_DESCRIPTOR RenderConnections[] = {
    { PCFILTER_NODE, 0, NODE_MUTE,   1 }, // From Wave Pin (0) to Mute Node Input (1)
    { NODE_MUTE,     0, NODE_VOLUME, 1 }, // From Mute Node Output (0) to Volume Node Input (1)
    { NODE_VOLUME,   0, PCFILTER_NODE, 1 } // From Volume Node Output (0) to Bridge Pin (1)
};

// Capture Connection Graph: Pin 0 (Bridge In) -> Mute -> Volume -> Pin 1 (Wave Out)
static const PCCONNECTION_DESCRIPTOR CaptureConnections[] = {
    { PCFILTER_NODE, 0, NODE_MUTE,   1 }, // From Bridge Pin (0) to Mute Node Input (1)
    { NODE_MUTE,     0, NODE_VOLUME, 1 }, // From Mute Node Output (0) to Volume Node Input (1)
    { NODE_VOLUME,   0, PCFILTER_NODE, 1 } // From Volume Node Output (0) to Wave Pin (1)
};

//=============================================================================
// Data Ranges for Endpoints
//=============================================================================

static const KSDATARANGE_AUDIO DataRange_Main = {
    {
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    2,                  // MaximumChannels
    32,                 // MinimumBitsPerSample
    32,                 // MaximumBitsPerSample
    48000,              // MinimumSampleFrequency
    48000               // MaximumSampleFrequency
};

static const KSDATARANGE_AUDIO DataRange_LowLatency = {
    {
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    2,                  // MaximumChannels
    32,                 // MinimumBitsPerSample
    32,                 // MaximumBitsPerSample
    48000,              // MinimumSampleFrequency
    48000               // MaximumSampleFrequency
};

static const KSDATARANGE_AUDIO DataRange_Surround = {
    {
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    8,                  // MaximumChannels
    32,                 // MinimumBitsPerSample
    32,                 // MaximumBitsPerSample
    48000,              // MinimumSampleFrequency
    48000               // MaximumSampleFrequency
};

static const KSDATARANGE_AUDIO DataRange_VirtualMic = {
    {
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    2,                  // MaximumChannels
    32,                 // MinimumBitsPerSample
    32,                 // MaximumBitsPerSample
    48000,              // MinimumSampleFrequency
    48000               // MaximumSampleFrequency
};

static const PKSDATARANGE PinDataRanges_Main[] = {
    (PKSDATARANGE)&DataRange_Main
};

static const PKSDATARANGE PinDataRanges_LowLatency[] = {
    (PKSDATARANGE)&DataRange_LowLatency
};

static const PKSDATARANGE PinDataRanges_Surround[] = {
    (PKSDATARANGE)&DataRange_Surround
};

static const PKSDATARANGE PinDataRanges_VirtualMic[] = {
    (PKSDATARANGE)&DataRange_VirtualMic
};

// Interface & Medium descriptors for streaming pins.
// WDK KSPIN_DESCRIPTOR expects const KSPIN_INTERFACE* and const KSPIN_MEDIUM*
// (both are KSIDENTIFIER: Set GUID + Id + Flags), not KSDATARANGE arrays.
static const KSPIN_INTERFACE PinInterfacesStream[] = {
    {
        STATICGUIDOF(KSINTERFACESETID_Standard),
        KSINTERFACE_STANDARD_STREAMING,
        0
    }
};

static const KSPIN_MEDIUM PinMediumsDontCare[] = {
    {
        STATICGUIDOF(KSMEDIUMSETID_Standard),
        KSMEDIUM_STANDARD_DEVIO,
        0
    }
};

//=============================================================================
// Pin Descriptors
//=============================================================================

// Helper macro for render pin tables
#define DEFINE_RENDER_PINS(Name, DataRanges)                                   \
static const PCPIN_DESCRIPTOR Pins_##Name[] = {                                \
    /* Pin 0: Streaming WaveRT Pin (Source -> internal graph) */                \
    {                                                                          \
        1, 1, 1,                                                               \
        NULL,                                                                  \
        {                                                                      \
            1, PinInterfacesStream,                                           \
            1, PinMediumsDontCare,                                            \
            SIZEOF_ARRAY(DataRanges), DataRanges,                              \
            KSPIN_DATAFLOW_OUT,                                                \
            KSPIN_COMMUNICATION_SINK,                                          \
            &KSCATEGORY_AUDIO,                                                 \
            NULL,                                                              \
            0                                                                  \
        }                                                                      \
    },                                                                         \
    /* Pin 1: Physical Bridge Pin (all instance counts zero) */                 \
    {                                                                          \
        0, 0, 0,                                                               \
        NULL,                                                                  \
        {                                                                      \
            0, NULL,                                                           \
            0, NULL,                                                           \
            0, NULL,                                                           \
            KSPIN_DATAFLOW_IN,                                                 \
            KSPIN_COMMUNICATION_NONE,                                          \
            &KSNODETYPE_SPEAKER,                                               \
            NULL,                                                              \
            0                                                                  \
        }                                                                      \
    }                                                                          \
}

DEFINE_RENDER_PINS(Main, PinDataRanges_Main);
DEFINE_RENDER_PINS(LowLatency, PinDataRanges_LowLatency);
DEFINE_RENDER_PINS(Surround, PinDataRanges_Surround);

// Pin table for Virtual Mic (Capture)
static const PCPIN_DESCRIPTOR Pins_VirtualMic[] = {
    /* Pin 0: Physical Bridge Pin (all instance counts zero) */
    {
        0, 0, 0,
        NULL,
        {
            0, NULL,
            0, NULL,
            0, NULL,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_MICROPHONE,
            NULL,
            0
        }
    },
    /* Pin 1: Streaming WaveRT Pin (Sink <- internal graph) */
    {
        1, 1, 1,
        NULL,
        {
            1, PinInterfacesStream,
            1, PinMediumsDontCare,
            SIZEOF_ARRAY(PinDataRanges_VirtualMic), PinDataRanges_VirtualMic,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    }
};

//=============================================================================
// Category Arrays
//=============================================================================

static const GUID RenderCategories[] = {
    STATICGUIDOF(KSCATEGORY_AUDIO),
    STATICGUIDOF(KSCATEGORY_RENDER)
};

static const GUID CaptureCategories[] = {
    STATICGUIDOF(KSCATEGORY_AUDIO),
    STATICGUIDOF(KSCATEGORY_CAPTURE)
};

//=============================================================================
// Filter Descriptors
//=============================================================================

#define DEFINE_FILTER_DESCRIPTOR(Name, PinsTable, CategoriesTable, ConnectionsTable) \
static const PCFILTER_DESCRIPTOR FilterDescriptor_##Name = {                        \
    0,                                      /* Version */                           \
    NULL,                                   /* AutomationTable */                   \
    sizeof(PCPIN_DESCRIPTOR),               /* PinSize */                           \
    SIZEOF_ARRAY(PinsTable),                /* PinCount */                          \
    PinsTable,                              /* Pins */                              \
    sizeof(PCNODE_DESCRIPTOR),              /* NodeSize */                          \
    SIZEOF_ARRAY(EndpointNodes),            /* NodeCount */                         \
    EndpointNodes,                          /* Nodes */                             \
    SIZEOF_ARRAY(ConnectionsTable),         /* ConnectionCount */                   \
    ConnectionsTable,                       /* Connections */                       \
    SIZEOF_ARRAY(CategoriesTable),          /* CategoryCount */                     \
    CategoriesTable                         /* Categories */                        \
}

DEFINE_FILTER_DESCRIPTOR(Main, Pins_Main, RenderCategories, RenderConnections);
DEFINE_FILTER_DESCRIPTOR(LowLatency, Pins_LowLatency, RenderCategories, RenderConnections);
DEFINE_FILTER_DESCRIPTOR(Surround, Pins_Surround, RenderCategories, RenderConnections);
DEFINE_FILTER_DESCRIPTOR(VirtualMic, Pins_VirtualMic, CaptureCategories, CaptureConnections);

//=============================================================================
// Public Filter Table Entry Points
//=============================================================================

extern "C" NTSTATUS HibikiGetFilterDescriptorEndpointV1(
    _In_  ULONG                     EndpointIndex,
    _Out_ const PCFILTER_DESCRIPTOR** Description) {
    if (Description == nullptr) return STATUS_INVALID_PARAMETER;

    // EndpointIndex is a zero-based slot into the topology table (same as
    // hibiki_endpoint_topology_get_v1), not the one-based endpoint_kind enum.
    switch (EndpointIndex) {
        case 0: /* HIBIKI_ENDPOINT_MAIN_RENDER_V1 */
            *Description = &FilterDescriptor_Main;
            return STATUS_SUCCESS;
        case 1: /* HIBIKI_ENDPOINT_LOW_LATENCY_RENDER_V1 */
            *Description = &FilterDescriptor_LowLatency;
            return STATUS_SUCCESS;
        case 2: /* HIBIKI_ENDPOINT_SURROUND_RENDER_V1 */
            *Description = &FilterDescriptor_Surround;
            return STATUS_SUCCESS;
        case 3: /* HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1 */
            *Description = &FilterDescriptor_VirtualMic;
            return STATUS_SUCCESS;
        default:
            *Description = nullptr;
            return STATUS_INVALID_PARAMETER;
    }
}

extern "C" NTSTATUS HibikiDataRangeIntersectionEndpointV1(
    _In_        ULONG              EndpointIndex,
    _In_        ULONG              PinId,
    _In_        PKSDATARANGE       DataRange,
    _In_        PKSDATARANGE       MatchingDataRange,
    _In_        ULONG              OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
                PVOID              ResultantFormat,
    _Out_       PULONG             ResultantFormatLength) {
    if (ResultantFormatLength == nullptr || DataRange == nullptr || MatchingDataRange == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(EndpointIndex, &topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&topology) == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Verify streaming pin ID (Pin 0 for Render, Pin 1 for Capture)
    const ULONG streaming_pin = (topology.direction == HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1) ? 1U : 0U;
    if (PinId != streaming_pin) {
        return STATUS_NOT_FOUND;
    }

    // Verify format specifiers
    if (!IsEqualGUIDAligned(DataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
        !IsEqualGUIDAligned(DataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) {
        return STATUS_NO_MATCH;
    }

    const ULONG required_size = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    *ResultantFormatLength = required_size;

    if (OutputBufferLength == 0) {
        return STATUS_BUFFER_OVERFLOW;
    }
    if (OutputBufferLength < required_size || ResultantFormat == nullptr) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    auto* formatExt = static_cast<KSDATAFORMAT_WAVEFORMATEXTENSIBLE*>(ResultantFormat);
    RtlZeroMemory(formatExt, sizeof(*formatExt));

    formatExt->DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    formatExt->DataFormat.Flags = 0;
    formatExt->DataFormat.SampleSize = topology.channel_count * sizeof(float);
    formatExt->DataFormat.Reserved = 0;
    formatExt->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    formatExt->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    formatExt->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

    const NTSTATUS buildStatus = HibikiWaveRtBuildFormatEndpointV1(
        EndpointIndex, &formatExt->WaveFormatExt);
    if (!NT_SUCCESS(buildStatus)) {
        return buildStatus;
    }

    return STATUS_SUCCESS;
}




