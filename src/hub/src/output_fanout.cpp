#include "hibiki/output_fanout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace hibiki {
namespace {

bool valid_channels(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
}

[[nodiscard]] bool is_printable_label(const std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x20U && ch != 0x7FU && !(ch >= 0x80U && ch <= 0x9FU);
    });
}

constexpr std::uint64_t kPublicationWritingBit = 1U;
constexpr std::uint64_t kPublicationSlotMask = 0x6U;
constexpr std::uint32_t kPublicationSlotShift = 1U;
constexpr std::uint32_t kPublicationGenerationShift = 3U;
constexpr std::uint32_t kNoPublicationReaderSlot = 0xFFFFFFFFU;
constexpr std::uint32_t kPublicationSlotCount = 3U;

[[nodiscard]] std::uint32_t publication_slot(
    const std::uint64_t token) noexcept {
    return static_cast<std::uint32_t>((token & kPublicationSlotMask) >>
                                      kPublicationSlotShift);
}

[[nodiscard]] bool try_reserve_publication(
    std::atomic<std::uint64_t>& publication,
    std::atomic<std::uint32_t>& reader_slot,
    std::uint64_t& stable_token,
    std::uint32_t& target_slot) noexcept {
    auto current = publication.load(std::memory_order_seq_cst);
    if ((current & kPublicationWritingBit) != 0U) { return false; }
    const auto current_slot =
        current == 0U ? kNoPublicationReaderSlot : publication_slot(current);
    if (current != 0U && current_slot >= kPublicationSlotCount) {
        return false;
    }
    const auto protected_slot = reader_slot.load(std::memory_order_seq_cst);
    target_slot = kNoPublicationReaderSlot;
    for (std::uint32_t candidate = 0U;
         candidate < kPublicationSlotCount; ++candidate) {
        if (candidate != current_slot && candidate != protected_slot) {
            target_slot = candidate;
            break;
        }
    }
    if (target_slot == kNoPublicationReaderSlot) { return false; }

    const auto current_generation = current >> kPublicationGenerationShift;
    const auto max_generation =
        std::numeric_limits<std::uint64_t>::max() >>
        kPublicationGenerationShift;
    const auto next_generation =
        current_generation >= max_generation ? 1U : current_generation + 1U;
    const auto in_progress =
        (next_generation << kPublicationGenerationShift) |
        (static_cast<std::uint64_t>(target_slot) << kPublicationSlotShift) |
        kPublicationWritingBit;
    if (!publication.compare_exchange_strong(
            current, in_progress, std::memory_order_seq_cst,
            std::memory_order_seq_cst)) {
        return false;
    }
    stable_token = in_progress & ~kPublicationWritingBit;
    return true;
}

struct PublicationReaderGuard final {
    std::atomic<std::uint32_t>& reader_slot;

    ~PublicationReaderGuard() {
        reader_slot.store(kNoPublicationReaderSlot,
                          std::memory_order_seq_cst);
    }
};

}  // namespace

bool validate_output_fanout_plan_v1(const OutputFanoutPlanV1& plan) noexcept {
    if (plan.schema_version != 1U || plan.revision == 0U ||
        !valid_channels(plan.output_channels) || plan.sink_count == 0U ||
        plan.sink_count > kOutputFanoutMaxSinksV1) {
        return false;
    }
    bool has_enabled_sink = false;
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        const auto& sink = plan.sinks[index];
        if (sink.id_bytes == 0U || sink.id_bytes > kOutputFanoutMaxIdBytesV1 ||
            sink.channels != plan.output_channels) {
            return false;
        }
        if (!is_printable_label(
                std::string_view(sink.sink_id.data(), sink.id_bytes))) {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            const auto& other = plan.sinks[prior];
            if (other.id_bytes == sink.id_bytes &&
                std::memcmp(other.sink_id.data(), sink.sink_id.data(), sink.id_bytes) == 0) {
                return false;
            }
        }
        has_enabled_sink = has_enabled_sink || sink.enabled;
    }
    return has_enabled_sink;
}

