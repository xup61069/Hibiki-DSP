// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_bus_layout.hpp"

namespace hibiki {
namespace {

bool valid_channels(const std::uint32_t channels) noexcept {
    return channels == 1U || channels == 2U || channels == 6U || channels == 8U;
}

bool valid_input_role(const Vst3BusRoleV1 role) noexcept {
    return role == Vst3BusRoleV1::Main || role == Vst3BusRoleV1::Auxiliary ||
           role == Vst3BusRoleV1::Sidechain;
}

bool valid_output_role(const Vst3BusRoleV1 role) noexcept {
    return role == Vst3BusRoleV1::Main || role == Vst3BusRoleV1::Auxiliary;
}

Vst3BusLayoutResultV1 validate_buses(
    const std::array<Vst3AudioBusV1, kVst3MaxAudioBusesV1>& buses,
    const std::uint32_t count,
    const bool input,
    std::uint32_t& total_channels,
    std::uint32_t& main_count) noexcept {
    for (std::size_t index = 0U; index < buses.size(); ++index) {
        const auto& bus = buses[index];
        if (index >= count) {
            if (bus.role != Vst3BusRoleV1::Unused || bus.active != 0U ||
                bus.reserved != 0U || bus.channels != 0U) {
                return Vst3BusLayoutResultV1::InvalidInactiveBus;
            }
            continue;
        }
        if (bus.reserved != 0U || bus.active > 1U) {
            return Vst3BusLayoutResultV1::InvalidReserved;
        }
        if (bus.active == 0U || !valid_channels(bus.channels)) {
            return bus.active == 0U ? Vst3BusLayoutResultV1::InvalidInactiveBus
                                    : Vst3BusLayoutResultV1::InvalidChannels;
        }
        if ((input && !valid_input_role(bus.role)) ||
            (!input && !valid_output_role(bus.role))) {
            return bus.role == Vst3BusRoleV1::Sidechain
                       ? Vst3BusLayoutResultV1::SidechainOutput
                       : Vst3BusLayoutResultV1::InvalidRole;
        }
        if (bus.role == Vst3BusRoleV1::Main) {
            ++main_count;
            if (index != 0U) return Vst3BusLayoutResultV1::MainNotFirst;
        }
        if (total_channels > kVst3MaxTotalBusChannelsV1 - bus.channels) {
            return Vst3BusLayoutResultV1::TooManyChannels;
        }
        total_channels += bus.channels;
    }
    return Vst3BusLayoutResultV1::Valid;
}

}  // namespace

Vst3BusLayoutResultV1 validate_vst3_bus_layout_v1(
    const Vst3BusLayoutV1& layout) noexcept {
    if (layout.schema_version != 1U) return Vst3BusLayoutResultV1::InvalidSchema;
    if (layout.input_bus_count == 0U || layout.output_bus_count == 0U ||
        layout.input_bus_count > kVst3MaxAudioBusesV1 ||
        layout.output_bus_count > kVst3MaxAudioBusesV1) {
        return Vst3BusLayoutResultV1::InvalidBusCount;
    }
    std::uint32_t input_channels = 0U;
    std::uint32_t output_channels = 0U;
    std::uint32_t input_main_count = 0U;
    std::uint32_t output_main_count = 0U;
    const auto input_result = validate_buses(layout.inputs, layout.input_bus_count, true,
                                              input_channels, input_main_count);
    if (input_result != Vst3BusLayoutResultV1::Valid) return input_result;
    const auto output_result = validate_buses(layout.outputs, layout.output_bus_count, false,
                                               output_channels, output_main_count);
    if (output_result != Vst3BusLayoutResultV1::Valid) return output_result;
    if (input_main_count == 0U) return Vst3BusLayoutResultV1::MissingMainInput;
    if (output_main_count == 0U) return Vst3BusLayoutResultV1::MissingMainOutput;
    if (input_main_count != 1U || output_main_count != 1U) {
        return Vst3BusLayoutResultV1::DuplicateMain;
    }
    return Vst3BusLayoutResultV1::Valid;
}

bool vst3_bus_layout_matches_main_v1(const Vst3BusLayoutV1& layout,
                                     const std::uint32_t input_channels,
                                     const std::uint32_t output_channels) noexcept {
    return validate_vst3_bus_layout_v1(layout) == Vst3BusLayoutResultV1::Valid &&
           input_channels > 0U && output_channels > 0U &&
           layout.inputs[0].channels == input_channels &&
           layout.outputs[0].channels == output_channels;
}

bool vst3_bus_layout_has_sidechain_v1(const Vst3BusLayoutV1& layout) noexcept {
    if (validate_vst3_bus_layout_v1(layout) != Vst3BusLayoutResultV1::Valid) return false;
    for (std::size_t index = 0U; index < layout.input_bus_count; ++index) {
        if (layout.inputs[index].role == Vst3BusRoleV1::Sidechain) return true;
    }
    return false;
}

}  // namespace hibiki
