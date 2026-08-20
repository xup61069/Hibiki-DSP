#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>

namespace hibiki {

enum class AcousticDeviceClass : std::uint8_t {
    Speaker,
    HeadphoneCoupler,
    HeadphoneEstimated,
};

struct AcousticAnchorV1 {
    std::uint32_t schema_version{1};
    AcousticDeviceClass device_class{AcousticDeviceClass::Speaker};
    double test_signal_dbfs{-20.0};
    double endpoint_gain_db{0.0};
    double measured_1k_spl_db{0.0};
    double uncertainty_db{0.0};
    double measured_f3_hz{0.0};
};

struct PhonEstimateV1 {
    double phon{0.0};
    double uncertainty_db{0.0};
    bool calibrated{false};
};

[[nodiscard]] bool validate_acoustic_anchor(const AcousticAnchorV1& anchor) noexcept;
[[nodiscard]] PhonEstimateV1 estimate_phon(const AcousticAnchorV1& anchor,
                                            double current_signal_dbfs,
                                            double current_endpoint_gain_db) noexcept;

}  // namespace hibiki
