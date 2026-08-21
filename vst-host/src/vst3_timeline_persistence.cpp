#include "hibiki/vst3_timeline_persistence.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#endif

namespace hibiki {
namespace {

constexpr std::size_t kEventSlotsV1 = kVst3TimelineMaxEventsV1;
constexpr std::size_t kNumberTokenMaxCharsV1 = 48U;

struct Cursor {
    std::string_view text;
    std::size_t pos{0U};

    [[nodiscard]] bool at_end() const noexcept { return pos >= text.size(); }
    [[nodiscard]] char peek() const noexcept { return text[pos]; }
};

void skip_whitespace(Cursor& cursor) noexcept {
    while (!cursor.at_end()) {
        const char c = cursor.peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++cursor.pos;
        } else {
            break;
        }
    }
}

bool consume_char(Cursor& cursor, char expected) noexcept {
    skip_whitespace(cursor);
    if (!cursor.at_end() && cursor.peek() == expected) {
        ++cursor.pos;
        return true;
    }
    return false;
}

bool parse_key(Cursor& cursor, std::string_view& key,
               Vst3TimelineParseErrorV1& error) noexcept {
    skip_whitespace(cursor);
    if (cursor.at_end() || cursor.peek() != '"') {
        error = Vst3TimelineParseErrorV1::unexpected_token;
        return false;
    }
    ++cursor.pos;
    const auto start = cursor.pos;
    while (!cursor.at_end()) {
        const char c = cursor.peek();
        if (c == '"') {
            key = cursor.text.substr(start, cursor.pos - start);
            ++cursor.pos;
            return true;
        }
        if (c == '\\' || static_cast<unsigned char>(c) < 0x20U) {
            error = Vst3TimelineParseErrorV1::unexpected_token;
            return false;
        }
        ++cursor.pos;
    }
    error = Vst3TimelineParseErrorV1::truncated;
    return false;
}

bool parse_uint_value(Cursor& cursor, std::uint64_t maximum, std::uint64_t& value,
                      Vst3TimelineParseErrorV1& error) noexcept {
    skip_whitespace(cursor);
    if (cursor.at_end() || cursor.peek() < '0' || cursor.peek() > '9') {
        error = Vst3TimelineParseErrorV1::invalid_number;
        return false;
    }
    std::uint64_t accumulated = 0U;
    bool overflow = false;
    while (!cursor.at_end()) {
        const char c = cursor.peek();
        if (c < '0' || c > '9') break;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (accumulated > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            overflow = true;
        }
        accumulated = accumulated * 10U + digit;
        ++cursor.pos;
    }
    if (overflow || accumulated > maximum) {
        error = Vst3TimelineParseErrorV1::value_out_of_range;
        return false;
    }
    value = accumulated;
    return true;
}

// Parses the bounded JSON number subset the canonical writer emits: an
// optional minus sign, digits with optional fraction and exponent, no leading
// zeros and no NaN/Infinity literals. Conversion goes through a fixed-size
// buffer so nothing unbounded enters strtod.
bool parse_double_value(Cursor& cursor, double& value,
                        Vst3TimelineParseErrorV1& error) noexcept {
    skip_whitespace(cursor);
    const auto start = cursor.pos;
    if (!cursor.at_end() && cursor.peek() == '-') ++cursor.pos;
    auto digit_run = [&]() {
        while (!cursor.at_end() && cursor.peek() >= '0' && cursor.peek() <= '9') ++cursor.pos;
    };
    // Integer part: a lone zero or [1-9][0-9]*; JSON forbids leading zeros.
    if (cursor.at_end() || cursor.peek() < '0' || cursor.peek() > '9') {
        error = Vst3TimelineParseErrorV1::invalid_number;
        return false;
    }
    if (cursor.peek() == '0') {
        ++cursor.pos;
    } else {
        ++cursor.pos;
        digit_run();
    }
    if (!cursor.at_end() && cursor.peek() == '.') {
        ++cursor.pos;
        const auto before = cursor.pos;
        digit_run();
        if (cursor.pos == before) {
            error = Vst3TimelineParseErrorV1::invalid_number;
            return false;
        }
    }
    if (!cursor.at_end() && (cursor.peek() == 'e' || cursor.peek() == 'E')) {
        ++cursor.pos;
        if (!cursor.at_end() && (cursor.peek() == '+' || cursor.peek() == '-')) ++cursor.pos;
        const auto before = cursor.pos;
        digit_run();
        if (cursor.pos == before) {
            error = Vst3TimelineParseErrorV1::invalid_number;
            return false;
        }
    }
    const auto length = cursor.pos - start;
    if (length == 0U || length > kNumberTokenMaxCharsV1) {
        error = Vst3TimelineParseErrorV1::invalid_number;
        return false;
    }
    char buffer[kNumberTokenMaxCharsV1 + 1U]{};
    cursor.text.copy(buffer, length, start);
    char* end_pointer = nullptr;
    value = std::strtod(buffer, &end_pointer);
    if (end_pointer != buffer + length || !std::isfinite(value)) {
        error = Vst3TimelineParseErrorV1::invalid_number;
        return false;
    }
    return true;
}

std::string format_normalized(double value) {
    char buffer[64]{};
    const auto written = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (written <= 0) return "0";
    return std::string(buffer, static_cast<std::size_t>(written));
}

Vst3TimelineParseErrorV1 parse_event_object(
    Cursor& cursor,
    Vst3ParameterTimelineEventV1& event) noexcept {
    if (!consume_char(cursor, '{')) return Vst3TimelineParseErrorV1::unexpected_token;
    std::uint32_t seen = 0U;
    constexpr std::uint32_t kSeenParameterId = 1U << 0;
    constexpr std::uint32_t kSeenSamplePosition = 1U << 1;
    constexpr std::uint32_t kSeenNormalizedValue = 1U << 2;

    for (;;) {
        skip_whitespace(cursor);
        if (!cursor.at_end() && cursor.peek() == '}') break;
        std::string_view key;
        Vst3TimelineParseErrorV1 error = Vst3TimelineParseErrorV1::none;
        if (!parse_key(cursor, key, error)) return error;
        if (!consume_char(cursor, ':')) return Vst3TimelineParseErrorV1::unexpected_token;

        if (key == "parameter_id") {
            if ((seen & kSeenParameterId) != 0U) {
                return Vst3TimelineParseErrorV1::duplicate_key;
            }
            std::uint64_t value = 0U;
            if (!parse_uint_value(cursor, std::numeric_limits<std::uint32_t>::max(),
                                  value, error)) {
                return error;
            }
            event.parameter_id = static_cast<std::uint32_t>(value);
            seen |= kSeenParameterId;
        } else if (key == "sample_position") {
            if ((seen & kSeenSamplePosition) != 0U) {
                return Vst3TimelineParseErrorV1::duplicate_key;
            }
            std::uint64_t value = 0U;
            if (!parse_uint_value(cursor, std::numeric_limits<std::uint64_t>::max(),
                                  value, error)) {
                return error;
            }
            event.sample_position = value;
            seen |= kSeenSamplePosition;
        } else if (key == "normalized_value") {
            if ((seen & kSeenNormalizedValue) != 0U) {
                return Vst3TimelineParseErrorV1::duplicate_key;
            }
            double value = 0.0;
            if (!parse_double_value(cursor, value, error)) return error;
            if (!(value >= 0.0 && value <= 1.0)) {
                return Vst3TimelineParseErrorV1::value_out_of_range;
            }
            event.normalized_value = value;
            seen |= kSeenNormalizedValue;
        } else {
            return Vst3TimelineParseErrorV1::unknown_key;
        }

        skip_whitespace(cursor);
        if (!cursor.at_end() && cursor.peek() == ',') {
            ++cursor.pos;
            continue;
        }
        break;
    }
    if (!consume_char(cursor, '}')) return Vst3TimelineParseErrorV1::truncated;
    if (seen != (kSeenParameterId | kSeenSamplePosition | kSeenNormalizedValue)) {
        return Vst3TimelineParseErrorV1::missing_key;
    }
    return Vst3TimelineParseErrorV1::none;
}

Vst3TimelineParseErrorV1 parse_events_array(Cursor& cursor,
                                            Vst3ParameterTimelineSnapshotV1& parsed) noexcept {
    if (!consume_char(cursor, '[')) return Vst3TimelineParseErrorV1::unexpected_token;
    for (std::size_t index = 0U; index < kEventSlotsV1; ++index) {
        if (index != 0U && !consume_char(cursor, ',')) {
            return Vst3TimelineParseErrorV1::unexpected_token;
        }
        auto error =
            parse_event_object(cursor, parsed.events[index]);
        if (error != Vst3TimelineParseErrorV1::none) return error;
    }
    if (!consume_char(cursor, ']')) return Vst3TimelineParseErrorV1::unexpected_token;
    return Vst3TimelineParseErrorV1::none;
}

}  // namespace

