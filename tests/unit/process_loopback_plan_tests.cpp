// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/process_loopback_plan.hpp"

#if defined(_WIN32)
#define HIBIKI_PROCESS_LOOPBACK_TEST_SEAM
#include "hibiki/windows_process_loopback.hpp"
#undef HIBIKI_PROCESS_LOOPBACK_TEST_SEAM

#include <audioclient.h>
#include <windows.h>
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
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

using hibiki::AudioSessionDescriptorV1;
using hibiki::AudioSessionRegistry;
using hibiki::ProcessLoopbackPlanResultV1;
using hibiki::ProcessLoopbackPlanV1;

AudioSessionDescriptorV1 make_session(std::uint32_t process_id,
                                      const std::string& lane_id,
                                      const std::string& output_group,
                                      bool active = true) {
    AudioSessionDescriptorV1 descriptor;
    descriptor.identity.endpoint_id = "endpoint";
    descriptor.identity.session_instance_id = "session-" + std::to_string(process_id)
        + "-" + lane_id + "-" + output_group;
    descriptor.identity.process_id = process_id;
    descriptor.active = active;
    descriptor.lane_id = lane_id;
    descriptor.output_group = output_group;
    return descriptor;
}

}  // namespace

#if defined(_WIN32)

namespace hibiki {

struct WindowsProcessLoopbackSourceTestAccessV1 final {
    static void attach(WindowsProcessLoopbackSourceV1& source,
                       IAudioCaptureClient* const capture_client) noexcept {
        source.capture_client_ = capture_client;
        source.config_.process_id = 1991U;
        source.config_.include_process_tree = true;
        source.state_ = WindowsProcessLoopbackStateV1::Running;
        source.sample_rate_ = 48000U;
        source.channels_ = 2U;
        source.frames_per_buffer_ = 64U;
        source.captured_frames_ = 0U;
        source.dropped_frames_ = 0U;
        source.last_error_ = S_OK;
    }
};

}  // namespace hibiki

namespace {

struct FakeCapturePacket final {
    std::array<float, 8> samples{};
    UINT32 frames{0U};
    DWORD flags{0U};
};

class FakeCaptureClient final : public IAudioCaptureClient {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IAudioCaptureClient)) {
            *object = static_cast<IAudioCaptureClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0U) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** data,
                                         UINT32* frames,
                                         DWORD* flags,
                                         UINT64* /*device_position*/,
                                         UINT64* /*qpc_position*/) override {
        ++get_buffer_calls;
        if (data == nullptr || frames == nullptr || flags == nullptr) return E_POINTER;
        *data = nullptr;
        *frames = 0U;
        *flags = 0U;
        if (FAILED(get_buffer_result)) return get_buffer_result;
        if (packet_acquired || packets.empty()) return AUDCLNT_E_OUT_OF_ORDER;

        auto& packet = packets.front();
        *data = reinterpret_cast<BYTE*>(packet.samples.data());
        *frames = get_buffer_frames_override == 0U ? packet.frames : get_buffer_frames_override;
        *flags = packet.flags;
        packet_acquired = true;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 frames) override {
        ++release_buffer_calls;
        last_released_frames = frames;
        if (FAILED(release_buffer_result)) return release_buffer_result;
        if (!packet_acquired || packets.empty()) return AUDCLNT_E_OUT_OF_ORDER;
        packets.erase(packets.begin());
        packet_acquired = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetNextPacketSize(UINT32* frames) override {
        ++get_next_packet_size_calls;
        if (frames == nullptr) return E_POINTER;
        *frames = 0U;
        if (FAILED(get_next_packet_size_result)) return get_next_packet_size_result;
        if (!packets.empty()) *frames = packets.front().frames;
        return S_OK;
    }

    void add_packet(const std::initializer_list<float> samples,
                    const UINT32 frames,
                    const DWORD flags = 0U) {
        FakeCapturePacket packet;
        packet.frames = frames;
        packet.flags = flags;
        std::size_t index = 0U;
        for (const float sample : samples) {
            if (index >= packet.samples.size()) break;
            packet.samples[index++] = sample;
        }
        packets.push_back(packet);
    }

    std::vector<FakeCapturePacket> packets;
    HRESULT get_next_packet_size_result{S_OK};
    HRESULT get_buffer_result{S_OK};
    HRESULT release_buffer_result{S_OK};
    UINT32 get_buffer_frames_override{0U};
    std::uint32_t get_next_packet_size_calls{0U};
    std::uint32_t get_buffer_calls{0U};
    std::uint32_t release_buffer_calls{0U};
    UINT32 last_released_frames{0U};
    bool packet_acquired{false};

private:
    ULONG references_{1U};
};

