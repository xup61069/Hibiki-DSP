#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_parameter_timeline.hpp"

#include <span>
#include <string>
#include <string_view>

namespace hibiki {

constexpr std::size_t kVst3TimelineStoreMaxEntriesV1 = 16U;
constexpr std::size_t kVst3TimelineStoreMaxIdBytesV1 = 64U;

enum class Vst3TimelineStoreStatusV1 : std::uint8_t {
    ok,
    invalid_argument,
    id_rejected,
    capacity_exhausted,
    not_found,
    io_error,
    parse_error,
};

[[nodiscard]] std::string_view to_string(Vst3TimelineStoreStatusV1 status) noexcept;

// Bounded per-timeline file store over the canonical vst3-parameter-timeline-v1
// document format: one "<id>.json" per timeline inside a single caller-chosen
// directory. Timeline IDs are restricted to a filename-safe subset so the
// ID-to-filename mapping stays bijective without any escaping layer. The store
// is a single-writer control-plane object; concurrent external writers are out
// of scope. Nothing here runs on the RT thread.
class Vst3TimelineFileStoreV1 final {
public:
    // Opens (creating if absent) the root directory. Fails closed when the
    // path is empty or exists as a non-directory.
    [[nodiscard]] bool open(const std::wstring& root) noexcept;
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept { return !root_.empty(); }

    [[nodiscard]] Vst3TimelineStoreStatusV1 save(
        std::string_view id,
        const Vst3ParameterTimelineSnapshotV1& snapshot);
    [[nodiscard]] Vst3TimelineStoreStatusV1 load(
        std::string_view id,
        Vst3ParameterTimelineSnapshotV1& destination) const;
    [[nodiscard]] Vst3TimelineStoreStatusV1 remove(std::string_view id);

    // Deterministic bounded listing of currently stored IDs (sorted, without
    // the ".json" suffix). Returns false with io_error when the directory
    // cannot be scanned; destination is untouched on failure.
    [[nodiscard]] Vst3TimelineStoreStatusV1 list_ids(
        std::span<std::string> destination,
        std::size_t& count) const;

private:
    [[nodiscard]] static bool valid_id(std::string_view id) noexcept;

    std::wstring root_;
};

}  // namespace hibiki
