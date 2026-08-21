#include "hibiki/windows_wasapi_handoff.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

WindowsWasapiSinkHandoffV1::~WindowsWasapiSinkHandoffV1() { stop(); }

WindowsWasapiSinkWorkerV1& WindowsWasapiSinkHandoffV1::active_worker() noexcept {
    return active_slot_ == 0U ? primary_ : secondary_;
}

WindowsWasapiSinkWorkerV1& WindowsWasapiSinkHandoffV1::candidate_worker() noexcept {
    return active_slot_ == 0U ? secondary_ : primary_;
}

const WindowsWasapiSinkWorkerV1& WindowsWasapiSinkHandoffV1::active_worker() const noexcept {
    return active_slot_ == 0U ? primary_ : secondary_;
}

const WindowsWasapiSinkWorkerV1& WindowsWasapiSinkHandoffV1::candidate_worker() const noexcept {
    return active_slot_ == 0U ? secondary_ : primary_;
}

bool WindowsWasapiSinkHandoffV1::start_initial(const WasapiOutputConfigV1& config,
                                               const std::uint32_t block_frames) noexcept {
    stop();
    if (!primary_.start(config, block_frames)) {
        state_ = WasapiSinkHandoffStateV1::Degraded;
        return false;
    }
    active_slot_ = 0U;
    channels_ = config.channels;
    fade_frames_ = 0U;
    fade_total_frames_ = 0U;
    state_ = WasapiSinkHandoffStateV1::Synced;
    return true;
}

bool WindowsWasapiSinkHandoffV1::begin(const WasapiOutputConfigV1& candidate,
                                       const std::uint32_t block_frames,
                                       const std::uint32_t fade_ms) noexcept {
    if ((state_ != WasapiSinkHandoffStateV1::Synced &&
         state_ != WasapiSinkHandoffStateV1::RolledBack) ||
        !active_worker().snapshot().endpoint_ready || fade_ms == 0U || fade_ms > 200U ||
        candidate.channels != channels_ || candidate.sample_rate == 0U ||
        candidate.sample_rate != active_worker().snapshot().sample_rate) {
        return false;
    }
    candidate_worker().stop();
    if (!candidate_worker().start(candidate, block_frames)) {
        state_ = WasapiSinkHandoffStateV1::Degraded;
        return false;
    }
    const auto total = (static_cast<std::uint64_t>(candidate.sample_rate) * fade_ms) / 1000U;
    fade_total_frames_ = std::max<std::uint64_t>(total, block_frames);
    fade_frames_ = 0U;
    state_ = WasapiSinkHandoffStateV1::Preparing;
    return true;
}

bool WindowsWasapiSinkHandoffV1::prepare() noexcept {
    if (state_ != WasapiSinkHandoffStateV1::Preparing) return false;
    const auto candidate_snapshot = candidate_worker().snapshot();
    if (candidate_snapshot.endpoint_ready) {
        state_ = WasapiSinkHandoffStateV1::Fading;
        return true;
    }
    if (!candidate_snapshot.running && candidate_snapshot.degraded) {
        candidate_worker().stop();
        state_ = WasapiSinkHandoffStateV1::RolledBack;
    }
    return false;
}

bool WindowsWasapiSinkHandoffV1::process(const float* const interleaved,
                                         const std::uint32_t frames,
                                         const std::uint32_t channels) noexcept {
    if (state_ == WasapiSinkHandoffStateV1::Synced) {
        return active_worker().submit(interleaved, frames, channels);
    }
    if (state_ != WasapiSinkHandoffStateV1::Fading || fade_total_frames_ == 0U) {
        return false;
    }
    const double progress = std::clamp(
        static_cast<double>(fade_frames_) / static_cast<double>(fade_total_frames_), 0.0, 1.0);
    const float old_gain = static_cast<float>(std::cos(progress * 1.5707963267948966));
    const float new_gain = static_cast<float>(std::sin(progress * 1.5707963267948966));
    if (!candidate_worker().submit_scaled(interleaved, frames, channels, new_gain)) {
        candidate_worker().stop();
        state_ = WasapiSinkHandoffStateV1::RolledBack;
        return active_worker().submit(interleaved, frames, channels);
    }
    if (!active_worker().submit_scaled(interleaved, frames, channels, old_gain)) {
        state_ = WasapiSinkHandoffStateV1::Degraded;
        return false;
    }
    fade_frames_ = std::min<std::uint64_t>(fade_total_frames_, fade_frames_ + frames);
    if (fade_frames_ >= fade_total_frames_) state_ = WasapiSinkHandoffStateV1::ReadyToCommit;
    return true;
}

bool WindowsWasapiSinkHandoffV1::commit() noexcept {
    if (state_ != WasapiSinkHandoffStateV1::ReadyToCommit) return false;
    active_worker().stop();
    active_slot_ = active_slot_ == 0U ? 1U : 0U;
    fade_frames_ = 0U;
    fade_total_frames_ = 0U;
    state_ = WasapiSinkHandoffStateV1::Synced;
    return true;
}

void WindowsWasapiSinkHandoffV1::rollback() noexcept {
    if (state_ == WasapiSinkHandoffStateV1::Preparing ||
        state_ == WasapiSinkHandoffStateV1::Fading ||
        state_ == WasapiSinkHandoffStateV1::ReadyToCommit) {
        candidate_worker().stop();
        fade_frames_ = 0U;
        fade_total_frames_ = 0U;
        state_ = active_worker().snapshot().running ? WasapiSinkHandoffStateV1::RolledBack
                                                    : WasapiSinkHandoffStateV1::Degraded;
    }
}

void WindowsWasapiSinkHandoffV1::stop() noexcept {
    primary_.stop();
    secondary_.stop();
    active_slot_ = 0U;
    fade_frames_ = 0U;
    fade_total_frames_ = 0U;
    channels_ = 0U;
    state_ = WasapiSinkHandoffStateV1::Unbound;
}

WasapiSinkHandoffSnapshotV1 WindowsWasapiSinkHandoffV1::snapshot() const noexcept {
    return WasapiSinkHandoffSnapshotV1{state_, active_slot_, fade_frames_, fade_total_frames_,
                                      primary_.snapshot(), secondary_.snapshot()};
}

}  // namespace hibiki
