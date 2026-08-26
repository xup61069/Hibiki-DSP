// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/session_command_queue.hpp"

#include <cstdint>
#include <cstdio>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::SessionCommandKindV1;
using hibiki::SessionCommandQueueV1;
using hibiki::SessionCommandWorkItemV1;
using hibiki::SessionRouteCommandV1;
using hibiki::SessionRouteRuleCommandV1;
using hibiki::SessionVolumeCommandV1;

SessionVolumeCommandV1 make_volume(std::uint64_t handle,
                                   std::int32_t requested_db_q16_16) {
    SessionVolumeCommandV1 command;
    command.handle = handle;
    command.requested_db_q16_16 = requested_db_q16_16;
    command.mute = 1U;
    command.catalog_sequence = handle * 10U;
    return command;
}

SessionRouteCommandV1 make_route(std::uint64_t handle, char lane_tag) {
    SessionRouteCommandV1 command;
    command.handle = handle;
    command.catalog_sequence = handle * 10U + 1U;
    constexpr char kLane[] = "lane-x";
    static_assert(sizeof(kLane) <= command.lane.size());
    for (std::size_t index = 0U; index < sizeof(kLane); ++index) {
        if (kLane[index] == 'x') {
            command.lane[static_cast<std::size_t>(index)] = lane_tag;
        } else {
            command.lane[static_cast<std::size_t>(index)] = kLane[index];
        }
    }
    command.output_group[0] = 'm';
    command.output_group[1] = 'a';
    command.output_group[2] = 'i';
    command.output_group[3] = 'n';
    command.lane_bytes = 6U;
    command.output_group_bytes = 4U;
    return command;
}

SessionRouteRuleCommandV1 make_route_rule(std::uint64_t sequence) {
    SessionRouteRuleCommandV1 command;
    command.schema_version = 1U;
    command.priority = 42;
    command.makeup_gain_q16_16 = 3 * 65536;
    command.enabled = 1U;
    command.catalog_sequence = sequence;
    const char* rule_id = "rule-64";
    for (std::size_t index = 0U; rule_id[index] != '\0'; ++index) {
        command.rule_id[index] = rule_id[index];
        ++command.rule_id_bytes;
    }
    return command;
}

}  // namespace

