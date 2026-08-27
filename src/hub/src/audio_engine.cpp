#include "hibiki/audio_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>

namespace hibiki {

namespace {

constexpr std::size_t kMaxLoudnessFormulaPointsV1 = 64U;
constexpr std::uint32_t kLoudnessPeqCrossfadeMs = 120U;
constexpr std::size_t kMaxLoudnessPeqCrossfadeFramesV1 =
    static_cast<std::size_t>(192000U) * 120U / 1000U;

struct LoudnessPeqCompileResultV1 {
    PeqFilterV1 filters[kMaxRealtimePeqFiltersV1]{};
    std::size_t filter_count{0U};
};

// Deterministic control-plane conversion from normalized compensation points
// to at most 16 peaking filters. Points must be sorted by frequency and every
// retained point keeps its already policy-limited gain. Q follows neighboring
// octave spacing; a single point falls back to a wide musical band.
bool compile_loudness_peq_v1(const CompensationResult& compensation,
                             LoudnessPeqCompileResultV1& result) noexcept {
    result = {};
    if (compensation.points.empty() || compensation.points.size() > kMaxLoudnessFormulaPointsV1) {
        return false;
    }
    for (std::size_t index = 1U; index < compensation.points.size(); ++index) {
        if (!(compensation.points[index - 1U].frequency_hz <
              compensation.points[index].frequency_hz)) {
            return false;
        }
    }

    struct Candidate {
        double frequency_hz;
        double gain_db;
        double magnitude_db;
    };
    Candidate candidates[kMaxLoudnessFormulaPointsV1];
    std::size_t candidate_count = 0U;
    for (const auto& point : compensation.points) {
        if (!std::isfinite(point.frequency_hz) || !std::isfinite(point.gain_db) ||
            point.frequency_hz <= 0.0) {
            return false;
        }
        candidates[candidate_count++] =
            Candidate{point.frequency_hz, point.gain_db, std::abs(point.gain_db)};
    }

    bool retained[kMaxLoudnessFormulaPointsV1]{};
    while (result.filter_count < kMaxRealtimePeqFiltersV1 &&
           result.filter_count < candidate_count) {
        std::size_t best = std::numeric_limits<std::size_t>::max();
        double best_magnitude = -1.0;
        for (std::size_t index = 0U; index < candidate_count; ++index) {
            if (retained[index]) continue;
            bool too_close = false;
            for (std::size_t selected = 0U; selected < candidate_count; ++selected) {
                if (retained[selected] &&
                    std::abs(std::log2(candidates[selected].frequency_hz /
                                       candidates[index].frequency_hz)) < (1.0 / 12.0)) {
                    too_close = true;
                    break;
                }
            }
            if (!too_close && candidates[index].magnitude_db > best_magnitude) {
                best_magnitude = candidates[index].magnitude_db;
                best = index;
            }
        }
        if (best == std::numeric_limits<std::size_t>::max()) break;
        retained[best] = true;
        ++result.filter_count;
    }

    if (result.filter_count == 0U) return false;
    std::size_t output_index = 0U;
    for (std::size_t index = 0U; index < candidate_count; ++index) {
        if (!retained[index]) continue;
        const auto& point = candidates[index];
        const std::size_t previous = index == 0U ? std::min(index + 1U, candidate_count - 1U)
                                                 : index - 1U;
        const std::size_t next = index + 1U >= candidate_count ? previous : index + 1U;
        const double lower_spacing = std::abs(std::log2(point.frequency_hz /
                                                        candidates[previous].frequency_hz));
        const double upper_spacing = std::abs(std::log2(candidates[next].frequency_hz /
                                                        point.frequency_hz));
        const double bandwidth_octaves =
            std::clamp((lower_spacing + upper_spacing) * 0.5, 0.125, 2.0);
        const double q = std::clamp(1.0 / (2.0 * std::sinh(std::log(2.0) *
                                                            bandwidth_octaves * 0.5)), 0.3, 12.0);
        result.filters[output_index++] = PeqFilterV1{point.frequency_hz, point.gain_db, q};
    }
    return true;
}

}  // namespace

AudioEngineModel::AudioEngineModel() : volume_bank_(std::make_unique<OutputGroupVolumeBankV1>()) {}

AudioEngineModel::~AudioEngineModel() = default;

bool AudioEngineModel::prepare_graph(const GraphConfigV1& graph,
                                     const std::uint64_t revision) noexcept {
    RtGraphSnapshotV1 candidate;
    if (!compile_rt_snapshot(graph, revision, candidate)) {
        state_ = EngineTransactionState::Degraded;
        has_pending_graph_ = false;
        pending_latency_bank_ = LaneLatencyBankV1{};
        return false;
    }
    std::array<LaneLatencyConfigV1, kMaxRtLanes> latency_configs{};
    for (std::size_t index = 0U; index < candidate.lane_count; ++index) {
        latency_configs[index] = LaneLatencyConfigV1{
            candidate.lanes[index].input_channels,
            candidate.lanes[index].compensation_delay_samples,
            candidate.lanes[index].enabled};
    }
    LaneLatencyBankV1 prepared_latency_bank;
    if (!prepared_latency_bank.prepare(
            std::span<const LaneLatencyConfigV1>(latency_configs.data(), candidate.lane_count))) {
        state_ = EngineTransactionState::Degraded;
        has_pending_graph_ = false;
        pending_latency_bank_ = LaneLatencyBankV1{};
        return false;
    }
    for (std::size_t index = 0U; index < candidate.lane_count; ++index) {
        const auto& lane = candidate.lanes[index];
        if (volume_bank_ == nullptr || !volume_bank_->register_group(std::string_view(
                lane.output_group.data(), lane.output_group_bytes))) {
            state_ = EngineTransactionState::Degraded;
            has_pending_graph_ = false;
            pending_latency_bank_ = LaneLatencyBankV1{};
            return false;
        }
    }
    pending_graph_ = candidate;
    pending_latency_bank_ = std::move(prepared_latency_bank);
    has_pending_graph_ = true;
    state_ = EngineTransactionState::Prepared;
    return true;
}

bool AudioEngineModel::commit_graph() noexcept {
    if (!has_pending_graph_) {
        return false;
    }
    active_graph_ = pending_graph_;
    // A committed graph is a new listening context. Start every bounded
    // true-peak guard from unity gain so attenuation accumulated by any
    // previous group cannot keep ducking quiet audio after the switch.
    if (volume_bank_ != nullptr) volume_bank_->reset_limiters();
    active_latency_bank_ = std::move(pending_latency_bank_);
    has_active_graph_ = true;
    has_pending_graph_ = false;
    state_ = EngineTransactionState::Ready;
    return true;
}

void AudioEngineModel::rollback_graph() noexcept {
    has_pending_graph_ = false;
    pending_latency_bank_ = LaneLatencyBankV1{};
    state_ = has_active_graph_ ? EngineTransactionState::Ready : EngineTransactionState::Degraded;
}

bool AudioEngineModel::prepare_ir(const std::string_view output_group,
                                  const IrWavDataV1& data,
                                  const IrPhaseResolutionV1& phase) noexcept {
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos || volume_bank_ == nullptr ||
        !volume_bank_->has_group(output_group) || !phase.valid ||
        phase.mode == IrPhaseMode::Bypass || data.schema_version != 1U ||
        data.sample_rate != sample_rate_.load(std::memory_order_acquire) ||
        data.channels == 0U || data.channels > 8U || data.frames() == 0U ||
        data.frames() > kMaxRealtimeIrTapsV1 ||
        ((has_active_graph_ || has_pending_graph_) && data.channels != 1U &&
         data.channels != (has_pending_graph_ ? pending_graph_.output_channels
                                              : active_graph_.output_channels)) ||
        (has_active_graph_ && active_graph_.strict_direct) ||
        (has_pending_graph_ && pending_graph_.strict_direct)) {
        has_pending_ir_ = false;
        return false;
    }

