// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/session_route_rules.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::AudioSessionDescriptorV1;
using hibiki::SessionGainOwner;
using hibiki::SessionRouteRuleResultV1;
using hibiki::SessionRouteRuleStoreV1;
using hibiki::SessionRouteRuleV1;

SessionRouteRuleV1 make_rule(const std::string& id, const int priority = 0) {
    SessionRouteRuleV1 rule{};
    rule.schema_version = 1U;
    rule.rule_id = id;
    rule.priority = priority;
    rule.enabled = true;
    rule.app_id = "spotify.exe";
    rule.lane_id = "lane.music";
    rule.output_group = "main";
    rule.gain_owner = SessionGainOwner::HibikiInternal;
    rule.makeup_gain_db = -3.0;
    return rule;
}

AudioSessionDescriptorV1 make_descriptor(const std::string& app_id,
                                         const std::string& display_name) {
    AudioSessionDescriptorV1 descriptor{};
    descriptor.schema_version = 1U;
    descriptor.identity.endpoint_id = "endpoint-a";
    descriptor.identity.session_instance_id = "session-1";
    descriptor.identity.process_id = 4321U;
    descriptor.display_name = display_name;
    descriptor.app_id = app_id;
    descriptor.active = true;
    descriptor.gain_owner = SessionGainOwner::WindowsSession;
    descriptor.lane_id = "lane.default";
    descriptor.output_group = "main-default";
    descriptor.makeup_gain_db = 0.0;
    return descriptor;
}

std::string sized_id(const std::size_t length) {
    return std::string(length, 'a');
}

}  // namespace

