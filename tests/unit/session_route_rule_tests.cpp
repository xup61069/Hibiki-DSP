// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/session_route_rules.hpp"

#include <cmath>
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

SessionRouteRuleV1 make_rule(const std::string& rule_id = "default-rule",
                             const std::int32_t priority = 10) {
    SessionRouteRuleV1 rule;
    rule.rule_id = rule_id;
    rule.priority = priority;
    rule.app_id = "game.exe";
    rule.lane_id = "game";
    rule.output_group = "main";
    rule.makeup_gain_db = -3.0;
    return rule;
}

AudioSessionDescriptorV1 make_descriptor(const std::string& app_id = "game.exe",
                                         const std::string& display_name = "") {
    AudioSessionDescriptorV1 descriptor;
    descriptor.app_id = app_id;
    descriptor.display_name = display_name;
    descriptor.gain_owner = SessionGainOwner::WindowsSession;
    return descriptor;
}

}  // namespace

int main() {
    // upsert: rejects wrong schema version.
    {
        SessionRouteRuleStoreV1 store;
        auto rule = make_rule("bad-schema");
        rule.schema_version = 2U;
        CHECK(store.upsert(rule) == SessionRouteRuleResultV1::invalid_argument);
        CHECK(store.size() == 0U);
    }
    // upsert: rejects invalid rule id characters and leading separators.
    {
        SessionRouteRuleStoreV1 store;
        auto upper = make_rule();
        upper.rule_id = "BadRule";
        CHECK(store.upsert(upper) == SessionRouteRuleResultV1::invalid_argument);
        auto leading_dot = make_rule();
        leading_dot.rule_id = ".hidden";
        CHECK(store.upsert(leading_dot) == SessionRouteRuleResultV1::invalid_argument);
        auto empty = make_rule();
        empty.rule_id.clear();
        CHECK(store.upsert(empty) == SessionRouteRuleResultV1::invalid_argument);
        CHECK(store.size() == 0U);
    }
    // upsert: requires at least one match criterion.
    {
        SessionRouteRuleStoreV1 store;
        auto no_match = make_rule();
        no_match.app_id.clear();
        CHECK(store.upsert(no_match) == SessionRouteRuleResultV1::invalid_argument);
    }
    // upsert: rejects non-finite or out-of-range makeup gain.
    {
        SessionRouteRuleStoreV1 store;
        auto hot = make_rule();
        hot.makeup_gain_db = 12.5;
        CHECK(store.upsert(hot) == SessionRouteRuleResultV1::invalid_argument);
        auto cold = make_rule();
        cold.makeup_gain_db = -144.5;
        CHECK(store.upsert(cold) == SessionRouteRuleResultV1::invalid_argument);
        auto nan = make_rule();
        nan.makeup_gain_db = std::numeric_limits<double>::quiet_NaN();
        CHECK(store.upsert(nan) == SessionRouteRuleResultV1::invalid_argument);
    }
    // upsert: same rule id replaces in place without growing the store.
    {
        SessionRouteRuleStoreV1 store;
        auto first = make_rule("replace-me");
        first.makeup_gain_db = -6.0;
        CHECK(store.upsert(first) == SessionRouteRuleResultV1::applied);
        auto second = make_rule("replace-me");
        second.makeup_gain_db = -1.0;
        CHECK(store.upsert(second) == SessionRouteRuleResultV1::applied);
        CHECK(store.size() == 1U);
        auto descriptor = make_descriptor();
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.makeup_gain_db == -1.0);
    }
    // apply: app id matching is case-insensitive exact.
    {
        SessionRouteRuleStoreV1 store;
        CHECK(store.upsert(make_rule("case-app")) ==
              SessionRouteRuleResultV1::applied);
        auto descriptor = make_descriptor("GAME.EXE");
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "game");
        auto different_app = make_descriptor("game2.exe");
        CHECK(store.apply(different_app) == SessionRouteRuleResultV1::no_match);
    }
    // apply: display name matching is case-insensitive substring.
    {
        SessionRouteRuleStoreV1 store;
        auto rule = make_rule("display-rule");
        rule.app_id.clear();
        rule.display_name_contains = "quiet";
        CHECK(store.upsert(rule) == SessionRouteRuleResultV1::applied);
        auto hit = make_descriptor("other.exe", "My QUIET Game");
        CHECK(store.apply(hit) == SessionRouteRuleResultV1::applied);
        auto miss = make_descriptor("other.exe", "Loud Game");
        CHECK(store.apply(miss) == SessionRouteRuleResultV1::no_match);
    }
    // apply: rules requiring both criteria reject partial matches.
    {
        SessionRouteRuleStoreV1 store;
        auto both = make_rule("both-criteria");
        both.display_name_contains = "special";
        CHECK(store.upsert(both) == SessionRouteRuleResultV1::applied);
        auto wrong_display = make_descriptor("game.exe", "nothing");
        CHECK(store.apply(wrong_display) == SessionRouteRuleResultV1::no_match);
        auto right_display = make_descriptor("GAME.EXE", "very special session");
        CHECK(store.apply(right_display) == SessionRouteRuleResultV1::applied);
    }
    // apply: disabled rules never match.
    {
        SessionRouteRuleStoreV1 store;
        auto disabled = make_rule("disabled");
        disabled.enabled = false;
        CHECK(store.upsert(disabled) == SessionRouteRuleResultV1::applied);
        auto descriptor = make_descriptor();
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::no_match);
    }
    // apply: highest priority wins regardless of insertion order.
    {
        SessionRouteRuleStoreV1 store;
        auto mid = make_rule("mid", 50);
        mid.lane_id = "lane-mid";
        CHECK(store.upsert(mid) == SessionRouteRuleResultV1::applied);
        auto top = make_rule("top", 900);
        top.lane_id = "lane-top";
        CHECK(store.upsert(top) == SessionRouteRuleResultV1::applied);
        auto low = make_rule("low", -20);
        low.lane_id = "lane-low";
        CHECK(store.upsert(low) == SessionRouteRuleResultV1::applied);
        auto descriptor = make_descriptor();
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "lane-top");
    }
    // apply: equal-priority matches are ambiguous and fail closed.
    {
        SessionRouteRuleStoreV1 store;
        auto left = make_rule("left", 40);
        left.lane_id = "lane-left";
        CHECK(store.upsert(left) == SessionRouteRuleResultV1::applied);
        auto right = make_rule("right", 40);
        right.lane_id = "lane-right";
        CHECK(store.upsert(right) == SessionRouteRuleResultV1::applied);
        auto before = make_descriptor();
        const auto original_lane = before.lane_id;
        CHECK(store.apply(before) == SessionRouteRuleResultV1::ambiguous);
        CHECK(before.lane_id == original_lane);
    }
    // upsert: capacity is bounded at 64 distinct rules.
    {
        SessionRouteRuleStoreV1 store;
        for (std::size_t index = 0U; index < hibiki::kMaxSessionRouteRulesV1; ++index) {
            auto rule = make_rule("rule-" + std::to_string(index));
            CHECK(store.upsert(rule) == SessionRouteRuleResultV1::applied);
        }
        auto overflow = make_rule("overflow");
        CHECK(store.upsert(overflow) ==
              SessionRouteRuleResultV1::capacity_exhausted);
        CHECK(store.size() == hibiki::kMaxSessionRouteRulesV1);
    }
    // remove / clear: lifecycle frees slots correctly.
    {
        SessionRouteRuleStoreV1 store;
        CHECK(store.upsert(make_rule("keep")) == SessionRouteRuleResultV1::applied);
        CHECK(store.remove("missing") == false);
        CHECK(store.remove("keep") == true);
        CHECK(store.size() == 0U);
        CHECK(store.upsert(make_rule("refill")) == SessionRouteRuleResultV1::applied);
        store.clear();
        CHECK(store.size() == 0U);
    }
    // apply: copies all routing fields from the winning rule.
    {
        SessionRouteRuleStoreV1 store;
        auto rule = make_rule("fields");
        rule.output_group = "surround";
        rule.gain_owner = SessionGainOwner::HibikiInternal;
        rule.makeup_gain_db = 4.5;
        CHECK(store.upsert(rule) == SessionRouteRuleResultV1::applied);
        auto descriptor = make_descriptor();
        descriptor.lane_id = "original-lane";
        descriptor.output_group = "original-group";
        descriptor.gain_owner = SessionGainOwner::WindowsSession;
        descriptor.makeup_gain_db = -20.0;
        CHECK(store.apply(descriptor) == SessionRouteRuleResultV1::applied);
        CHECK(descriptor.lane_id == "game");
        CHECK(descriptor.output_group == "surround");
        CHECK(descriptor.gain_owner == SessionGainOwner::HibikiInternal);
        CHECK(descriptor.makeup_gain_db == 4.5);
    }

    return 0;
}