FakeCaptureClient* attach_fake_capture_client(
    hibiki::WindowsProcessLoopbackSourceV1& source) {
    auto fake = std::make_unique<FakeCaptureClient>();
    auto* raw = fake.release();
    hibiki::WindowsProcessLoopbackSourceTestAccessV1::attach(source, raw);
    return raw;
}

int run_process_loopback_read_tests() {
    // A normal packet is copied and released with its acquired frame count.
    {
        hibiki::WindowsProcessLoopbackSourceV1 source;
        auto* fake = attach_fake_capture_client(source);
        fake->add_packet({0.25F, -0.5F, 1.0F, -1.0F}, 2U);
        std::array<float, 4> output{0.0F, 0.0F, 0.0F, 0.0F};
        std::uint32_t frames_read = 0U;

        CHECK(source.read(output.data(), 2U, frames_read));
        CHECK(frames_read == 2U);
        CHECK(output[0] == 0.25F && output[1] == -0.5F);
        CHECK(output[2] == 1.0F && output[3] == -1.0F);
        CHECK(fake->get_next_packet_size_calls == 1U);
        CHECK(fake->get_buffer_calls == 1U);
        CHECK(fake->release_buffer_calls == 1U);
        CHECK(fake->last_released_frames == 2U);
        const auto snapshot = source.snapshot();
        CHECK(snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Running);
        CHECK(snapshot.captured_frames == 2U);
        CHECK(snapshot.dropped_frames == 0U);
    }

    // An oversized packet is acquired before being dropped, and the next
    // packet remains consumable after the successful release.
    {
        hibiki::WindowsProcessLoopbackSourceV1 source;
        auto* fake = attach_fake_capture_client(source);
        fake->add_packet({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F}, 3U);
        fake->add_packet({7.0F, 8.0F}, 1U);
        std::array<float, 4> output{91.0F, 92.0F, 93.0F, 94.0F};
        std::uint32_t frames_read = 0U;

        CHECK(!source.read(output.data(), 2U, frames_read));
        CHECK(frames_read == 0U);
        CHECK(output[0] == 91.0F && output[1] == 92.0F);
        CHECK(output[2] == 93.0F && output[3] == 94.0F);
        CHECK(fake->get_next_packet_size_calls == 1U);
        CHECK(fake->get_buffer_calls == 1U);
        CHECK(fake->release_buffer_calls == 1U);
        CHECK(fake->last_released_frames == 3U);
        CHECK(!fake->packet_acquired);

        CHECK(source.read(output.data(), 2U, frames_read));
        CHECK(frames_read == 1U);
        CHECK(output[0] == 7.0F && output[1] == 8.0F);
        const auto snapshot = source.snapshot();
        CHECK(snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Running);
        CHECK(snapshot.captured_frames == 1U);
        CHECK(snapshot.dropped_frames == 3U);
    }

    // A failed acquire does not release or claim any packet frames.
    {
        hibiki::WindowsProcessLoopbackSourceV1 source;
        auto* fake = attach_fake_capture_client(source);
        fake->add_packet({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F}, 3U);
        fake->get_buffer_result = E_FAIL;
        std::array<float, 4> output{81.0F, 82.0F, 83.0F, 84.0F};
        std::uint32_t frames_read = 0U;

        CHECK(!source.read(output.data(), 2U, frames_read));
        CHECK(frames_read == 0U);
        CHECK(output[0] == 81.0F && output[1] == 82.0F);
        CHECK(output[2] == 83.0F && output[3] == 84.0F);
        CHECK(fake->get_buffer_calls == 1U);
        CHECK(fake->release_buffer_calls == 0U);
        const auto snapshot = source.snapshot();
        CHECK(snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Degraded);
        CHECK(snapshot.last_error == E_FAIL);
        CHECK(snapshot.captured_frames == 0U);
        CHECK(snapshot.dropped_frames == 0U);
    }

    // A failed release after an oversized acquire degrades without counting
    // the packet as dropped and preserves the actual release HRESULT.
    {
        hibiki::WindowsProcessLoopbackSourceV1 source;
        auto* fake = attach_fake_capture_client(source);
        fake->add_packet({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F}, 3U);
        fake->release_buffer_result = AUDCLNT_E_OUT_OF_ORDER;
        std::array<float, 4> output{71.0F, 72.0F, 73.0F, 74.0F};
        std::uint32_t frames_read = 0U;

        CHECK(!source.read(output.data(), 2U, frames_read));
        CHECK(frames_read == 0U);
        CHECK(output[0] == 71.0F && output[1] == 72.0F);
        CHECK(output[2] == 73.0F && output[3] == 74.0F);
        CHECK(fake->get_buffer_calls == 1U);
        CHECK(fake->release_buffer_calls == 1U);
        CHECK(fake->last_released_frames == 3U);
        const auto snapshot = source.snapshot();
        CHECK(snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Degraded);
        CHECK(snapshot.last_error == AUDCLNT_E_OUT_OF_ORDER);
        CHECK(snapshot.captured_frames == 0U);
        CHECK(snapshot.dropped_frames == 0U);
    }

    // A normal-path release failure is surfaced instead of reporting frames
    // as captured; callers must ignore the acquired output on false.
    {
        hibiki::WindowsProcessLoopbackSourceV1 source;
        auto* fake = attach_fake_capture_client(source);
        fake->add_packet({0.5F, -0.5F}, 1U);
        fake->release_buffer_result = E_FAIL;
        std::array<float, 2> output{61.0F, 62.0F};
        std::uint32_t frames_read = 0U;

        CHECK(!source.read(output.data(), 2U, frames_read));
        CHECK(frames_read == 0U);
        CHECK(fake->get_buffer_calls == 1U);
        CHECK(fake->release_buffer_calls == 1U);
        CHECK(fake->last_released_frames == 1U);
        const auto snapshot = source.snapshot();
        CHECK(snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Degraded);
        CHECK(snapshot.last_error == E_FAIL);
        CHECK(snapshot.captured_frames == 0U);
    }

    // Post-acquire metadata failure still releases once and preserves a
    // release failure over the local E_INVALIDARG diagnosis.
    {
        hibiki::WindowsProcessLoopbackSourceV1 source;
        auto* fake = attach_fake_capture_client(source);
        fake->add_packet({0.5F, -0.5F}, 1U);
        fake->release_buffer_result = AUDCLNT_E_OUT_OF_ORDER;
        fake->get_buffer_frames_override = 2U;
        std::array<float, 2> output{51.0F, 52.0F};
        std::uint32_t frames_read = 0U;

        CHECK(!source.read(output.data(), 1U, frames_read));
        CHECK(frames_read == 0U);
        CHECK(fake->get_buffer_calls == 1U);
        CHECK(fake->release_buffer_calls == 1U);
        CHECK(fake->last_released_frames == 2U);
        const auto snapshot = source.snapshot();
        CHECK(snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Degraded);
        CHECK(snapshot.last_error == AUDCLNT_E_OUT_OF_ORDER);
        CHECK(snapshot.captured_frames == 0U);
        CHECK(snapshot.dropped_frames == 0U);
    }

    return 0;
}

}  // namespace

