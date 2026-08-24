#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::size_t kVst3CrashReportMaxEntriesV1 = 16U;
constexpr std::size_t kVst3CrashReportDigestBytesV1 = 32U;
constexpr std::size_t kVst3CrashReportMaxDocumentBytesV1 = 64U * 1024U;
constexpr std::uint64_t kVst3CrashReportSchemaVersionV1 = 1U;

using Vst3Sha256DigestV1 = std::array<std::uint8_t, kVst3CrashReportDigestBytesV1>;

// Small self-contained SHA-256 for control-plane redaction digests. The
// implementation uses fixed-size stack blocks only; callers pass data as a
// span and receive the digest by value.
[[nodiscard]] Vst3Sha256DigestV1 vst3_sha256_v1(
    std::span<const std::uint8_t> input) noexcept;

// De-identified crash-report reason taxonomy. Values are wire-stable; the
// JSON reader accepts exactly these spellings and nothing else.
enum class Vst3CrashReportReasonV1 : std::uint8_t {
    none = 0,
    worker_exit_nonzero,
    worker_timeout,
    pipe_failure,
    protocol_error,
    job_object_failure,
};

struct Vst3CrashReportEntryV1 {
    std::uint64_t schema_version{kVst3CrashReportSchemaVersionV1};
    std::int64_t captured_utc{0};
    Vst3CrashReportReasonV1 reason{Vst3CrashReportReasonV1::none};
    std::uint32_t exit_code{0};
    std::uint64_t uptime_ms{0};
    Vst3Sha256DigestV1 module_sha256{};
};

enum class Vst3CrashReportResultV1 : std::uint8_t {
    ok,
    invalid_argument,
    output_too_small,
};

[[nodiscard]] bool validate_vst3_crash_report_entry_v1(
    const Vst3CrashReportEntryV1& entry) noexcept;

// Fixed-capacity oldest-first ring of de-identified crash reports. Appending
// to a full store evicts the oldest entry; the store never grows and holds no
// raw paths, PIDs, handles, command lines, or opaque plugin bytes.
class Vst3CrashReportStoreV1 final {
public:
    Vst3CrashReportStoreV1() noexcept = default;

    [[nodiscard]] Vst3CrashReportResultV1 append(
        const Vst3CrashReportEntryV1& entry) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] const Vst3CrashReportEntryV1& entry_at(
        std::size_t index) const noexcept {
        return entries_[index];
    }
    void clear() noexcept;

private:
    std::array<Vst3CrashReportEntryV1, kVst3CrashReportMaxEntriesV1>
        entries_{};
    std::size_t count_{0U};
};

// Canonical JSON document for the whole store. Writer emits a fixed key set
// in fixed order; reader is a strict fail-closed tokenizer that rebuilds the
// destination only after the whole document validates.
[[nodiscard]] Vst3CrashReportResultV1 serialize_vst3_crash_report_store_v1(
    const Vst3CrashReportStoreV1& store,
    std::span<char> destination,
    std::size_t& bytes_written) noexcept;

[[nodiscard]] Vst3CrashReportResultV1 parse_vst3_crash_report_store_v1(
    std::span<const char> document,
    Vst3CrashReportStoreV1& destination) noexcept;

}  // namespace hibiki
