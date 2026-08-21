#include "hibiki/vst3_timeline_file_store.hpp"

#include "hibiki/vst3_timeline_persistence.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#endif

namespace hibiki {
namespace {

bool is_ascii_alnum(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool is_reserved_windows_stem(std::string_view stem) noexcept {
    static constexpr std::string_view kReserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    if (stem.size() != 3U && stem.size() != 4U) return false;
    for (const auto reserved : kReserved) {
        if (stem.size() != reserved.size()) continue;
        bool equal = true;
        for (std::size_t index = 0U; index < reserved.size(); ++index) {
            const auto left = static_cast<char>(stem[index] >= 'a' && stem[index] <= 'z'
                ? stem[index] - ('a' - 'A')
                : stem[index]);
            equal = equal && left == reserved[index];
        }
        if (equal) return true;
    }
    return false;
}

}  // namespace

std::string_view to_string(Vst3TimelineStoreStatusV1 status) noexcept {
    switch (status) {
        case Vst3TimelineStoreStatusV1::ok: return "ok";
        case Vst3TimelineStoreStatusV1::invalid_argument: return "invalid_argument";
        case Vst3TimelineStoreStatusV1::id_rejected: return "id_rejected";
        case Vst3TimelineStoreStatusV1::capacity_exhausted: return "capacity_exhausted";
        case Vst3TimelineStoreStatusV1::not_found: return "not_found";
        case Vst3TimelineStoreStatusV1::io_error: return "io_error";
        case Vst3TimelineStoreStatusV1::parse_error: return "parse_error";
    }
    return "unknown";
}

bool Vst3TimelineFileStoreV1::valid_id(const std::string_view id) noexcept {
    if (id.empty() || id.size() > kVst3TimelineStoreMaxIdBytesV1) return false;
    if (!is_ascii_alnum(id.front())) return false;
    for (const char c : id) {
        const bool allowed = is_ascii_alnum(c) || c == '.' || c == '_' || c == '-';
        if (!allowed) return false;
    }
    if (id.back() == '.') return false;
    return !is_reserved_windows_stem(id);
}

bool Vst3TimelineFileStoreV1::open(const std::wstring& root) noexcept {
    if (root.empty()) return false;
    std::error_code ec;
    if (std::filesystem::exists(root, ec)) {
        if (ec || !std::filesystem::is_directory(root, ec) || ec) return false;
    } else {
        std::filesystem::create_directories(root, ec);
        if (ec) return false;
    }
    root_ = root;
    return true;
}

void Vst3TimelineFileStoreV1::close() noexcept { root_.clear(); }

Vst3TimelineStoreStatusV1 Vst3TimelineFileStoreV1::save(
    const std::string_view id,
    const Vst3ParameterTimelineSnapshotV1& snapshot) {
    if (!is_open()) return Vst3TimelineStoreStatusV1::invalid_argument;
    if (!valid_id(id)) return Vst3TimelineStoreStatusV1::id_rejected;

    std::array<std::string, kVst3TimelineStoreMaxEntriesV1> existing{};
    std::size_t existing_count = 0U;
    auto list_status = list_ids(existing, existing_count);
    if (list_status != Vst3TimelineStoreStatusV1::ok) return list_status;
    bool already_stored = false;
    for (std::size_t index = 0U; index < existing_count; ++index) {
        already_stored = already_stored || existing[index] == id;
    }
    if (!already_stored && existing_count >= kVst3TimelineStoreMaxEntriesV1) {
        return Vst3TimelineStoreStatusV1::capacity_exhausted;
    }

    const std::filesystem::path path =
        std::filesystem::path(root_) /
        (std::string(id) + ".json");
    const auto result = save_vst3_parameter_timeline_file_v1(snapshot, path.wstring());
    switch (result) {
        case Vst3TimelineFileErrorV1::none: return Vst3TimelineStoreStatusV1::ok;
        case Vst3TimelineFileErrorV1::serialize_error:
            return Vst3TimelineStoreStatusV1::invalid_argument;
        default: return Vst3TimelineStoreStatusV1::io_error;
    }
}

Vst3TimelineStoreStatusV1 Vst3TimelineFileStoreV1::load(
    const std::string_view id,
    Vst3ParameterTimelineSnapshotV1& destination) const {
    if (!is_open()) return Vst3TimelineStoreStatusV1::invalid_argument;
    if (!valid_id(id)) return Vst3TimelineStoreStatusV1::id_rejected;
    const std::filesystem::path path =
        std::filesystem::path(root_) / (std::string(id) + ".json");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return Vst3TimelineStoreStatusV1::not_found;
    }
    const auto result = load_vst3_parameter_timeline_file_v1(path.wstring(), destination);
    switch (result) {
        case Vst3TimelineFileErrorV1::none: return Vst3TimelineStoreStatusV1::ok;
        case Vst3TimelineFileErrorV1::io_error: return Vst3TimelineStoreStatusV1::io_error;
        default: return Vst3TimelineStoreStatusV1::parse_error;
    }
}

Vst3TimelineStoreStatusV1 Vst3TimelineFileStoreV1::remove(const std::string_view id) {
    if (!is_open()) return Vst3TimelineStoreStatusV1::invalid_argument;
    if (!valid_id(id)) return Vst3TimelineStoreStatusV1::id_rejected;
    const std::filesystem::path path =
        std::filesystem::path(root_) / (std::string(id) + ".json");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return Vst3TimelineStoreStatusV1::not_found;
    }
    std::filesystem::remove(path, ec);
    if (ec) return Vst3TimelineStoreStatusV1::io_error;
    return Vst3TimelineStoreStatusV1::ok;
}

Vst3TimelineStoreStatusV1 Vst3TimelineFileStoreV1::list_ids(
    std::span<std::string> destination,
    std::size_t& count) const {
    count = 0U;
    if (!is_open()) return Vst3TimelineStoreStatusV1::invalid_argument;
    std::error_code ec;
    std::array<std::string, kVst3TimelineStoreMaxEntriesV1> found{};
    std::size_t found_count = 0U;
    for (auto entry = std::filesystem::directory_iterator(root_, ec);
         entry != std::filesystem::directory_iterator(); entry.increment(ec)) {
        if (ec) return Vst3TimelineStoreStatusV1::io_error;
        if (!entry->is_regular_file(ec) || ec) continue;
        const auto filename = entry->path().filename().string();
        if (filename.size() < 6U) continue;  // "<x>.json" minimum
        if (filename.substr(filename.size() - 5U) != ".json") continue;
        const auto stem = filename.substr(0U, filename.size() - 5U);
        if (!valid_id(stem)) continue;  // foreign or malformed file: skipped, never loaded
        if (found_count >= found.size()) return Vst3TimelineStoreStatusV1::capacity_exhausted;
        found[found_count++] = stem;
    }
    if (ec) return Vst3TimelineStoreStatusV1::io_error;
    std::sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(found_count));
    count = found_count;
    if (count > destination.size()) return Vst3TimelineStoreStatusV1::capacity_exhausted;
    for (std::size_t index = 0U; index < count; ++index) destination[index] = found[index];
    return Vst3TimelineStoreStatusV1::ok;
}

}  // namespace hibiki