    // IrConvolverV1 is a fixed ~256 KB structure; keep the candidate off the
    // stack so control-plane transactions cannot exhaust a default-size
    // thread stack (see Issue #278's measured 0xC00000FD overflow). Fail
    // closed on allocation failure like every other prepare step.
    std::unique_ptr<IrConvolverV1> candidate;
    try {
        candidate = std::make_unique<IrConvolverV1>();
    } catch (...) {
        has_pending_ir_ = false;
        return false;
    }
    const auto convolver_channels = has_pending_graph_
                                       ? pending_graph_.output_channels
                                       : (has_active_graph_ ? active_graph_.output_channels
                                                            : data.channels);
    if (!prepare_ir_convolver_from_wav_v1(*candidate, data, phase, convolver_channels)) {
        has_pending_ir_ = false;
        return false;
    }
    pending_ir_ = {};
    pending_ir_.attached = true;
    pending_ir_.output_group_bytes = static_cast<std::uint8_t>(output_group.size());
    std::copy(output_group.begin(), output_group.end(), pending_ir_.output_group.begin());
    pending_ir_.phase = phase;
    pending_ir_.convolver = std::move(*candidate);
    has_pending_ir_ = true;
    return true;
}

bool AudioEngineModel::commit_ir() noexcept {
    if (!has_pending_ir_) return false;
    active_ir_ = std::move(pending_ir_);
    has_active_ir_ = active_ir_.attached;
    has_pending_ir_ = false;
    return true;
}

bool AudioEngineModel::prepare_ir_clear() noexcept {
    pending_ir_ = {};
    has_pending_ir_ = true;
    return true;
}

void AudioEngineModel::rollback_ir() noexcept {
    pending_ir_ = {};
    has_pending_ir_ = false;
}

bool AudioEngineModel::prepare_loudness_peq(
    const std::string_view output_group,
    const std::span<const EqualLoudnessFormulaPointV1> points,
    const double current_phon,
    const EqualLoudnessPolicyV1& policy) noexcept {
    if (points.data() == nullptr && !points.empty()) return false;
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos || volume_bank_ == nullptr ||
        !volume_bank_->has_group(output_group) ||
        points.size() > kMaxLoudnessFormulaPointsV1) {
        has_pending_loudness_peq_ = false;
        return false;
    }
    const auto compensation = build_formula_compensation(points, current_phon, policy);
    if (compensation.points.empty()) {
        has_pending_loudness_peq_ = false;
        return false;
    }
    LoudnessPeqCompileResultV1 compiled{};
    if (!compile_loudness_peq_v1(compensation, compiled)) {
        has_pending_loudness_peq_ = false;
        return false;
    }
    LoudnessGraphAttachmentV1 candidate{};
    candidate.attached = true;
    candidate.output_group_bytes = static_cast<std::uint8_t>(output_group.size());
    std::copy(output_group.begin(), output_group.end(),
              candidate.output_group.begin());
    const auto loudness_channels = has_pending_graph_
                                      ? pending_graph_.output_channels
                                      : (has_active_graph_ ? active_graph_.output_channels
                                                           : 2U);
    if (!candidate.peq.prepare(
            std::span<const PeqFilterV1>(compiled.filters, compiled.filter_count),
            sample_rate_.load(std::memory_order_acquire),
            loudness_channels)) {
        pending_loudness_peq_ = {};
        has_pending_loudness_peq_ = false;
        return false;
    }
    // Keep the exact control inputs so a later live phon update can rebuild
    // the attachment without re-reading any external source.
    candidate.formula_point_count = points.size();
    std::copy(points.begin(), points.end(), candidate.formula_points.begin());
    candidate.current_phon = current_phon;
    candidate.policy = policy;
    // Every explicit prepare resets live recompute to opt-in. The phon
    // update path re-enables it before commit so a running pipeline keeps
    // working without leaking state into unrelated attachments.
    candidate.live_update_enabled = false;
    pending_loudness_peq_ = std::move(candidate);
    has_pending_loudness_peq_ = true;
    return true;
}

bool AudioEngineModel::prepare_loudness_peq_clear() noexcept {
    pending_loudness_peq_ = {};
    has_pending_loudness_peq_ = true;
    return true;
}

bool AudioEngineModel::commit_loudness_peq() noexcept {
    if (!has_pending_loudness_peq_) return false;
    const bool crossfade =
        pending_loudness_peq_.attached && has_active_loudness_peq_ &&
        active_loudness_peq_.attached &&
        pending_loudness_peq_.output_group_bytes ==
            active_loudness_peq_.output_group_bytes &&
        std::equal(pending_loudness_peq_.output_group.begin(),
                   pending_loudness_peq_.output_group.begin() +
                       pending_loudness_peq_.output_group_bytes,
                   active_loudness_peq_.output_group.begin());
    if (crossfade) {
        const auto sample_rate = sample_rate_.load(std::memory_order_acquire);
        const auto fade_frames = static_cast<std::uint64_t>(sample_rate) *
                                 kLoudnessPeqCrossfadeMs / 1000U;
        if (sample_rate == 0U ||
            fade_frames > static_cast<std::uint64_t>(
                              kMaxLoudnessPeqCrossfadeFramesV1)) {
            return false;
        }
        previous_loudness_peq_ = std::move(active_loudness_peq_);
        if (!loudness_crossfade_.begin(
                static_cast<std::size_t>(fade_frames))) {
            previous_loudness_peq_ = {};
            return false;
        }
    } else {
        previous_loudness_peq_ = {};
        loudness_crossfade_.reset();
    }
    active_loudness_peq_ = std::move(pending_loudness_peq_);
    has_active_loudness_peq_ = active_loudness_peq_.attached;
    has_pending_loudness_peq_ = false;
    return true;
}

