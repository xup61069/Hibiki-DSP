#include "hibiki/volume_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace hibiki {

double effective_gain_db(const double requested_db, const double safety_ceiling_db) noexcept {
    if (!std::isfinite(requested_db) || !std::isfinite(safety_ceiling_db)) {
        return -144.0;
    }
    return std::clamp(std::min(requested_db, safety_ceiling_db), -144.0, 12.0);
}

std::int32_t db_to_q16_16(const double db) noexcept {
    if (!std::isfinite(db)) {
        return -144 * 65536;
    }
    const double limited = std::clamp(db, -144.0, 12.0);
    return static_cast<std::int32_t>(std::llround(limited * 65536.0));
}

double q16_16_to_db(const std::int32_t value) noexcept {
    return std::clamp(static_cast<double>(value) / 65536.0, -144.0, 12.0);
}

OutputGroupVolumeStateV1 reconcile(OutputGroupVolumeStateV1 state) noexcept {
    state.schema_version = 1;
    state.effective_db = effective_gain_db(state.requested_db, state.safety_ceiling_db);
    if (state.actuator == ActuatorMode::StrictDirect) {
        state.effective_db = 0.0;
    }
    return state;
}

VolumeRamp make_ramp(const OutputGroupVolumeStateV1& before,
                     const OutputGroupVolumeStateV1& after) noexcept {
    const bool mute_transition = before.mute != after.mute;
    const std::uint32_t duration = mute_transition ? (after.mute ? 5U : 15U) : 8U;
    return VolumeRamp{before.effective_db, after.effective_db, duration};
}

void VolumeRampProcessorV1::reset(const double current_db, const bool mute) noexcept {
    current_db_ = std::isfinite(current_db) ? std::clamp(current_db, -144.0, 12.0) : -60.0;
    target_db_ = mute ? -144.0 : current_db_;
    target_q16_16_ = db_to_q16_16(current_db_);
    remaining_frames_ = 0U;
    target_mute_ = mute;
    initialized_ = true;
}

void VolumeRampProcessorV1::observe_target(const std::int32_t effective_db_q16_16,
                                           const bool mute,
                                           std::uint32_t sample_rate) noexcept {
    if (sample_rate < 8000U || sample_rate > 192000U) sample_rate = 48000U;
    if (initialized_ && target_q16_16_ == effective_db_q16_16 && target_mute_ == mute) {
        return;
    }

    if (!initialized_) {
        reset(-60.0, false);
    }
    const auto previous_mute = target_mute_;
    const auto target_db = mute ? -144.0 : q16_16_to_db(effective_db_q16_16);
    const auto duration_ms = mute != previous_mute ? (mute ? 5U : 15U) : 8U;
    const auto frame_count = static_cast<std::uint64_t>(sample_rate) * duration_ms / 1000U;
    target_db_ = std::isfinite(target_db) ? std::clamp(target_db, -144.0, 12.0) : -144.0;
    target_q16_16_ = effective_db_q16_16;
    target_mute_ = mute;
    remaining_frames_ = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
        frame_count, 1U, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
}

float VolumeRampProcessorV1::next_gain() noexcept {
    if (remaining_frames_ > 0U) {
        current_db_ += (target_db_ - current_db_) / static_cast<double>(remaining_frames_);
        --remaining_frames_;
        if (remaining_frames_ == 0U) current_db_ = target_db_;
    }
    if (target_mute_ && remaining_frames_ == 0U) return 0.0F;
    if (!std::isfinite(current_db_) || current_db_ <= -144.0) return 0.0F;
    return static_cast<float>(std::pow(10.0, current_db_ / 20.0));
}

VolumeNotificationResult apply_windows_notification(
    OutputGroupVolumeStateV1& state, const VolumeNotificationV1& notification) noexcept {
    if (notification.generation == 0U || !std::isfinite(notification.requested_db) ||
        notification.requested_db < -144.0 || notification.requested_db > 12.0) {
        return VolumeNotificationResult::Invalid;
    }
    if (notification.generation < state.generation) {
        return VolumeNotificationResult::StaleGeneration;
    }
    state.requested_db = notification.requested_db;
    state.mute = notification.mute;
    state.generation = notification.generation;
    state.origin = VolumeOrigin::Windows;
    state = reconcile(state);
    return VolumeNotificationResult::Accepted;
}

OutputGroupVolumeBankV1::OutputGroupVolumeBankV1() noexcept {
    (void)register_group("main");
}

bool OutputGroupVolumeBankV1::valid_group(const std::string_view output_group) noexcept {
    return !output_group.empty() && output_group.size() <= kMaxOutputVolumeGroupBytesV1 &&
           output_group.find('\0') == std::string_view::npos;
}