#endif  // defined(_WIN32)

int main() {
    // Applied: two sessions on the same process collapse into one entry.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(100U, "game", "main")));
        auto second = make_session(100U, "game", "main");
        second.identity.session_instance_id = "session-100-game-main-2";
        CHECK(registry.upsert(second));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::Applied);
        CHECK(plan.schema_version == 1U);
        CHECK(plan.size == 1U);
        CHECK(plan.entries[0].process_id == 100U);
        CHECK(plan.entries[0].session_count == 2U);
        CHECK(plan.entries[0].include_process_tree);
        CHECK(plan.entries[0].lane_id == "game");
        CHECK(plan.entries[0].output_group == "main");
    }

    // NoRoutes: empty registry and filtered-out sessions.
    {
        AudioSessionRegistry registry;
        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::NoRoutes);
        CHECK(plan.size == 0U);

        auto inactive = make_session(200U, "game", "main");
        inactive.active = false;
        CHECK(registry.upsert(inactive));
        auto missing_lane = make_session(201U, "", "main");
        missing_lane.identity.session_instance_id = "session-201--main";
        CHECK(registry.upsert(missing_lane));
        auto missing_group = make_session(202U, "game", "");
        missing_group.identity.session_instance_id = "session-202-game-";
        CHECK(registry.upsert(missing_group));
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::NoRoutes);
        CHECK(plan.size == 0U);
    }

    // InvalidProcessIdentity resets any partially built plan.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(300U, "game", "main")));
        auto zero_pid = make_session(0U, "music", "main");
        zero_pid.identity.session_instance_id = "session-zero";
        CHECK(registry.upsert(zero_pid));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::InvalidProcessIdentity);
        CHECK(plan.size == 0U);
    }

    // DuplicateLane: same lane bound by two different processes fails closed.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(400U, "game", "main")));
        CHECK(registry.upsert(make_session(401U, "game", "main")));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::DuplicateLane);
        CHECK(plan.size == 0U);
    }

    // AmbiguousProcess: one process mapped to two lanes fails closed.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(500U, "game", "main")));
        CHECK(registry.upsert(make_session(500U, "music", "main")));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::AmbiguousProcess);
        CHECK(plan.size == 0U);
    }

    // CapacityExhausted: exactly kMaxEntries processes succeed.
    {
        AudioSessionRegistry registry;
        for (std::uint32_t pid = 600U; pid < 600U + 64U; ++pid) {
            CHECK(registry.upsert(make_session(pid, "lane-" + std::to_string(pid), "main")));
        }

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::Applied);
        CHECK(plan.size == ProcessLoopbackPlanV1::kMaxEntries);
    }

    // CapacityExhausted: one more than capacity fails and resets the plan.
    {
        AudioSessionRegistry registry;
        for (std::uint32_t pid = 700U; pid < 700U + 65U; ++pid) {
            CHECK(registry.upsert(make_session(pid, "lane-" + std::to_string(pid), "main")));
        }

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::CapacityExhausted);
        CHECK(plan.size == 0U);
    }

    // A failure after several valid entries discards all of them.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(800U, "alpha", "main")));
        CHECK(registry.upsert(make_session(801U, "beta", "main")));
        CHECK(registry.upsert(make_session(802U, "gamma", "main")));
        CHECK(registry.upsert(make_session(0U, "delta", "main")));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::InvalidProcessIdentity);
        CHECK(plan.size == 0U);
    }

#if defined(_WIN32)
    CHECK(run_process_loopback_read_tests() == 0);
#endif

    std::fputs("process_loopback_plan_tests passed\n", stdout);
    return 0;
}
