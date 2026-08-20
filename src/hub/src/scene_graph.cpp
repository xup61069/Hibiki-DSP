#include "hibiki/scene_graph.hpp"

#include <cmath>
#include <algorithm>

namespace hibiki {

bool validate_graph(const GraphConfigV1& graph) noexcept {
    if (graph.schema_version != 1 || graph.lanes.empty() || graph.lanes.size() > kMaxRtLanes ||
        (graph.output_channels != 2 && graph.output_channels != 6 && graph.output_channels != 8)) {
        return false;
    }

    for (std::size_t lane_index = 0; lane_index < graph.lanes.size(); ++lane_index) {
        const auto& lane = graph.lanes[lane_index];
        if (lane.id.empty() || lane.output_group.empty() ||
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
    for (std::size_t index = 0; index < graph.lanes.size(); ++index) {
        const auto& source = graph.lanes[index];
        auto& target = compiled.lanes[index];
        target.input_channels = source.channel_count;
        target.channel_map = source.channel_map;
        target.enabled = source.enabled;
        target.makeup_gain_linear = static_cast<float>(
            std::pow(10.0, source.makeup_gain_db / 20.0));
    }
    snapshot = compiled;
    return true;
}

bool process_graph(const RtGraphSnapshotV1& snapshot,
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

    for (std::size_t lane_index = 0; lane_index < snapshot.lane_count; ++lane_index) {
        const auto& lane = snapshot.lanes[lane_index];
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
                const auto destination = lane.channel_map[source_channel];
                if (destination >= 0) {
                    output_frame[static_cast<std::size_t>(destination)] +=
                        input_frame[source_channel] * lane.makeup_gain_linear;
                }
            }
        }
    }
    return true;
}

}  // namespace hibiki
