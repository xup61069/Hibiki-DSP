// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/equal_loudness.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::CompensationResult;
using hibiki::EqualLoudnessContourPoint;
using hibiki::EqualLoudnessFormulaPointV1;
using hibiki::EqualLoudnessMode;
using hibiki::EqualLoudnessPolicyV1;

EqualLoudnessPolicyV1 valid_policy() {
    EqualLoudnessPolicyV1 policy{};
    policy.mode = EqualLoudnessMode::Relative;
    policy.reference_phon = 80.0;
    policy.strength = 1.0;
    policy.max_boost_db = 6.0;
    return policy;
}

std::vector<EqualLoudnessContourPoint> contour_with_anchor(
    const std::vector<double>& frequencies,
    const std::vector<double>& spl_values) {
    std::vector<EqualLoudnessContourPoint> curve;
    curve.reserve(frequencies.size());
    for (std::size_t index = 0; index < frequencies.size(); ++index) {
        curve.push_back(EqualLoudnessContourPoint{frequencies[index], spl_values[index]});
    }
    return curve;
}

}  // namespace

int run_audio_engine_loudness_geometry_tests();

int main() {
    // ---- validate_policy ---------------------------------------------------
    {
        const auto base = valid_policy();
        CHECK(hibiki::validate_policy(base));

        auto bad_schema = base;
        bad_schema.schema_version = 2U;
        CHECK(!hibiki::validate_policy(bad_schema));

        auto bad_standard = base;
        bad_standard.standard = "iso-226";
        CHECK(!hibiki::validate_policy(bad_standard));

        auto bad_mode = base;
        bad_mode.mode = static_cast<EqualLoudnessMode>(99U);
        CHECK(!hibiki::validate_policy(bad_mode));

        auto low_phon = base;
        low_phon.reference_phon = 19.0;
        CHECK(!hibiki::validate_policy(low_phon));

        auto high_phon = base;
        high_phon.reference_phon = 91.0;
        CHECK(!hibiki::validate_policy(high_phon));

        auto negative_strength = base;
        negative_strength.strength = -0.1;
        CHECK(!hibiki::validate_policy(negative_strength));

        auto excessive_strength = base;
        excessive_strength.strength = 1.5;
        CHECK(!hibiki::validate_policy(excessive_strength));

        auto negative_boost = base;
        negative_boost.max_boost_db = -1.0;
        CHECK(!hibiki::validate_policy(negative_boost));

        auto excessive_boost = base;
        excessive_boost.max_boost_db = 13.0;
        CHECK(!hibiki::validate_policy(excessive_boost));

        auto bad_f3 = base;
        bad_f3.measured_f3_hz = -5.0;
        CHECK(!hibiki::validate_policy(bad_f3));

        // An optional anchor ID still has to obey the shared printable UTF-8
        // contract in every mode, not only when calibrated mode consumes it.
        auto valid_ascii_anchor = base;
        valid_ascii_anchor.anchor_id = "living-room";
        CHECK(hibiki::validate_policy(valid_ascii_anchor));

        auto valid_multibyte_anchor = base;
        valid_multibyte_anchor.anchor_id = "\xE5\xAE\x89\xE5\x85\xA8";
        CHECK(hibiki::validate_policy(valid_multibyte_anchor));

        auto control_anchor = base;
        control_anchor.anchor_id = "anchor\n";
        CHECK(!hibiki::validate_policy(control_anchor));
        control_anchor.anchor_id = std::string("anchor") + static_cast<char>(0x7F);
        CHECK(!hibiki::validate_policy(control_anchor));
        control_anchor.anchor_id = std::string("anchor") + static_cast<char>(0x9F);
        CHECK(!hibiki::validate_policy(control_anchor));
        control_anchor.anchor_id = std::string("anchor") + static_cast<char>(0x80);
        CHECK(!hibiki::validate_policy(control_anchor));
        control_anchor.anchor_id = "anchor\xE2\x82";
        CHECK(!hibiki::validate_policy(control_anchor));
        control_anchor.anchor_id = std::string("anchor") + std::string("\0hidden", 7U);
        CHECK(!hibiki::validate_policy(control_anchor));

        auto boundary_anchor = base;
        boundary_anchor.anchor_id = std::string(64, 'a');
        CHECK(hibiki::validate_policy(boundary_anchor));
        boundary_anchor.anchor_id = std::string(65, 'a');
        CHECK(!hibiki::validate_policy(boundary_anchor));

        // Calibrated mode requires a non-empty anchor and the calibrated
        // standard string; Relative mode rejects calibrated=true.
        auto calibrated_empty_anchor = base;
        calibrated_empty_anchor.mode = EqualLoudnessMode::Calibrated;
        calibrated_empty_anchor.standard = "equal-loudness-calibrated";
        CHECK(!hibiki::validate_policy(calibrated_empty_anchor));

        auto calibrated_ok = calibrated_empty_anchor;
        calibrated_ok.anchor_id = "living-room";
        CHECK(hibiki::validate_policy(calibrated_ok));

        auto calibrated_wrong_standard = calibrated_ok;
        calibrated_wrong_standard.standard = "equal-loudness-derived";
        CHECK(!hibiki::validate_policy(calibrated_wrong_standard));

        auto relative_calibrated_flag = base;
        relative_calibrated_flag.calibrated = true;
        CHECK(!hibiki::validate_policy(relative_calibrated_flag));
    }

    // ---- equal_loudness_spl_from_phon --------------------------------------
    {
        const EqualLoudnessFormulaPointV1 point{};  // 1 kHz defaults
        const hibiki::EqualLoudnessFormulaReferenceV1 reference{};

        double spl = 0.0;
        CHECK(hibiki::equal_loudness_spl_from_phon(point, reference, 60.0, spl));
        CHECK(std::isfinite(spl));

        CHECK(!hibiki::equal_loudness_spl_from_phon(point, reference, 19.0, spl));
        CHECK(!hibiki::equal_loudness_spl_from_phon(point, reference, 91.0, spl));

        EqualLoudnessFormulaPointV1 high_band{};
        high_band.frequency_hz = 8000.0;
        CHECK(!hibiki::equal_loudness_spl_from_phon(high_band, reference, 85.0, spl));
        CHECK(hibiki::equal_loudness_spl_from_phon(high_band, reference, 40.0, spl));

        EqualLoudnessFormulaPointV1 out_of_range{};
        out_of_range.frequency_hz = 15000.0;
        CHECK(!hibiki::equal_loudness_spl_from_phon(out_of_range, reference, 60.0, spl));

        EqualLoudnessFormulaPointV1 bad_alpha{};
        bad_alpha.alpha_f = 0.0;
        CHECK(!hibiki::equal_loudness_spl_from_phon(bad_alpha, reference, 60.0, spl));
    }

    // ---- build_compensation ------------------------------------------------
    {
        const std::vector<double> frequencies{20.0, 1000.0, 12500.0};

        // Identity curves: current == reference means zero gain at every point.
        const auto identity_current =
            contour_with_anchor(frequencies, {60.0, 60.0, 60.0});
        const auto identity_reference =
            contour_with_anchor(frequencies, {60.0, 60.0, 60.0});
        const CompensationResult identity =
            hibiki::build_compensation(identity_current, identity_reference, valid_policy());
        CHECK(identity.points.size() == frequencies.size());
        for (const auto& point : identity.points) {
            CHECK(point.gain_db == 0.0);
            CHECK(!point.limited);
        }

        // A uniform offset between curves is normalized away by the 1 kHz anchor.
        const auto shifted_current =
            contour_with_anchor(frequencies, {70.0, 70.0, 70.0});
        const auto unchanged_reference =
            contour_with_anchor(frequencies, {60.0, 60.0, 60.0});
        const CompensationResult normalized =
            hibiki::build_compensation(shifted_current, unchanged_reference, valid_policy());
        CHECK(normalized.points.size() == frequencies.size());
        for (const auto& point : normalized.points) {
            CHECK(point.gain_db == 0.0);
        }
    }
    {
        const std::vector<double> frequencies{100.0, 1000.0, 4000.0};
        const auto current =
            contour_with_anchor(frequencies, {75.0, 65.0, 65.0});
        const auto reference =
            contour_with_anchor(frequencies, {60.0, 60.0, 60.0});

        auto policy = valid_policy();

        // strength scales the relative gain linearly.
        policy.strength = 0.5;
        const CompensationResult half =
            hibiki::build_compensation(current, reference, policy);
        CHECK(half.points[0].gain_db == 5.0);

        policy.strength = 1.0;
        const CompensationResult full =
            hibiki::build_compensation(current, reference, policy);
        CHECK(full.points[0].gain_db == 6.0);
        CHECK(full.points[0].limited);   // capped by max_boost_db
        CHECK(full.points[1].gain_db == 0.0);
        CHECK(full.points[2].gain_db == 0.0);

        // max_boost_db caps positive gain and marks the point limited.
        policy.max_boost_db = 3.0;
        const CompensationResult capped =
            hibiki::build_compensation(current, reference, policy);
        CHECK(capped.points[0].gain_db == 3.0);
        CHECK(capped.points[0].limited);
        CHECK(capped.limited);
    }
    {
        // measured_f3_hz zeroes positive gain below the crossover.
        const std::vector<double> frequencies{50.0, 1000.0, 3000.0};
        const auto current = contour_with_anchor(frequencies, {75.0, 65.0, 70.0});
        const auto reference = contour_with_anchor(frequencies, {60.0, 60.0, 62.0});
        auto policy = valid_policy();
        policy.max_boost_db = 12.0;
        policy.measured_f3_hz = 100.0;
        const CompensationResult f3 =
            hibiki::build_compensation(current, reference, policy);
        CHECK(f3.points[0].gain_db == 0.0);
        CHECK(f3.points[0].limited);
        CHECK(f3.points[1].gain_db == 0.0);
        CHECK(!f3.points[1].limited);
        CHECK(f3.points[2].gain_db == 3.0);
        CHECK(!f3.points[2].limited);
    }
    {
        // Frequencies outside 20 Hz..12.5 kHz are limited to zero gain.
        const std::vector<double> frequencies{10.0, 1000.0};
        const auto current = contour_with_anchor(frequencies, {75.0, 65.0});
        const auto reference = contour_with_anchor(frequencies, {60.0, 60.0});
        const CompensationResult out_of_range =
            hibiki::build_compensation(current, reference, valid_policy());
        CHECK(out_of_range.points[0].limited);
        CHECK(out_of_range.points[0].gain_db == 0.0);
    }

    CHECK(run_audio_engine_loudness_geometry_tests() == 0);

    {
        // Structural rejections keep an empty result with a diagnostic.
        const std::vector<double> frequencies{1000.0};
        const auto current = contour_with_anchor(frequencies, {65.0});
        const auto reference = contour_with_anchor(frequencies, {60.0});

        CHECK(hibiki::build_compensation({}, {}, valid_policy()).points.empty());
        CHECK(hibiki::build_compensation(current, {}, valid_policy()).points.empty());

        std::vector<EqualLoudnessContourPoint> missing_anchor;
        missing_anchor.push_back(EqualLoudnessContourPoint{2000.0, 65.0});
        std::vector<EqualLoudnessContourPoint> reference_missing_anchor;
        reference_missing_anchor.push_back(EqualLoudnessContourPoint{2000.0, 60.0});
        CHECK(hibiki::build_compensation(missing_anchor, reference_missing_anchor,
                                         valid_policy())
                  .points.empty());

        const auto misaligned_reference = contour_with_anchor({1500.0}, {60.0});
        CHECK(hibiki::build_compensation(current, misaligned_reference,
                                         valid_policy())
                  .points.empty());
    }
    // ---- build_formula_compensation ----------------------------------------
    {
        const std::vector<EqualLoudnessFormulaPointV1> points{
            EqualLoudnessFormulaPointV1{100.0, 0.4, 8.0, 0.0},
            EqualLoudnessFormulaPointV1{1000.0, 0.30, 2.4, 0.0},
            EqualLoudnessFormulaPointV1{4000.0, 0.25, 1.0, 0.0},
        };

        auto policy = valid_policy();
        policy.reference_phon = 60.0;

        const CompensationResult result =
            hibiki::build_formula_compensation(points, 70.0, policy);
        CHECK(result.points.size() == points.size());
        for (const auto& point : result.points) {
            CHECK(std::isfinite(point.gain_db));
            CHECK(point.gain_db <= policy.max_boost_db + 1e-9);
        }

        // Missing 1 kHz anchor fails the whole result.
        const std::vector<EqualLoudnessFormulaPointV1> no_anchor{
            EqualLoudnessFormulaPointV1{500.0, 0.35, 5.0, 0.0},
        };
        CHECK(hibiki::build_formula_compensation(no_anchor, 70.0, policy).points.empty());

        // Out-of-domain phon fails the whole result.
        CHECK(hibiki::build_formula_compensation(points, 95.0, policy).points.empty());
    }

    std::fputs("equal loudness tests passed\n", stdout);
    return 0;
}
