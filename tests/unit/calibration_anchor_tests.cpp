// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/calibration.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using namespace hibiki;

AcousticAnchorV1 make_valid_anchor() {
    AcousticAnchorV1 anchor;
    anchor.schema_version = 1;
    anchor.device_class = AcousticDeviceClass::Speaker;
    anchor.test_signal_dbfs = -20.0;
    anchor.endpoint_gain_db = 0.0;
    anchor.measured_1k_spl_db = 88.0;
    anchor.uncertainty_db = 1.5;
    anchor.measured_f3_hz = 45.0;
    return anchor;
}
}  // namespace

int main() {
    const auto qnan_d = std::numeric_limits<double>::quiet_NaN();
    const auto inf_d = std::numeric_limits<double>::infinity();

    // ---- validate_acoustic_anchor: schema -----------------------------------
    {
        auto anchor = make_valid_anchor();
        CHECK(validate_acoustic_anchor(anchor));
        anchor.device_class = static_cast<AcousticDeviceClass>(0xFFU);
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.schema_version = 2;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.schema_version = 0;
        CHECK(!validate_acoustic_anchor(anchor));
    }

    // ---- validate_acoustic_anchor: test_signal_dbfs bounds ------------------
    {
        auto anchor = make_valid_anchor();
        anchor.test_signal_dbfs = 0.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.test_signal_dbfs = -144.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.test_signal_dbfs = 0.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.test_signal_dbfs = -144.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.test_signal_dbfs = qnan_d;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.test_signal_dbfs = inf_d;
        CHECK(!validate_acoustic_anchor(anchor));
    }

    // ---- validate_acoustic_anchor: endpoint_gain_db bounds ------------------
    {
        auto anchor = make_valid_anchor();
        anchor.endpoint_gain_db = 12.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.endpoint_gain_db = -144.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.endpoint_gain_db = 12.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.endpoint_gain_db = -144.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.endpoint_gain_db = qnan_d;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.endpoint_gain_db = -inf_d;
        CHECK(!validate_acoustic_anchor(anchor));
    }

    // ---- validate_acoustic_anchor: measured_1k_spl_db bounds ----------------
    {
        auto anchor = make_valid_anchor();
        anchor.measured_1k_spl_db = 0.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.measured_1k_spl_db = 140.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.measured_1k_spl_db = 140.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.measured_1k_spl_db = -0.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.measured_1k_spl_db = qnan_d;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.measured_1k_spl_db = inf_d;
        CHECK(!validate_acoustic_anchor(anchor));
    }

    // ---- validate_acoustic_anchor: uncertainty_db bounds --------------------
    {
        auto anchor = make_valid_anchor();
        anchor.uncertainty_db = 0.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.uncertainty_db = 36.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.uncertainty_db = 36.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.uncertainty_db = -0.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.uncertainty_db = qnan_d;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.uncertainty_db = inf_d;
        CHECK(!validate_acoustic_anchor(anchor));
    }

    // ---- validate_acoustic_anchor: measured_f3_hz bounds --------------------
    {
        auto anchor = make_valid_anchor();
        anchor.measured_f3_hz = 0.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.measured_f3_hz = 20000.0;
        CHECK(validate_acoustic_anchor(anchor));
        anchor.measured_f3_hz = 20000.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.measured_f3_hz = -0.0001;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.measured_f3_hz = qnan_d;
        CHECK(!validate_acoustic_anchor(anchor));
        anchor.measured_f3_hz = inf_d;
        CHECK(!validate_acoustic_anchor(anchor));
    }

    // ---- estimate_phon: fail-closed defaults ---------------------------------
    {
        const PhonEstimateV1 fallback{};
        CHECK(fallback.phon == 0.0);
        CHECK(fallback.uncertainty_db == 0.0);
        CHECK(!fallback.calibrated);

        auto anchor = make_valid_anchor();
        anchor.uncertainty_db = 36.0;
        const auto bad_anchor = estimate_phon(anchor, qnan_d, 0.0);
        CHECK(bad_anchor.phon == 0.0 && bad_anchor.uncertainty_db == 0.0 &&
              !bad_anchor.calibrated);
        const auto bad_gain = estimate_phon(anchor, -20.0, inf_d);
        CHECK(bad_gain.phon == 0.0 && !bad_gain.calibrated);

        anchor.schema_version = 3;
        const auto invalid_schema = estimate_phon(anchor, -20.0, 0.0);
        CHECK(invalid_schema.phon == 0.0 && invalid_schema.uncertainty_db == 0.0 &&
              !invalid_schema.calibrated);

        anchor.schema_version = 1;
        anchor.device_class = static_cast<AcousticDeviceClass>(0xFFU);
        const auto invalid_device_class = estimate_phon(anchor, -20.0, 0.0);
        CHECK(invalid_device_class.phon == 0.0 &&
              invalid_device_class.uncertainty_db == 0.0 &&
              !invalid_device_class.calibrated);
    }

    // ---- estimate_phon: linear model ----------------------------------------
    {
        const auto anchor = make_valid_anchor();
        const auto reference = estimate_phon(anchor, -20.0, 0.0);
        CHECK(reference.phon == 88.0);
        CHECK(reference.uncertainty_db == 1.5);
        CHECK(reference.calibrated);

        const auto shifted = estimate_phon(anchor, -14.0, -3.0);
        CHECK(std::abs(shifted.phon - 91.0) < 1e-12);
        CHECK(shifted.uncertainty_db == 1.5);

        const auto quiet = estimate_phon(anchor, -30.0, 4.0);
        CHECK(std::abs(quiet.phon - 82.0) < 1e-12);
    }

    // ---- estimate_phon: calibrated device classes ---------------------------
    {
        auto anchor = make_valid_anchor();
        anchor.device_class = AcousticDeviceClass::Speaker;
        CHECK(estimate_phon(anchor, -20.0, 0.0).calibrated);
        anchor.device_class = AcousticDeviceClass::HeadphoneCoupler;
        CHECK(estimate_phon(anchor, -20.0, 0.0).calibrated);
        anchor.device_class = AcousticDeviceClass::HeadphoneEstimated;
        const auto estimated = estimate_phon(anchor, -20.0, 0.0);
        CHECK(!estimated.calibrated);
        CHECK(estimated.phon == 88.0);
        CHECK(estimated.uncertainty_db == 1.5);
    }

    std::fputs("calibration_anchor_tests passed\n", stdout);
    return 0;
}
