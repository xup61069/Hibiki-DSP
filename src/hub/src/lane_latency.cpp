#include "hibiki/lane_latency.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {

bool LaneLatencyBankV1::prepare(const std::span<const LaneLatencyConfigV1> configs) noexcept {
    if (configs.size() > kLaneLatencyMaxLanesV1) return false;

    LaneLatencyBankV1 candidate;
    try {
        for (std::size_t index = 0U; index < configs.size(); ++index) {
            const auto& config = configs[index];
            if (config.channel_count == 0U || config.channel_count > kLaneLatencyMaxChannelsV1 ||
                config.delay_samples > kLaneLatencyMaxSamplesV1) {
                return false;
            }
            auto& slot = candidate.slots_[index];
            slot.channels = config.channel_count;
            slot.delay_samples = config.enabled ? config.delay_samples : 0U;
            slot.ring_length = slot.delay_samples == 0U ? 0U : slot.delay_samples;
            if (slot.ring_length != 0U) {
                slot.ring.assign(static_cast<std::size_t>(slot.channels) * slot.ring_length, 0.0F);
            }
            slot.scratch = std::make_unique<float[]>(
                static_cast<std::size_t>(slot.channels) * kLaneLatencyMaxFramesV1);
            slot.prepared = true;
        }
    } catch (...) {
        return false;
    }
    *this = std::move(candidate);
    return true;
}

void LaneLatencyBankV1::reset() noexcept {
    for (auto& slot : slots_) {
        if (!slot.prepared) continue;
        std::fill(slot.ring.begin(), slot.ring.end(), 0.0F);
        if (slot.scratch != nullptr) {
            std::fill_n(slot.scratch.get(),
                        static_cast<std::size_t>(slot.channels) * kLaneLatencyMaxFramesV1,
                        0.0F);
        }
        slot.write_index = 0U;
    }
}

bool LaneLatencyBankV1::process_lane(const std::size_t lane_index,
                                     const float* const input_interleaved,
                                     const std::uint32_t channels,
                                     const std::size_t frames) noexcept {
    if (lane_index >= slots_.size() || input_interleaved == nullptr || frames == 0U ||
        frames > kLaneLatencyMaxFramesV1) {
        return false;
    }
    auto& slot = slots_[lane_index];
    if (!slot.prepared || channels != slot.channels || slot.scratch == nullptr) return false;

    const auto sample_count = frames * static_cast<std::size_t>(channels);
    for (std::size_t sample = 0U; sample < sample_count; ++sample) {
        if (!std::isfinite(input_interleaved[sample])) {
            std::fill_n(slot.scratch.get(), sample_count, 0.0F);
            std::fill(slot.ring.begin(), slot.ring.end(), 0.0F);
            slot.write_index = 0U;
            return false;
        }
    }

    if (slot.delay_samples == 0U) {
        std::memcpy(slot.scratch.get(), input_interleaved, sample_count * sizeof(float));
        return true;
    }

    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            const auto ring_offset = static_cast<std::size_t>(channel) * slot.ring_length +
                                     slot.write_index;
            const auto sample_offset = frame * static_cast<std::size_t>(channels) + channel;
            slot.scratch[sample_offset] = slot.ring[ring_offset];
            slot.ring[ring_offset] = input_interleaved[sample_offset];
        }
        slot.write_index = (slot.write_index + 1U) % slot.ring_length;
    }
    return true;
}

const float* LaneLatencyBankV1::output(const std::size_t lane_index) const noexcept {
    if (lane_index >= slots_.size() || !slots_[lane_index].prepared ||
        slots_[lane_index].scratch == nullptr) {
        return nullptr;
    }
    return slots_[lane_index].scratch.get();
}

bool LaneLatencyBankV1::prepared(const std::size_t lane_index) const noexcept {
    return lane_index < slots_.size() && slots_[lane_index].prepared;
}

std::uint32_t LaneLatencyBankV1::delay_samples(const std::size_t lane_index) const noexcept {
    return lane_index < slots_.size() ? slots_[lane_index].delay_samples : 0U;
}

}  // namespace hibiki
