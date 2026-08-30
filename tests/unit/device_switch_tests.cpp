// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_switch.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::DeviceSwitchState;
using hibiki::DeviceSwitchTransaction;
using hibiki::DeviceTargetV1;

DeviceTargetV1 make_target(const std::string& endpoint_id = "endpoint-a",
                           std::uint32_t channels = 2U) {
    DeviceTargetV1 target;
    target.endpoint_id = endpoint_id;
    target.channels = channels;
    target.sample_rate = 48000U;
    target.buffer_frames = 128U;
    return target;
}

}  // namespace

int main() {
    // begin: rejects empty endpoint id.
    {
        DeviceSwitchTransaction transaction;
        auto bad = make_target();
        bad.endpoint_id.clear();
        CHECK(transaction.begin(bad) == false);
        CHECK(transaction.state() == DeviceSwitchState::Unbound);
    }
    // begin: endpoint identity uses the same bounded printable UTF-8 contract
    // as DeviceSwitch wire/schema validation.
    {
        const std::string embedded_nul("endpoint\0suffix", 15U);
        const std::vector<std::string> invalid_ids{
            std::string(261U, 'x'),
            std::string("endpoint\x80", 9U),
            std::string("endpoint\xC0\xAF", 10U),
            std::string("endpoint\xED\xA0\x80", 11U),
            std::string("endpoint\xF4\x90\x80\x80", 12U),
            std::string("endpoint\x01", 9U),
            std::string("endpoint\x7F", 9U),
            std::string("endpoint\xC2\x85", 10U),
            embedded_nul,
        };
        for (const auto& endpoint_id : invalid_ids) {
            DeviceSwitchTransaction transaction;
            CHECK(!transaction.begin(make_target(endpoint_id)));
            CHECK(transaction.state() == DeviceSwitchState::Unbound);
            CHECK(transaction.active_target().endpoint_id.empty());
        }

        DeviceSwitchTransaction transaction;
        const std::string valid_multibyte("\xE5\x96\x87\xE5\x8F\xAD", 6U);
        CHECK(transaction.begin(make_target(valid_multibyte)));
        CHECK(transaction.state() == DeviceSwitchState::Binding);
        transaction.rollback();

        const auto valid_boundary = make_target(std::string(260U, 'x'));
        CHECK(transaction.begin(valid_boundary));
        CHECK(transaction.prepare_complete());
        CHECK(transaction.commit());
        const auto active_before_invalid = transaction.active_target();
        CHECK(!transaction.begin(make_target(std::string(261U, 'x'))));
        CHECK(transaction.state() == DeviceSwitchState::Synced);
        CHECK(transaction.active_target().endpoint_id == active_before_invalid.endpoint_id);
        CHECK(transaction.active_target().channels == active_before_invalid.channels);
        CHECK(transaction.active_target().sample_rate == active_before_invalid.sample_rate);
        CHECK(transaction.active_target().buffer_frames == active_before_invalid.buffer_frames);
    }
    // begin: rejects unsupported channel counts.
    {
        DeviceSwitchTransaction transaction;
        for (const std::uint32_t channels : {0U, 1U, 3U, 4U, 5U, 7U, 16U}) {
            auto bad = make_target("endpoint-a", channels);
            CHECK(transaction.begin(bad) == false);
            CHECK(transaction.state() == DeviceSwitchState::Unbound);
        }
    }
    // begin: rejects zero sample rate or buffer frames.
    {
        DeviceSwitchTransaction transaction;
        auto zero_rate = make_target();
        zero_rate.sample_rate = 0U;
        CHECK(transaction.begin(zero_rate) == false);
        auto zero_frames = make_target();
        zero_frames.buffer_frames = 0U;
        CHECK(transaction.begin(zero_frames) == false);
        CHECK(transaction.state() == DeviceSwitchState::Unbound);
    }
    // happy path: full lifecycle promotes pending target to active.
    {
        DeviceSwitchTransaction transaction;
        const auto target = make_target("endpoint-a", 8U);
        CHECK(transaction.state() == DeviceSwitchState::Unbound);
        CHECK(transaction.begin(target));
        CHECK(transaction.state() == DeviceSwitchState::Binding);
        CHECK(transaction.prepare_complete());
        CHECK(transaction.state() == DeviceSwitchState::WritePending);
        CHECK(transaction.commit());
        CHECK(transaction.state() == DeviceSwitchState::Synced);
        CHECK(transaction.active_target().endpoint_id == "endpoint-a");
        CHECK(transaction.active_target().channels == 8U);
        CHECK(transaction.active_target().sample_rate == 48000U);
        CHECK(transaction.active_target().buffer_frames == 128U);
    }
    // prepare/commit out of order are rejected from Unbound.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.prepare_complete() == false);
        CHECK(transaction.commit() == false);
        CHECK(transaction.state() == DeviceSwitchState::Unbound);
        CHECK(transaction.active_target().endpoint_id.empty());
    }
    // commit before prepare is rejected; state stays Binding.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.begin(make_target()));
        CHECK(transaction.commit() == false);
        CHECK(transaction.state() == DeviceSwitchState::Binding);
    }
    // rollback from first-time binding returns to Unbound and drops the target.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.begin(make_target()));
        transaction.rollback();
        CHECK(transaction.state() == DeviceSwitchState::Unbound);
        CHECK(transaction.active_target().endpoint_id.empty());
    }
    // rebind from Synced enters Rebinding and keeps the old active target.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.begin(make_target("endpoint-a", 2U)));
        CHECK(transaction.prepare_complete());
        CHECK(transaction.commit());
        CHECK(transaction.begin(make_target("endpoint-b", 6U)));
        CHECK(transaction.state() == DeviceSwitchState::Rebinding);
        CHECK(transaction.active_target().endpoint_id == "endpoint-a");
        // rollback restores the previously synced target.
        transaction.rollback();
        CHECK(transaction.state() == DeviceSwitchState::Synced);
        CHECK(transaction.active_target().endpoint_id == "endpoint-a");
        CHECK(transaction.active_target().channels == 2U);
    }
    // successful rebind swaps the active target.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.begin(make_target("endpoint-a", 2U)));
        CHECK(transaction.prepare_complete());
        CHECK(transaction.commit());
        CHECK(transaction.begin(make_target("endpoint-b", 8U)));
        CHECK(transaction.prepare_complete());
        CHECK(transaction.commit());
        CHECK(transaction.state() == DeviceSwitchState::Synced);
        CHECK(transaction.active_target().endpoint_id == "endpoint-b");
        CHECK(transaction.active_target().channels == 8U);
    }
    // mark_degraded latches prepare/commit so a degraded switch cannot write;
    // with no synced target yet, rollback resets the transaction to Unbound.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.begin(make_target()));
        transaction.mark_degraded();
        CHECK(transaction.state() == DeviceSwitchState::Degraded);
        CHECK(transaction.prepare_complete() == false);
        CHECK(transaction.commit() == false);
        transaction.rollback();
        CHECK(transaction.state() == DeviceSwitchState::Unbound);
        CHECK(transaction.active_target().endpoint_id.empty());
    }
    // degraded after a successful switch: a fresh bind remains allowed for
    // recovery, and rolling it back restores the last synced target.
    {
        DeviceSwitchTransaction transaction;
        CHECK(transaction.begin(make_target("endpoint-a", 2U)));
        CHECK(transaction.begin(make_target("recovery", 2U)));
        CHECK(transaction.prepare_complete());
        CHECK(transaction.commit());
        transaction.mark_degraded();
        CHECK(transaction.state() == DeviceSwitchState::Degraded);
        CHECK(transaction.prepare_complete() == false);
        CHECK(transaction.commit() == false);
        CHECK(transaction.begin(make_target("endpoint-b", 8U)));
        CHECK(transaction.prepare_complete());
        transaction.rollback();
        CHECK(transaction.state() == DeviceSwitchState::Synced);
        CHECK(transaction.active_target().endpoint_id == "recovery");
        CHECK(transaction.active_target().channels == 2U);
    }

    return 0;
}
