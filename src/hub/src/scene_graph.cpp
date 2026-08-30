#include "hibiki/scene_graph.hpp"
#include "hibiki/control_payloads.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <string_view>

namespace hibiki {

namespace {

[[nodiscard]] bool is_printable_label(std::string_view value,
                                      std::size_t maximum_bytes) noexcept {
    if (value.empty() || value.size() > maximum_bytes) {
        return false;
    }
    return is_printable_utf8_v1(value);
}

[[nodiscard]] bool checked_graph_output_samples(
    const std::size_t frames,
    const std::uint32_t output_channels,
    std::size_t& output_samples) noexcept {
    // Both graph input lanes and outputs are bounded to at most eight
    // interleaved channels; protect every later frame/channel stride too.
    if (output_channels == 0U || output_channels > 8U ||
        frames > std::numeric_limits<std::size_t>::max() / 8U) {
        return false;
    }
    output_samples = frames * static_cast<std::size_t>(output_channels);
    return true;
}

template <typename Sample>
[[nodiscard]] bool finite_interleaved_samples(const Sample* const interleaved,
                                              const std::size_t frames,
                                              const std::uint32_t channels) noexcept {
    const auto sample_count = frames * static_cast<std::size_t>(channels);
    for (std::size_t index = 0U; index < sample_count; ++index) {
        if (!std::isfinite(interleaved[index])) return false;
    }
    return true;
}

}  // namespace

bool validate_graph(const GraphConfigV1& graph) noexcept {
    if (graph.schema_version != 1 || graph.lanes.empty() || graph.lanes.size() > kMaxRtLanes ||
        (graph.output_channels != 2 && graph.output_channels != 6 && graph.output_channels != 8)) {
        return false;
    }
    if (graph.sample_format != kGraphSampleFormatFloat32V1 &&
        graph.sample_format != kGraphSampleFormatFloat64V1) {
        return false;
    }

    for (std::size_t lane_index = 0; lane_index < graph.lanes.size(); ++lane_index) {
        const auto& lane = graph.lanes[lane_index];
        if (lane.id.empty() || lane.output_group.empty() ||
            !is_printable_label(lane.id, kMaxLaneIdBytes) ||
            !is_printable_label(lane.output_group, kMaxOutputGroupBytes) ||
            lane.output_group.size() > kMaxOutputGroupBytes ||
            lane.output_group.find('\0') != std::string::npos ||
            (lane.channel_count != 2 && lane.channel_count != 6 && lane.channel_count != 8) ||
            !std::isfinite(lane.makeup_gain_db) || lane.makeup_gain_db < -144.0 ||
            lane.makeup_gain_db > 12.0 ||
            lane.reported_latency_samples > kLaneLatencyMaxSamplesV1) {
            return false;
        }
        for (std::size_t prior = 0; prior < lane_index; ++prior) {
            if (graph.lanes[prior].id == lane.id) {
                return false;
            }
        }
        for (std::uint32_t channel = 0; channel < lane.channel_count; ++channel) {
            const auto destination = lane.channel_map[channel];
            if (destination < -1 || destination >= static_cast<std::int8_t>(graph.output_channels)) {
                return false;
            }
        }
        if (lane.matrix_enabled) {
            if (graph.strict_direct) return false;
            for (const auto& row : lane.channel_matrix) {
                for (const auto gain : row) {
                    if (!std::isfinite(gain) || std::abs(gain) > 8.0F) return false;
                }
            }
        }
        if (graph.strict_direct && (std::abs(lane.makeup_gain_db) > 1e-12 ||
                                    lane.reported_latency_samples != 0U)) {
            return false;
        }
    }
    return true;
}

bool compile_rt_snapshot(const GraphConfigV1& graph,
                         const std::uint64_t revision,
                         RtGraphSnapshotV1& snapshot) noexcept {
    if (!validate_graph(graph)) {
        return false;
    }

    RtGraphSnapshotV1 compiled{};
    compiled.schema_version = 1;
    compiled.output_channels = graph.output_channels;
    compiled.lane_count = static_cast<std::uint32_t>(graph.lanes.size());
    compiled.revision = revision;
    compiled.strict_direct = graph.strict_direct;
    compiled.sample_format = graph.sample_format;
    std::array<std::uint32_t, kMaxRtLanes> group_max_latency{};
    for (std::size_t index = 0U; index < graph.lanes.size(); ++index) {
        const auto& source = graph.lanes[index];
        if (!source.enabled) continue;
        for (std::size_t peer = 0U; peer < graph.lanes.size(); ++peer) {
            const auto& peer_lane = graph.lanes[peer];
            if (peer_lane.enabled && peer_lane.output_group == source.output_group) {
                group_max_latency[index] = std::max(group_max_latency[index],
                                                    peer_lane.reported_latency_samples);
            }
        }
    }
    for (std::size_t index = 0; index < graph.lanes.size(); ++index) {
        const auto& source = graph.lanes[index];
        auto& target = compiled.lanes[index];
        target.input_channels = source.channel_count;
        target.channel_map = source.channel_map;
        target.matrix_enabled = source.matrix_enabled;
        target.channel_matrix = source.channel_matrix;
        target.output_group_bytes = static_cast<std::uint8_t>(source.output_group.size());
        std::copy(source.output_group.begin(), source.output_group.end(), target.output_group.begin());
        target.enabled = source.enabled;
        target.reported_latency_samples = source.enabled ? source.reported_latency_samples : 0U;
        target.compensation_delay_samples = source.enabled
                                                 ? group_max_latency[index] -
                                                       target.reported_latency_samples
                                                 : 0U;
        target.makeup_gain_linear = static_cast<float>(
            std::pow(10.0, source.makeup_gain_db / 20.0));
    }
    snapshot = compiled;
    return true;
}

namespace {

bool process_graph_filtered(const RtGraphSnapshotV1& snapshot,
                            const std::string_view output_group,
                            const std::span<const RtLaneInputV1> inputs,
                            float* const output_interleaved,
                            const std::size_t frames,
                            LaneLatencyBankV1* const latency_bank) noexcept {
    if (snapshot.schema_version != 1 || snapshot.lane_count > kMaxRtLanes ||
        snapshot.output_channels == 0 || snapshot.output_channels > 8 ||
        (snapshot.sample_format != kGraphSampleFormatFloat32V1 &&
         snapshot.sample_format != kGraphSampleFormatFloat64V1) ||
        output_interleaved == nullptr || inputs.size() < snapshot.lane_count) {
        return false;
    }

    std::size_t output_samples = 0U;
    if (!checked_graph_output_samples(frames, snapshot.output_channels, output_samples)) {
        return false;
    }
    // The float graph's optional latency bank owns fixed scratch storage for
    // at most kLaneLatencyMaxFramesV1 frames. Reject an oversized block here,
    // before clearing caller output or invoking the first lane, so the public
    // graph boundary remains all-or-nothing when latency compensation is in
    // use.
    if (latency_bank != nullptr && frames > kLaneLatencyMaxFramesV1) {
        return false;
    }

    // Validate every lane that this render will consume before clearing the
    // caller output or advancing a latency bank. Disabled/malformed lanes
    // retain their existing skip semantics; a valid participating lane must
    // not be allowed to poison the graph with NaN or infinity.
    for (std::size_t lane_index = 0U; lane_index < snapshot.lane_count; ++lane_index) {
        const auto& lane = snapshot.lanes[lane_index];
        const bool is_target_group = output_group.empty() ||
            (lane.output_group_bytes == output_group.size() &&
             std::equal(output_group.begin(), output_group.end(), lane.output_group.begin()));
        if (!lane.enabled || (!is_target_group && latency_bank == nullptr)) continue;
        const auto& input = inputs[lane_index];
        if (input.interleaved == nullptr || input.channel_count != lane.input_channels ||
            input.channel_count == 0U || input.channel_count > 8U) {
            continue;
        }
        if (!finite_interleaved_samples(input.interleaved, frames, input.channel_count)) {
            return false;
        }
    }

    std::fill_n(output_interleaved, output_samples, 0.0F);
    bool matched_output_group = output_group.empty();

    for (std::size_t lane_index = 0; lane_index < snapshot.lane_count; ++lane_index) {
        const auto& lane = snapshot.lanes[lane_index];
        const bool is_target_group = output_group.empty() ||
            (lane.output_group_bytes == output_group.size() &&
             std::equal(output_group.begin(), output_group.end(),
                        lane.output_group.begin()));
        if (!is_target_group) {
            // Every enabled lane must advance its fixed delay clock once per
            // render block, even when it does not mix into the target output
            // group. Otherwise its cross-block ring falls behind the shared
            // audio timeline and returns stale plugin-latency compensation
            // on the next render for its own group.
            if (latency_bank != nullptr && lane.enabled) {
                const auto& background_input = inputs[lane_index];
                if (background_input.interleaved != nullptr &&
                    background_input.channel_count == lane.input_channels &&
                    background_input.channel_count > 0 &&
                    background_input.channel_count <= 8) {
                    if (!latency_bank->process_lane(lane_index,
                                                    background_input.interleaved,
                                                    background_input.channel_count,
                                                    frames)) {
                        return false;
                    }
                }
            }
            continue;
        }
        matched_output_group = true;
        const auto& input = inputs[lane_index];
        if (!lane.enabled || input.interleaved == nullptr ||
            input.channel_count != lane.input_channels || input.channel_count > 8) {
            continue;
        }
        const float* lane_interleaved = input.interleaved;
        if (latency_bank != nullptr) {
            if (!latency_bank->process_lane(lane_index, input.interleaved,
                                            input.channel_count, frames)) {
                return false;
            }
            lane_interleaved = latency_bank->output(lane_index);
            if (lane_interleaved == nullptr) return false;
        }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto* input_frame = lane_interleaved + frame * input.channel_count;
            auto* output_frame = output_interleaved + frame * snapshot.output_channels;
            for (std::uint32_t source_channel = 0; source_channel < input.channel_count;
                 ++source_channel) {
                if (lane.matrix_enabled) {
                    for (std::uint32_t destination = 0U; destination < snapshot.output_channels;
                         ++destination) {
                        output_frame[destination] += input_frame[source_channel] *
                                                     lane.channel_matrix[source_channel][destination] *
                                                     lane.makeup_gain_linear;
                    }
                } else {
                    const auto destination = lane.channel_map[source_channel];
                    if (destination >= 0) {
                        output_frame[static_cast<std::size_t>(destination)] +=
                            input_frame[source_channel] * lane.makeup_gain_linear;
                    }
                }
            }
        }
    }
    return matched_output_group;
}

}  // namespace

