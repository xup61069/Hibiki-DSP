// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_catalog.hpp"

#include <algorithm>

namespace hibiki {
namespace {

bool printable_utf8_bytes(const std::string& value, const std::size_t max_bytes) noexcept {
    if (value.empty() || value.size() > max_bytes) return false;
    for (const unsigned char byte : value) {
        if (byte < 0x20U || byte == 0x7fU) return false;
    }
    return true;
}

bool valid_flow(const PhysicalDeviceFlowV1 flow) noexcept {
    return flow == PhysicalDeviceFlowV1::Render || flow == PhysicalDeviceFlowV1::Capture;
}

bool valid_availability(const PhysicalDeviceAvailabilityV1 availability) noexcept {
    return availability == PhysicalDeviceAvailabilityV1::Active ||
           availability == PhysicalDeviceAvailabilityV1::Disabled ||
           availability == PhysicalDeviceAvailabilityV1::Unplugged ||
           availability == PhysicalDeviceAvailabilityV1::Unknown;
}

bool valid_channels(const std::uint32_t channels) noexcept {
    return channels == 1U || channels == 2U || channels == 6U || channels == 8U;
}

bool valid_rate(const std::uint32_t rate) noexcept {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

}  // namespace

bool validate_physical_device_descriptor_v1(
    const PhysicalDeviceDescriptorV1& descriptor) noexcept {
    return descriptor.schema_version == 1U &&
           printable_utf8_bytes(descriptor.endpoint_id, kPhysicalDeviceEndpointIdMaxBytesV1) &&
           printable_utf8_bytes(descriptor.display_name, kPhysicalDeviceNameMaxBytesV1) &&
           valid_flow(descriptor.flow) && valid_availability(descriptor.availability) &&
           valid_channels(descriptor.channels) && valid_rate(descriptor.sample_rate) &&
           descriptor.buffer_frames >= 16U && descriptor.buffer_frames <= 4096U &&
           (!descriptor.is_default ||
            descriptor.availability == PhysicalDeviceAvailabilityV1::Active);
}

std::size_t PhysicalDeviceCatalogV1::index_of(const std::string& endpoint_id) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (entries_[index].endpoint_id == endpoint_id) return index;
    }
    return size_;
}

const PhysicalDeviceDescriptorV1* PhysicalDeviceCatalogV1::find(
    const std::string& endpoint_id) const noexcept {
    const auto index = index_of(endpoint_id);
    return index < size_ ? &entries_[index] : nullptr;
}

PhysicalDeviceDescriptorV1* PhysicalDeviceCatalogV1::find(
    const std::string& endpoint_id) noexcept {
    const auto index = index_of(endpoint_id);
    return index < size_ ? &entries_[index] : nullptr;
}

void PhysicalDeviceCatalogV1::clear_defaults(
    const PhysicalDeviceFlowV1 flow,
    const std::string* const except_endpoint) noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        auto& entry = entries_[index];
        if (entry.flow == flow &&
            (except_endpoint == nullptr || entry.endpoint_id != *except_endpoint)) {
            entry.is_default = false;
        }
    }
}

PhysicalDeviceCatalogResultV1 PhysicalDeviceCatalogV1::upsert(
    const PhysicalDeviceDescriptorV1& descriptor) noexcept {
    if (!validate_physical_device_descriptor_v1(descriptor)) {
        return PhysicalDeviceCatalogResultV1::InvalidDescriptor;
    }
    const auto existing = index_of(descriptor.endpoint_id);
    try {
        if (existing < size_) {
            const auto old_flow = entries_[existing].flow;
            if (descriptor.last_sequence != 0U &&
                descriptor.last_sequence < entries_[existing].last_sequence) {
                return PhysicalDeviceCatalogResultV1::InvalidState;
            }
            entries_[existing] = descriptor;
            if (descriptor.is_default) clear_defaults(descriptor.flow, &descriptor.endpoint_id);
            if (old_flow != descriptor.flow) clear_defaults(old_flow, nullptr);
            return PhysicalDeviceCatalogResultV1::Replaced;
        }
        if (size_ >= entries_.size()) return PhysicalDeviceCatalogResultV1::CapacityExceeded;
        entries_[size_] = descriptor;
        ++size_;
        if (descriptor.is_default) clear_defaults(descriptor.flow, &descriptor.endpoint_id);
        return PhysicalDeviceCatalogResultV1::Accepted;
    } catch (...) {
        return PhysicalDeviceCatalogResultV1::InvalidDescriptor;
    }
}

