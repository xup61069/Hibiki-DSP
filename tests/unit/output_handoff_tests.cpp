// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/output_handoff.hpp"
#include "hibiki/windows_wasapi_handoff.hpp"

#include <cmath>
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

using hibiki::DeviceTargetV1;
using hibiki::OutputHandoffCoordinatorV1;
using hibiki::OutputHandoffStateV1;
using hibiki::detail::WasapiHandoffSubmissionHooksV1;
using hibiki::detail::WasapiHandoffSubmissionResultV1;
using hibiki::detail::submit_wasapi_handoff_fade_block_v1;

struct FakeWasapiWorker {
    bool accept_submit{true};
    bool running{true};
    std::uint32_t submit_calls{0U};
    std::uint32_t stop_calls{0U};
};

bool fake_submit(void* const context,
                 const float*,
                 const std::uint32_t,
                 const std::uint32_t,
                 const float) noexcept {
    auto& worker = *static_cast<FakeWasapiWorker*>(context);
    ++worker.submit_calls;
    return worker.running && worker.accept_submit;
}

void fake_stop(void* const context) noexcept {
    auto& worker = *static_cast<FakeWasapiWorker*>(context);
    ++worker.stop_calls;
    worker.running = false;
}

DeviceTargetV1 valid_target(const char* endpoint = "hibiki-main") {
    return DeviceTargetV1{endpoint, 2U, 48000U, 128U};
}

constexpr std::uint32_t kChannels = 2U;
constexpr std::size_t kTotalFrames = 1440U;  // exactly 48000 * 30 ms

}  // namespace