namespace {

bool process_graph_filtered_f64(const RtGraphSnapshotV1& snapshot,
                                const std::string_view output_group,
                                const std::span<const RtLaneInputF64V1> inputs,
                                double* const output_interleaved,
                                const std::size_t frames) noexcept {
    if (snapshot.schema_version != 1 || snapshot.lane_count > kMaxRtLanes ||
        snapshot.output_channels == 0 || snapshot.output_channels > 8 ||
        (snapshot.sample_format != kGraphSampleFormatFloat32V1 &&
         snapshot.sample_format != kGraphSampleFormatFloat64V1)) {
        return false;
    }
    if (output_interleaved == nullptr || inputs.size() < snapshot.lane_count) {
        return false;
    }

    std::size_t output_samples = 0U;
    if (!checked_graph_output_samples(frames, snapshot.output_channels, output_samples)) {
        return false;
    }

    // The f64 entry points have no latency bank to perform this validation on;
    // preflight their participating lane inputs before touching caller output.
    for (std::size_t lane_index = 0U; lane_index < snapshot.lane_count; ++lane_index) {
        const auto& lane = snapshot.lanes[lane_index];
        const bool is_target_group = output_group.empty() ||
            (lane.output_group_bytes == output_group.size() &&
             std::equal(output_group.begin(), output_group.end(), lane.output_group.begin()));
        if (!lane.enabled || !is_target_group) continue;
        const auto& input = inputs[lane_index];
        if (input.interleaved == nullptr || input.channel_count != lane.input_channels ||
            input.channel_count == 0U || input.channel_count > 8U) {
            continue;
        }
        if (!finite_interleaved_samples(input.interleaved, frames, input.channel_count)) {
            return false;
        }
    }

    std::fill_n(output_interleaved, output_samples, 0.0);
    bool matched_output_group = output_group.empty();

    for (std::size_t lane_index = 0; lane_index < snapshot.lane_count; ++lane_index) {
        const auto& lane = snapshot.lanes[lane_index];
        const bool is_target_group = output_group.empty() ||
            (lane.output_group_bytes == output_group.size() &&
             std::equal(output_group.begin(), output_group.end(),
                        lane.output_group.begin()));
        if (!is_target_group) {
            continue;
        }
        matched_output_group = true;
        const auto& input = inputs[lane_index];
        if (!lane.enabled || input.interleaved == nullptr ||
            input.channel_count != lane.input_channels || input.channel_count > 8 ||
            input.channel_count == 0) {
            continue;
        }
        const auto makeup_gain_linear =
            static_cast<double>(lane.makeup_gain_linear);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto* input_frame = input.interleaved +
                                      frame * static_cast<std::size_t>(input.channel_count);
            auto* output_frame = output_interleaved +
                                 frame * static_cast<std::size_t>(snapshot.output_channels);
            for (std::uint32_t source_channel = 0; source_channel < input.channel_count;
                 ++source_channel) {
                if (lane.matrix_enabled) {
                    for (std::uint32_t destination = 0U; destination < snapshot.output_channels;
                         ++destination) {
                        output_frame[destination] += input_frame[source_channel] *
                            static_cast<double>(lane.channel_matrix[source_channel][destination]) *
                            makeup_gain_linear;
                    }
                } else {
                    const auto destination = lane.channel_map[source_channel];
                    if (destination >= 0) {
                        output_frame[static_cast<std::size_t>(destination)] +=
                            input_frame[source_channel] * makeup_gain_linear;
                    }
                }
            }
        }
    }
    return matched_output_group;
}

}  // namespace