void AudioEngineModel::rollback_loudness_peq() noexcept {
    pending_loudness_peq_ = {};
    has_pending_loudness_peq_ = false;
}

bool AudioEngineModel::update_loudness_phon(
    const std::string_view output_group,
    const double new_phon) noexcept {
    // Fail closed outside the bounded phon proxy domain. The equal-loudness formula
    // itself is frequency-dependent; 20..90 is the safe superset used by the
    // prepare path and by contract tests.
    if (new_phon < 20.0 || new_phon > 90.0 || !std::isfinite(new_phon)) {
        return false;
    }
    if (!has_active_loudness_peq_ || !active_loudness_peq_.attached ||
        (has_active_graph_ && active_graph_.strict_direct) ||
        !active_loudness_peq_.live_update_enabled ||
        has_pending_loudness_peq_ ||
        !has_active_loudness_peq(output_group)) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (active_loudness_peq_.last_phon_update_time_.time_since_epoch().count() != 0) {
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - active_loudness_peq_.last_phon_update_time_)
                .count();
        const bool big_step =
            std::abs(new_phon - active_loudness_peq_.last_loudness_phon_) >= 3.0;
        if (!big_step && elapsed_ms < 250) {
            return false;
        }
    }
    if (!prepare_loudness_peq(
            output_group,
            std::span<const EqualLoudnessFormulaPointV1>(
                active_loudness_peq_.formula_points.data(),
                active_loudness_peq_.formula_point_count),
            new_phon,
            active_loudness_peq_.policy)) {
        return false;
    }
    pending_loudness_peq_.live_update_enabled = true;
    if (!commit_loudness_peq()) {
        rollback_loudness_peq();
        return false;
    }
    active_loudness_peq_.last_loudness_phon_ = new_phon;
    active_loudness_peq_.last_phon_update_time_ = now;
    return true;
}

bool AudioEngineModel::loudness_peq_transaction_idle() const noexcept {
    return !has_pending_loudness_peq_;
}

bool AudioEngineModel::loudness_peq_transition_complete() const noexcept {
    return loudness_peq_transaction_idle() && !loudness_crossfade_.active;
}

bool AudioEngineModel::has_active_loudness_peq(const std::string_view output_group) const noexcept {
    return has_active_loudness_peq_ && active_loudness_peq_.attached &&
           active_loudness_peq_.output_group_bytes == output_group.size() &&
           std::equal(output_group.begin(), output_group.end(),
                      active_loudness_peq_.output_group.begin());
}

void AudioEngineModel::reset_loudness_peq_state() noexcept {
    active_loudness_peq_ = {};
    pending_loudness_peq_ = {};
    has_active_loudness_peq_ = false;
    has_pending_loudness_peq_ = false;
    previous_loudness_peq_ = {};
    loudness_crossfade_.reset();
}

void AudioEngineModel::set_loudness_live_update(
    const std::string_view output_group,
    const bool enabled) noexcept {
    if (!has_active_loudness_peq_ || !active_loudness_peq_.attached ||
        !has_active_loudness_peq(output_group)) {
        return;
    }
    active_loudness_peq_.live_update_enabled = enabled;
    // Re-baseline this group's debounce window to the attachment's current
    // phon so the first live update after opt-in is not compared against a
    // stale baseline from a previous attachment.
    active_loudness_peq_.last_loudness_phon_ =
        active_loudness_peq_.current_phon;
}

AudioEngineModel::LoudnessCurveSnapshotV1 AudioEngineModel::loudness_curve_snapshot()
    const noexcept {
    LoudnessCurveSnapshotV1 snapshot;
    if (!has_active_loudness_peq_ || !active_loudness_peq_.attached) return snapshot;
    snapshot.attached = true;
    snapshot.output_group = active_loudness_peq_.output_group;
    snapshot.output_group_bytes = active_loudness_peq_.output_group_bytes;
    snapshot.current_phon = active_loudness_peq_.current_phon;

    const auto compensation = build_formula_compensation(
        std::span<const EqualLoudnessFormulaPointV1>(
            active_loudness_peq_.formula_points.data(),
            active_loudness_peq_.formula_point_count),
        active_loudness_peq_.current_phon,
        active_loudness_peq_.policy);

    // Keep every finite compensation anchor. Zero-gain anchors are legal:
    // they describe a flat segment of the applied control curve.
    std::size_t point_count = 0U;
    for (const auto& point : compensation.points) {
        if (point_count >= snapshot.points.size()) break;
        if (!std::isfinite(point.frequency_hz) || !std::isfinite(point.gain_db)) continue;
        snapshot.points[point_count++] =
            EqVisualSnapshotPointV1{point.frequency_hz, point.gain_db};
    }

    // The bounded visual frame requires at least four samples. Densify the
    // SAME piecewise-linear control curve by repeatedly splitting its widest
    // log-frequency gap; this changes drawing resolution, never the applied
    // EQ response. Fewer than two anchors stays fail-closed.
    while (point_count < 4U && point_count < snapshot.points.size()) {
        std::size_t gap_index = point_count;
        double best_ratio = 1.0;
        for (std::size_t index = 0U; index + 1U < point_count; ++index) {
            const auto low = snapshot.points[index].frequency_hz;
            const auto high = snapshot.points[index + 1U].frequency_hz;
            if (!(low > 0.0) || !(high > low)) return snapshot;
            const auto ratio = high / low;
            if (ratio > best_ratio) {
                best_ratio = ratio;
                gap_index = index;
            }
        }
        if (gap_index >= point_count) break;
        const auto low = snapshot.points[gap_index].frequency_hz;
        const auto high = snapshot.points[gap_index + 1U].frequency_hz;
        const EqVisualSnapshotPointV1 midpoint{
            std::sqrt(low * high),
            0.5 * (snapshot.points[gap_index].gain_db +
                   snapshot.points[gap_index + 1U].gain_db)};
        for (std::size_t index = point_count; index > gap_index + 1U; --index) {
            snapshot.points[index] = snapshot.points[index - 1U];
        }
        snapshot.points[gap_index + 1U] = midpoint;
        ++point_count;
    }
    snapshot.point_count = point_count;
    return snapshot;
}

