#include "hibiki/output_fanout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {
namespace {

bool valid_channels(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
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
        if (std::any_of(sink.sink_id.begin(), sink.sink_id.begin() + sink.id_bytes,
                        [](const char value) { return value == '\0'; })) {
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
        if (source.sink_id.empty() || source.sink_id.size() > kOutputFanoutMaxIdBytesV1 ||
            source.sink_id.find('\0') != std::string::npos || source.channels != output_channels) {
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

}  // namespace hibiki