int main() {
    // 1) Invalid rules never enter the store.
    {
        SessionRouteRuleStoreV1 store;

        auto wrong_schema = make_rule("schema");
        wrong_schema.schema_version = 2U;
        CHECK(store.upsert(wrong_schema) == SessionRouteRuleResultV1::invalid_argument);

        CHECK(store.upsert(make_rule("")) ==
              SessionRouteRuleResultV1::invalid_argument);
        CHECK(store.upsert(make_rule("Upper")) ==
              SessionRouteRuleResultV1::invalid_argument);
        CHECK(store.upsert(make_rule(".lead")) ==
              SessionRouteRuleResultV1::invalid_argument);
        CHECK(store.upsert(make_rule("-lead")) ==
              SessionRouteRuleResultV1::invalid_argument);
        CHECK(store.upsert(make_rule(
                  sized_id(hibiki::kSessionRouteRuleMaxIdBytesV1 + 1U))) ==
              SessionRouteRuleResultV1::invalid_argument);
        const std::string nul_id{"bad\0id", 6};
        CHECK(store.upsert(make_rule(nul_id)) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto oversized_app = make_rule("app.long");
        oversized_app.app_id =
            std::string(hibiki::kSessionRouteRuleMaxMatchBytesV1 + 1U, 'x');
        CHECK(store.upsert(oversized_app) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto oversized_display = make_rule("display.long");
        oversized_display.display_name_contains =
            std::string(hibiki::kSessionRouteRuleMaxMatchBytesV1 + 1U, 'y');
        CHECK(store.upsert(oversized_display) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto whitespace_app = make_rule("app.space");
        whitespace_app.app_id = "   ";
        CHECK(store.upsert(whitespace_app) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto whitespace_lane = make_rule("lane.space");
        whitespace_lane.lane_id = " \t";
        CHECK(store.upsert(whitespace_lane) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto no_match_fields = make_rule("match.empty");
        no_match_fields.app_id.clear();
        CHECK(store.upsert(no_match_fields) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto empty_group = make_rule("group.empty");
        empty_group.output_group.clear();
        CHECK(store.upsert(empty_group) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto priority_high = make_rule("priority.high");
        priority_high.priority = 1000001;
        CHECK(store.upsert(priority_high) ==
              SessionRouteRuleResultV1::invalid_argument);
        auto priority_low = make_rule("priority.low");
        priority_low.priority = -1000001;
        CHECK(store.upsert(priority_low) ==
              SessionRouteRuleResultV1::invalid_argument);

        auto gain_nan = make_rule("gain.nan");
        gain_nan.makeup_gain_db = std::numeric_limits<double>::quiet_NaN();
        CHECK(store.upsert(gain_nan) ==
              SessionRouteRuleResultV1::invalid_argument);
        auto gain_hot = make_rule("gain.hot");
        gain_hot.makeup_gain_db = 12.5;
        CHECK(store.upsert(gain_hot) ==
              SessionRouteRuleResultV1::invalid_argument);
        auto gain_cold = make_rule("gain.cold");
        gain_cold.makeup_gain_db = -144.5;
        CHECK(store.upsert(gain_cold) ==
              SessionRouteRuleResultV1::invalid_argument);

        CHECK(store.size() == 0U);
    }

    // 2) Gain boundaries are inclusive.
    {
        SessionRouteRuleStoreV1 store;
        auto min_gain = make_rule("gain.min");
        min_gain.makeup_gain_db = -144.0;
        CHECK(store.upsert(min_gain) == SessionRouteRuleResultV1::applied);
        auto max_gain = make_rule("gain.max");
        max_gain.makeup_gain_db = 12.0;
        CHECK(store.upsert(max_gain) == SessionRouteRuleResultV1::applied);
        CHECK(store.size() == 2U);
    }

    // 3) Lifecycle: upsert, replace, remove, clear.
    {
        SessionRouteRuleStoreV1 store;
        CHECK(store.upsert(make_rule("alpha")) == SessionRouteRuleResultV1::applied);
        CHECK(store.upsert(make_rule("beta")) == SessionRouteRuleResultV1::applied);
        CHECK(store.size() == 2U);

        auto replaced = make_rule("alpha");
        replaced.priority = 42;
        replaced.lane_id = "lane.replaced";
        CHECK(store.upsert(replaced) == SessionRouteRuleResultV1::applied);
        CHECK(store.size() == 2U);

        AudioSessionDescriptorV1 descriptor =
            make_descriptor("SPOTIFY.EXE", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "lane.replaced");

        auto broken_replacement = make_rule("alpha");
        broken_replacement.schema_version = 2U;
        CHECK(store.upsert(broken_replacement) ==
              SessionRouteRuleResultV1::invalid_argument);

        CHECK(store.remove("alpha"));
        CHECK(store.size() == 1U);
        CHECK(!store.remove("alpha"));
        CHECK(!store.remove("never-existed"));

        store.clear();
        CHECK(store.size() == 0U);
        AudioSessionDescriptorV1 unmatched =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(unmatched) == SessionRouteRuleResultV1::no_match);
    }

    // 4) Capacity limit is bounded fail-closed.
    {
        SessionRouteRuleStoreV1 store;
        for (std::size_t index = 0U; index < hibiki::kMaxSessionRouteRulesV1;
             ++index) {
            const auto id = "r" + std::to_string(index);
            CHECK(store.upsert(make_rule(id)) == SessionRouteRuleResultV1::applied);
        }
        CHECK(store.size() == hibiki::kMaxSessionRouteRulesV1);
        CHECK(store.upsert(make_rule("overflow")) ==
              SessionRouteRuleResultV1::capacity_exhausted);
        CHECK(store.size() == hibiki::kMaxSessionRouteRulesV1);
    }

    // 5) Match semantics: folded-exact app id and folded-substring display name.
    {
        SessionRouteRuleStoreV1 store;
        auto exact = make_rule("exact.app");
        exact.app_id = "Spotify.EXE";
        exact.lane_id = "lane.exact";
        CHECK(store.upsert(exact) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 lower = make_descriptor("spotify.exe", "");
        CHECK(store.apply(lower) == SessionRouteRuleResultV1::applied);
        CHECK(lower.lane_id == "lane.exact");
        AudioSessionDescriptorV1 upper = make_descriptor("SPOTIFY.EXE", "");
        CHECK(store.apply(upper) == SessionRouteRuleResultV1::applied);
        CHECK(upper.lane_id == "lane.exact");
        AudioSessionDescriptorV1 longer = make_descriptor("xspotify.exec", "");
        CHECK(store.apply(longer) == SessionRouteRuleResultV1::no_match);
    }
    {
        SessionRouteRuleStoreV1 store;
        auto substring = make_rule("substring.name");
        substring.app_id.clear();
        substring.display_name_contains = "Spot";
        substring.lane_id = "lane.substring";
        CHECK(store.upsert(substring) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 hit = make_descriptor("", "Spotify Free");
        CHECK(store.apply(hit) == SessionRouteRuleResultV1::applied);
        AudioSessionDescriptorV1 folded =
            make_descriptor("", "MOBILE SPOTIFY SERVICE");
        CHECK(store.apply(folded) == SessionRouteRuleResultV1::applied);
        AudioSessionDescriptorV1 miss = make_descriptor("", "Sporify");
        CHECK(store.apply(miss) == SessionRouteRuleResultV1::no_match);
    }
    {
        SessionRouteRuleStoreV1 store;
        auto combined = make_rule("combined.matchers");
        combined.app_id = "games.exe";
        combined.display_name_contains = "launcher";
        combined.lane_id = "lane.games";
        CHECK(store.upsert(combined) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 both =
            make_descriptor("GAMES.EXE", "Game Launcher");
        CHECK(store.apply(both) == SessionRouteRuleResultV1::applied);
        AudioSessionDescriptorV1 wrong_display =
            make_descriptor("games.exe", "Something Else");
        CHECK(store.apply(wrong_display) == SessionRouteRuleResultV1::no_match);
        AudioSessionDescriptorV1 wrong_app =
            make_descriptor("other.exe", "Game Launcher");
        CHECK(store.apply(wrong_app) == SessionRouteRuleResultV1::no_match);
    }

    // 6) Disabled rules never match.
    {
        SessionRouteRuleStoreV1 store;
        auto disabled = make_rule("disabled.rule");
        disabled.enabled = false;
        CHECK(store.upsert(disabled) == SessionRouteRuleResultV1::applied);
        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::no_match);
    }

    // 7) Priority arbitration: highest wins regardless of insertion order.
    {
        SessionRouteRuleStoreV1 store;
        auto low = make_rule("low.priority", 1);
        low.lane_id = "lane.low";
        CHECK(store.upsert(low) == SessionRouteRuleResultV1::applied);
        auto high = make_rule("high.priority", 9);
        high.lane_id = "lane.high";
        CHECK(store.upsert(high) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "lane.high");
    }
    {
        SessionRouteRuleStoreV1 store;
        auto high = make_rule("high.priority", 9);
        high.lane_id = "lane.high";
        CHECK(store.upsert(high) == SessionRouteRuleResultV1::applied);
        auto low = make_rule("low.priority", 1);
        low.lane_id = "lane.low";
        CHECK(store.upsert(low) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "lane.high");
    }

    // 8) Equal priorities fail closed instead of depending on order.
    {
        SessionRouteRuleStoreV1 store;
        auto first = make_rule("amb.first", 5);
        first.lane_id = "lane.first";
        CHECK(store.upsert(first) == SessionRouteRuleResultV1::applied);
        auto second = make_rule("amb.second", 5);
        second.lane_id = "lane.second";
        CHECK(store.upsert(second) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::ambiguous);
        CHECK(descriptor.lane_id == "lane.default");
        CHECK(descriptor.output_group == "main-default");
    }
    {
        // A disabled competitor removes the ambiguity.
        SessionRouteRuleStoreV1 store;
        auto active = make_rule("tie.active", 7);
        active.lane_id = "lane.active";
        CHECK(store.upsert(active) == SessionRouteRuleResultV1::applied);
        auto inactive = make_rule("tie.inactive", 7);
        inactive.lane_id = "lane.inactive";
        inactive.enabled = false;
        CHECK(store.upsert(inactive) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "lane.active");
    }

    // 9) A lone negative-priority rule still applies.
    {
        SessionRouteRuleStoreV1 store;
        auto lonely = make_rule("lonely.negative", -1000000);
        CHECK(store.upsert(lonely) == SessionRouteRuleResultV1::applied);
        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Spotify");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "lane.music");
    }

    // 10) Applied fields are copied; unrelated descriptor fields stay intact.
    {
        SessionRouteRuleStoreV1 store;
        auto rule = make_rule("fields.copy", 3);
        rule.output_group = "group.custom";
        rule.gain_owner = SessionGainOwner::HibikiInternal;
        rule.makeup_gain_db = -6.5;
        CHECK(store.upsert(rule) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 descriptor =
            make_descriptor("spotify.exe", "Display Name");
        descriptor.identity.endpoint_id = "endpoint-z";
        descriptor.identity.session_instance_id = "session-9";
        descriptor.identity.process_id = 777U;
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);

        CHECK(descriptor.lane_id == "lane.music");
        CHECK(descriptor.output_group == "group.custom");
        CHECK(descriptor.gain_owner == SessionGainOwner::HibikiInternal);
        CHECK(descriptor.makeup_gain_db == -6.5);
        CHECK(descriptor.schema_version == 1U);
        CHECK(descriptor.identity.endpoint_id == "endpoint-z");
        CHECK(descriptor.identity.session_instance_id == "session-9");
        CHECK(descriptor.identity.process_id == 777U);
        CHECK(descriptor.display_name == "Display Name");
        CHECK(descriptor.active);
    }

    // 11) Process ID is advisory only and cannot change routing decisions.
    {
        SessionRouteRuleStoreV1 store;
        auto rule = make_rule("pid.agnostic");
        CHECK(store.upsert(rule) == SessionRouteRuleResultV1::applied);

        AudioSessionDescriptorV1 first =
            make_descriptor("spotify.exe", "Spotify");
        first.identity.process_id = 1111U;
        AudioSessionDescriptorV1 second =
            make_descriptor("spotify.exe", "Spotify");
        second.identity.process_id = 999999U;

        CHECK(store.apply(first) == SessionRouteRuleResultV1::applied);
        CHECK(store.apply(second) == SessionRouteRuleResultV1::applied);
        CHECK(first.lane_id == second.lane_id);
        CHECK(first.output_group == second.output_group);
        CHECK(first.gain_owner == second.gain_owner);
        CHECK(first.makeup_gain_db == second.makeup_gain_db);
    }

    std::fputs("session route rule store tests passed\n", stdout);
    return 0;
}