std::string_view to_string(Vst3TimelineParseErrorV1 error) noexcept {
    switch (error) {
        case Vst3TimelineParseErrorV1::none: return "none";
        case Vst3TimelineParseErrorV1::invalid_argument: return "invalid_argument";
        case Vst3TimelineParseErrorV1::too_large: return "too_large";
        case Vst3TimelineParseErrorV1::truncated: return "truncated";
        case Vst3TimelineParseErrorV1::unexpected_token: return "unexpected_token";
        case Vst3TimelineParseErrorV1::unknown_key: return "unknown_key";
        case Vst3TimelineParseErrorV1::duplicate_key: return "duplicate_key";
        case Vst3TimelineParseErrorV1::missing_key: return "missing_key";
        case Vst3TimelineParseErrorV1::invalid_number: return "invalid_number";
        case Vst3TimelineParseErrorV1::value_out_of_range: return "value_out_of_range";
        case Vst3TimelineParseErrorV1::unsupported_version: return "unsupported_version";
        case Vst3TimelineParseErrorV1::event_count_mismatch: return "event_count_mismatch";
        case Vst3TimelineParseErrorV1::invalid_snapshot: return "invalid_snapshot";
    }
    return "unknown";
}

std::string_view to_string(Vst3TimelineFileErrorV1 error) noexcept {
    switch (error) {
        case Vst3TimelineFileErrorV1::none: return "none";
        case Vst3TimelineFileErrorV1::invalid_argument: return "invalid_argument";
        case Vst3TimelineFileErrorV1::io_error: return "io_error";
        case Vst3TimelineFileErrorV1::too_large: return "too_large";
        case Vst3TimelineFileErrorV1::serialize_error: return "serialize_error";
        case Vst3TimelineFileErrorV1::parse_error: return "parse_error";
    }
    return "unknown";
}

