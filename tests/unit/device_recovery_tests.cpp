// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_recovery.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::DeviceRecoveryCoordinator;
using hibiki::DeviceRecoveryEventKind;
using hibiki::DeviceRecoveryEventV1;
using hibiki::DeviceRecoveryState;
using hibiki::OutputGroupVolumeStateV1;

}  // namespace

int main() {
    // observe: sequence==0 rejected.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(!recovery.observe(DeviceRecoveryEventV1{
            0U, DeviceRecoveryEventKind::DefaultChanged, true}));
        CHECK(recovery.state() == DeviceRecoveryState::Stable);
    }
    // observe: duplicate/out-of-order sequences rejected.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(recovery.observe(
            DeviceRecoveryEventV1{5U, DeviceRecoveryEventKind::DefaultChanged, true}));
        CHECK(recovery.last_event_sequence() == 5U);
        CHECK(!recovery.observe(
            DeviceRecoveryEventV1{5U, DeviceRecoveryEventKind::DefaultChanged, true}));
        CHECK(!recovery.observe(
            DeviceRecoveryEventV1{3U, DeviceRecoveryEventKind::EndpointAdded, true}));
        CHECK(recovery.last_event_sequence() == 5U);
    }
    // observe: DefaultChanged/EndpointAdded always trigger RebindPending.
    for (const auto kind : {DeviceRecoveryEventKind::DefaultChanged,
                            DeviceRecoveryEventKind::EndpointAdded}) {
        DeviceRecoveryCoordinator recovery;
        CHECK(recovery.observe(DeviceRecoveryEventV1{1U, kind, false}));
        CHECK(recovery.state() == DeviceRecoveryState::RebindPending);
    }
    // observe: invalidation events ignored when not affecting active endpoint
    // and an active target exists.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(recovery.observe(DeviceRecoveryEventV1{
            1U, DeviceRecoveryEventKind::DefaultChanged, true}));
        CHECK(recovery.begin_rebind(hibiki::DeviceTargetV1{"hibiki-main", 2U, 48000U, 128U}));
        CHECK(recovery.prepare());
        CHECK(recovery.commit());
        CHECK(!recovery.observe(DeviceRecoveryEventV1{
            2U, DeviceRecoveryEventKind::FormatChanged, false}));
        CHECK(recovery.state() == DeviceRecoveryState::Stable);
    }
    // observe: invalidation events trigger RebindPending when affecting active
    // endpoint even without an active target.
    for (const auto kind : {DeviceRecoveryEventKind::EndpointRemoved,
                            DeviceRecoveryEventKind::EndpointInvalidated,
                            DeviceRecoveryEventKind::FormatChanged,
                            DeviceRecoveryEventKind::AudioServiceRestarted}) {
        DeviceRecoveryCoordinator recovery;
        CHECK(recovery.observe(DeviceRecoveryEventV1{1U, kind, true}));
        CHECK(recovery.state() == DeviceRecoveryState::RebindPending);
    }
    // observe: EndpointStateChanged only reacts to active-endpoint events.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(!recovery.observe(
            DeviceRecoveryEventV1{1U, DeviceRecoveryEventKind::EndpointStateChanged, false}));
        CHECK(recovery.state() == DeviceRecoveryState::Stable);
        CHECK(recovery.observe(
            DeviceRecoveryEventV1{2U, DeviceRecoveryEventKind::EndpointStateChanged, true}));
        CHECK(recovery.state() == DeviceRecoveryState::RebindPending);
    }
    // rebind: begin from Stable rejected.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(!recovery.begin_rebind(hibiki::DeviceTargetV1{"hibiki-main", 2U, 48000U, 128U}));
        CHECK(recovery.state() == DeviceRecoveryState::Stable);
    }
    // rebind: full transaction RebindPending -> Rebinding -> commit -> Stable.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(recovery.observe(
            DeviceRecoveryEventV1{1U, DeviceRecoveryEventKind::DefaultChanged, true}));
        CHECK(recovery.begin_rebind(hibiki::DeviceTargetV1{"hibiki-main", 2U, 48000U, 128U}));
        CHECK(recovery.state() == DeviceRecoveryState::Rebinding);
        CHECK(!recovery.commit());  // prepare not called yet.
        CHECK(recovery.prepare());
        CHECK(recovery.commit());
        CHECK(recovery.state() == DeviceRecoveryState::Stable);
        CHECK(recovery.transaction().active_target().endpoint_id == "hibiki-main");
    }
    // rebind: rollback with previous stable binding returns to Stable.
    {
        DeviceRecoveryCoordinator recovery;
        CHECK(recovery.begin_rebind(hibiki::DeviceTargetV1{"old-sink", 2U, 48000U, 128U}) == false);
    }
    // rebind: catalog-based begin rejects unselectable endpoints.
    {
        DeviceRecoveryCoordinator recovery;
        hibiki::PhysicalDeviceCatalogV1 catalog;
        hibiki::PhysicalDeviceDescriptorV1 descriptor;
        descriptor.endpoint_id = "recovery-render";
        descriptor.display_name = "Recovery Render";
        descriptor.flow = hibiki::PhysicalDeviceFlowV1::Render;
        descriptor.availability = hibiki::PhysicalDeviceAvailabilityV1::Active;
        CHECK(catalog.upsert(descriptor) ==
              hibiki::PhysicalDeviceCatalogResultV1::Accepted);
        CHECK(recovery.observe(
            DeviceRecoveryEventV1{1U, DeviceRecoveryEventKind::EndpointInvalidated, true}));
        CHECK(recovery.begin_rebind(catalog, "recovery-render"));
        CHECK(recovery.state() == DeviceRecoveryState::Rebinding);
    }
    // rebind: catalog-based begin with empty id or unplugged endpoint rejected.
    {
        DeviceRecoveryCoordinator recovery;
        hibiki::PhysicalDeviceCatalogV1 catalog;
        hibiki::PhysicalDeviceDescriptorV1 descriptor;
        descriptor.endpoint_id = "unplugged-sink";
        descriptor.display_name = "Unplugged Sink";
        descriptor.flow = hibiki::PhysicalDeviceFlowV1::Render;
        descriptor.availability = hibiki::PhysicalDeviceAvailabilityV1::Unplugged;
        CHECK(catalog.upsert(descriptor) ==
              hibiki::PhysicalDeviceCatalogResultV1::Accepted);
        CHECK(recovery.observe(
            DeviceRecoveryEventV1{1U, DeviceRecoveryEventKind::EndpointInvalidated, true}));
        CHECK(!recovery.begin_rebind(catalog, ""));
        CHECK(!recovery.begin_rebind(catalog, "unplugged-sink"));
        CHECK(!recovery.begin_rebind(catalog, "missing-endpoint"));
        CHECK(recovery.state() == DeviceRecoveryState::RebindPending);
    }
    // safe restart: clamps to safe-start dB, forces mute, bumps generation.
    {
        DeviceRecoveryCoordinator recovery;
        OutputGroupVolumeStateV1 volume;
        volume.requested_db = 0.0;
        volume.safety_ceiling_db = 0.0;
        const auto before_generation = volume.generation;
        const auto restarted = recovery.safe_restart_state(volume, -48.0);
        CHECK(restarted.mute);
        CHECK(std::abs(restarted.requested_db + 48.0) < 1e-12);
        CHECK(restarted.effective_db <= -48.0);
        CHECK(restarted.generation == before_generation + 1U);
    }
    // safe restart: generation saturates at the largest nonzero freshness
    // token instead of wrapping to the reserved zero value.
    {
        DeviceRecoveryCoordinator recovery;
        OutputGroupVolumeStateV1 volume;
        volume.requested_db = 0.0;
        volume.generation = (std::numeric_limits<std::uint64_t>::max)() - 1U;
        const auto restarted = recovery.safe_restart_state(volume, -48.0);
        CHECK(restarted.generation == (std::numeric_limits<std::uint64_t>::max)());
        CHECK(restarted.mute);
        CHECK(restarted.requested_db == -48.0);
    }
    {
        DeviceRecoveryCoordinator recovery;
        OutputGroupVolumeStateV1 volume;
        volume.requested_db = 0.0;
        volume.generation = (std::numeric_limits<std::uint64_t>::max)();
        const auto restarted = recovery.safe_restart_state(volume, -48.0);
        CHECK(restarted.generation == (std::numeric_limits<std::uint64_t>::max)());
        CHECK(restarted.generation != 0U);
        CHECK(restarted.mute);
        CHECK(restarted.requested_db == -48.0);
    }
    // safe restart: non-finite safe_start falls back to -60 dB.
    {
        DeviceRecoveryCoordinator recovery;
        OutputGroupVolumeStateV1 volume;
        volume.requested_db = -10.0;
        const auto restarted = recovery.safe_restart_state(
            volume, std::numeric_limits<double>::quiet_NaN());
        CHECK(std::abs(restarted.requested_db + 60.0) < 1e-12);
    }

    // safe restart: below -144 clamps to -144; above 0 clamps to 0.
    {
        DeviceRecoveryCoordinator recovery;
        OutputGroupVolumeStateV1 volume;
        volume.requested_db = 0.0;
        const auto low = recovery.safe_restart_state(volume, -200.0);
        CHECK(std::abs(low.requested_db + 144.0) < 1e-12);
        const auto high = recovery.safe_restart_state(volume, 12.0);
        CHECK(std::abs(high.requested_db - 0.0) < 1e-12);
    }

    std::fputs("device_recovery_tests passed\n", stdout);
    return 0;
}