bool AudioEngineModel::prepare_program_aware(
    const std::string_view output_group,
    const ProgramAwareLevelPolicyV1& policy) noexcept {
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos || volume_bank_ == nullptr ||
        !volume_bank_->has_group(output_group)) {
        pending_program_aware_ = {};
        has_pending_program_aware_ = false;
        return false;
    }
    if (!validate_program_aware_policy(policy)) {
        pending_program_aware_ = {};
        has_pending_program_aware_ = false;
        return false;
    }
    auto candidate_bank = std::make_unique<ProgramAwareLevelBankV1>();
    if (!candidate_bank->register_group(output_group)) {
        pending_program_aware_ = {};
        has_pending_program_aware_ = false;
        return false;
    }
    const auto sample_rate = sample_rate_.load(std::memory_order_acquire);
    if (!candidate_bank->configure_group(output_group, policy, sample_rate)) {
        pending_program_aware_ = {};
        has_pending_program_aware_ = false;
        return false;
    }
    pending_program_aware_.attached = true;
    pending_program_aware_.output_group_bytes = static_cast<std::uint8_t>(output_group.size());
    std::copy(output_group.begin(), output_group.end(),
              pending_program_aware_.output_group.begin());
    pending_program_aware_.bank = std::move(candidate_bank);
    has_pending_program_aware_ = true;
    return true;
}

bool AudioEngineModel::prepare_program_aware_clear() noexcept {
    pending_program_aware_ = {};
    has_pending_program_aware_ = true;
    return true;
}

bool AudioEngineModel::commit_program_aware() noexcept {
    if (!has_pending_program_aware_) return false;
    active_program_aware_ = std::move(pending_program_aware_);
    has_active_program_aware_ = active_program_aware_.attached;
    has_pending_program_aware_ = false;
    return true;
}

void AudioEngineModel::rollback_program_aware() noexcept {
    pending_program_aware_ = {};
    has_pending_program_aware_ = false;
}

bool AudioEngineModel::program_aware_transaction_idle() const noexcept {
    return !has_pending_program_aware_;
}

bool AudioEngineModel::has_active_program_aware(
    const std::string_view output_group) const noexcept {
    return has_active_program_aware_ && active_program_aware_.attached &&
           active_program_aware_.output_group_bytes == output_group.size() &&
           std::equal(output_group.begin(), output_group.end(),
                      active_program_aware_.output_group.begin());
}

AudioEngineModel::ProgramAwareTelemetrySnapshotV1
AudioEngineModel::program_aware_visual_snapshot() const noexcept {
    ProgramAwareTelemetrySnapshotV1 snapshot;
    if (!has_active_program_aware_ || !active_program_aware_.attached ||
        active_program_aware_.bank == nullptr) {
        return snapshot;
    }
    if (has_active_graph_ && active_graph_.strict_direct) return snapshot;

    const std::string_view group(
        active_program_aware_.output_group.data(),
        active_program_aware_.output_group_bytes);
    auto* controller = active_program_aware_.bank->controller_for_group(group);
    if (controller == nullptr ||
        controller->sample_rate() != sample_rate_.load(std::memory_order_relaxed)) {
        return snapshot;
    }
    return program_aware_telemetry_snapshot(group);
}

AudioEngineModel::ProgramAwareTelemetrySnapshotV1
AudioEngineModel::program_aware_telemetry_snapshot(
    const std::string_view output_group) const noexcept {
    if ((has_active_graph_ && active_graph_.strict_direct) ||
        !has_active_program_aware(output_group) ||
        active_program_aware_.bank == nullptr) {
        return {};
    }
    auto* controller =
        active_program_aware_.bank->controller_for_group(output_group);
    if (controller == nullptr ||
        controller->sample_rate() != sample_rate_.load(std::memory_order_relaxed)) {
        return {};
    }
    const auto telemetry = controller->read_telemetry();
    ProgramAwareTelemetrySnapshotV1 snapshot;
    snapshot.valid = telemetry.valid && telemetry.enabled &&
                     !telemetry.silence_gated;
    if (!snapshot.valid) return snapshot;
    snapshot.enabled = true;
    snapshot.silence_gated = false;
    snapshot.measured_dbfs = telemetry.measured_dbfs;
    snapshot.applied_gain_db = telemetry.applied_gain_db;
    snapshot.bass_correction_gain_db = telemetry.bass_correction_gain_db;
    snapshot.night_compression_gain_db = telemetry.night_compression_gain_db;
    snapshot.sequence = telemetry.sequence;
    return snapshot;
}

void AudioEngineModel::reset_program_aware_state() noexcept {
    active_program_aware_ = {};
    pending_program_aware_ = {};
    has_active_program_aware_ = false;
    has_pending_program_aware_ = false;
}

bool AudioEngineModel::prepare_vst3_lane(
    const std::string_view output_group,
    const std::uint32_t channels,
    const std::span<float> ring_storage) noexcept {
    if (output_group.empty() || output_group.size() > kMaxOutputGroupBytesV1 ||
        output_group.find('\0') != std::string_view::npos ||
        volume_bank_ == nullptr ||
        !volume_bank_->has_group(output_group)) {
        return false;
    }
    // Prepare on the pending bank; commit is the only RT-visible swap.
    if (!pending_vst3_lanes_.prepare_lane(output_group, channels,
                                           ring_storage)) {
        return false;
    }
    has_pending_vst3_lanes_ = true;
    return true;
}

bool AudioEngineModel::prepare_vst3_lane_clear(
    const std::string_view output_group) noexcept {
    if (output_group.empty()) { return false; }
    if (!pending_vst3_lanes_.has_lane(output_group) &&
        !active_vst3_lanes_.has_lane(output_group)) {
        return false;
    }
    // Copy active lanes into pending, then remove the target lane.
    pending_vst3_lanes_ = active_vst3_lanes_;
    if (!pending_vst3_lane_clear_target_.empty()) {
        pending_vst3_lane_clear_target_ = {};
    }
    pending_vst3_lane_clear_target_ = output_group;
    has_pending_vst3_lanes_ = true;
    return true;
}

bool AudioEngineModel::commit_vst3_lane() noexcept {
    if (!has_pending_vst3_lanes_) { return false; }
    if (!pending_vst3_lane_clear_target_.empty()) {
        // Clear transaction: copy back without the removed lane.
        pending_vst3_lanes_.clear_lane(pending_vst3_lane_clear_target_);
        pending_vst3_lane_clear_target_ = {};
    }
    active_vst3_lanes_ = pending_vst3_lanes_;
    has_active_vst3_lanes_ = true;
    has_pending_vst3_lanes_ = false;
    return true;
}

void AudioEngineModel::rollback_vst3_lane() noexcept {
    pending_vst3_lanes_.clear_all();
    pending_vst3_lane_clear_target_ = {};
    has_pending_vst3_lanes_ = false;
}

bool AudioEngineModel::vst3_lane_transaction_idle() const noexcept {
    return !has_pending_vst3_lanes_;
}

bool AudioEngineModel::has_active_vst3_lane(
    const std::string_view output_group) const noexcept {
    return has_active_vst3_lanes_ &&
           active_vst3_lanes_.has_lane(output_group);
}

void AudioEngineModel::reset_vst3_lane_state() noexcept {
    vst3_tap_.reset();
    active_vst3_lanes_.reset();
    pending_vst3_lanes_.reset();
    pending_vst3_lane_clear_target_ = {};
    has_active_vst3_lanes_ = false;
    has_pending_vst3_lanes_ = false;
}