bool serialize_vst3_parameter_timeline_v1(
    const Vst3ParameterTimelineSnapshotV1& snapshot,
    std::string& destination) {
    if (!validate_vst3_parameter_timeline_v1(snapshot)) return false;
    const auto count = static_cast<std::size_t>(snapshot.event_count);
    std::string out;
    out.reserve(kEventSlotsV1 * 80U + 64U);
    out += "{\n  \"schema_version\": 1,\n  \"event_count\": ";
    out += std::to_string(count);
    out += ",\n  \"events\": [";
    for (std::size_t index = 0U; index < kEventSlotsV1; ++index) {
        out += (index % 4U == 0U) ? "\n    " : " ";
        const Vst3ParameterTimelineEventV1& event =
            index < count ? snapshot.events[index]
                          : Vst3ParameterTimelineEventV1{};
        out += "{\"parameter_id\": ";
        out += std::to_string(event.parameter_id);
        out += ", \"sample_position\": ";
        out += std::to_string(event.sample_position);
        out += ", \"normalized_value\": ";
        out += format_normalized(event.normalized_value);
        out += "}";
        out += (index + 1U < kEventSlotsV1) ? "," : "\n  ";
    }
    out += "]\n}\n";
    destination = std::move(out);
    return true;
}

Vst3TimelineParseErrorV1 parse_vst3_parameter_timeline_v1(
    const std::string_view text,
    Vst3ParameterTimelineSnapshotV1& destination) {
    if (text.size() > kVst3TimelineMaxSerializedBytesV1) {
        return Vst3TimelineParseErrorV1::too_large;
    }
    Cursor cursor{text};
    if (!consume_char(cursor, '{')) return Vst3TimelineParseErrorV1::unexpected_token;

    Vst3ParameterTimelineSnapshotV1 parsed{};
    std::uint64_t version = 0U;
    std::uint64_t declared_count = 0U;
    std::uint32_t seen = 0U;
    constexpr std::uint32_t kSeenVersion = 1U << 0;
    constexpr std::uint32_t kSeenCount = 1U << 1;
    constexpr std::uint32_t kSeenEvents = 1U << 2;

    for (;;) {
        skip_whitespace(cursor);
        std::string_view key;
        Vst3TimelineParseErrorV1 error = Vst3TimelineParseErrorV1::none;
        if (!parse_key(cursor, key, error)) return error;
        if (!consume_char(cursor, ':')) return Vst3TimelineParseErrorV1::unexpected_token;

        if (key == "schema_version") {
            if ((seen & kSeenVersion) != 0U) return Vst3TimelineParseErrorV1::duplicate_key;
            if (!parse_uint_value(cursor, std::numeric_limits<std::uint64_t>::max(),
                                  version, error)) {
                return error;
            }
            if (version != 1U) return Vst3TimelineParseErrorV1::unsupported_version;
            seen |= kSeenVersion;
        } else if (key == "event_count") {
            if ((seen & kSeenCount) != 0U) return Vst3TimelineParseErrorV1::duplicate_key;
            if (!parse_uint_value(cursor, kEventSlotsV1, declared_count, error)) {
                return error;
            }
            seen |= kSeenCount;
        } else if (key == "events") {
            if ((seen & kSeenEvents) != 0U) return Vst3TimelineParseErrorV1::duplicate_key;
            error = parse_events_array(cursor, parsed);
            if (error != Vst3TimelineParseErrorV1::none) return error;
            seen |= kSeenEvents;
        } else {
            return Vst3TimelineParseErrorV1::unknown_key;
        }

        if (consume_char(cursor, ',')) continue;
        if (consume_char(cursor, '}')) break;
        return Vst3TimelineParseErrorV1::unexpected_token;
    }

    if (seen != (kSeenVersion | kSeenCount | kSeenEvents)) {
        return Vst3TimelineParseErrorV1::missing_key;
    }

    // The canonical writer pads every slot beyond the declared count with the
    // default event; anything else hides data the schema cannot express.
    for (std::size_t index = static_cast<std::size_t>(declared_count);
         index < kEventSlotsV1; ++index) {
        const auto& slot = parsed.events[index];
        const Vst3ParameterTimelineEventV1 empty{};
        if (slot.parameter_id != empty.parameter_id ||
            slot.sample_position != empty.sample_position ||
            slot.normalized_value != empty.normalized_value) {
            return Vst3TimelineParseErrorV1::event_count_mismatch;
        }
    }
    parsed.event_count = static_cast<std::uint32_t>(declared_count);
    if (!validate_vst3_parameter_timeline_v1(parsed)) {
        return Vst3TimelineParseErrorV1::invalid_snapshot;
    }
    skip_whitespace(cursor);
    if (!cursor.at_end()) {
        return Vst3TimelineParseErrorV1::truncated;
    }
    destination = parsed;
    return Vst3TimelineParseErrorV1::none;
}