int main() {
    // The production fade path and this deterministic seam share the same
    // candidate-stop decision. Active rejection stops the accepted candidate
    // before the class process() maps the result to Degraded; no commit result
    // can be produced from WasapiHandoffSubmissionResultV1::ActiveRejected.
    {
        FakeWasapiWorker candidate;
        FakeWasapiWorker active{false};
        float silence[2] = {};
        const auto result = submit_wasapi_handoff_fade_block_v1(
            WasapiHandoffSubmissionHooksV1{&candidate, fake_submit, fake_stop, &active,
                                           fake_submit},
            silence, 1U, 2U, 0.8F, 0.6F);
        CHECK(result == WasapiHandoffSubmissionResultV1::ActiveRejected);
        CHECK(candidate.submit_calls == 1U);
        CHECK(candidate.stop_calls == 1U);
        CHECK(!candidate.running);
        CHECK(active.submit_calls == 1U);
    }

    // Candidate rejection also stops the candidate and never submits to the
    // active worker through the fade branch; the caller may then use its
    // separate unscaled rollback fallback.
    {
        FakeWasapiWorker candidate{false};
        FakeWasapiWorker active;
        float silence[2] = {};
        const auto result = submit_wasapi_handoff_fade_block_v1(
            WasapiHandoffSubmissionHooksV1{&candidate, fake_submit, fake_stop, &active,
                                           fake_submit},
            silence, 1U, 2U, 0.8F, 0.6F);
        CHECK(result == WasapiHandoffSubmissionResultV1::CandidateRejected);
        CHECK(candidate.submit_calls == 1U);
        CHECK(candidate.stop_calls == 1U);
        CHECK(!candidate.running);
        CHECK(active.submit_calls == 0U);
    }

    // A complete fade block leaves both workers running and is accepted.
    {
        FakeWasapiWorker candidate;
        FakeWasapiWorker active;
        float silence[2] = {};
        const auto result = submit_wasapi_handoff_fade_block_v1(
            WasapiHandoffSubmissionHooksV1{&candidate, fake_submit, fake_stop, &active,
                                           fake_submit},
            silence, 1U, 2U, 0.8F, 0.6F);
        CHECK(result == WasapiHandoffSubmissionResultV1::Accepted);
        CHECK(candidate.stop_calls == 0U);
        CHECK(candidate.running);
        CHECK(active.submit_calls == 1U);
    }

    // Happy path: begin -> prepare -> process to fade completion -> commit.
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(coordinator.state() == OutputHandoffStateV1::Idle);
        CHECK(coordinator.begin(valid_target()));
        CHECK(coordinator.state() == OutputHandoffStateV1::Preparing);
        // The previous target stays active until the new one commits.
        CHECK(coordinator.active_target().endpoint_id.empty());
        CHECK(coordinator.prepare());
        CHECK(coordinator.state() == OutputHandoffStateV1::Fading);

        const auto started = coordinator.crossfade();
        CHECK(started.active);
        CHECK(started.channels == kChannels);
        CHECK(started.total_frames == kTotalFrames);
        CHECK(started.processed_frames == 0U);

        std::vector<float> old_pcm(kChannels * kTotalFrames, 0.25F);
        std::vector<float> new_pcm(kChannels * kTotalFrames, 0.75F);
        std::vector<float> out(kChannels * kTotalFrames, 0.0F);

        CHECK(coordinator.process(old_pcm.data(), new_pcm.data(), out.data(), 700U));
        CHECK(coordinator.crossfade().processed_frames == 700U);

        // Commit must stay fail-closed until every fade frame was rendered.
        CHECK(!coordinator.commit());
        CHECK(coordinator.state() == OutputHandoffStateV1::Fading);

        constexpr double half_pi = 1.57079632679489661923;
        const double position = static_cast<double>(699U) /
                                static_cast<double>(kTotalFrames);
        const float expected_old = static_cast<float>(std::cos(position * half_pi));
        const float expected_new = static_cast<float>(std::sin(position * half_pi));
        const float expected = 0.25F * expected_old + 0.75F * expected_new;
        CHECK(std::fabs(out[2U * 699U] - expected) < 1e-5F);

        const std::size_t remaining = kTotalFrames - 700U;
        CHECK(coordinator.process(old_pcm.data() + 2U * 700U,
                                  new_pcm.data() + 2U * 700U,
                                  out.data() + 2U * 700U,
                                  remaining));
        const auto finished = coordinator.crossfade();
        CHECK(!finished.active);
        CHECK(finished.processed_frames == kTotalFrames);

        CHECK(coordinator.commit());
        CHECK(coordinator.state() == OutputHandoffStateV1::Committed);
        CHECK(coordinator.active_target().endpoint_id == "hibiki-main");
        CHECK(!coordinator.process(old_pcm.data(), new_pcm.data(), out.data(), 1U));
        CHECK(!coordinator.commit());
    }

    // begin is rejected while a handoff is Preparing or Fading.
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(coordinator.begin(valid_target()));
        CHECK(!coordinator.begin(valid_target("hibiki-alt")));
        CHECK(coordinator.state() == OutputHandoffStateV1::Preparing);
        CHECK(coordinator.prepare());
        CHECK(!coordinator.begin(valid_target("hibiki-alt")));
        CHECK(coordinator.state() == OutputHandoffStateV1::Fading);
    }

    // A committed handoff can begin the next transaction.
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(coordinator.begin(valid_target()));
        CHECK(coordinator.prepare());
        std::vector<float> silence(kChannels * kTotalFrames, 0.0F);
        CHECK(coordinator.process(silence.data(), silence.data(),
                                 silence.data(), kTotalFrames));
        CHECK(coordinator.commit());
        CHECK(coordinator.begin(valid_target("hibiki-alt")));
        CHECK(coordinator.state() == OutputHandoffStateV1::Preparing);
        CHECK(coordinator.active_target().endpoint_id == "hibiki-main");
        std::vector<float> alt_silence(kChannels * kTotalFrames, 0.0F);
        CHECK(coordinator.prepare());
        CHECK(coordinator.process(alt_silence.data(), alt_silence.data(),
                                  alt_silence.data(), kTotalFrames));
        CHECK(coordinator.commit());
        CHECK(coordinator.active_target().endpoint_id == "hibiki-alt");
    }

    // Invalid endpoint/format targets degrade the coordinator fail-closed.
    for (const auto& broken :
         {DeviceTargetV1{"", 2U, 48000U, 128U},
          DeviceTargetV1{"hibiki-main", 3U, 48000U, 128U},
          DeviceTargetV1{"hibiki-main", 2U, 0U, 128U},
          DeviceTargetV1{"hibiki-main", 2U, 48000U, 0U}}) {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(!coordinator.begin(broken));
        CHECK(coordinator.state() == OutputHandoffStateV1::Degraded);
        CHECK(!coordinator.prepare());
        float scratch[8] = {};
        CHECK(!coordinator.process(scratch, scratch, scratch, 1U));
        CHECK(!coordinator.commit());
    }

    // Transaction-valid but crossfade-invalid configurations also degrade.
    for (const auto rate : {22050U, 384000U}) {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(!coordinator.begin(DeviceTargetV1{"hibiki-main", 2U, rate, 128U}));
        CHECK(coordinator.state() == OutputHandoffStateV1::Degraded);
    }
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(!coordinator.begin(valid_target(), 0U));
        CHECK(coordinator.state() == OutputHandoffStateV1::Degraded);
    }
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(!coordinator.begin(valid_target(), 201U));
        CHECK(coordinator.state() == OutputHandoffStateV1::Degraded);
    }

    // Operations without begin are rejected and keep the Idle state.
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(!coordinator.prepare());
        float scratch[8] = {};
        CHECK(!coordinator.process(scratch, scratch, scratch, 1U));
        CHECK(!coordinator.process(nullptr, nullptr, nullptr, 1U));
        CHECK(!coordinator.process(scratch, scratch, scratch, 0U));
        CHECK(!coordinator.commit());
        CHECK(coordinator.state() == OutputHandoffStateV1::Idle);
    }

    // Null buffers during an active fade are rejected without losing state.
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(coordinator.begin(valid_target()));
        CHECK(coordinator.prepare());
        std::vector<float> pcm(kChannels, 0.5F);
        CHECK(!coordinator.process(nullptr, pcm.data(), pcm.data(), 1U));
        CHECK(!coordinator.process(pcm.data(), nullptr, pcm.data(), 1U));
        CHECK(!coordinator.process(pcm.data(), pcm.data(), nullptr, 1U));
        CHECK(coordinator.state() == OutputHandoffStateV1::Fading);
        CHECK(coordinator.crossfade().processed_frames == 0U);
    }

    // Rollback resets the fade and blocks stale operations until re-begin.
    {
        OutputHandoffCoordinatorV1 coordinator;
        CHECK(coordinator.begin(valid_target()));
        CHECK(coordinator.prepare());
        std::vector<float> silence(kChannels * 32U, 0.0F);
        CHECK(coordinator.process(silence.data(), silence.data(), silence.data(), 32U));

        coordinator.rollback();
        CHECK(coordinator.state() == OutputHandoffStateV1::RolledBack);
        CHECK(!coordinator.crossfade().active);
        CHECK(coordinator.crossfade().processed_frames == 0U);
        CHECK(!coordinator.process(silence.data(), silence.data(), silence.data(), 1U));
        CHECK(!coordinator.commit());

        CHECK(coordinator.begin(valid_target("hibiki-recovery")));
        CHECK(coordinator.state() == OutputHandoffStateV1::Preparing);
    }

    // Rollback from Idle is itself observable and remains fail-closed.
    {
        OutputHandoffCoordinatorV1 coordinator;
        coordinator.rollback();
        CHECK(coordinator.state() == OutputHandoffStateV1::RolledBack);
        CHECK(!coordinator.prepare());
        CHECK(!coordinator.commit());
    }

    std::fputs("output_handoff_tests passed\n", stdout);
    return 0;
}