bool process_graph(const RtGraphSnapshotV1& snapshot,
                   const std::span<const RtLaneInputV1> inputs,
                   float* const output_interleaved,
                   const std::size_t frames,
                   LaneLatencyBankV1* const latency_bank) noexcept {
    return process_graph_filtered(snapshot, {}, inputs, output_interleaved, frames, latency_bank);
}

bool process_graph_for_output_group(const RtGraphSnapshotV1& snapshot,
                                    const std::string_view output_group,
                                    const std::span<const RtLaneInputV1> inputs,
                                    float* const output_interleaved,
                                    const std::size_t frames,
                                    LaneLatencyBankV1* const latency_bank) noexcept {
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos) {
        return false;
    }
    return process_graph_filtered(snapshot, output_group, inputs, output_interleaved, frames,
                                  latency_bank);
}

bool process_graph_f64(const RtGraphSnapshotV1& snapshot,
                       const std::span<const RtLaneInputF64V1> inputs,
                       double* const output_interleaved,
                       const std::size_t frames) noexcept {
    return process_graph_filtered_f64(snapshot, {}, inputs, output_interleaved, frames);
}

bool process_graph_for_output_group_f64(const RtGraphSnapshotV1& snapshot,
                                        const std::string_view output_group,
                                        const std::span<const RtLaneInputF64V1> inputs,
                                        double* const output_interleaved,
                                        const std::size_t frames) noexcept {
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos) {
        return false;
    }
    return process_graph_filtered_f64(snapshot, output_group, inputs, output_interleaved,
                                      frames);
}

}  // namespace hibiki
