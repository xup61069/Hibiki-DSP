#include "hibiki/output_handoff.hpp"

#include <utility>

namespace hibiki {

bool OutputHandoffCoordinatorV1::begin(DeviceTargetV1 target,
                                       const std::uint32_t crossfade_ms) noexcept {
    if (state_ == OutputHandoffStateV1::Preparing || state_ == OutputHandoffStateV1::Fading) {
        return false;
    }
    const auto target_channels = target.channels;
    const auto target_sample_rate = target.sample_rate;
    if (!transaction_.begin(std::move(target))) {
        state_ = OutputHandoffStateV1::Degraded;
        return false;
    }
    if (!crossfade_.begin(target_channels, target_sample_rate, crossfade_ms)) {
        transaction_.rollback();
        state_ = OutputHandoffStateV1::Degraded;
        return false;
    }
    state_ = OutputHandoffStateV1::Preparing;
    return true;
}

bool OutputHandoffCoordinatorV1::prepare() noexcept {
    if (state_ != OutputHandoffStateV1::Preparing || !transaction_.prepare_complete()) {
        return false;
    }
    state_ = OutputHandoffStateV1::Fading;
    return true;
}

bool OutputHandoffCoordinatorV1::process(const float* const old_interleaved,
                                         const float* const new_interleaved,
                                         float* const output_interleaved,
                                         const std::size_t frames) noexcept {
    if (state_ != OutputHandoffStateV1::Fading ||
        !crossfade_.process(old_interleaved, new_interleaved, output_interleaved, frames)) {
        return false;
    }
    return true;
}

bool OutputHandoffCoordinatorV1::commit() noexcept {
    if (state_ != OutputHandoffStateV1::Fading || crossfade_.snapshot().active ||
        !transaction_.commit()) {
        return false;
    }
    state_ = OutputHandoffStateV1::Committed;
    return true;
}

void OutputHandoffCoordinatorV1::rollback() noexcept {
    crossfade_.reset();
    transaction_.rollback();
    state_ = OutputHandoffStateV1::RolledBack;
}

}  // namespace hibiki
