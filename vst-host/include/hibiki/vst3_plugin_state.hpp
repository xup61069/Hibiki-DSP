#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hibiki {

constexpr std::size_t kVst3PluginStateMaxEntriesV1 = 16U;
constexpr std::size_t kVst3PluginStateMaxIdBytesV1 = 64U;
constexpr std::size_t kVst3PluginStateMaxPluginIdBytesV1 = 128U;
constexpr std::size_t kVst3PluginStateMaxBytesV1 = 1024U * 1024U;

struct Vst3PluginStateIdentityV1 {
    std::string plugin_id;
    std::string class_id;
    std::array<std::uint8_t, 32U> module_sha256{};
};

[[nodiscard]] bool validate_vst3_plugin_state_identity_v1(
    const Vst3PluginStateIdentityV1& identity) noexcept;

enum class Vst3PluginStateResultV1 : std::uint8_t {
    ok,
    invalid_argument,
    capacity_exhausted,
    missing,
    identity_mismatch,
    version_mismatch,
    output_too_small,
};

// Private control-plane state store. The bytes are copied into caller-owned
// private storage only; this class has no file/network serializer and must not
// be used to commit third-party state blobs to the public repository.
class Vst3PluginStateStoreV1 final {
public:
    Vst3PluginStateStoreV1() noexcept = default;

    [[nodiscard]] Vst3PluginStateResultV1 capture(
        std::string_view state_id,
        const Vst3PluginStateIdentityV1& identity,
        std::uint32_t state_version,
        std::span<const std::uint8_t> bytes);

    [[nodiscard]] Vst3PluginStateResultV1 restore(
        std::string_view state_id,
        const Vst3PluginStateIdentityV1& expected_identity,
        std::uint32_t expected_state_version,
        std::span<std::uint8_t> destination,
        std::size_t& bytes_written) const noexcept;

    [[nodiscard]] bool remove(std::string_view state_id) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    struct Slot {
        bool occupied{false};
        std::string state_id;
        Vst3PluginStateIdentityV1 identity{};
        std::uint32_t state_version{0U};
        std::vector<std::uint8_t> bytes;
    };

    [[nodiscard]] bool valid_state_id(std::string_view state_id) const noexcept;
    [[nodiscard]] std::size_t find(std::string_view state_id) const noexcept;

    std::array<Slot, kVst3PluginStateMaxEntriesV1> slots_{};
    std::size_t count_{0U};
};

}  // namespace hibiki
