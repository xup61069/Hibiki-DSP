#include "hibiki/scene_graph.hpp"

#include <cmath>
#include <algorithm>
#include <string_view>

namespace hibiki {

bool validate_graph(const GraphConfigV1& graph) noexcept {
    if (graph.schema_version != 1 || graph.lanes.empty() || graph.lanes.size() > kMaxRtLanes ||
        (graph.output_channels != 2 && graph.output_channels != 6 && graph.output_channels != 8)) {
        return false;
    }

    for (std::size_t lane_index = 0; lane_index < graph.lanes.size(); ++lane_index) {
        const auto& lane = graph.lanes[lane_index];
        if (lane.id.empty() || lane.output_group.empty() ||
            lane.output_group.size() > kMaxOutputGroupBytes ||
            lane.output_group.find('\0') != std::string::npos ||
            (lane.channel_count != 2 && lane.channel_count != 6 && lane.channel_count != 8) ||
            !std::isfinite(lane.makeup_gain_db) || lane.makeup_gain_db < -144.0 ||
            lane.makeup_gain_db > 12.0) {
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
        if (graph.strict_direct && std::abs(lane.makeup_gain_db) > 1e-12) {
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
                            const std::size_t frames) noexcept {
    if (snapshot.schema_version != 1 || snapshot.lane_count > kMaxRtLanes ||
        snapshot.output_channels == 0 || snapshot.output_channels > 8 ||
        output_interleaved == nullptr || inputs.size() < snapshot.lane_count) {
        return false;
    }

    const auto output_samples = frames * static_cast<std::size_t>(snapshot.output_channels);
    std::fill_n(output_interleaved, output_samples, 0.0F);
    bool matched_output_group = output_group.empty();

    for (std::size_t lane_index = 0; lane_index < snapshot.lane_count; ++lane_index) {
        const auto& lane = snapshot.lanes[lane_index];
        if (!output_group.empty() &&
            (lane.output_group_bytes != output_group.size() ||
             !std::equal(output_group.begin(), output_group.end(), lane.output_group.begin()))) {
            continue;
        }
        matched_output_group = true;
        const auto& input = inputs[lane_index];
        if (!lane.enabled || input.interleaved == nullptr ||
            input.channel_count != lane.input_channels || input.channel_count > 8) {
            continue;
        }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto* input_frame = input.interleaved + frame * input.channel_count;
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

bool process_graph(const RtGraphSnapshotV1& snapshot,
                   const std::span<const RtLaneInputV1> inputs,
                   float* const output_interleaved,
                   const std::size_t frames) noexcept {
    return process_graph_filtered(snapshot, {}, inputs, output_interleaved, frames);
}

bool process_graph_for_output_group(const RtGraphSnapshotV1& snapshot,
                                    const std::string_view output_group,
                                    const std::span<const RtLaneInputV1> inputs,
                                    float* const output_interleaved,
                                    const std::size_t frames) noexcept {
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos) {
        return false;
    }
    return process_graph_filtered(snapshot, output_group, inputs, output_interleaved, frames);
}

}  // namespace hibiki
