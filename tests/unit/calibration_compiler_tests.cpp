// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/calibration_compiler.hpp"
#include "hibiki/exporters.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::CalibrationCompilePolicyV1;
using hibiki::CalibrationCompileResultV1;
using hibiki::CalibrationResponsePointV1;
using hibiki::CalibrationTargetCurveIdV1;
using hibiki::PeqFilterV1;
using hibiki::compile_bounded_peq_correction_v1;
using hibiki::sample_calibration_target_curve_v1;

constexpr CalibrationResponsePointV1 point(const double frequency_hz,
                                            const double measured_db,
                                            const double target_db = 0.0) noexcept
{
    return {frequency_hz, measured_db, target_db};
}

}  // namespace

int main()
{
    // ---- target curve naming and sampling ----------------------------------
    {
        CHECK(hibiki::calibration_target_curve_name_v1(CalibrationTargetCurveIdV1::Flat)
              == std::string_view{"flat"});
        CHECK(hibiki::calibration_target_curve_name_v1(CalibrationTargetCurveIdV1::HarmanInEar)
              == std::string_view{"harman-in-ear"});
        CHECK(hibiki::calibration_target_curve_name_v1(CalibrationTargetCurveIdV1::HarmanOverEar)
              == std::string_view{"harman-over-ear"});
        CHECK(hibiki::calibration_target_curve_name_v1(static_cast<CalibrationTargetCurveIdV1>(99U))
              == std::string_view{""});

        double db = -999.0;
        CHECK(sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::Flat, 20.0, db));
        CHECK(db == 0.0);
        CHECK(sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::Flat, 24000.0, db));
        CHECK(db == 0.0);

        // Out-of-range frequencies and invalid curve IDs fail closed.
        CHECK(!sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::Flat, 9.9, db));
        CHECK(!sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::Flat, 24000.1, db));
        CHECK(!sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::Flat,
                                                  std::nan(""), db));
        CHECK(!sample_calibration_target_curve_v1(static_cast<CalibrationTargetCurveIdV1>(42U),
                                                  1000.0, db));

        // Harman in-ear anchors are interpolated log-frequency between points.
        CHECK(sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::HarmanInEar, 1000.0, db));
        CHECK(std::abs(db - 0.0) < 1e-12);
        CHECK(sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::HarmanInEar, 2000.0, db));
        CHECK(db < 0.0);
        CHECK(sample_calibration_target_curve_v1(CalibrationTargetCurveIdV1::HarmanInEar, 3000.0, db));
        CHECK(db > 3.0);
        CHECK(db < 4.0);
    }

    // ---- compile policy validation ------------------------------------------
    {
        const CalibrationCompilePolicyV1 valid;
        CHECK(hibiki::validate_calibration_compile_policy_v1(valid));

        auto invalid = valid;
        invalid.schema_version = 2U;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.max_filters = 0U;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.min_frequency_hz = 5.0;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.max_frequency_hz = 25000.0;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.min_frequency_hz = invalid.max_frequency_hz;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.max_boost_db = -1.0;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.max_cut_db = 45.0;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.min_q = 0.05;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.max_q = 101.0;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.min_q = invalid.max_q + 1.0;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.min_spacing_octaves = 0.001;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.ignore_error_db = -0.1;
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));

        invalid = valid;
        invalid.max_boost_db = std::nan("");
        CHECK(!hibiki::validate_calibration_compile_policy_v1(invalid));
    }

    // ---- response validation -------------------------------------------------
    const CalibrationCompilePolicyV1 policy{};
    {
        const std::vector<CalibrationResponsePointV1> empty;
        CHECK(!hibiki::validate_calibration_response_v1(empty, policy));

        const std::vector<CalibrationResponsePointV1> below_range{point(15.0, 0.0)};
        CHECK(!hibiki::validate_calibration_response_v1(below_range, policy));

        const std::vector<CalibrationResponsePointV1> above_range{point(21000.0, 0.0)};
        CHECK(!hibiki::validate_calibration_response_v1(above_range, policy));

        const std::vector<CalibrationResponsePointV1> unsorted{
            point(500.0, 0.0), point(400.0, 0.0),
        };
        CHECK(!hibiki::validate_calibration_response_v1(unsorted, policy));

        const std::vector<CalibrationResponsePointV1> duplicate{
            point(500.0, 0.0), point(500.0, 0.0),
        };
        CHECK(!hibiki::validate_calibration_response_v1(duplicate, policy));

        const std::vector<CalibrationResponsePointV1> too_quiet{point(500.0, -145.0)};
        CHECK(!hibiki::validate_calibration_response_v1(too_quiet, policy));

        const std::vector<CalibrationResponsePointV1> too_loud{point(500.0, 13.0)};
        CHECK(!hibiki::validate_calibration_response_v1(too_loud, policy));

        const std::vector<CalibrationResponsePointV1> nan_point{point(500.0, std::nan(""))};
        CHECK(!hibiki::validate_calibration_response_v1(nan_point, policy));

        const std::vector<CalibrationResponsePointV1> good{point(500.0, -1.0), point(1000.0, 0.5)};
        CHECK(hibiki::validate_calibration_response_v1(good, policy));
    }

    // ---- flat response compiles nothing --------------------------------------
    {
        const std::vector<CalibrationResponsePointV1> response{
            point(100.0, 0.0), point(1000.0, 0.0), point(10000.0, 0.0),
        };
        const auto result = compile_bounded_peq_correction_v1(response, policy);
        CHECK(result.filters.empty());
        CHECK(!result.limited);
        CHECK(result.maximum_requested_correction_db == 0.0);
        CHECK(!result.diagnostic.empty());
    }

    // ---- greedy selection picks the largest residual -------------------------
    {
        const std::vector<CalibrationResponsePointV1> response{
            point(200.0, 0.0),   // correction +3 dB
            point(1000.0, -0.1), // correction +0.1 dB (inside ignore threshold)
            point(8000.0, -6.0), // correction +6 dB: must be picked first
        };
        const auto result = compile_bounded_peq_correction_v1(response, policy);
        CHECK(result.filters.size() == 1U);
        CHECK(result.filters[0].frequency_hz == 8000.0);
        CHECK(result.filters[0].gain_db == 6.0);
        CHECK(!result.limited);
        CHECK(result.maximum_requested_correction_db > 5.9);
    }

    // ---- boost is clamped and limited is flagged -----------------------------
    {
        const std::vector<CalibrationResponsePointV1> response{
            point(1000.0, -30.0), // requested +30 dB boost; policy caps at +6 dB
        };
        const auto result = compile_bounded_peq_correction_v1(response, policy);
        CHECK(result.filters.size() == 1U);
        if (!result.filters.empty()) {
            CHECK(result.filters[0].frequency_hz == 1000.0);
            CHECK(result.filters[0].gain_db == 6.0);
        }
        CHECK(result.limited);
    }

    // ---- cut respects max_cut_db ---------------------------------------------
    {
        const std::vector<CalibrationResponsePointV1> response{
            point(500.0, 0.0, -144.0), // correction -144 dB; policy caps at -12 dB
        };
        const auto result = compile_bounded_peq_correction_v1(response, policy);
        CHECK(result.filters.size() == 1U);
        if (!result.filters.empty()) {
            CHECK(result.filters[0].frequency_hz == 500.0);
            CHECK(result.filters[0].gain_db == -12.0);
        }
        CHECK(result.limited);
    }

    // ---- min_spacing prevents adjacent filters --------------------------------
    {
        const std::vector<CalibrationResponsePointV1> response{
            point(980.0, -8.0),
            point(1000.0, -10.0),
        };
        const auto result = compile_bounded_peq_correction_v1(response, policy);
        CHECK(result.filters.size() == 1U);
        CHECK(result.filters[0].frequency_hz == 1000.0);
    }

    // ---- max_filters bounds the filter count ---------------------------------
    {
        CalibrationCompilePolicyV1 bounded_policy{};
        bounded_policy.max_filters = 2U;
        const std::vector<CalibrationResponsePointV1> response{
            point(63.0, -8.0), point(250.0, -8.0), point(1000.0, -8.0),
            point(4000.0, -8.0), point(16000.0, -8.0),
        };
        const auto result = compile_bounded_peq_correction_v1(response, bounded_policy);
        CHECK(result.filters.size() == 2U);
        CHECK(result.limited);
    }

    // ---- Q values stay inside the policy window -------------------------------
    {
        const std::vector<CalibrationResponsePointV1> response{
            point(20.0, -4.0), point(21.0, -4.0), point(22.0, -4.0),
        };
        const auto result = compile_bounded_peq_correction_v1(response, policy);
        CHECK(result.filters.size() >= 1U);
        for (const auto& filter : result.filters) {
            CHECK(filter.q >= policy.min_q);
            CHECK(filter.q <= policy.max_q);
        }
    }

    return 0;
}