bool AudioEngineModel::has_active_ir(const std::string_view output_group) const noexcept {
    return has_active_ir_ && active_ir_.attached &&
           active_ir_.output_group_bytes == output_group.size() &&
           std::equal(output_group.begin(), output_group.end(), active_ir_.output_group.begin());
}

bool AudioEngineModel::ir_transaction_idle() const noexcept { return !has_pending_ir_; }

IrConvolverStatusV1 AudioEngineModel::ir_status(const std::string_view output_group) const noexcept {
    return has_active_ir(output_group) ? active_ir_.convolver.status() : IrConvolverStatusV1{};
}

VolumeNotificationResult AudioEngineModel::apply_windows_volume(
    const VolumeNotificationV1& notification) noexcept {
    return apply_windows_volume("main", notification);
}

VolumeNotificationResult AudioEngineModel::apply_windows_volume(
    const std::string_view output_group,
    const VolumeNotificationV1& notification) noexcept {
    return volume_bank_ != nullptr
               ? volume_bank_->apply_windows_notification(output_group, notification)
               : VolumeNotificationResult::Invalid;
}

OutputGroupVolumeStateV1 AudioEngineModel::volume_state(
    const std::string_view output_group) const noexcept {
    return volume_bank_ != nullptr ? volume_bank_->state(output_group)
                                   : OutputGroupVolumeStateV1{};
}

bool AudioEngineModel::process(const std::span<const RtLaneInputV1> inputs,
                               float* const output_interleaved,
                               const std::size_t frames) noexcept {
    if (!has_active_graph_ ||
        !process_graph(active_graph_, inputs, output_interleaved, frames,
                       &active_latency_bank_)) {
        return false;
    }
    if (!apply_ir("main", output_interleaved, frames)) return false;
    if (!apply_loudness_peq("main", output_interleaved, frames)) return false;
    if (!apply_group_master("main", output_interleaved, frames)) return false;
    if (!active_graph_.strict_direct) {
        auto* const main_limiter =
            volume_bank_ != nullptr
                ? volume_bank_->limiter_for_group("main")
                : nullptr;
        (void)(main_limiter != nullptr
                   ? main_limiter->limit_in_place(
                         output_interleaved, frames, active_graph_.output_channels,
                         -1.0, sample_rate_.load(std::memory_order_relaxed))
                   : 1.0F);
    }
    return true;
}

bool AudioEngineModel::process_output_group(const std::string_view output_group,
                                            const std::span<const RtLaneInputV1> inputs,
                                            float* const output_interleaved,
                                            const std::size_t frames) noexcept {
    if (!has_active_graph_ ||
        !process_graph_for_output_group(active_graph_, output_group, inputs,
                                        output_interleaved, frames, &active_latency_bank_)) {
        return false;
    }
    if (!apply_ir(output_group, output_interleaved, frames)) return false;
    if (!apply_loudness_peq(output_group, output_interleaved, frames)) return false;
    if (!apply_program_aware(output_group, output_interleaved, frames)) return false;
    // The tap is control-plane telemetry for VST3 lane workers. When no
    // lane is active the sandbox never reads it, so skip the isfinite scan
    // and copies entirely instead of paying the cost every RT block.
    if (has_active_vst3_lanes_) {
        (void)vst3_tap_.publish(output_group, output_interleaved,
                                frames, active_graph_.output_channels);
    }
    if (!apply_vst3_lanes(output_group, output_interleaved, frames)) return false;
    if (!apply_group_master(output_group, output_interleaved, frames)) return false;
    if (!active_graph_.strict_direct) {
        auto* const group_limiter =
            volume_bank_ != nullptr
                ? volume_bank_->limiter_for_group(output_group)
                : nullptr;
        (void)(group_limiter != nullptr
                   ? group_limiter->limit_in_place(
                         output_interleaved, frames, active_graph_.output_channels,
                         -1.0, sample_rate_.load(std::memory_order_relaxed))
                   : 1.0F);
    }
    return true;
}

bool AudioEngineModel::process_f64(const std::span<const RtLaneInputF64V1> inputs,
                                   double* const output_interleaved,
                                   const std::size_t frames) noexcept {
    if (frames == 0U || output_interleaved == nullptr) return false;
    if (!has_active_graph_ ||
        (active_graph_.sample_format != kGraphSampleFormatFloat32V1 &&
         active_graph_.sample_format != kGraphSampleFormatFloat64V1) ||
        !process_graph_f64(active_graph_, inputs, output_interleaved, frames)) {
        return false;
    }
    if (!apply_group_master_f64("main", output_interleaved, frames)) return false;
    return true;
}

bool AudioEngineModel::process_output_group_f64(
    const std::string_view output_group,
    const std::span<const RtLaneInputF64V1> inputs,
    double* const output_interleaved,
    const std::size_t frames) noexcept {
    if (output_group.empty() || frames == 0U || output_interleaved == nullptr) return false;
    if (!has_active_graph_ ||
        (active_graph_.sample_format != kGraphSampleFormatFloat32V1 &&
         active_graph_.sample_format != kGraphSampleFormatFloat64V1) ||
        !process_graph_for_output_group_f64(active_graph_, output_group, inputs,
                                            output_interleaved, frames)) {
        return false;
    }
    if (!apply_group_master_f64(output_group, output_interleaved, frames)) return false;
    return true;
}

bool AudioEngineModel::prepare_output_fanout(const OutputFanoutPlanV1& plan,
                                             const double source_step) noexcept {
    return output_fanout_.prepare(plan, source_step);
}

bool AudioEngineModel::observe_output_fanout_clock(const std::size_t sink_index,
                                                   const double source_frames,
                                                   const double sink_frames,
                                                   const double elapsed_seconds) noexcept {
    return output_fanout_.observe_clock(sink_index, source_frames, sink_frames,
                                        elapsed_seconds);
}

bool AudioEngineModel::process_output_group_fanout(
    const std::string_view output_group,
    const std::span<const RtLaneInputV1> inputs,
    float* const graph_output_interleaved,
    const std::size_t frames,
    const std::span<float* const> outputs,
    const std::span<const std::size_t> output_capacities,
    const std::span<std::size_t> output_frames) noexcept {
    const auto fanout = output_fanout_.snapshot();
    if (!fanout.prepared || !has_active_graph_ ||
        fanout.output_channels != active_graph_.output_channels ||
        graph_output_interleaved == nullptr || frames == 0U ||
        !process_output_group(output_group, inputs, graph_output_interleaved, frames)) {
        return false;
    }
    return output_fanout_.process(graph_output_interleaved, frames, outputs,
                                  output_capacities, output_frames);
}

OutputFanoutRuntimeSnapshotV1 AudioEngineModel::output_fanout_snapshot() const noexcept {
    return output_fanout_.snapshot();
}

