// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_session_registry.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
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
using hibiki::AudioSessionIdentityV1;
using hibiki::AudioSessionRegistry;
using hibiki::SessionGainOwner;

AudioSessionDescriptorV1 make_session(const std::string& endpoint,
                                      const std::string& instance,
                                      std::uint32_t process_id = 100U) {
    AudioSessionDescriptorV1 descriptor;
    descriptor.identity.endpoint_id = endpoint;
    descriptor.identity.session_instance_id = instance;
    descriptor.identity.process_id = process_id;
    descriptor.display_name = "display";
    descriptor.app_id = "app.exe";
    return descriptor;
}

}  // namespace

int main() {
    // upsert rejects wrong schema version and empty identity fields.
    {
        AudioSessionRegistry registry;
        auto bad_schema = make_session("ep", "inst");
        bad_schema.schema_version = 2U;
        CHECK(!registry.upsert(bad_schema));

        auto empty_endpoint = make_session("", "inst");
        CHECK(!registry.upsert(empty_endpoint));
        auto empty_instance = make_session("ep", "");
        CHECK(!registry.upsert(empty_instance));
        CHECK(registry.sessions().empty());
    }

    // Unknown gain owners are rejected without creating or mutating a session.
    {
        AudioSessionRegistry registry;
        auto unknown = make_session("ep", "unknown");
        unknown.gain_owner = static_cast<SessionGainOwner>(0xFFU);
        CHECK(!registry.upsert(unknown));
        CHECK(registry.sessions().empty());

        auto existing = make_session("ep", "existing");
        existing.display_name = "original";
        existing.gain_owner = SessionGainOwner::HibikiInternal;
        CHECK(registry.upsert(existing));
        const AudioSessionIdentityV1 identity{"ep", "existing", 100U};
        auto refresh = existing;
        refresh.display_name = "must-not-apply";
        refresh.gain_owner = static_cast<SessionGainOwner>(0xFFU);
        CHECK(!registry.upsert(refresh));
        CHECK(registry.find(identity)->display_name == "original" &&
              registry.find(identity)->gain_owner == SessionGainOwner::HibikiInternal);
        CHECK(!registry.set_gain_owner(identity, static_cast<SessionGainOwner>(0xFFU)));
        CHECK(registry.find(identity)->gain_owner == SessionGainOwner::HibikiInternal);
        CHECK(registry.set_gain_owner(identity, SessionGainOwner::WindowsSession));
        CHECK(registry.find(identity)->gain_owner == SessionGainOwner::WindowsSession);
    }

    // upsert keeps existing lane binding when refreshing the same session.
    {
        AudioSessionRegistry registry;
        auto first = make_session("ep", "inst", 42U);
        CHECK(registry.upsert(first));
        const AudioSessionIdentityV1 identity{"ep", "inst", 42U};
        CHECK(registry.bind(identity, "game", "headphones"));
        CHECK(registry.set_gain_owner(identity, SessionGainOwner::HibikiInternal));
        CHECK(registry.set_makeup_gain_db(identity, -6.0));

        auto refresh = make_session("ep", "inst", 42U);
        refresh.display_name = "renamed";
        CHECK(registry.upsert(refresh));
        CHECK(registry.sessions().size() == 1U);
        const auto& stored = registry.sessions()[0];
        CHECK(stored.display_name == "renamed");
        CHECK(stored.lane_id == "game");
        CHECK(stored.output_group == "headphones");
        CHECK(stored.gain_owner == SessionGainOwner::HibikiInternal);
        CHECK(std::fabs(stored.makeup_gain_db + 6.0) < 1e-9);
    }

    // bind validates lane/output_group bounds and printability.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session("ep", "inst")));
        const AudioSessionIdentityV1 identity{"ep", "inst", 100U};
        CHECK(!registry.bind(identity, "", "main"));
        CHECK(!registry.bind(identity, "lane", ""));
        const std::string og65(65, 'x');
        CHECK(!registry.bind(identity, "lane", og65));
        const std::string og64(64, 'x');
        CHECK(registry.bind(identity, "lane", og64));
        CHECK(!registry.bind({"ep", "missing", 0U}, "lane", "main"));
    }

    // remove deletes the matching session only.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session("ep", "a", 10U)));
        CHECK(registry.upsert(make_session("ep", "b", 11U)));
        const AudioSessionIdentityV1 a{"ep", "a", 10U};
        const AudioSessionIdentityV1 ghost{"ep", "ghost", 99U};
        CHECK(!registry.remove(ghost));
        CHECK(registry.remove(a));
        CHECK(registry.sessions().size() == 1U);
        CHECK(registry.find(a) == nullptr);
        CHECK(registry.find({"ep", "b", 11U}) != nullptr);
    }

    // mark_endpoint_sessions_inactive touches only the matching endpoint.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session("ep-a", "s1", 20U)));
        CHECK(registry.upsert(make_session("ep-a", "s2", 21U)));
        CHECK(registry.upsert(make_session("ep-b", "s3", 22U)));
        registry.mark_endpoint_sessions_inactive("ep-a");
        int inactive_a = 0;
        int inactive_b = 0;
        for (const auto& session : registry.sessions()) {
            if (session.identity.endpoint_id == "ep-a") {
                if (!session.active) ++inactive_a;
            } else if (!session.active) {
                ++inactive_b;
            }
        }
        CHECK(inactive_a == 2);
        CHECK(inactive_b == 0);
    }

    // Capacity: exactly kMaxSessions entries succeed, one more is rejected.
    {
        AudioSessionRegistry registry;
        for (std::uint32_t i = 0U; i < 256U; ++i) {
            const std::string instance = "inst-" + std::to_string(i);
            CHECK(registry.upsert(make_session("cap", instance, i)));
        }
        CHECK(registry.sessions().size() == 256U);
        CHECK(!registry.upsert(make_session("cap", "overflow", 999U)));
        CHECK(registry.sessions().size() == 256U);
        // A refresh of an existing session still succeeds at capacity.
        CHECK(registry.upsert(make_session("cap", "inst-7", 7U)));
    }

    // set_makeup_gain_db clamps nothing but rejects non-finite and out-of-range.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session("ep", "inst")));
        const AudioSessionIdentityV1 identity{"ep", "inst", 100U};
        const double nan_value = std::numeric_limits<double>::quiet_NaN();
        CHECK(!registry.set_makeup_gain_db(identity, nan_value));
        CHECK(!registry.set_makeup_gain_db(identity, -144.5));
        CHECK(!registry.set_makeup_gain_db(identity, 12.5));
        CHECK(registry.set_makeup_gain_db(identity, -144.0));
        CHECK(registry.set_makeup_gain_db(identity, 12.0));
        CHECK(!registry.set_makeup_gain_db({"ep", "ghost", 0U}, 0.0));
    }

    std::fputs("audio_session_registry_tests passed\n", stdout);
    return 0;
}
