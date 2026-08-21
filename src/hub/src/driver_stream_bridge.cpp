#include "hibiki/driver_stream_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {

bool decode_driver_stream_packet_v1(
    const std::span<const std::uint8_t> packet,
    const std::span<float> sample_storage,
    DriverStreamLaneBlockV1& block) noexcept {
    block = {};
    if (packet.empty() || !hibiki_driver_stream_packet_validate_v1(packet.data(), packet.size()) ||
        packet.size() < sizeof(hibiki_driver_stream_packet_header_v1)) {
        return false;
    }
    hibiki_driver_stream_packet_header_v1 header{};
    std::memcpy(&header, packet.data(), sizeof(header));
    const std::size_t samples = static_cast<std::size_t>(header.frames) * header.channels;
    if (samples == 0U || sample_storage.size() < samples) {
        return false;
    }
    const std::uint8_t* payload = nullptr;
    std::size_t payload_bytes = 0U;
    if (hibiki_driver_stream_packet_payload_v1(packet.data(), packet.size(), &payload,
                                               &payload_bytes) == 0 ||
        payload_bytes != samples * sizeof(float)) {
        return false;
    }
    std::memcpy(sample_storage.data(), payload, payload_bytes);
    for (std::size_t index = 0U; index < samples; ++index) {
        if (!std::isfinite(sample_storage[index])) {
            std::fill(sample_storage.begin(), sample_storage.begin() + samples, 0.0F);
            return false;
        }
    }
    block.interleaved = sample_storage.data();
    std::copy_n(header.endpoint_guid, block.endpoint_guid.size(), block.endpoint_guid.data());
    block.channels = header.channels;
    block.sample_rate = header.sample_rate;
    block.frames = header.frames;
    block.sequence = header.sequence;
    block.generation = header.generation;
    block.flags = header.flags;
    block.packet_type = header.packet_type;
    return true;
}

}  // namespace hibiki
