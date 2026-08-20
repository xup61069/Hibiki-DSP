#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <span>
#include <cstdint>
#include <string>
#include <vector>

namespace hibiki {

struct PeqFilterV1 {
    double frequency_hz{1000.0};
    double gain_db{0.0};
    double q{1.0};
};

[[nodiscard]] bool validate_peq_filter(const PeqFilterV1& filter) noexcept;
[[nodiscard]] std::string export_equalizer_apo(std::span<const PeqFilterV1> filters);
[[nodiscard]] std::string export_camilladsp_yaml(std::span<const PeqFilterV1> filters);
[[nodiscard]] std::string export_rew_filter_list(std::span<const PeqFilterV1> filters);
[[nodiscard]] std::string export_hibiki_profile(std::span<const PeqFilterV1> filters);
[[nodiscard]] std::vector<std::uint8_t> export_wav_f32_ir(std::span<const float> interleaved_samples,
                                                          std::uint32_t sample_rate,
                                                          std::uint16_t channels);

}  // namespace hibiki