PhysicalDeviceCatalogResultV1 PhysicalDeviceCatalogV1::remove(
    const std::string& endpoint_id) noexcept {
    const auto index = index_of(endpoint_id);
    if (index >= size_) return PhysicalDeviceCatalogResultV1::NotFound;
    for (std::size_t next = index + 1U; next < size_; ++next) {
        entries_[next - 1U] = std::move(entries_[next]);
    }
    entries_[size_ - 1U] = PhysicalDeviceDescriptorV1{};
    --size_;
    return PhysicalDeviceCatalogResultV1::Accepted;
}

PhysicalDeviceCatalogResultV1 PhysicalDeviceCatalogV1::set_availability(
    const std::string& endpoint_id,
    const PhysicalDeviceAvailabilityV1 availability,
    const std::uint64_t sequence) noexcept {
    if (!valid_availability(availability)) return PhysicalDeviceCatalogResultV1::InvalidState;
    auto* entry = find(endpoint_id);
    if (entry == nullptr) return PhysicalDeviceCatalogResultV1::NotFound;
    if (sequence != 0U && sequence < entry->last_sequence) {
        return PhysicalDeviceCatalogResultV1::InvalidState;
    }
    entry->availability = availability;
    entry->last_sequence = std::max(entry->last_sequence, sequence);
    if (availability != PhysicalDeviceAvailabilityV1::Active) entry->is_default = false;
    return PhysicalDeviceCatalogResultV1::Accepted;
}

PhysicalDeviceCatalogResultV1 PhysicalDeviceCatalogV1::mark_default(
    const std::string& endpoint_id,
    const PhysicalDeviceFlowV1 flow,
    const std::uint64_t sequence) noexcept {
    if (!valid_flow(flow)) return PhysicalDeviceCatalogResultV1::InvalidState;
    auto* entry = find(endpoint_id);
    if (entry == nullptr) return PhysicalDeviceCatalogResultV1::NotFound;
    if (entry->flow != flow || entry->availability != PhysicalDeviceAvailabilityV1::Active) {
        return PhysicalDeviceCatalogResultV1::InvalidState;
    }
    if (sequence != 0U && sequence < entry->last_sequence) {
        return PhysicalDeviceCatalogResultV1::InvalidState;
    }
    clear_defaults(flow, &entry->endpoint_id);
    entry->is_default = true;
    entry->last_sequence = std::max(entry->last_sequence, sequence);
    return PhysicalDeviceCatalogResultV1::Accepted;
}

bool PhysicalDeviceCatalogV1::selectable(const std::string& endpoint_id,
                                         const PhysicalDeviceFlowV1 flow) const noexcept {
    const auto* entry = find(endpoint_id);
    return entry != nullptr && entry->flow == flow &&
           entry->availability == PhysicalDeviceAvailabilityV1::Active;
}

const PhysicalDeviceDescriptorV1* PhysicalDeviceCatalogV1::default_device(
    const PhysicalDeviceFlowV1 flow) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (entries_[index].flow == flow && entries_[index].is_default &&
            entries_[index].availability == PhysicalDeviceAvailabilityV1::Active) {
            return &entries_[index];
        }
    }
    return nullptr;
}

void PhysicalDeviceCatalogV1::swap(PhysicalDeviceCatalogV1& other) noexcept {
    entries_.swap(other.entries_);
    std::swap(size_, other.size_);
}

}  // namespace hibiki
