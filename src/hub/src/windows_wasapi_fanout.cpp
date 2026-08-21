#include "hibiki/windows_wasapi_fanout.hpp"

#include <string_view>

namespace hibiki {

WindowsWasapiFanoutV1::~WindowsWasapiFanoutV1() { stop(); }

bool WindowsWasapiFanoutV1::prepare(
    const std::span<const WasapiFanoutSinkConfigV1> configs,
    const std::uint32_t block_frames) noexcept {
    stop();
    if (configs.empty() || configs.size() > kMaxSinks || block_frames == 0U ||
        block_frames > 4096U) {
        degraded_ = true;
        return false;
    }
    std::uint32_t common_channels = 0U;
    std::uint32_t common_rate = 0U;
    for (std::size_t index = 0U; index < configs.size(); ++index) {
        if (!configs[index].enabled) continue;
        const auto& output = configs[index].output;
        if (output.endpoint_id.empty() || output.channels == 0U || output.sample_rate == 0U) {
            degraded_ = true;
            stop();
            return false;
        }
        if (common_channels == 0U) {
            common_channels = output.channels;
            common_rate = output.sample_rate;
        } else if (output.channels != common_channels || output.sample_rate != common_rate) {
            degraded_ = true;
            stop();
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (configs[previous].enabled &&
                configs[previous].output.endpoint_id == output.endpoint_id) {
                degraded_ = true;
                stop();
                return false;
            }
        }
        enabled_[index] = true;
        ++enabled_count_;
    }
    if (enabled_count_ == 0U) {
        degraded_ = true;
        stop();
        return false;
    }
    sink_count_ = static_cast<std::uint32_t>(configs.size());
    for (std::size_t index = 0U; index < configs.size(); ++index) {
        if (!enabled_[index] || sinks_[index].start_initial(configs[index].output, block_frames)) {
            continue;
        }
        degraded_ = true;
        stop();
        return false;
    }
    degraded_ = false;
    return true;
}

bool WindowsWasapiFanoutV1::begin_handoff(const std::size_t sink_index,
                                          const WasapiOutputConfigV1& candidate,
                                          const std::uint32_t block_frames,
                                          const std::uint32_t fade_ms) noexcept {
    if (sink_index >= sink_count_ || !enabled_[sink_index] || degraded_) return false;
    return sinks_[sink_index].begin(candidate, block_frames, fade_ms);
}

bool WindowsWasapiFanoutV1::prepare_handoff(const std::size_t sink_index) noexcept {
    if (sink_index >= sink_count_ || !enabled_[sink_index] || degraded_) return false;
    return sinks_[sink_index].prepare();
}

bool WindowsWasapiFanoutV1::commit_handoff(const std::size_t sink_index) noexcept {
    if (sink_index >= sink_count_ || !enabled_[sink_index] || degraded_) return false;
    return sinks_[sink_index].commit();
}

void WindowsWasapiFanoutV1::rollback_handoff(const std::size_t sink_index) noexcept {
    if (sink_index < sink_count_ && enabled_[sink_index]) sinks_[sink_index].rollback();
}

bool WindowsWasapiFanoutV1::process(const float* const interleaved,
                                    const std::uint32_t frames,
                                    const std::uint32_t channels) noexcept {
    if (degraded_ || interleaved == nullptr || frames == 0U || enabled_count_ == 0U) {
        return false;
    }
    bool all_submitted = true;
    for (std::size_t index = 0U; index < sink_count_; ++index) {
        if (!enabled_[index]) continue;
        if (!sinks_[index].process(interleaved, frames, channels)) all_submitted = false;
    }
    if (!all_submitted) degraded_ = true;
    return all_submitted;
}

void WindowsWasapiFanoutV1::stop() noexcept {
    for (auto& sink : sinks_) sink.stop();
    enabled_.fill(false);
    sink_count_ = 0U;
    enabled_count_ = 0U;
}

WasapiFanoutSnapshotV1 WindowsWasapiFanoutV1::snapshot() const noexcept {
    WasapiFanoutSnapshotV1 result{};
    result.sink_count = sink_count_;
    result.enabled_count = enabled_count_;
    result.degraded = degraded_;
    for (std::size_t index = 0U; index < kMaxSinks; ++index) {
        result.sinks[index] = sinks_[index].snapshot();
    }
    return result;
}

}  // namespace hibiki
