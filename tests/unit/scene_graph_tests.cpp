// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_graph.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::compile_rt_snapshot;
using hibiki::GraphConfigV1;
using hibiki::kGraphSampleFormatFloat64V1;
using hibiki::kLaneLatencyMaxSamplesV1;
using hibiki::kMaxRtLanes;
using hibiki::LaneConfigV1;
using hibiki::process_graph;
using hibiki::process_graph_f64;
using hibiki::process_graph_for_output_group;
using hibiki::process_graph_for_output_group_f64;
using hibiki::RtGraphSnapshotV1;
using hibiki::validate_graph;

LaneConfigV1 make_lane(const std::string& id,
                       const std::string& output_group,
                       std::uint32_t channel_count = 2U) noexcept {
    LaneConfigV1 lane;
    lane.id = id;
    lane.output_group = output_group;
    lane.channel_count = channel_count;
    return lane;
}

GraphConfigV1 stereo_two_lane_graph() noexcept {
    GraphConfigV1 graph;
    graph.lanes.push_back(make_lane("music", "main"));
    auto voice = make_lane("voice", "main");
    voice.channel_map = {0, -1, -1, -1, -1, -1, -1, -1};
    graph.lanes.push_back(voice);
    return graph;
}

}  // namespace

int main() {
    // validate: schema version, empty lanes and unsupported layouts are rejected.
    {
        GraphConfigV1 graph = stereo_two_lane_graph();
        CHECK(validate_graph(graph));
        graph.schema_version = 2U;
        CHECK(!validate_graph(graph));
        graph.schema_version = 1U;
        graph.lanes.clear();
        CHECK(!validate_graph(graph));
        GraphConfigV1 surround = stereo_two_lane_graph();
        surround.output_channels = 4U;
        CHECK(!validate_graph(surround));
        GraphConfigV1 unknown_format = stereo_two_lane_graph();
        unknown_format.sample_format = 7U;
        CHECK(!validate_graph(unknown_format));
    }

    // validate: printable labels, capacity bounds, duplicate ids and latency caps.
    {
        GraphConfigV1 graph = stereo_two_lane_graph();
        graph.lanes[0].id.clear();
        CHECK(!validate_graph(graph));
        graph.lanes[0].id = std::string(65, 'a');
        CHECK(!validate_graph(graph));
        graph.lanes[0].id = "music\t";
        CHECK(!validate_graph(graph));
        graph.lanes[0].id = "music";
        graph.lanes[0].output_group.clear();
        CHECK(!validate_graph(graph));
        graph.lanes[0].output_group = std::string(65, 'g');
        CHECK(!validate_graph(graph));
        graph.lanes[0].output_group = "main";
        CHECK(validate_graph(graph));

        GraphConfigV1 duplicate = stereo_two_lane_graph();
        duplicate.lanes[1].id = "music";
        CHECK(!validate_graph(duplicate));

        GraphConfigV1 too_many_lanes;
        for (std::size_t index = 0; index <= kMaxRtLanes; ++index) {
            too_many_lanes.lanes.push_back(make_lane("lane" + std::to_string(index), "main"));
        }
        CHECK(too_many_lanes.lanes.size() == kMaxRtLanes + 1U);
        CHECK(!validate_graph(too_many_lanes));

        GraphConfigV1 bad_gain = stereo_two_lane_graph();
        bad_gain.lanes[0].makeup_gain_db = 12.5;
        CHECK(!validate_graph(bad_gain));
        bad_gain.lanes[0].makeup_gain_db = std::numeric_limits<double>::quiet_NaN();
        CHECK(!validate_graph(bad_gain));
        bad_gain.lanes[0].makeup_gain_db = -144.0;
        CHECK(validate_graph(bad_gain));

        GraphConfigV1 bad_latency = stereo_two_lane_graph();
        bad_latency.lanes[0].reported_latency_samples = kLaneLatencyMaxSamplesV1 + 1U;
        CHECK(!validate_graph(bad_latency));

        GraphConfigV1 bad_channel_count = stereo_two_lane_graph();
        bad_channel_count.lanes[0].channel_count = 3U;
        CHECK(!validate_graph(bad_channel_count));

        GraphConfigV1 bad_map = stereo_two_lane_graph();
        bad_map.lanes[0].channel_count = 8U;
        bad_map.lanes[0].channel_map = {0, 1, 2, 3, 4, 5, 6, 7};
        CHECK(!validate_graph(bad_map));  // destination 6/7 out of range for stereo output
    }

    // validate: strict_direct forbids matrices, makeup gain and plugin latency; matrix gains are bounded.
    {
        GraphConfigV1 strict = stereo_two_lane_graph();
        strict.strict_direct = true;
        CHECK(validate_graph(strict));
        strict.lanes[0].makeup_gain_db = -3.0;
        CHECK(!validate_graph(strict));
        strict.lanes[0].makeup_gain_db = 0.0;
        strict.lanes[0].reported_latency_samples = 12U;
        CHECK(!validate_graph(strict));
        strict.lanes[0].reported_latency_samples = 0U;
        strict.lanes[0].matrix_enabled = true;
        CHECK(!validate_graph(strict));

        GraphConfigV1 matrix = stereo_two_lane_graph();
        matrix.lanes[0].matrix_enabled = true;
        matrix.lanes[0].channel_matrix[0][0] = 8.5F;
        CHECK(!validate_graph(matrix));
        matrix.lanes[0].channel_matrix[0][0] = 8.0F;
        CHECK(validate_graph(matrix));
    }

    // compile: group compensation delay equals group max minus own reported latency; disabled lanes are excluded.
    {
        GraphConfigV1 graph = stereo_two_lane_graph();
        graph.lanes[0].reported_latency_samples = 32U;
        graph.lanes[1].reported_latency_samples = 96U;
        RtGraphSnapshotV1 snapshot;
        CHECK(compile_rt_snapshot(graph, 41U, snapshot));
        CHECK(snapshot.revision == 41U && snapshot.lane_count == 2U);
        CHECK(snapshot.lanes[0].compensation_delay_samples == 64U);
        CHECK(snapshot.lanes[1].compensation_delay_samples == 0U);
        CHECK(snapshot.sample_format == hibiki::kGraphSampleFormatFloat32V1);

        graph.lanes[1].enabled = false;
        CHECK(compile_rt_snapshot(graph, 42U, snapshot));
        CHECK(snapshot.lanes[0].compensation_delay_samples == 0U);
        CHECK(!snapshot.lanes[1].enabled);
        CHECK(snapshot.lanes[1].compensation_delay_samples == 0U);
        CHECK(snapshot.lanes[1].reported_latency_samples == 0U);
        GraphConfigV1 bad_latency = stereo_two_lane_graph();
        bad_latency.lanes[0].reported_latency_samples = kLaneLatencyMaxSamplesV1 + 1U;
        CHECK(!compile_rt_snapshot(bad_latency, 43U, snapshot));
    }

    // Render entry points reject frame geometry that cannot safely address
    // the maximum supported eight-channel interleaved lane/output buffers.
    {
        constexpr std::size_t kOverflowFrames =
            std::numeric_limits<std::size_t>::max() / 8U + 1U;
        const std::array<float, 2> input{1.0F, 2.0F};
        const std::array<hibiki::RtLaneInputV1, 2> inputs{{
            {input.data(), 2U},
            {input.data(), 2U},
        }};
        RtGraphSnapshotV1 snapshot;
        CHECK(compile_rt_snapshot(stereo_two_lane_graph(), 1U, snapshot));
        std::array<float, 2> rendered{9.0F, 9.0F};
        CHECK(!process_graph(snapshot,
                             std::span<const hibiki::RtLaneInputV1>(inputs),
                             rendered.data(), kOverflowFrames));
        CHECK(rendered[0] == 9.0F && rendered[1] == 9.0F);
        CHECK(!process_graph_for_output_group(
            snapshot, "main", std::span<const hibiki::RtLaneInputV1>(inputs),
            rendered.data(), kOverflowFrames));
        CHECK(rendered[0] == 9.0F && rendered[1] == 9.0F);

        const std::array<double, 2> input_f64{1.0, 2.0};
        const std::array<hibiki::RtLaneInputF64V1, 2> inputs_f64{{
            {input_f64.data(), 2U},
            {input_f64.data(), 2U},
        }};
        std::array<double, 2> rendered_f64{9.0, 9.0};
        CHECK(!process_graph_f64(
            snapshot, std::span<const hibiki::RtLaneInputF64V1>(inputs_f64),
            rendered_f64.data(), kOverflowFrames));
        CHECK(rendered_f64[0] == 9.0 && rendered_f64[1] == 9.0);
        CHECK(!process_graph_for_output_group_f64(
            snapshot, "main", std::span<const hibiki::RtLaneInputF64V1>(inputs_f64),
            rendered_f64.data(), kOverflowFrames));
        CHECK(rendered_f64[0] == 9.0 && rendered_f64[1] == 9.0);
    }

    // render f32: identity map sums both lanes into the shared group; muted channels are skipped.
    {
        const std::array<float, 4> music_input{1.0F, 2.0F, 3.0F, 4.0F};
        const std::array<float, 4> voice_input{10.0F, 20.0F, 30.0F, 40.0F};
        const std::array<hibiki::RtLaneInputV1, 2> inputs{{
            {music_input.data(), 2U},
            {voice_input.data(), 2U},
        }};
        RtGraphSnapshotV1 snapshot;
        GraphConfigV1 graph = stereo_two_lane_graph();
        CHECK(compile_rt_snapshot(graph, 1U, snapshot));
        std::array<float, 8> rendered{};
        CHECK(process_graph(snapshot, std::span<const hibiki::RtLaneInputV1>(inputs), rendered.data(), 2U));
        CHECK(std::fabs(rendered[0] - 11.0F) < 1e-5F);
        CHECK(std::fabs(rendered[1] - 2.0F) < 1e-5F);
        CHECK(std::fabs(rendered[2] - 33.0F) < 1e-5F);
        CHECK(std::fabs(rendered[3] - 4.0F) < 1e-5F);
        CHECK(!process_graph(snapshot, std::span<const hibiki::RtLaneInputV1>(inputs), nullptr, 2U));
    }

    // render f32: output-group filter mixes only lanes of that group and fails closed on missing groups.
    {
        GraphConfigV1 graph;
        graph.lanes.push_back(make_lane("music", "headphones"));
        graph.lanes.push_back(make_lane("voice", "speakers"));
        RtGraphSnapshotV1 snapshot;
        CHECK(compile_rt_snapshot(graph, 2U, snapshot));
        const std::array<float, 2> music_input{1.0F, 2.0F};
        const std::array<float, 2> voice_input{10.0F, 20.0F};
        const std::array<hibiki::RtLaneInputV1, 2> inputs{{
            {music_input.data(), 2U},
            {voice_input.data(), 2U},
        }};
        std::array<float, 4> rendered{};
        CHECK(process_graph_for_output_group(snapshot, "headphones",
                                             std::span<const hibiki::RtLaneInputV1>(inputs),
                                             rendered.data(), 1U));
        CHECK(std::fabs(rendered[0] - 1.0F) < 1e-5F);
        CHECK(std::fabs(rendered[1] - 2.0F) < 1e-5F);
        CHECK(!process_graph_for_output_group(snapshot, "missing",
                                              std::span<const hibiki::RtLaneInputV1>(inputs),
                                              rendered.data(), 1U));
        CHECK(!process_graph_for_output_group(snapshot, {},
                                              std::span<const hibiki::RtLaneInputV1>(inputs),
                                              rendered.data(), 1U));
    }

    // render f64: double path accumulates in double and honors sample-format validation.
    {
        GraphConfigV1 graph;
        auto lane_a = make_lane("a", "main");
        lane_a.channel_count = 8U;
        lane_a.channel_map = {-1, -1, -1, -1, 4, 5, 6, 7};
        auto lane_b = make_lane("b", "aux");
        lane_b.channel_count = 8U;
        lane_b.matrix_enabled = true;
        lane_b.channel_matrix[0][0] = 0.5F;
        graph.lanes.push_back(lane_a);
        graph.lanes.push_back(lane_b);
        graph.output_channels = 8U;
        graph.sample_format = kGraphSampleFormatFloat64V1;
        RtGraphSnapshotV1 snapshot;
        CHECK(compile_rt_snapshot(graph, 3U, snapshot));

        std::array<double, 16> a_input{};
        for (std::size_t index = 0; index < a_input.size(); ++index) {
            a_input[index] = static_cast<double>(index) + 1.0;
        }
        std::array<double, 16> b_input{};
        b_input[0] = 10.0;
        b_input[1] = 20.0;
        b_input[8] = 30.0;
        const std::array<hibiki::RtLaneInputF64V1, 2> inputs{{
            {a_input.data(), 8U},
            {b_input.data(), 8U},
        }};
        std::array<double, 16> rendered{};
        CHECK(process_graph_f64(snapshot, std::span<const hibiki::RtLaneInputF64V1>(inputs),
                                rendered.data(), 2U));
        // main lane: input ch4..7 -> output ch4..7; aux lane matrix: in ch0 -> out0 * 0.5.
        CHECK(std::fabs(rendered[0] - 5.0) < 1e-9);
        CHECK(std::fabs(rendered[1] - 0.0) < 1e-9);
        CHECK(std::fabs(rendered[4] - 5.0) < 1e-9);
        CHECK(std::fabs(rendered[5] - 6.0) < 1e-9);
        CHECK(std::fabs(rendered[6] - 7.0) < 1e-9);
        CHECK(std::fabs(rendered[7] - 8.0) < 1e-9);
        CHECK(std::fabs(rendered[8] - 15.0) < 1e-9);  // frame 1: aux ch0 (30) * 0.5
        CHECK(std::fabs(rendered[12] - 13.0) < 1e-9);

        std::array<double, 16> aux_rendered{};
        CHECK(process_graph_for_output_group_f64(snapshot, "aux",
                                                 std::span<const hibiki::RtLaneInputF64V1>(inputs),
                                                 aux_rendered.data(), 2U));
        CHECK(std::fabs(aux_rendered[0] - 5.0) < 1e-9);
        CHECK(std::fabs(aux_rendered[8] - 15.0) < 1e-9);
        CHECK(std::fabs(aux_rendered[4] - 0.0) < 1e-9);
        CHECK(!process_graph_for_output_group_f64(snapshot, "nope",
                                                  std::span<const hibiki::RtLaneInputF64V1>(inputs),
                                                  aux_rendered.data(), 2U));

        GraphConfigV1 bad_format = graph;
        bad_format.sample_format = 3U;
        RtGraphSnapshotV1 unused;
        CHECK(!validate_graph(bad_format));
        CHECK(!compile_rt_snapshot(bad_format, 4U, unused));
    }

    return 0;
}