int main() {
    // Applied: volume/route/route-rule helpers tag the work item kind and the
    // pop copies the payload fields back out unchanged (contract baseline).
    {
        SessionCommandQueueV1 queue;
        const auto volume = make_volume(7U, -12 * 65536);
        CHECK(queue.try_push_volume(volume));
        SessionCommandWorkItemV1 item{};
        CHECK(queue.try_pop(item));
        CHECK(item.kind == SessionCommandKindV1::Volume);
        CHECK(item.volume.handle == 7U);
        CHECK(item.volume.requested_db_q16_16 == -12 * 65536);
        CHECK(item.volume.mute == 1U);
        CHECK(item.volume.catalog_sequence == 70U);
        CHECK(!queue.try_pop(item));
    }
    {
        // Applied: route payloads keep bounded lane/output bytes plus text.
        SessionCommandQueueV1 queue;
        CHECK(queue.try_push_route(make_route(9U, 'r')));
        SessionCommandWorkItemV1 item{};
        CHECK(queue.try_pop(item));
        CHECK(item.kind == SessionCommandKindV1::Route);
        CHECK(item.route.handle == 9U);
        CHECK(item.route.catalog_sequence == 91U);
        CHECK(item.route.lane_bytes == 6U && item.route.lane[5] == 'r');
        CHECK(item.route.output_group_bytes == 4U &&
              item.route.output_group[0] == 'm' && item.route.output_group[3] == 'n');
    }
    {
        // Applied: route-rule payloads carry their own schema and identity.
        SessionCommandQueueV1 queue;
        CHECK(queue.try_push_route_rule(make_route_rule(1234U)));
        SessionCommandWorkItemV1 item{};
        CHECK(queue.try_pop(item));
        CHECK(item.kind == SessionCommandKindV1::RouteRule);
        CHECK(item.route_rule.schema_version == 1U);
        CHECK(item.route_rule.priority == 42);
        CHECK(item.route_rule.catalog_sequence == 1234U);
        CHECK(item.route_rule.rule_id_bytes == 7U &&
              item.route_rule.rule_id[0] == 'r' && item.route_rule.rule_id[6] == '4');
    }
    {
        // Applied: pushing one kind must not leak stale bytes into the other
        // union-style payload members; every helper starts from a zeroed item.
        SessionCommandQueueV1 queue;
        auto noisy_route = make_route(11U, 'z');
        noisy_route.lane.fill('q');
        noisy_route.output_group.fill('q');
        CHECK(queue.try_push_route(noisy_route));
        CHECK(queue.try_push_volume(make_volume(12U, -65536)));
        SessionCommandWorkItemV1 item{};
        CHECK(queue.try_pop(item));
        CHECK(item.kind == SessionCommandKindV1::Route);
        bool route_text_bounded = true;
        for (std::size_t index = item.route.lane_bytes; index < item.route.lane.size();
             ++index) {
            route_text_bounded = route_text_bounded && item.route.lane[index] == 'q';
        }
        for (std::size_t index = item.route.output_group_bytes;
             index < item.route.output_group.size(); ++index) {
            route_text_bounded =
                route_text_bounded && item.route.output_group[index] == 'q';
        }
        CHECK(route_text_bounded);
        CHECK(item.volume.handle == 0U && item.route_rule.priority == 0);
        CHECK(queue.try_pop(item));
        CHECK(item.kind == SessionCommandKindV1::Volume);
        CHECK(item.route.lane_bytes == 0U && item.route_rule.rule_id_bytes == 0U);
        CHECK(item.route_rule.priority == 0);
    }
    {
        // Applied: capacity is exactly 64; slot 65 fails closed and counts one
        // drop without disturbing queued entries.
        SessionCommandQueueV1 queue;
        for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity; ++index) {
            CHECK(queue.try_push_volume(make_volume(index + 1U, -1000)));
        }
        const auto overflow_volume = make_volume(999U, -1000);
        CHECK(!queue.try_push_volume(overflow_volume));
        CHECK(queue.dropped() == 1U);
        for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity; ++index) {
            SessionCommandWorkItemV1 item{};
            CHECK(queue.try_pop(item));
            CHECK(item.volume.handle == index + 1U);
        }
        SessionCommandWorkItemV1 probe{};
        CHECK(!queue.try_pop(probe));
    }
    {
        // Applied: SPSC ring wraps modulo 64 and preserves FIFO order across
        // the wrap boundary (fill, drain, refill past the physical end).
        SessionCommandQueueV1 queue;
        for (std::size_t round = 0U; round < 3U; ++round) {
            for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity; ++index) {
                const std::uint64_t token =
                    static_cast<std::uint64_t>(round) * 1000U + index;
                CHECK(queue.try_push_volume(make_volume(token, -2000)));
            }
            for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity; ++index) {
                SessionCommandWorkItemV1 item{};
                CHECK(queue.try_pop(item));
                CHECK(item.volume.handle ==
                      static_cast<std::uint64_t>(round) * 1000U + index);
            }
        }
        CHECK(queue.dropped() == 0U);
    }
    {
        // Applied: interleaved push/pop across the wrap keeps per-slot data
        // isolated (each popped item matches exactly what was pushed there).
        SessionCommandQueueV1 queue;
        for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity / 2U; ++index) {
            CHECK(queue.try_push_route(make_route(index + 1U, static_cast<char>('a' + index))));
        }
        for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity / 2U; ++index) {
            SessionCommandWorkItemV1 item{};
            CHECK(queue.try_pop(item));
            CHECK(item.route.handle == index + 1U);
        }
        // head is now at 32; fill past the physical array end to force wrap.
        for (std::size_t index = 32U; index < SessionCommandQueueV1::kCapacity + 8U; ++index) {
            const auto volume = make_volume(index * 2U, -3000);
            CHECK(queue.try_push_volume(volume));
        }
        for (std::size_t index = 32U; index < SessionCommandQueueV1::kCapacity + 8U; ++index) {
            SessionCommandWorkItemV1 item{};
            CHECK(queue.try_pop(item));
            CHECK(item.kind == SessionCommandKindV1::Volume);
            CHECK(item.volume.handle == index * 2U);
        }
        SessionCommandWorkItemV1 wrap_probe{};
        CHECK(!queue.try_pop(wrap_probe));
        CHECK(queue.dropped() == 0U);
    }
    {
        // Applied: reset clears pending work items so no prior-host command can
        // leak into a new runtime binding, zeroes dropped counter and empties
        // the queue even when it was full.
        SessionCommandQueueV1 queue;
        for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity; ++index) {
            CHECK(queue.try_push_route_rule(make_route_rule(index + 1U)));
        }
        const auto overflow_rule = make_route_rule(65U);
        CHECK(!queue.try_push_route_rule(overflow_rule) && queue.dropped() == 1U);
        queue.reset();
        SessionCommandWorkItemV1 item{};
        CHECK(!queue.try_pop(item));
        CHECK(queue.dropped() == 0U);
        // The reset zeroed slot storage itself: a fresh push/pop cycle sees a
        // clean item rather than residue from the pre-reset full queue.
        CHECK(queue.try_push_volume(make_volume(77U, -4096)));
        SessionCommandWorkItemV1 residue_probe{};
        residue_probe.route_rule.priority = -5;
        CHECK(queue.try_pop(residue_probe));
        CHECK(residue_probe.kind == SessionCommandKindV1::Volume &&
              residue_probe.volume.handle == 77U);
        CHECK(residue_probe.route.handle == 0U && residue_probe.route_rule.priority == 0);
        CHECK(!queue.try_pop(item));
    }
    std::fputs("session command queue tests passed\n", stdout);
    return 0;
}