bool prepare_output_fanout_plan_v1(
    const std::span<const OutputFanoutSinkConfigV1> configs,
    const std::uint32_t output_channels,
    const std::uint64_t revision,
    OutputFanoutPlanV1& plan) noexcept {
    if (configs.empty() || configs.size() > kOutputFanoutMaxSinksV1 || revision == 0U ||
        !valid_channels(output_channels)) {
        return false;
    }
    OutputFanoutPlanV1 candidate{};
    candidate.revision = revision;
    candidate.output_channels = output_channels;
    candidate.sink_count = static_cast<std::uint32_t>(configs.size());
    for (std::size_t index = 0U; index < configs.size(); ++index) {
        const auto& source = configs[index];
        if (source.sink_id.empty() || source.sink_id.size() > kOutputFanoutMaxIdBytesV1 ||
            !is_printable_label(source.sink_id) || source.channels != output_channels) {
            return false;
        }
        auto& target = candidate.sinks[index];
        target.id_bytes = static_cast<std::uint8_t>(source.sink_id.size());
        std::copy(source.sink_id.begin(), source.sink_id.end(), target.sink_id.begin());
        target.channels = source.channels;
        target.enabled = source.enabled;
    }
    if (!validate_output_fanout_plan_v1(candidate)) return false;
    plan = candidate;
    return true;
}

bool fanout_interleaved_v1(const OutputFanoutPlanV1& plan,
                           const float* const input_interleaved,
                           const std::size_t frames,
                           const std::span<float* const> outputs,
                           const std::span<const std::size_t> output_capacities) noexcept {
    if (!validate_output_fanout_plan_v1(plan) || input_interleaved == nullptr || frames == 0U ||
        frames > kOutputFanoutMaxInputFramesV1 ||
        outputs.size() < plan.sink_count || output_capacities.size() < plan.sink_count) {
        return false;
    }
    const auto channel_count = static_cast<std::size_t>(plan.output_channels);
    if (channel_count == 0U ||
        frames > std::numeric_limits<std::size_t>::max() / channel_count) {
        return false;
    }
    const auto samples = frames * channel_count;
    for (std::size_t sample = 0U; sample < samples; ++sample) {
        if (!std::isfinite(input_interleaved[sample])) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        const auto& sink = plan.sinks[index];
        if (sink.enabled && (outputs[index] == nullptr || output_capacities[index] < frames)) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        if (plan.sinks[index].enabled) {
            std::memcpy(outputs[index], input_interleaved, samples * sizeof(float));
        }
    }
    return true;
}

bool OutputFanoutRuntimeV1::prepare(const OutputFanoutPlanV1& plan,
                                    const double source_step) noexcept {
    if (!validate_output_fanout_plan_v1(plan) || !std::isfinite(source_step) ||
        source_step < 0.25 || source_step > 4.0) {
        return false;
    }
    std::array<OutputSinkModel, kOutputFanoutMaxSinksV1> candidates{};
    for (std::size_t index = 0U; index < plan.sink_count; ++index) {
        if (plan.sinks[index].enabled &&
            !candidates[index].prepare(plan.output_channels, source_step)) {
            return false;
        }
    }
    auto candidate_scratch =
        std::unique_ptr<ScratchStorage>(new (std::nothrow) ScratchStorage{});
    if (candidate_scratch == nullptr) {
        return false;
    }
    plan_ = plan;
    sinks_ = candidates;
    scratch_ = std::move(candidate_scratch);
    applied_clock_sequences_.fill(0U);
    // Invalidate prior tokens without clearing an in-flight reader hazard;
    // publish_clock_snapshot() will select a different slot until that
    // reader's guard releases it.
    for (std::size_t index = 0U; index < kOutputFanoutMaxSinksV1; ++index) {
        auto& request = clock_requests_[index];
        request.publication.store(0U, std::memory_order_seq_cst);
        clock_publications_[index].publication.store(
            0U, std::memory_order_seq_cst);
        publish_clock_snapshot(index, sinks_[index].snapshot());
    }
    prepared_ = true;
    return true;
}

void OutputFanoutRuntimeV1::reset() noexcept {
    if (!prepared_) {
        return;
    }
    // Keep reader hazards intact while invalidating and republishing every
    // request/snapshot generation.
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (plan_.sinks[index].enabled) {
            sinks_[index].reset();
        }
        auto& request = clock_requests_[index];
        request.publication.store(0U, std::memory_order_seq_cst);
        applied_clock_sequences_[index] = 0U;
        clock_publications_[index].publication.store(
            0U, std::memory_order_seq_cst);
        publish_clock_snapshot(index, sinks_[index].snapshot());
    }
    for (std::size_t index = plan_.sink_count; index < kOutputFanoutMaxSinksV1;
         ++index) {
        clock_requests_[index].publication.store(0U,
                                                  std::memory_order_seq_cst);
        applied_clock_sequences_[index] = 0U;
        clock_publications_[index].publication.store(
            0U, std::memory_order_seq_cst);
        publish_clock_snapshot(index, sinks_[index].snapshot());
    }
}