void AudioEngineModel::set_sample_rate(const std::uint32_t sample_rate) noexcept {
    if (sample_rate >= 8000U && sample_rate <= 192000U) {
        sample_rate_.store(sample_rate, std::memory_order_release);
    }
}

bool AudioEngineModel::bind_asio_transport(const std::wstring_view mapping_name,
                                           const std::uint32_t channels,
                                           const std::uint32_t sample_rate,
                                           const std::uint32_t frames_per_buffer) noexcept {
    return asio_transport_.bind(mapping_name, channels, sample_rate, frames_per_buffer);
}

void AudioEngineModel::unbind_asio_transport() noexcept { asio_transport_.unbind(); }

bool AudioEngineModel::asio_transport_bound() const noexcept { return asio_transport_.bound(); }

bool AudioEngineModel::process_asio_transport(
    const std::size_t lane_index,
    float* const transport_interleaved,
    const std::uint32_t transport_capacity_frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::size_t output_capacity_frames,
    AsioTransportBlockV1& block) noexcept {
    block = {};
    if (!has_active_graph_ || lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || transport_interleaved == nullptr ||
        output_interleaved == nullptr ||
        active_graph_.lanes[lane_index].input_channels == 0U) {
        return false;
    }
    if (!asio_transport_.pop(transport_interleaved, transport_capacity_frames, block) ||
        block.frames == 0U || block.frames > output_capacity_frames ||
        block.channels != active_graph_.lanes[lane_index].input_channels) {
        return false;
    }
    return process_lane_block(lane_index, transport_interleaved, block.channels, block.frames,
                              lane_inputs, output_interleaved);
}

bool AudioEngineModel::process_asio_transport_to_wasapi(
    const std::size_t lane_index,
    float* const transport_interleaved,
    const std::uint32_t transport_capacity_frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::size_t output_capacity_frames,
    AsioTransportBlockV1& block) noexcept {
    block = {};
    if (!has_active_graph_ || lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || transport_interleaved == nullptr ||
        output_interleaved == nullptr || active_graph_.lanes[lane_index].input_channels == 0U ||
        !asio_transport_.pop(transport_interleaved, transport_capacity_frames, block) ||
        block.frames == 0U || block.frames > output_capacity_frames ||
        block.channels != active_graph_.lanes[lane_index].input_channels) {
        return false;
    }
    return process_lane_block_to_wasapi(lane_index, transport_interleaved, block.channels,
                                        block.frames, lane_inputs, output_interleaved);
}

bool AudioEngineModel::process_driver_stream_packet(
    const std::size_t lane_index,
    const std::string_view expected_endpoint_guid,
    const std::span<const std::uint8_t> packet,
    const std::span<float> packet_sample_storage,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved) noexcept {
    if (!has_active_graph_ || expected_endpoint_guid.empty() || expected_endpoint_guid.size() >=
                                      HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1 ||
        lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || output_interleaved == nullptr ||
        active_graph_.lanes[lane_index].input_channels == 0U) {
        return false;
    }
    DriverStreamLaneBlockV1 block{};
    if (!decode_driver_stream_packet_v1(packet, packet_sample_storage, block) ||
        block.packet_type != HIBIKI_DRIVER_STREAM_RENDER_V1 ||
        std::string_view(block.endpoint_guid.data()) != expected_endpoint_guid ||
        block.sample_rate != sample_rate_.load(std::memory_order_acquire) ||
        block.channels != active_graph_.lanes[lane_index].input_channels ||
        block.interleaved == nullptr) {
        return false;
    }
    if (block.frames == 0U) return false;
    return process_lane_block(lane_index, block.interleaved, block.channels, block.frames,
                              lane_inputs, output_interleaved);
}

bool AudioEngineModel::process_driver_stream_packet_to_wasapi(
    const std::size_t lane_index,
    const std::string_view expected_endpoint_guid,
    const std::span<const std::uint8_t> packet,
    const std::span<float> packet_sample_storage,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved) noexcept {
    if (!has_active_graph_ || expected_endpoint_guid.empty() ||
        expected_endpoint_guid.size() >= HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1 ||
        lane_index >= active_graph_.lane_count || lane_inputs.size() < active_graph_.lane_count ||
        output_interleaved == nullptr || active_graph_.lanes[lane_index].input_channels == 0U) {
        return false;
    }
    DriverStreamLaneBlockV1 block{};
    if (!decode_driver_stream_packet_v1(packet, packet_sample_storage, block) ||
        block.packet_type != HIBIKI_DRIVER_STREAM_RENDER_V1 ||
        std::string_view(block.endpoint_guid.data()) != expected_endpoint_guid ||
        block.sample_rate != sample_rate_.load(std::memory_order_acquire) ||
        block.channels != active_graph_.lanes[lane_index].input_channels ||
        block.interleaved == nullptr || block.frames == 0U) {
        return false;
    }
    return process_lane_block_to_wasapi(lane_index, block.interleaved, block.channels,
                                        block.frames, lane_inputs, output_interleaved);
}

bool AudioEngineModel::encode_driver_stream_packet_from_lane(
    const std::size_t lane_index,
    const std::string_view endpoint_guid,
    const std::uint64_t sequence,
    const std::uint64_t generation,
    const std::uint32_t flags,
    const float* const input_interleaved,
    const std::uint32_t input_channels,
    const std::size_t frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const processed_output_interleaved,
    const std::span<std::uint8_t> packet,
    std::size_t& written_bytes) noexcept {
    written_bytes = 0U;
    const auto sample_rate = sample_rate_.load(std::memory_order_acquire);
    const auto output_channels = active_graph_.output_channels;
    if (!has_active_graph_ || lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || input_interleaved == nullptr ||
        processed_output_interleaved == nullptr || packet.data() == nullptr ||
        endpoint_guid.empty() ||
        endpoint_guid.size() >= HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1 ||
        std::find(endpoint_guid.begin(), endpoint_guid.end(), '\0') != endpoint_guid.end() ||
        sequence == 0U || generation == 0U ||
        (flags & ~(HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1 |
                   HIBIKI_DRIVER_STREAM_FLAG_SILENCE_V1)) != 0U ||
        frames == 0U || frames > HIBIKI_DRIVER_STREAM_MAX_FRAMES_V1 ||
        input_channels != active_graph_.lanes[lane_index].input_channels ||
        (output_channels != 2U && output_channels != 6U && output_channels != 8U) ||
        (sample_rate != 44100U && sample_rate != 48000U && sample_rate != 96000U &&
         sample_rate != 192000U)) {
        return false;
    }

    const auto input_samples = frames * static_cast<std::size_t>(input_channels);
    const auto output_samples = frames * static_cast<std::size_t>(output_channels);
    const auto packet_bytes = HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 +
                              output_samples * sizeof(float);
    if (packet.size() < packet_bytes) return false;
    for (std::size_t index = 0U; index < input_samples; ++index) {
        if (!std::isfinite(input_interleaved[index])) return false;
    }

    std::array<char, HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1> endpoint_guid_c{};
    std::copy(endpoint_guid.begin(), endpoint_guid.end(), endpoint_guid_c.begin());
    if (!process_lane_block(lane_index, input_interleaved, input_channels, frames, lane_inputs,
                            processed_output_interleaved)) {
        return false;
    }
    for (std::size_t index = 0U; index < output_samples; ++index) {
        if (!std::isfinite(processed_output_interleaved[index])) return false;
    }
    return hibiki_driver_stream_packet_encode_v1(
               packet.data(), packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1, sequence,
               endpoint_guid_c.data(), output_channels, sample_rate,
               static_cast<std::uint32_t>(frames), flags, generation,
               processed_output_interleaved, &written_bytes) == 1;
}