OutputGroupVolumeBankV1::Slot* OutputGroupVolumeBankV1::find_slot(
    const std::string_view output_group) noexcept {
    if (!valid_group(output_group)) return nullptr;
    for (auto& slot : slots_) {
        if (slot.used && slot.group_bytes == output_group.size() &&
            std::equal(output_group.begin(), output_group.end(), slot.group.begin())) {
            return &slot;
        }
    }
    return nullptr;
}

const OutputGroupVolumeBankV1::Slot* OutputGroupVolumeBankV1::find_slot(
    const std::string_view output_group) const noexcept {
    if (!valid_group(output_group)) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.used && slot.group_bytes == output_group.size() &&
            std::equal(output_group.begin(), output_group.end(), slot.group.begin())) {
            return &slot;
        }
    }
    return nullptr;
}

void OutputGroupVolumeBankV1::publish_rt_word(Slot& slot) noexcept {
    const auto effective_q16 = db_to_q16_16(slot.control.effective_db);
    const auto packed = (static_cast<std::uint64_t>(
                             static_cast<std::uint32_t>(effective_q16)) << 32U) |
                        (slot.control.mute ? 1ULL : 0ULL);
    slot.rt_word.store(packed, std::memory_order_release);
}

bool OutputGroupVolumeBankV1::register_group(const std::string_view output_group) noexcept {
    if (!valid_group(output_group)) return false;
    if (find_slot(output_group) != nullptr) return true;
    if (group_count_ >= kMaxOutputVolumeGroupsV1) return false;

    for (auto& slot : slots_) {
        if (slot.used) continue;
        slot.used = true;
        slot.group_bytes = static_cast<std::uint8_t>(output_group.size());
        std::copy(output_group.begin(), output_group.end(), slot.group.begin());
        slot.control = OutputGroupVolumeStateV1{};
        slot.control = reconcile(slot.control);
        slot.ramp.reset(slot.control.effective_db, slot.control.mute);
        publish_rt_word(slot);
        ++group_count_;
        return true;
    }
    return false;
}

bool OutputGroupVolumeBankV1::has_group(const std::string_view output_group) const noexcept {
    return find_slot(output_group) != nullptr;
}

TruePeakLimiterV1* OutputGroupVolumeBankV1::limiter_for_group(
    const std::string_view output_group) const noexcept {
    auto* const slot = const_cast<OutputGroupVolumeBankV1::Slot*>(
        find_slot(output_group));
    return slot != nullptr ? &slot->limiter : nullptr;
}

void OutputGroupVolumeBankV1::reset_limiters() const noexcept {
    for (auto& slot : slots_) {
        if (slot.used) slot.limiter.reset();
    }
}

VolumeNotificationResult OutputGroupVolumeBankV1::apply_windows_notification(
    const std::string_view output_group,
    const VolumeNotificationV1& notification) noexcept {
    auto* const slot = find_slot(output_group);
    if (slot == nullptr) return VolumeNotificationResult::Invalid;
    const auto result = hibiki::apply_windows_notification(slot->control, notification);
    if (result == VolumeNotificationResult::Accepted) publish_rt_word(*slot);
    return result;
}

OutputGroupVolumeStateV1 OutputGroupVolumeBankV1::state(
    const std::string_view output_group) const noexcept {
    const auto* const slot = find_slot(output_group);
    if (slot != nullptr) return slot->control;
    const auto* const main = find_slot("main");
    return main != nullptr ? main->control : OutputGroupVolumeStateV1{};
}

bool OutputGroupVolumeBankV1::apply_to_interleaved(const std::string_view output_group,
                                                   float* const interleaved,
                                                   const std::size_t frames,
                                                   const std::uint32_t channels,
                                                   std::uint32_t sample_rate) const noexcept {
    if (interleaved == nullptr || frames == 0U || channels == 0U || channels > 8U) return false;
    const auto channel_count = static_cast<std::size_t>(channels);
    if (frames > std::numeric_limits<std::size_t>::max() / channel_count) return false;
    const auto* const slot = find_slot(output_group);
    if (slot == nullptr) return false;

    const auto volume_word = slot->rt_word.load(std::memory_order_acquire);
    const auto effective_q16 = static_cast<std::int32_t>(volume_word >> 32U);
    slot->ramp.observe_target(effective_q16, (volume_word & 1ULL) != 0ULL, sample_rate);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const auto gain = slot->ramp.next_gain();
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            interleaved[frame * channels + channel] *= gain;
        }
    }
    return true;
}

}  // namespace hibiki