bool OutputFanoutRuntimeV1::observe_clock(const std::size_t sink_index,
                                          const double source_frames,
                                          const double sink_frames,
                                          const double elapsed_seconds) noexcept {
    if (!prepared_ || sink_index >= plan_.sink_count || !plan_.sinks[sink_index].enabled ||
        !std::isfinite(source_frames) || !std::isfinite(sink_frames) ||
        !std::isfinite(elapsed_seconds) || source_frames <= 0.0 || sink_frames <= 0.0 ||
        elapsed_seconds <= 0.0) {
        return false;
    }

    auto& request = clock_requests_[sink_index];
    std::uint64_t stable_token = 0U;
    std::uint32_t target_slot = kNoPublicationReaderSlot;
    if (!try_reserve_publication(request.publication, request.reader_slot,
                                 stable_token, target_slot)) {
        return false;
    }
    auto& slot = request.slots[target_slot];
    slot.source_frames.store(source_frames, std::memory_order_release);
    slot.sink_frames.store(sink_frames, std::memory_order_release);
    slot.elapsed_seconds.store(elapsed_seconds, std::memory_order_release);
    request.publication.store(stable_token, std::memory_order_seq_cst);
    return true;
}

bool OutputFanoutRuntimeV1::apply_pending_clock_observations() noexcept {
    bool changed = false;
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (!plan_.sinks[index].enabled) {
            continue;
        }
        auto& request = clock_requests_[index];
        const auto before = request.publication.load(std::memory_order_seq_cst);
        if (before == 0U || (before & kPublicationWritingBit) != 0U ||
            before == applied_clock_sequences_[index]) {
            continue;
        }
        const auto slot_index = publication_slot(before);
        if (slot_index >= kPublicationSlotCount) { continue; }

        double source_frames = 0.0;
        double sink_frames = 0.0;
        double elapsed_seconds = 0.0;
        bool stable = false;
        {
            request.reader_slot.store(slot_index, std::memory_order_seq_cst);
            PublicationReaderGuard reader_guard{request.reader_slot};
            if (request.publication.load(std::memory_order_seq_cst) != before) {
                continue;
            }
            const auto& slot = request.slots[slot_index];
            source_frames =
                slot.source_frames.load(std::memory_order_acquire);
            sink_frames = slot.sink_frames.load(std::memory_order_acquire);
            elapsed_seconds =
                slot.elapsed_seconds.load(std::memory_order_acquire);
            const auto after = request.publication.load(std::memory_order_seq_cst);
            stable = before == after &&
                     (after & kPublicationWritingBit) == 0U;
        }
        if (!stable || !std::isfinite(source_frames) ||
            !std::isfinite(sink_frames) || !std::isfinite(elapsed_seconds) ||
            source_frames <= 0.0 || sink_frames <= 0.0 ||
            elapsed_seconds <= 0.0) {
            continue;
        }
        sinks_[index].observe_clock(source_frames, sink_frames, elapsed_seconds);
        applied_clock_sequences_[index] = before;
        changed = true;
    }
    return changed;
}

void OutputFanoutRuntimeV1::publish_clock_snapshot(
    const std::size_t sink_index,
    const OutputSinkClockSnapshotV1& snapshot) noexcept {
    auto& publication = clock_publications_[sink_index];
    std::uint64_t stable_token = 0U;
    std::uint32_t target_slot = kNoPublicationReaderSlot;
    if (!try_reserve_publication(publication.publication,
                                 publication.reader_slot, stable_token,
                                 target_slot)) {
        return;
    }
    auto& slot = publication.slots[target_slot];
    slot.ratio.store(snapshot.ratio, std::memory_order_release);
    slot.drift_ppm.store(snapshot.drift_ppm, std::memory_order_release);
    slot.source_step.store(snapshot.source_step, std::memory_order_release);
    slot.prepared.store(snapshot.prepared, std::memory_order_release);
    publication.publication.store(stable_token, std::memory_order_seq_cst);
}

