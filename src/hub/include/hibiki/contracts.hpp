#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <string>
#include <vector>

#include "hibiki/ir_phase.hpp"

namespace hibiki {

enum class LatencyMode : std::uint8_t {
    Game,
    Balanced,
    MovieLinearPhase,
    StrictDirect,
};

struct SceneProfileV1 {
    std::uint32_t schema_version{1};
    std::string id;
    std::string name;
    std::vector<std::string> lanes;
    std::string output_group;
    LatencyMode latency_mode{LatencyMode::Game};
    IrPhasePolicyV1 ir_phase{};
    bool auto_attenuate{true};
    double limiter_dbtp{-1.0};
};

struct DistributionProfileV1 {
    std::uint32_t schema_version{1};
    std::string distribution_id;
    std::string driver_hardware_id;
    std::string driver_service;
    std::string endpoint_main_guid;
    std::string endpoint_low_latency_guid;
    std::string endpoint_surround_guid;
    std::string endpoint_virtual_mic_guid;
    std::string asio_clsid;
    std::string ipc_namespace;
};

[[nodiscard]] bool validate_scene(const SceneProfileV1& scene) noexcept;

}  // namespace hibiki
