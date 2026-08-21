#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hibiki {

// Control-plane catalog for physical endpoints discovered by the Windows
// device worker. It intentionally contains no COM types so tests and future
// platform adapters can share the same validation and selection semantics.
enum class PhysicalDeviceFlowV1 : std::uint8_t {
    Render = 0,
    Capture = 1,
};

enum class PhysicalDeviceAvailabilityV1 : std::uint8_t {
    Active = 0,
    Disabled = 1,
    Unplugged = 2,
    Unknown = 3,
};

struct PhysicalDeviceDescriptorV1 {
    std::uint32_t schema_version{1};
    std::string endpoint_id;
    std::string display_name;
    PhysicalDeviceFlowV1 flow{PhysicalDeviceFlowV1::Render};
    PhysicalDeviceAvailabilityV1 availability{PhysicalDeviceAvailabilityV1::Unknown};
    std::uint32_t channels{2};
    std::uint32_t sample_rate{48000};
    std::uint32_t buffer_frames{128};
    bool is_default{false};
    std::uint64_t last_sequence{0};
};

enum class PhysicalDeviceCatalogResultV1 : std::uint8_t {
    Accepted,
    Replaced,
    InvalidDescriptor,
    CapacityExceeded,
    NotFound,
    InvalidState,
};

constexpr std::size_t kPhysicalDeviceCatalogCapacityV1 = 32U;
constexpr std::size_t kPhysicalDeviceEndpointIdMaxBytesV1 = 260U;
constexpr std::size_t kPhysicalDeviceNameMaxBytesV1 = 128U;

[[nodiscard]] bool validate_physical_device_descriptor_v1(
    const PhysicalDeviceDescriptorV1& descriptor) noexcept;

class PhysicalDeviceCatalogV1 final {
public:
    PhysicalDeviceCatalogV1() noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const PhysicalDeviceDescriptorV1* find(
        const std::string& endpoint_id) const noexcept;
    [[nodiscard]] PhysicalDeviceDescriptorV1* find(
        const std::string& endpoint_id) noexcept;

    // Upsert is control-plane only. A failed allocation or invalid descriptor
    // leaves the previous catalog untouched.
    [[nodiscard]] PhysicalDeviceCatalogResultV1 upsert(
        const PhysicalDeviceDescriptorV1& descriptor) noexcept;
    [[nodiscard]] PhysicalDeviceCatalogResultV1 remove(
        const std::string& endpoint_id) noexcept;
    [[nodiscard]] PhysicalDeviceCatalogResultV1 set_availability(
        const std::string& endpoint_id,
        PhysicalDeviceAvailabilityV1 availability,
        std::uint64_t sequence) noexcept;
    [[nodiscard]] PhysicalDeviceCatalogResultV1 mark_default(
        const std::string& endpoint_id,
        PhysicalDeviceFlowV1 flow,
        std::uint64_t sequence) noexcept;

    [[nodiscard]] bool selectable(const std::string& endpoint_id,
                                  PhysicalDeviceFlowV1 flow) const noexcept;
    [[nodiscard]] const PhysicalDeviceDescriptorV1* default_device(
        PhysicalDeviceFlowV1 flow) const noexcept;

    // The returned array is stable until the next mutating operation. It is
    // intended for control/UI snapshots, never for an audio callback.
    [[nodiscard]] const std::array<PhysicalDeviceDescriptorV1,
                                   kPhysicalDeviceCatalogCapacityV1>& entries() const noexcept {
        return entries_;
    }

    // Control-plane transaction primitive. Both catalogs remain valid and the
    // swap is non-throwing, so a worker can publish a complete candidate
    // without exposing a partially enumerated list.
    void swap(PhysicalDeviceCatalogV1& other) noexcept;

private:
    [[nodiscard]] std::size_t index_of(const std::string& endpoint_id) const noexcept;
    void clear_defaults(PhysicalDeviceFlowV1 flow,
                        const std::string* except_endpoint) noexcept;

    std::array<PhysicalDeviceDescriptorV1, kPhysicalDeviceCatalogCapacityV1> entries_{};
    std::size_t size_{0};
};

}  // namespace hibiki