bool AudioEngineModel::start_wasapi_output(const WasapiOutputConfigV1& config,
                                           const std::uint32_t block_frames) noexcept {
    return wasapi_handoff_.start_initial(config, block_frames);
}

bool AudioEngineModel::begin_wasapi_output_handoff(const WasapiOutputConfigV1& candidate,
                                                   const std::uint32_t block_frames,
                                                   const std::uint32_t fade_ms) noexcept {
    return wasapi_handoff_.begin(candidate, block_frames, fade_ms);
}

bool AudioEngineModel::prepare_wasapi_output_handoff() noexcept {
    return wasapi_handoff_.prepare();
}

bool AudioEngineModel::commit_wasapi_output_handoff() noexcept {
    return wasapi_handoff_.commit();
}

void AudioEngineModel::rollback_wasapi_output_handoff() noexcept {
    wasapi_handoff_.rollback();
}

void AudioEngineModel::stop_wasapi_output() noexcept { wasapi_handoff_.stop(); }

WasapiSinkHandoffSnapshotV1 AudioEngineModel::wasapi_output_snapshot() const noexcept {
    return wasapi_handoff_.snapshot();
}

bool AudioEngineModel::process_output_group_to_wasapi(
    const std::string_view output_group,
    const std::span<const RtLaneInputV1> inputs,
    float* const graph_output_interleaved,
    const std::size_t frames) noexcept {
    if (frames == 0U || frames > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        graph_output_interleaved == nullptr || active_graph_.output_channels == 0U ||
        active_graph_.output_channels > 8U ||
        !process_output_group(output_group, inputs, graph_output_interleaved, frames)) {
        return false;
    }
    return wasapi_handoff_.process(graph_output_interleaved,
                                   static_cast<std::uint32_t>(frames),
                                   active_graph_.output_channels);
}

bool AudioEngineModel::prepare_wasapi_fanout(
    const std::span<const WasapiFanoutSinkConfigV1> configs,
    const std::uint32_t block_frames) noexcept {
    return wasapi_fanout_.prepare(configs, block_frames);
}

bool AudioEngineModel::process_output_group_to_wasapi_fanout(
    const std::string_view output_group,
    const std::span<const RtLaneInputV1> inputs,
    float* const graph_output_interleaved,
    const std::size_t frames) noexcept {
    if (frames == 0U ||
        frames > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        graph_output_interleaved == nullptr || active_graph_.output_channels == 0U ||
        active_graph_.output_channels > 8U ||
        !process_output_group(output_group, inputs, graph_output_interleaved, frames)) {
        return false;
    }
    return wasapi_fanout_.process(graph_output_interleaved,
                                  static_cast<std::uint32_t>(frames),
                                  active_graph_.output_channels);
}

WasapiFanoutSnapshotV1 AudioEngineModel::wasapi_fanout_snapshot() const noexcept {
    return wasapi_fanout_.snapshot();
}

void AudioEngineModel::stop_wasapi_fanout() noexcept { wasapi_fanout_.stop(); }

bool AudioEngineModel::process_lane_block(const std::size_t lane_index,
                                          const float* const input_interleaved,
                                          const std::uint32_t input_channels,
                                          const std::size_t frames,
                                          const std::span<RtLaneInputV1> lane_inputs,
                                          float* const output_interleaved) noexcept {
    if (!has_active_graph_ || lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || input_interleaved == nullptr ||
        output_interleaved == nullptr || frames == 0U ||
        input_channels != active_graph_.lanes[lane_index].input_channels) {
        return false;
    }
    const auto previous = lane_inputs[lane_index];
    lane_inputs[lane_index] = RtLaneInputV1{input_interleaved, input_channels};
    const bool processed = process(std::span<const RtLaneInputV1>(lane_inputs.data(), lane_inputs.size()),
                                   output_interleaved, frames);
    lane_inputs[lane_index] = previous;
    return processed;
}

bool AudioEngineModel::process_lane_block_to_wasapi(
    const std::size_t lane_index,
    const float* const input_interleaved,
    const std::uint32_t input_channels,
    const std::size_t frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved) noexcept {
    if (frames == 0U ||
        frames > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        !process_lane_block(lane_index, input_interleaved, input_channels, frames, lane_inputs,
                            output_interleaved) || active_graph_.output_channels == 0U ||
        active_graph_.output_channels > 8U) {
        return false;
    }
    return wasapi_handoff_.process(output_interleaved, static_cast<std::uint32_t>(frames),
                                   active_graph_.output_channels);
}

bool AudioEngineModel::apply_group_master(const std::string_view output_group,
                                          float* const output_interleaved,
                                          const std::size_t frames) noexcept {
    if (active_graph_.strict_direct) return true;
    return volume_bank_ != nullptr && volume_bank_->apply_to_interleaved(
        output_group, output_interleaved, frames, active_graph_.output_channels,
        sample_rate_.load(std::memory_order_relaxed));
}

bool AudioEngineModel::apply_group_master_f64(
    const std::string_view output_group,
    double* const output_interleaved,
    const std::size_t frames) noexcept {
    if (active_graph_.strict_direct) return true;
    if (volume_bank_ == nullptr) return false;
    const auto channel_count = active_graph_.output_channels;
    if (channel_count == 0U || channel_count > 8U) return false;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        auto* const output_frame =
            output_interleaved + frame * static_cast<std::size_t>(channel_count);
        std::array<float, 8> float_frame{};
        for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
            float_frame[channel] = static_cast<float>(output_frame[channel]);
        }
        if (!volume_bank_->apply_to_interleaved(
                output_group, float_frame.data(), 1U, channel_count,
                sample_rate_.load(std::memory_order_relaxed))) {
            return false;
        }
        for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
            output_frame[channel] = static_cast<double>(float_frame[channel]);
        }
    }
    return true;
}

