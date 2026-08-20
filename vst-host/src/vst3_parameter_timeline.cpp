#include "hibiki/vst3_parameter_timeline.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

bool valid_event(const Vst3ParameterTimelineEventV1& event) noexcept {
    return std::isfinite(event.normalized_value) && event.normalized_value >= 0.0 &&
           event.normalized_value <= 1.0;
}

bool comes_before(const Vst3ParameterTimelineEventV1& left,
                  const Vst3ParameterTimelineEventV1& right) noexcept {
    if (left.sample_position != right.sample_position) {
        return left.sample_position < right.sample_position;
    }
    return left.parameter_id < right.parameter_id;
}

}  // namespace

bool validate_vst3_parameter_timeline_v1(
    const Vst3ParameterTimelineSnapshotV1& snapshot) noexcept {
    if (snapshot.schema_version != 1U || snapshot.event_count > kVst3TimelineMaxEventsV1) {
        return false;
    }
    std::array<std::uint32_t, 16U> parameter_ids{};
    std::size_t unique_parameters = 0U;
    for (std::size_t index = 0U; index < snapshot.event_count; ++index) {
        if (!valid_event(snapshot.events[index])) return false;
        if (index != 0U && comes_before(snapshot.events[index], snapshot.events[index - 1U])) {
            return false;
        }
        const auto parameter_id = snapshot.events[index].parameter_id;
        bool known_parameter = false;
        for (std::size_t prior = 0U; prior < unique_parameters; ++prior) {
            known_parameter = known_parameter || parameter_ids[prior] == parameter_id;
        }
        if (!known_parameter) {
            if (unique_parameters >= parameter_ids.size()) return false;
            parameter_ids[unique_parameters++] = parameter_id;
        }
    }
    return true;
}

bool Vst3ParameterTimelineV1::append(const Vst3ParameterTimelineEventV1& event) noexcept {
    if (!valid_event(event) || snapshot_.event_count >= kVst3TimelineMaxEventsV1) return false;
    const auto count = static_cast<std::size_t>(snapshot_.event_count);
    std::size_t insert_at = count;
    while (insert_at > 0U && comes_before(event, snapshot_.events[insert_at - 1U])) {
        snapshot_.events[insert_at] = snapshot_.events[insert_at - 1U];
        --insert_at;
    }
    snapshot_.events[insert_at] = event;
    snapshot_.event_count = static_cast<std::uint32_t>(count + 1U);
    if (!validate_vst3_parameter_timeline_v1(snapshot_)) {
        (void)erase(insert_at);
        return false;
    }
    return true;
}

bool Vst3ParameterTimelineV1::erase(const std::size_t index) noexcept {
    const auto count = static_cast<std::size_t>(snapshot_.event_count);
    if (index >= count) return false;
    for (std::size_t cursor = index + 1U; cursor < count; ++cursor) {
        snapshot_.events[cursor - 1U] = snapshot_.events[cursor];
    }
    snapshot_.events[count - 1U] = {};
    snapshot_.event_count = static_cast<std::uint32_t>(count - 1U);
    return true;
}

void Vst3ParameterTimelineV1::clear() noexcept { snapshot_ = {}; }

bool Vst3ParameterTimelineV1::collect_block(
    const std::uint64_t block_start,
    const std::uint32_t frames,
    const std::span<Vst3WorkerParameterPointV1> destination,
    std::size_t& count) const noexcept {
    count = 0U;
    if (frames == 0U || frames > kVst3TimelineMaxBlockFramesV1 ||
        !validate_vst3_parameter_timeline_v1(snapshot_)) {
        return false;
    }
    const auto block_end = block_start + static_cast<std::uint64_t>(frames);
    if (block_end < block_start) return false;
    for (std::size_t index = 0U; index < snapshot_.event_count; ++index) {
        const auto& event = snapshot_.events[index];
        if (event.sample_position < block_start) continue;
        if (event.sample_position >= block_end) break;
        if (count >= destination.size() || count >= kVst3WorkerMaxParameterPointsV1) {
            count = 0U;
            return false;
        }
        destination[count++] = Vst3WorkerParameterPointV1{
            event.parameter_id,
            static_cast<std::int32_t>(event.sample_position - block_start),
            event.normalized_value};
    }
    return true;
}

}  // namespace hibiki
