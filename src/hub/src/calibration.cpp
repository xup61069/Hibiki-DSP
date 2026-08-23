#include "hibiki/calibration.hpp"

#include <cmath>

namespace hibiki {

bool validate_acoustic_anchor(const AcousticAnchorV1& anchor) noexcept {
    return anchor.schema_version == 1 && std::isfinite(anchor.test_signal_dbfs) &&
           std::isfinite(anchor.endpoint_gain_db) &&
           anchor.endpoint_gain_db >= -144.0 && anchor.endpoint_gain_db <= 12.0 &&
           std::isfinite(anchor.measured_1k_spl_db) &&
           anchor.measured_1k_spl_db >= 0.0 && anchor.measured_1k_spl_db <= 140.0 &&
           std::isfinite(anchor.uncertainty_db) && anchor.uncertainty_db >= 0.0 &&
           anchor.uncertainty_db <= 36.0 &&
           std::isfinite(anchor.measured_f3_hz) && anchor.measured_f3_hz >= 0.0 &&
           anchor.measured_f3_hz <= 20000.0 &&
           anchor.test_signal_dbfs <= 0.0 && anchor.test_signal_dbfs >= -144.0;
}

PhonEstimateV1 estimate_phon(const AcousticAnchorV1& anchor,
                             const double current_signal_dbfs,
                             const double current_endpoint_gain_db) noexcept {
    if (!validate_acoustic_anchor(anchor) || !std::isfinite(current_signal_dbfs) ||
        !std::isfinite(current_endpoint_gain_db)) {
        return {};
    }
    PhonEstimateV1 estimate;
    estimate.phon = anchor.measured_1k_spl_db + (current_signal_dbfs - anchor.test_signal_dbfs) +
                    (current_endpoint_gain_db - anchor.endpoint_gain_db);
    estimate.uncertainty_db = anchor.uncertainty_db;
    estimate.calibrated = anchor.device_class != AcousticDeviceClass::HeadphoneEstimated;
    return estimate;
}

}  // namespace hibiki
