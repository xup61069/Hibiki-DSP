#pragma once

// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hibiki {

constexpr std::uint32_t kIpcMagicV1 = 0x314B4948U; // ASCII "HIK1" in little-endian.
constexpr std::uint16_t kIpcVersionV1 = 1;
constexpr std::size_t kIpcMaxPayloadBytes = 1024U * 1024U;

enum class IpcMessageType : std::uint16_t {
    Hello = 1,
    VolumeNotification = 2,
    GraphPrepare = 3,
    GraphCommit = 4,
    GraphRollback = 5,
    Ack = 6,
    Error = 7,
    SceneApply = 8,
    DeviceSwitch = 9,
    DeviceCatalogSnapshot = 10,
    DeviceCatalogRequest = 11,
    ControlStatusSnapshot = 12,
    ControlStatusRequest = 13,
    SessionCatalogSnapshot = 14,
    SessionCatalogRequest = 15,
    SessionVolumeCommand = 16,
};

struct IpcHeaderV1 {
    std::uint32_t magic{kIpcMagicV1};
    std::uint16_t version{kIpcVersionV1};
    IpcMessageType type{IpcMessageType::Error};
    std::uint32_t payload_bytes{0};
    std::uint64_t request_id{0};
};

struct IpcFrameV1 {
    IpcHeaderV1 header{};
    std::vector<std::uint8_t> payload;
};

enum class IpcDecodeError : std::uint8_t {
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidType,
    OversizedPayload,
    LengthMismatch,
};

[[nodiscard]] std::vector<std::uint8_t> encode_ipc_frame(const IpcFrameV1& frame);
[[nodiscard]] std::optional<IpcFrameV1> decode_ipc_frame(
    std::span<const std::uint8_t> bytes, IpcDecodeError& error);
[[nodiscard]] bool is_valid_message_type(IpcMessageType type) noexcept;

}  // namespace hibiki
