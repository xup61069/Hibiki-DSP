#include "hibiki/driver_control_bridge.hpp"

#include <algorithm>

namespace hibiki {
namespace {

constexpr std::int32_t kMinDbQ16_16 = -144 * 65536;
constexpr std::int32_t kMaxDbQ16_16 = 12 * 65536;

bool valid_db_q16_16(const std::int32_t value) noexcept {
    return value >= kMinDbQ16_16 && value <= kMaxDbQ16_16;
}

std::size_t bounded_length(const std::array<char, HIBIKI_ENDPOINT_GUID_CAPACITY>& value) noexcept {
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] == '\0') return index;
    }
    return value.size();
}

std::size_t bounded_length(const std::string_view value) noexcept {
    return value.find('\0') == std::string_view::npos ? value.size() : value.find('\0');
}

}  // namespace

bool decode_driver_endpoint_state_packet_v1(
    const std::span<const std::uint8_t> packet,
    DriverEndpointStateV1& state,
    std::uint16_t& message_type,
    std::uint64_t& request_id) noexcept {
    state = {};
    message_type = 0U;
    request_id = 0U;
    hibiki_driver_endpoint_state_v1 decoded{};
    if (hibiki_driver_endpoint_state_packet_decode_v1(
            packet.data(), packet.size(), &decoded, &message_type, &request_id) == 0) {
        return false;
    }
    std::copy_n(decoded.endpoint_guid, state.endpoint_guid.size(), state.endpoint_guid.begin());
    std::copy_n(decoded.event_context_guid, state.event_context_guid.size(),
                state.event_context_guid.begin());
    state.channel_count = decoded.channel_count;
    state.sample_rate = decoded.sample_rate;
    state.frames_per_buffer = decoded.frames_per_buffer;
    state.requested_db_q16_16 = decoded.requested_db_q16_16;
    state.safety_ceiling_db_q16_16 = decoded.safety_ceiling_db_q16_16;
    state.effective_db_q16_16 = decoded.effective_db_q16_16;
    state.mute = decoded.mute != 0U;
    state.generation = decoded.generation;
    state.actuator = decoded.actuator;
    return true;
}

bool DriverVolumeLinkV1::add_ignored_event_context(const std::string_view context) noexcept {
    const auto length = bounded_length(context);
    if (length == 0U || length >= HIBIKI_ENDPOINT_GUID_CAPACITY) return false;
    const auto normalized = context.substr(0U, length);
    if (is_ignored(normalized)) return true;
    if (ignored_context_count_ >= kMaxIgnoredContexts) return false;
    auto& destination = ignored_contexts_[ignored_context_count_];
    destination.fill('\0');
    std::copy_n(context.data(), length, destination.data());
    ++ignored_context_count_;
    return true;
}

void DriverVolumeLinkV1::clear_ignored_event_contexts() noexcept {
    for (auto& context : ignored_contexts_) context.fill('\0');
    ignored_context_count_ = 0U;
}

bool DriverVolumeLinkV1::is_ignored(const std::string_view context) const noexcept {
    const auto length = bounded_length(context);
    if (length == 0U || length >= HIBIKI_ENDPOINT_GUID_CAPACITY) return false;
    for (std::size_t index = 0U; index < ignored_context_count_; ++index) {
        const std::string_view registered(ignored_contexts_[index].data(), length);
        if (registered == context.substr(0U, length) &&
            ignored_contexts_[index][length] == '\0') {
            return true;
        }
    }
    return false;
}

DriverVolumeSyncResultV1 DriverVolumeLinkV1::apply(
    AudioEngineModel& engine,
    const std::string_view output_group,
    const DriverEndpointStateV1& state) const noexcept {
    if (output_group.empty() || bounded_length(state.endpoint_guid) == 0U ||
        bounded_length(state.endpoint_guid) >= state.endpoint_guid.size() ||
        bounded_length(state.event_context_guid) >= state.event_context_guid.size() ||
        (state.channel_count != 2U && state.channel_count != 6U &&
         state.channel_count != 8U) ||
        (state.sample_rate != 44100U && state.sample_rate != 48000U &&
         state.sample_rate != 96000U && state.sample_rate != 192000U) ||
        state.frames_per_buffer == 0U || state.frames_per_buffer > 4096U ||
        !valid_db_q16_16(state.requested_db_q16_16) ||
        !valid_db_q16_16(state.safety_ceiling_db_q16_16) ||
        !valid_db_q16_16(state.effective_db_q16_16) || state.generation == 0U ||
        state.actuator > HIBIKI_ACTUATOR_STRICT_DIRECT) {
        return DriverVolumeSyncResultV1::Invalid;
    }
    const std::string_view context(state.event_context_guid.data(),
                                   bounded_length(state.event_context_guid));
    if (is_ignored(context)) return DriverVolumeSyncResultV1::IgnoredSelf;
    const VolumeNotificationV1 notification{
        q16_16_to_db(state.requested_db_q16_16), state.mute, state.generation};
    switch (engine.apply_windows_volume(output_group, notification)) {
        case VolumeNotificationResult::Accepted:
            return DriverVolumeSyncResultV1::Applied;
        case VolumeNotificationResult::StaleGeneration:
            return DriverVolumeSyncResultV1::StaleGeneration;
        case VolumeNotificationResult::Invalid:
            return DriverVolumeSyncResultV1::Invalid;
    }
    return DriverVolumeSyncResultV1::Invalid;
}

}  // namespace hibiki