bool OutputFanoutRuntimeV1::read_clock_snapshot(
    const std::size_t sink_index,
    OutputSinkClockSnapshotV1& snapshot) const noexcept {
    const auto& publication = clock_publications_[sink_index];
    for (std::size_t attempt = 0U; attempt < 2U; ++attempt) {
        const auto before = publication.publication.load(
            std::memory_order_seq_cst);
        if (before == 0U || (before & kPublicationWritingBit) != 0U) {
            continue;
        }
        const auto slot_index = publication_slot(before);
        if (slot_index >= kPublicationSlotCount) { continue; }
        publication.reader_slot.store(slot_index, std::memory_order_seq_cst);
        PublicationReaderGuard reader_guard{publication.reader_slot};
        if (publication.publication.load(std::memory_order_seq_cst) != before) {
            continue;
        }
        const auto& slot = publication.slots[slot_index];
        const double ratio = slot.ratio.load(std::memory_order_acquire);
        const double drift_ppm = slot.drift_ppm.load(std::memory_order_acquire);
        const double source_step =
            slot.source_step.load(std::memory_order_acquire);
        const bool prepared = slot.prepared.load(std::memory_order_acquire);
        const auto after = publication.publication.load(
            std::memory_order_seq_cst);
        if (before == after && (after & kPublicationWritingBit) == 0U) {
            snapshot = OutputSinkClockSnapshotV1{
                ratio, drift_ppm, source_step, prepared};
            return true;
        }
    }
    return false;
}

bool OutputFanoutRuntimeV1::process(
    const float* const input_interleaved,
    const std::size_t input_frames,
    const std::span<float* const> outputs,
    const std::span<const std::size_t> output_capacities,
    const std::span<std::size_t> output_frames) noexcept {
    if (!prepared_ || scratch_ == nullptr || input_interleaved == nullptr || input_frames == 0U ||
        input_frames > kOutputFanoutMaxInputFramesV1 || outputs.size() < plan_.sink_count ||
        output_capacities.size() < plan_.sink_count || output_frames.size() < plan_.sink_count) {
        return false;
    }

    // A rejected block must not leave caller-visible frame counts from a
    // previous successful call, including when the first enabled sink fails
    // the capacity preflight before later sink entries are visited.
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        output_frames[index] = 0U;
    }
    const auto samples = input_frames * static_cast<std::size_t>(plan_.output_channels);
    for (std::size_t sample = 0U; sample < samples; ++sample) {
        if (!std::isfinite(input_interleaved[sample])) {
            return false;
        }
    }

    const auto state_before = sinks_;
    const auto applied_sequences_before = applied_clock_sequences_;
    const bool clock_changed = apply_pending_clock_observations();
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (!plan_.sinks[index].enabled) {
            continue;
        }
        const auto required_frames = sinks_[index].required_output_frames(input_frames);
        if (outputs[index] == nullptr || output_capacities[index] < required_frames) {
            sinks_ = state_before;
            applied_clock_sequences_ = applied_sequences_before;
            return false;
        }
    }

    std::array<std::size_t, kOutputFanoutMaxSinksV1> rendered_frames{};
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (plan_.sinks[index].enabled &&
            !sinks_[index].process(input_interleaved, input_frames,
                                    scratch_->blocks[index].data(),
                                    kOutputFanoutMaxResampledFramesV1, rendered_frames[index])) {
            sinks_ = state_before;
            applied_clock_sequences_ = applied_sequences_before;
            return false;
        }
    }
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        if (plan_.sinks[index].enabled) {
            const auto sink_samples = rendered_frames[index] * plan_.output_channels;
            std::copy_n(scratch_->blocks[index].data(), sink_samples, outputs[index]);
            output_frames[index] = rendered_frames[index];
        }
    }
    if (clock_changed) {
        for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
            if (plan_.sinks[index].enabled) {
                publish_clock_snapshot(index, sinks_[index].snapshot());
            }
        }
    }
    return true;
}

OutputFanoutRuntimeSnapshotV1 OutputFanoutRuntimeV1::snapshot() const noexcept {
    OutputFanoutRuntimeSnapshotV1 result{};
    result.prepared = prepared_;
    result.output_channels = plan_.output_channels;
    result.sink_count = plan_.sink_count;
    for (std::size_t index = 0U; index < plan_.sink_count; ++index) {
        (void)read_clock_snapshot(index, result.sinks[index]);
    }
    return result;
}

}  // namespace hibiki
