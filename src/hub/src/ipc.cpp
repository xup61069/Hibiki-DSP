#include "hibiki/ipc.hpp"

#include <algorithm>

namespace hibiki {
namespace {

constexpr std::size_t kHeaderBytes = 20;

void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    std::uint32_t value = 0;
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* bytes) noexcept {
    std::uint64_t value = 0;
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

}  // namespace

bool is_valid_message_type(const IpcMessageType type) noexcept {
    switch (type) {
        case IpcMessageType::Hello:
        case IpcMessageType::VolumeNotification:
        case IpcMessageType::GraphPrepare:
        case IpcMessageType::GraphCommit:
        case IpcMessageType::GraphRollback:
        case IpcMessageType::Ack:
        case IpcMessageType::Error:
        case IpcMessageType::SceneApply:
        case IpcMessageType::DeviceSwitch:
        case IpcMessageType::DeviceCatalogSnapshot:
        case IpcMessageType::DeviceCatalogRequest:
        case IpcMessageType::ControlStatusSnapshot:
        case IpcMessageType::ControlStatusRequest:
        case IpcMessageType::SessionCatalogSnapshot:
        case IpcMessageType::SessionCatalogRequest:
        case IpcMessageType::SessionVolumeCommand:
        case IpcMessageType::SessionRouteCommand:
            return true;
    }
    return false;
}

std::vector<std::uint8_t> encode_ipc_frame(const IpcFrameV1& frame) {
    if (frame.payload.size() > kIpcMaxPayloadBytes || !is_valid_message_type(frame.header.type)) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderBytes + frame.payload.size());
    append_u32(bytes, kIpcMagicV1);
    append_u16(bytes, kIpcVersionV1);
    append_u16(bytes, static_cast<std::uint16_t>(frame.header.type));
    append_u32(bytes, static_cast<std::uint32_t>(frame.payload.size()));
    append_u64(bytes, frame.header.request_id);
    bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
    return bytes;
}

std::optional<IpcFrameV1> decode_ipc_frame(const std::span<const std::uint8_t> bytes,
                                           IpcDecodeError& error) {
    error = IpcDecodeError::None;
    if (bytes.size() < kHeaderBytes) {
        error = IpcDecodeError::Truncated;
        return std::nullopt;
    }
    const auto* raw = bytes.data();
    if (read_u32(raw) != kIpcMagicV1) {
        error = IpcDecodeError::InvalidMagic;
        return std::nullopt;
    }
    if (read_u16(raw + 4) != kIpcVersionV1) {
        error = IpcDecodeError::UnsupportedVersion;
        return std::nullopt;
    }
    const auto type = static_cast<IpcMessageType>(read_u16(raw + 6));
    if (!is_valid_message_type(type)) {
        error = IpcDecodeError::InvalidType;
        return std::nullopt;
    }
    const auto payload_bytes = read_u32(raw + 8);
    if (payload_bytes > kIpcMaxPayloadBytes) {
        error = IpcDecodeError::OversizedPayload;
        return std::nullopt;
    }
    if (bytes.size() - kHeaderBytes != payload_bytes) {
        error = IpcDecodeError::LengthMismatch;
        return std::nullopt;
    }

    IpcFrameV1 frame;
    frame.header.magic = kIpcMagicV1;
    frame.header.version = kIpcVersionV1;
    frame.header.type = type;
    frame.header.payload_bytes = payload_bytes;
    frame.header.request_id = read_u64(raw + 12);
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes), bytes.end());
    return frame;
}

}  // namespace hibiki