Vst3TimelineFileErrorV1 save_vst3_parameter_timeline_file_v1(
    const Vst3ParameterTimelineSnapshotV1& snapshot,
    const std::wstring& path) {
    if (path.empty()) return Vst3TimelineFileErrorV1::invalid_argument;
    std::string document;
    if (!serialize_vst3_parameter_timeline_v1(snapshot, document)) {
        return Vst3TimelineFileErrorV1::serialize_error;
    }
    std::error_code ec;
    const auto temporary = path + L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) return Vst3TimelineFileErrorV1::io_error;
        stream.write(document.data(), static_cast<std::streamsize>(document.size()));
        stream.close();
        if (!stream.good()) return Vst3TimelineFileErrorV1::io_error;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::filesystem::remove(temporary, ec);
        return Vst3TimelineFileErrorV1::io_error;
    }
#else
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return Vst3TimelineFileErrorV1::io_error;
    }
#endif
    return Vst3TimelineFileErrorV1::none;
}

Vst3TimelineFileErrorV1 load_vst3_parameter_timeline_file_v1(
    const std::wstring& path,
    Vst3ParameterTimelineSnapshotV1& destination) {
    if (path.empty()) return Vst3TimelineFileErrorV1::invalid_argument;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return Vst3TimelineFileErrorV1::io_error;
    if (size > kVst3TimelineMaxSerializedBytesV1) {
        return Vst3TimelineFileErrorV1::too_large;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) return Vst3TimelineFileErrorV1::io_error;
        if (size > 0U) {
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        }
        stream.close();
        if (!stream.good() && size > 0U) return Vst3TimelineFileErrorV1::io_error;
    }
    const auto result = parse_vst3_parameter_timeline_v1(text, destination);
    if (result != Vst3TimelineParseErrorV1::none) {
        return Vst3TimelineFileErrorV1::parse_error;
    }
    return Vst3TimelineFileErrorV1::none;
}

}  // namespace hibiki
