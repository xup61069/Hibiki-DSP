// SPDX-License-Identifier: MS-PL
//
// WDK-only property adapter scaffold. This file is intentionally excluded
// from the portable CMake target until a real SYSVAD-derived miniport supplies
// one context per endpoint. It contains no GPL/user-space linkage.

#if !defined(_NTDDK_)
#error "Compile this adapter only inside a WDK PortCls driver project"
#endif

#include <ntddk.h>
#include <portcls.h>
#include <ks.h>
#include <ksmedia.h>

#include "hibiki/endpoint_topology_v1.h"
#include "hibiki/wavert_endpoint_state_v1.h"

struct hibiki_wdk_endpoint_context_v1 {
    FAST_MUTEX property_lock;
    hibiki_wavert_endpoint_state_v1 state;
};

static constexpr ULONG HIBIKI_ALL_CHANNELS_ID_V1 = MAXULONG;

extern "C" NTSTATUS HibikiPropertyContextInitializeEndpointV1(
    _Out_ hibiki_wdk_endpoint_context_v1* context,
    _In_ ULONG endpoint_index,
    _In_ ULONG actuator) {
    if (context == nullptr) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(context, sizeof(*context));
    hibiki_endpoint_topology_v1 topology{};
    if (hibiki_endpoint_topology_get_v1(endpoint_index, &topology) == 0 ||
        hibiki_endpoint_topology_validate_v1(&topology) == 0 ||
        topology.channel_count > 8U) {
        return STATUS_INVALID_PARAMETER;
    }
    ExInitializeFastMutex(&context->property_lock);
    if (hibiki_wavert_endpoint_state_init_v1(
            &context->state, topology.endpoint_guid, topology.channel_count,
            topology.sample_rate, actuator) == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS hibiki_validate_request(
    _In_ PPCPROPERTY_REQUEST request,
    _In_ ULONG value_bytes,
    _In_ ULONG instance_bytes) {
    if (request == nullptr || request->PropertyItem == nullptr ||
        (instance_bytes != 0U && request->Instance == nullptr)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (request->InstanceSize < instance_bytes) {
        request->ValueSize = value_bytes;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if ((request->Verb & KSPROPERTY_TYPE_BASICSUPPORT) != 0U) {
        if (request->Value == nullptr || request->ValueSize < sizeof(ULONG)) {
            request->ValueSize = sizeof(ULONG);
            return STATUS_BUFFER_TOO_SMALL;
        }
        return STATUS_SUCCESS;
    }
    if (request->Value == nullptr) return STATUS_INVALID_PARAMETER;
    if (request->ValueSize < value_bytes || request->InstanceSize < instance_bytes) {
        request->ValueSize = value_bytes;
        return STATUS_BUFFER_TOO_SMALL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS hibiki_validate_verb(_In_ PPCPROPERTY_REQUEST request) {
    if (request == nullptr) return STATUS_INVALID_PARAMETER;
    if ((request->Verb & KSPROPERTY_TYPE_BASICSUPPORT) != 0U) return STATUS_SUCCESS;
    const auto access = request->Verb & (KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET);
    return access == KSPROPERTY_TYPE_GET || access == KSPROPERTY_TYPE_SET
               ? STATUS_SUCCESS
               : STATUS_INVALID_DEVICE_REQUEST;
}

extern "C" NTSTATUS HibikiPropertyHandlerVolumeV1(
    _In_ PPCPROPERTY_REQUEST request,
    _Inout_ hibiki_wdk_endpoint_context_v1* context) {
    if (context == nullptr) return STATUS_INVALID_PARAMETER;
    const auto validation = hibiki_validate_request(request, sizeof(LONG), sizeof(ULONG));
    if (!NT_SUCCESS(validation)) return validation;
    const auto verb_validation = hibiki_validate_verb(request);
    if (!NT_SUCCESS(verb_validation)) return verb_validation;

    const auto channel = *static_cast<const ULONG*>(request->Instance);
    if (channel >= context->state.channel_count && channel != HIBIKI_ALL_CHANNELS_ID_V1) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((request->Verb & KSPROPERTY_TYPE_BASICSUPPORT) != 0U) {
        *static_cast<ULONG*>(request->Value) = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
        request->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    ExAcquireFastMutex(&context->property_lock);
    if ((request->Verb & KSPROPERTY_TYPE_GET) != 0U) {
        *static_cast<LONG*>(request->Value) = context->state.effective_db_q16_16;
        request->ValueSize = sizeof(LONG);
    } else if ((request->Verb & KSPROPERTY_TYPE_SET) != 0U) {
        const auto requested = *static_cast<const LONG*>(request->Value);
        const auto next_generation = context->state.generation + 1U;
        if (!hibiki_wavert_endpoint_state_apply_volume_v1(
                &context->state,
                requested,
                context->state.safety_ceiling_db_q16_16,
                context->state.mute,
                next_generation,
                nullptr)) {
            ExReleaseFastMutex(&context->property_lock);
            return STATUS_INVALID_PARAMETER;
        }
    } else {
        ExReleaseFastMutex(&context->property_lock);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    ExReleaseFastMutex(&context->property_lock);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS HibikiPropertyHandlerMuteV1(
    _In_ PPCPROPERTY_REQUEST request,
    _Inout_ hibiki_wdk_endpoint_context_v1* context) {
    if (context == nullptr) return STATUS_INVALID_PARAMETER;
    const auto validation = hibiki_validate_request(request, sizeof(BOOL), sizeof(ULONG));
    if (!NT_SUCCESS(validation)) return validation;
    const auto verb_validation = hibiki_validate_verb(request);
    if (!NT_SUCCESS(verb_validation)) return verb_validation;
    const auto channel = *static_cast<const ULONG*>(request->Instance);
    if (channel >= context->state.channel_count && channel != HIBIKI_ALL_CHANNELS_ID_V1) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((request->Verb & KSPROPERTY_TYPE_BASICSUPPORT) != 0U) {
        *static_cast<ULONG*>(request->Value) = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
        request->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    ExAcquireFastMutex(&context->property_lock);
    if ((request->Verb & KSPROPERTY_TYPE_GET) != 0U) {
        *static_cast<BOOL*>(request->Value) = context->state.mute != 0U ? TRUE : FALSE;
        request->ValueSize = sizeof(BOOL);
    } else if ((request->Verb & KSPROPERTY_TYPE_SET) != 0U) {
        const auto mute = *static_cast<const BOOL*>(request->Value) != FALSE ? 1U : 0U;
        if (!hibiki_wavert_endpoint_state_apply_volume_v1(
                &context->state,
                context->state.requested_db_q16_16,
                context->state.safety_ceiling_db_q16_16,
                mute,
                context->state.generation + 1U,
                nullptr)) {
            ExReleaseFastMutex(&context->property_lock);
            return STATUS_INVALID_PARAMETER;
        }
    } else {
        ExReleaseFastMutex(&context->property_lock);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    ExReleaseFastMutex(&context->property_lock);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS HibikiPropertyDispatchV1(
    _In_ PPCPROPERTY_REQUEST request,
    _Inout_ hibiki_wdk_endpoint_context_v1* context) {
    if (request == nullptr || request->PropertyItem == nullptr ||
        request->PropertyItem->Set == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!IsEqualGUIDAligned(*request->PropertyItem->Set, KSPROPSETID_Audio)) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    switch (request->PropertyItem->Id) {
        case KSPROPERTY_AUDIO_VOLUMELEVEL:
            return HibikiPropertyHandlerVolumeV1(request, context);
        case KSPROPERTY_AUDIO_MUTE:
            return HibikiPropertyHandlerMuteV1(request, context);
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}
