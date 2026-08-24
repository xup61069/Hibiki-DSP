#include "hibiki/output_fanout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

namespace hibiki {
namespace {

bool valid_channels(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
}

bool is_printable_sink_id(std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x20U && ch != 0x7FU && !(ch >= 0x80U && ch <= 0x9FU);
    });
}

}  // namespace

bool validate_output_fanout_plan_v1(const OutputFanoutPlanV1& plan) noexcept {
    if (plan.schema_version != 1U || plan.revision == 0U ||
        !valid_channels(plan.output_channels) || plan.sink_count == 0U ||
        plan.sink_count > kOutputFanoutMaxSinksV1) {
        return false;
    }
    bool has_enabled_sink = false;
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        const auto& sink = plan.sinks[index];
        if (sink.id_bytes == 0U || sink.id_bytes > kOutputFanoutMaxIdBytesV1 ||
            sink.channels != plan.output_channels) {
            return false;
        }
        if (!is_printable_sink_id(
                std::string_view(sink.sink_id.data(), sink.id_bytes))) {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            const auto& other = plan.sinks[prior];
            if (other.id_bytes == sink.id_bytes &&
                std::memcmp(other.sink_id.data(), sink.sink_id.data(), sink.id_bytes) == 0) {
                return false;
            }
        }
        has_enabled_sink = has_enabled_sink || sink.enabled;
    }
    return has_enabled_sink;
}

bool prepare_output_fanout_plan_v1(
    const std::span<const OutputFanoutSinkConfigV1> configs,
    const std::uint32_t output_channels,
    const std::uint64_t revision,
    OutputFanoutPlanV1& plan) noexcept {
    if (configs.empty() || configs.size() > kOutputFanoutMaxSinksV1 || revision == 0U ||
        !valid_channels(output_channels)) {
        return false;
    }
    OutputFanoutPlanV1 candidate{};
    candidate.revision = revision;
    candidate.output_channels = output_channels;
    candidate.sink_count = static_cast<std::uint32_t>(configs.size());
    for (std::size_t index = 0U; index < configs.size(); ++index) {
        const auto& source = configs[index];
        if (source.sink_id.empty() ||
            source.sink_id.size() > kOutputFanoutMaxIdBytesV1 ||
            !is_printable_sink_id(source.sink_id) ||
            source.channels != output_channels) {
            return false;
        }
        auto& target = candidate.sinks[index];
        target.id_bytes = static_cast<std::uint8_t>(source.sink_id.size());
        std::copy(source.sink_id.begin(), source.sink_id.end(), target.sink_id.begin());
        target.channels = source.channels;
        target.enabled = source.enabled;
    }
    if (!validate_output_fanout_plan_v1(candidate)) return false;
    plan = candidate;
    return true;
}

bool fanout_interleaved_v1(const OutputFanoutPlanV1& plan,
                           const float* const input_interleaved,
                           const std::size_t frames,
                           const std::span<float* const> outputs,
                           const std::span<const std::size_t> output_capacities) noexcept {
    if (!validate_output_fanout_plan_v1(plan) || input_interleaved == nullptr || frames == 0U ||
        outputs.size() < plan.sink_count || output_capacities.size() < plan.sink_count) {
        return false;
    }
    const auto samples = frames * static_cast<std::size_t>(plan.output_channels);
    for (std::size_t sample = 0U; sample < samples; ++sample) {
        if (!std::isfinite(input_interleaved[sample])) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        const auto& sink = plan.sinks[index];
        if (sink.enabled && (outputs[index] == nullptr || output_capacities[index] < frames)) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        if (plan.sinks[index].enabled) {
            std::memcpy(outputs[index], input_interleaved, samples * sizeof(float));
        }
    }
    return true;
}

bool OutputFanoutRuntimeV1::prepare(const OutputFanoutPlanV1& plan,
                                    const double source_step) noexcept {
    if (!validate_output_fanout_plan_v1(plan) || !std::isfinite(source_step) ||
        source_step < 0.25 || source_step > 4.0) {
        return false;
    }
    std::array<OutputSinkModel, kOutputFanoutMaxSinksV1> candidates{};
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        if (plan.sinks[index].enabled &&
            !candidates[index].prepare(plan.output_channels, source_step)) {
            return false;
        }
    }
    auto candidate_scratch =
        std::unique_ptr<ScratchStorage>(new (std::nothrow) ScratchStorage{});
    if (candidate_scratch == nullptr) {
        return false;
    }
    plan_ = plan;
    sinks_ = candidates;
    scratch_ = std::move(candidate_scratch);
    prepared_ = true;
    return true;
}

void OutputFanoutRuntimeV1::reset() noexcept {
    if (!prepared_) {
        return;
    }
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (plan_.sinks[index].enabled) {
            sinks_[index].reset();
        }
    }
}

bool OutputFanoutRuntimeV1::observe_clock(const std::size_t sink_index,
                                          const double source_frames,
                                          const double sink_frames,
                                          const double elapsed_seconds) noexcept {
    if (!prepared_ || sink_index >= plan_.sink_count || !plan_.sinks[sink_index].enabled ||
        !std::isfinite(source_frames) || !std::isfinite(sink_frames) ||
        !std::isfinite(elapsed_seconds) || source_frames <= 0.0 || sink_frames <= 0.0 ||
        elapsed_seconds <= 0.0) {
        return false;
    }
    sinks_[sink_index].observe_clock(source_frames, sink_frames, elapsed_seconds);
    return true;
}

bool OutputFanoutRuntimeV1::process(
    const float* const input_interleaved,
    const std::size_t input_frames,
    const std::span<float* const> outputs,
    const std::span<const std::size_t> output_capacities,
    const std::span<std::size_t> output_frames) noexcept {
    if (!prepared_ || scratch_ == nullptr || input_interleaved == nullptr || input_frames == 0U ||
        input_frames > kOutputFanoutMaxInputFramesV1 || outputs.size() < plan_.sink_count ||
        output_capacities.size() < plan_.sink_count || output_frames.size() < plan_.sink_count) {
        return false;
    }
    const auto samples = input_frames * static_cast<std::size_t>(plan_.output_channels);
    for (std::size_t sample = 0U; sample < samples; ++sample) {
        if (!std::isfinite(input_interleaved[sample])) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        output_frames[index] = 0U;
        if (!plan_.sinks[index].enabled) {
            continue;
        }
        const auto required_frames = sinks_[index].required_output_frames(input_frames);
        if (outputs[index] == nullptr || output_capacities[index] < required_frames) {
            return false;
        }
    }

    const auto state_before = sinks_;
    std::array<std::size_t, kOutputFanoutMaxSinksV1> rendered_frames{};
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (plan_.sinks[index].enabled &&
            !sinks_[index].process(input_interleaved, input_frames,
                                    scratch_->blocks[index].data(),
                                    kOutputFanoutMaxResampledFramesV1, rendered_frames[index])) {
            sinks_ = state_before;
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (plan_.sinks[index].enabled) {
            const auto sink_samples = rendered_frames[index] * plan_.output_channels;
            std::copy_n(scratch_->blocks[index].data(), sink_samples, outputs[index]);
            output_frames[index] = rendered_frames[index];
        }
    }
    return true;
}

OutputFanoutRuntimeSnapshotV1 OutputFanoutRuntimeV1::snapshot() const noexcept {
    OutputFanoutRuntimeSnapshotV1 result{};
    result.prepared = prepared_;
    result.output_channels = plan_.output_channels;
    result.sink_count = plan_.sink_count;
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        result.sinks[index] = sinks_[index].snapshot();
    }
    return result;
}

}  // namespace hibiki
