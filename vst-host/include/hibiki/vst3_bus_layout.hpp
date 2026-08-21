// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hibiki {

constexpr std::size_t kVst3MaxAudioBusesV1 = 8U;
constexpr std::uint32_t kVst3MaxTotalBusChannelsV1 = 32U;

enum class Vst3BusRoleV1 : std::uint8_t {
    Unused = 0,
    Main = 1,
    Auxiliary = 2,
    Sidechain = 3,
};

struct Vst3AudioBusV1 {
    Vst3BusRoleV1 role{Vst3BusRoleV1::Unused};
    std::uint8_t active{0U};
    std::uint16_t reserved{0U};
    std::uint32_t channels{0U};
};

struct Vst3BusLayoutV1 {
    std::uint32_t schema_version{1U};
    std::uint32_t input_bus_count{0U};
    std::uint32_t output_bus_count{0U};
    std::array<Vst3AudioBusV1, kVst3MaxAudioBusesV1> inputs{};
    std::array<Vst3AudioBusV1, kVst3MaxAudioBusesV1> outputs{};
};

enum class Vst3BusLayoutResultV1 : std::uint8_t {
    Valid,
    InvalidSchema,
    InvalidBusCount,
    MissingMainInput,
    MissingMainOutput,
    MainNotFirst,
    DuplicateMain,
    InvalidRole,
    InvalidChannels,
    InvalidInactiveBus,
    InvalidReserved,
    TooManyChannels,
    SidechainOutput,
};

[[nodiscard]] Vst3BusLayoutResultV1 validate_vst3_bus_layout_v1(
    const Vst3BusLayoutV1& layout) noexcept;

[[nodiscard]] bool vst3_bus_layout_matches_main_v1(
    const Vst3BusLayoutV1& layout,
    std::uint32_t input_channels,
    std::uint32_t output_channels) noexcept;

[[nodiscard]] bool vst3_bus_layout_has_sidechain_v1(
    const Vst3BusLayoutV1& layout) noexcept;

}  // namespace hibiki
