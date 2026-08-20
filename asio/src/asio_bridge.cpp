#include "hibiki/asio_bridge.hpp"

#include <cmath>

namespace hibiki {
namespace {

bool valid_stream(const AsioStreamConfigV1& config) noexcept {
    const bool valid_rate = config.sample_rate == 44100 || config.sample_rate == 48000 ||
                            config.sample_rate == 96000 || config.sample_rate == 192000;
    const bool valid_channels = config.channels == 2 || config.channels == 6 || config.channels == 8;
    return valid_rate && valid_channels && config.frames_per_buffer > 0 &&
           config.frames_per_buffer <= 4096;
}

float db_to_linear(const double db) noexcept {
    if (!std::isfinite(db) || db <= -144.0) {
        return 0.0F;
    }
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

}  // namespace

bool AsioBridgeModel::prepare(const AsioStreamConfigV1& config) noexcept {
    if (!valid_stream(config)) {
        return false;
    }
    state_.stream = config;
    state_.prepared = true;
    return true;
}

void AsioBridgeModel::release() noexcept {
    state_.prepared = false;
}

void AsioBridgeModel::apply_group_volume(const OutputGroupVolumeStateV1& volume) noexcept {
    state_.effective_db = volume.effective_db;
    state_.mute = volume.mute;
    linear_gain_ = volume.mute ? 0.0F : db_to_linear(volume.effective_db);
}

bool AsioBridgeModel::process_interleaved(const float* const input,
                                          float* const output,
                                          const std::size_t frames) const noexcept {
    if (!state_.prepared || input == nullptr || output == nullptr ||
        frames > static_cast<std::size_t>(state_.stream.frames_per_buffer)) {
        return false;
    }
    const auto samples = frames * static_cast<std::size_t>(state_.stream.channels);
    for (std::size_t index = 0; index < samples; ++index) {
        output[index] = input[index] * linear_gain_;
    }
    return true;
}

}  // namespace hibiki
