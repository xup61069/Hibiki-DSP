#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_parameter_timeline.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace hibiki {

// Canonical persistence for vst3-parameter-timeline-v1 documents. The writer
// emits one deterministic shape that satisfies the accepted JSON schema
// (fixed key set, all 256 event slots present); the reader is a strict,
// bounded tokenizer for exactly that shape plus insignificant whitespace.
// This is a control-plane file contract, not a general JSON library: any
// deviation fails closed and leaves the destination untouched. Nothing here
// runs on the RT thread.

constexpr std::size_t kVst3TimelineMaxSerializedBytesV1 = 64U * 1024U;

enum class Vst3TimelineParseErrorV1 : std::uint8_t {
    none,
    invalid_argument,
    too_large,
    truncated,
    unexpected_token,
    unknown_key,
    duplicate_key,
    missing_key,
    invalid_number,
    value_out_of_range,
    unsupported_version,
    event_count_mismatch,
    invalid_snapshot,
};

[[nodiscard]] std::string_view to_string(Vst3TimelineParseErrorV1 error) noexcept;

// Serializes a validator-passing snapshot into the canonical document form.
// Fails without touching the destination when the snapshot itself is invalid.
[[nodiscard]] bool serialize_vst3_parameter_timeline_v1(
    const Vst3ParameterTimelineSnapshotV1& snapshot,
    std::string& destination);

[[nodiscard]] Vst3TimelineParseErrorV1 parse_vst3_parameter_timeline_v1(
    std::string_view text,
    Vst3ParameterTimelineSnapshotV1& destination);

enum class Vst3TimelineFileErrorV1 : std::uint8_t {
    none,
    invalid_argument,
    io_error,
    too_large,
    serialize_error,
    parse_error,
};

[[nodiscard]] std::string_view to_string(Vst3TimelineFileErrorV1 error) noexcept;

// Writes the canonical document through a temporary file and an atomic-style
// replace so an interrupted save cannot corrupt an existing document.
[[nodiscard]] Vst3TimelineFileErrorV1 save_vst3_parameter_timeline_file_v1(
    const Vst3ParameterTimelineSnapshotV1& snapshot,
    const std::wstring& path);

[[nodiscard]] Vst3TimelineFileErrorV1 load_vst3_parameter_timeline_file_v1(
    const std::wstring& path,
    Vst3ParameterTimelineSnapshotV1& destination);

}  // namespace hibiki