bool AudioEngineModel::apply_ir(const std::string_view output_group,
                                float* const output_interleaved,
                                const std::size_t frames) noexcept {
    if (active_graph_.strict_direct || !has_active_ir_ || !active_ir_.attached ||
        output_interleaved == nullptr || frames == 0U ||
        active_ir_.output_group_bytes != output_group.size() ||
        !std::equal(output_group.begin(), output_group.end(), active_ir_.output_group.begin())) {
        return true;
    }
    return active_ir_.convolver.process_interleaved(output_interleaved, frames,
                                                    active_graph_.output_channels);
}

bool AudioEngineModel::apply_loudness_peq(const std::string_view output_group,
                                          float* const output_interleaved,
                                          const std::size_t frames) noexcept {
    if ((has_active_graph_ && active_graph_.strict_direct) ||
        (!has_active_loudness_peq_ && !loudness_crossfade_.active)) {
        return true;
    }
    if (frames == 0U) return true;
    if (output_interleaved == nullptr ||
        frames > kMaxLoudnessPeqCrossfadeFramesV1) {
        return false;
    }

    const std::size_t channel_count = active_graph_.output_channels;
    if (channel_count == 0U ||
        frames > std::numeric_limits<std::size_t>::max() / channel_count) {
        return false;
    }
    const auto matches_attachment =
        [](const LoudnessGraphAttachmentV1& attachment,
           const std::string_view group) noexcept {
            return attachment.attached &&
                   attachment.output_group_bytes == group.size() &&
                   std::equal(group.begin(), group.end(),
                              attachment.output_group.begin());
        };
    const bool active_matches =
        has_active_loudness_peq_ &&
        matches_attachment(active_loudness_peq_, output_group);
    const bool previous_matches =
        loudness_crossfade_.active &&
        matches_attachment(previous_loudness_peq_, output_group);
    if (!active_matches && !previous_matches) return true;

    const auto sample_rate = sample_rate_.load(std::memory_order_relaxed);
    if ((active_matches &&
         (active_loudness_peq_.peq.sample_rate() != sample_rate ||
          active_loudness_peq_.peq.channels() != channel_count)) ||
        (previous_matches &&
         (previous_loudness_peq_.peq.sample_rate() != sample_rate ||
          previous_loudness_peq_.peq.channels() != channel_count))) {
        return false;
    }

    std::array<float, kMaxLoudnessPeqCrossfadeFramesV1> old_samples{};
    const float* old_block = nullptr;
    if (previous_matches) {
        std::copy_n(output_interleaved, frames * channel_count,
                    old_samples.begin());
        if (!previous_loudness_peq_.peq.process_interleaved(
                old_samples.data(), frames)) {
            return false;
        }
        old_block = old_samples.data();
    }
    if (active_matches &&
        !active_loudness_peq_.peq.process_interleaved(output_interleaved,
                                                      frames)) {
        return false;
    }
    if (!loudness_crossfade_.active) {
        return active_matches;
    }

    constexpr double half_pi = std::numbers::pi_v<double> / 2.0;
    const std::size_t total_frames = loudness_crossfade_.total_frames;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const auto absolute = loudness_crossfade_.processed_frames + frame;
        const auto bounded = (std::min)(absolute, total_frames);
        const auto position = static_cast<double>(bounded) /
                              static_cast<double>(total_frames);
        const auto old_gain = static_cast<float>(std::cos(position * half_pi));
        const auto new_gain = static_cast<float>(std::sin(position * half_pi));
        for (std::size_t channel = 0U; channel < channel_count; ++channel) {
            const auto index = frame * channel_count + channel;
            float value = output_interleaved[index] * new_gain;
            if (old_block != nullptr) {
                value += old_block[index] * old_gain;
            }
            output_interleaved[index] = value;
        }
    }
    loudness_crossfade_.processed_frames =
        (std::min)(total_frames,
                   loudness_crossfade_.processed_frames + frames);
    if (loudness_crossfade_.processed_frames >= total_frames) {
        loudness_crossfade_.reset();
        previous_loudness_peq_ = {};
    }
    return true;
}

bool AudioEngineModel::apply_program_aware(const std::string_view output_group,
                                            float* const output_interleaved,
                                            const std::size_t frames) noexcept {
    if ((has_active_graph_ && active_graph_.strict_direct) ||
        !has_active_program_aware_ || !active_program_aware_.attached ||
        active_program_aware_.bank == nullptr) {
        return true;
    }
    if (output_interleaved == nullptr || frames == 0U ||
        output_group.empty() || output_group.size() >= kMaxOutputGroupBytes ||
        output_group.find('\0') != std::string_view::npos) {
        return false;
    }
    auto* controller =
        active_program_aware_.bank->controller_for_group(output_group);
    // A freshly committed controller has not rendered a block yet, so its
    // status flag is still false; validity is enforced inside
    // process_interleaved via the configured policy instead of here.
    if (controller == nullptr) return true;
    if (controller->sample_rate() != sample_rate_.load(std::memory_order_relaxed)) {
        return true;
    }
    return controller->process_interleaved(output_interleaved, frames,
                                           active_graph_.output_channels);
}

bool AudioEngineModel::apply_vst3_lanes(
    const std::string_view output_group,
    float* const output_interleaved,
    const std::size_t frames) noexcept {
    if ((has_active_graph_ && active_graph_.strict_direct) ||
        !has_active_vst3_lanes_) {
        return true;
    }
    if (output_interleaved == nullptr || frames == 0U) {
        return false;
    }
    if (!active_vst3_lanes_.has_lane(output_group)) {
        return true;  // No VST3 lane for this group; passthrough.
    }
    // Pop the processed block from the ring into a temporary buffer and
    // overwrite the output. If insufficient data, passthrough (do not block).
    const auto channels = active_vst3_lanes_.channel_count(output_group);
    if (channels == 0U ||
        frames * channels > kMaxVst3RingFramesV1 * 8U) {
        return true;
    }
    std::array<float, kMaxVst3RingFramesV1 * 8U> temp{};
    if (!active_vst3_lanes_.pop(output_group, temp.data(), frames)) {
        return true;  // Ring underrun: passthrough.
    }
    std::memcpy(output_interleaved, temp.data(),
                frames * channels * sizeof(float));
    return true;
}

bool AudioEngineModel::read_vst3_tap(
    const std::string_view output_group,
    float* const destination,
    const std::size_t max_frames,
    std::uint32_t& channels_out,
    std::size_t& frames_out,
    std::uint64_t& sequence_out) const noexcept {
    return vst3_tap_.read(output_group, destination, max_frames,
                          channels_out, frames_out, sequence_out);
}

bool AudioEngineModel::push_vst3_lane(
    const std::string_view output_group,
    const float* const interleaved,
    const std::size_t frames) noexcept {
    if (!has_active_vst3_lanes_) { return false; }
    return active_vst3_lanes_.push(output_group, interleaved, frames);
}


}  // namespace hibiki
