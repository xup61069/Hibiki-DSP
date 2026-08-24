// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_crash_report.hpp"

#include <cstring>
#include <string_view>

namespace hibiki {
namespace {

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32U - bits));
}

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

void sha256_compress(std::uint32_t (&state)[8],
                     const std::uint8_t (&block)[64]) noexcept {
    std::uint32_t schedule[64U];
    for (std::size_t index = 0U; index < 16U; ++index) {
        const std::size_t base = index * 4U;
        schedule[index] = (static_cast<std::uint32_t>(block[base]) << 24U) |
                          (static_cast<std::uint32_t>(block[base + 1U]) << 16U) |
                          (static_cast<std::uint32_t>(block[base + 2U]) << 8U) |
                          static_cast<std::uint32_t>(block[base + 3U]);
    }
    for (std::size_t index = 16U; index < 64U; ++index) {
        const std::uint32_t w15 = schedule[index - 15U];
        const std::uint32_t w2 = schedule[index - 2U];
        const std::uint32_t s0 = rotate_right(w15, 7U) ^ rotate_right(w15, 18U) ^ (w15 >> 3U);
        const std::uint32_t s1 = rotate_right(w2, 17U) ^ rotate_right(w2, 19U) ^ (w2 >> 10U);
        schedule[index] = schedule[index - 16U] + s0 + schedule[index - 7U] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t index = 0U; index < 64U; ++index) {
        const std::uint32_t sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const std::uint32_t choice = (e & f) ^ ((~e & 0xFFFFFFFFU) & g);
        const std::uint32_t temp1 = h + sum1 + choice + kSha256RoundConstants[index] + schedule[index];
        const std::uint32_t sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

const char* reason_name(Vst3CrashReportReasonV1 reason) noexcept {
    switch (reason) {
        case Vst3CrashReportReasonV1::none:
            return "none";
        case Vst3CrashReportReasonV1::worker_exit_nonzero:
            return "worker_exit_nonzero";
        case Vst3CrashReportReasonV1::worker_timeout:
            return "worker_timeout";
        case Vst3CrashReportReasonV1::pipe_failure:
            return "pipe_failure";
        case Vst3CrashReportReasonV1::protocol_error:
            return "protocol_error";
        case Vst3CrashReportReasonV1::job_object_failure:
            return "job_object_failure";
    }
    return nullptr;
}

bool reason_from_name(std::string_view name,
                      Vst3CrashReportReasonV1& destination) noexcept {
    if (name == "none") { destination = Vst3CrashReportReasonV1::none; }
    else if (name == "worker_exit_nonzero") { destination = Vst3CrashReportReasonV1::worker_exit_nonzero; }
    else if (name == "worker_timeout") { destination = Vst3CrashReportReasonV1::worker_timeout; }
    else if (name == "pipe_failure") { destination = Vst3CrashReportReasonV1::pipe_failure; }
    else if (name == "protocol_error") { destination = Vst3CrashReportReasonV1::protocol_error; }
    else if (name == "job_object_failure") { destination = Vst3CrashReportReasonV1::job_object_failure; }
    else { return false; }
    return true;
}

std::size_t write_unsigned_decimal(char* output, std::uint64_t value) noexcept {
    char scratch[20U];
    std::size_t count = 0U;
    do {
        scratch[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (std::size_t index = 0U; index < count; ++index) {
        output[index] = scratch[count - 1U - index];
    }
    return count;
}

std::size_t write_signed_decimal(char* output, std::int64_t value) noexcept {
    std::size_t offset = 0U;
    const std::uint64_t magnitude = value < 0
        ? (0ULL - static_cast<std::uint64_t>(value))
        : static_cast<std::uint64_t>(value);
    if (value < 0) output[offset++] = '-';
    return offset + write_unsigned_decimal(output + offset, magnitude);
}

bool parse_unsigned_decimal(std::string_view token, std::uint64_t limit,
                            std::uint64_t& destination) noexcept {
    if (token.empty() || token.size() > 20U) return false;
    std::uint64_t value = 0U;
    for (const char character : token) {
        if (character < '0' || character > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (value > (limit - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    destination = value;
    return true;
}

bool write_hex_digest(const Vst3Sha256DigestV1& digest, char* output) noexcept {
    constexpr char kHexDigits[] = "0123456789abcdef";
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        output[index * 2U] = kHexDigits[digest[index] >> 4U];
        output[index * 2U + 1U] = kHexDigits[digest[index] & 0x0FU];
    }
    return true;
}

bool parse_hex_digest(std::string_view token,
                      Vst3Sha256DigestV1& destination) noexcept {
    if (token.size() != kVst3CrashReportDigestBytesV1 * 2U) return false;
    auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return 10 + (character - 'a');
        return -1;
    };
    for (std::size_t index = 0U; index < destination.size(); ++index) {
        const int high = nibble(token[index * 2U]);
        const int low = nibble(token[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        destination[index] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    return true;
}

struct Writer final {
    std::span<char> output;
    std::size_t position{0U};
    bool ok{true};

    void raw(std::string_view text) noexcept {
        if (!ok || position + text.size() > output.size()) {
            ok = false;
            return;
        }
        for (const char character : text) output[position++] = character;
    }
    void decimal_u64(std::uint64_t value) noexcept {
        char scratch[20U];
        const std::size_t count = write_unsigned_decimal(scratch, value);
        raw(std::string_view{scratch, count});
    }
    void decimal_i64(std::int64_t value) noexcept {
        char scratch[21U];
        const std::size_t count = write_signed_decimal(scratch, value);
        raw(std::string_view{scratch, count});
    }
};

void write_entry(Writer& writer, const Vst3CrashReportEntryV1& entry) noexcept {
    writer.raw("{\"schema_version\":");
    writer.decimal_u64(entry.schema_version);
    writer.raw(",\"captured_utc\":");
    writer.decimal_i64(entry.captured_utc);
    writer.raw(",\"reason\":\"");
    writer.raw(reason_name(entry.reason));
    writer.raw("\",\"exit_code\":");
    writer.decimal_u64(entry.exit_code);
    writer.raw(",\"uptime_ms\":");
    writer.decimal_u64(entry.uptime_ms);
    writer.raw(",\"module_sha256\":\"");
    char hex[kVst3CrashReportDigestBytesV1 * 2U];
    static_cast<void>(write_hex_digest(entry.module_sha256, hex));
    writer.raw(std::string_view{hex, sizeof(hex)});
    writer.raw("\"}");
}

struct Cursor final {
    std::string_view text;
    std::size_t position{0U};

    [[nodiscard]] bool done() const noexcept { return position >= text.size(); }
    [[nodiscard]] char peek() const noexcept { return text[position]; }
    void advance() noexcept { ++position; }
    bool literal(std::string_view expected) noexcept {
        if (text.substr(position, expected.size()) != expected) return false;
        position += expected.size();
        return true;
    }
};

void skip_whitespace(Cursor& cursor) noexcept {
    while (!cursor.done()) {
        const char character = cursor.peek();
        if (character == ' ' || character == '\t' || character == '\n' ||
            character == '\r') {
            cursor.advance();
        } else {
            break;
        }
    }
}

bool read_json_string(Cursor& cursor, std::string_view& destination) noexcept {
    if (cursor.done() || cursor.peek() != '"') return false;
    cursor.advance();
    const std::size_t start = cursor.position;
    while (!cursor.done()) {
        const char character = cursor.peek();
        if (character == '"') {
            destination = cursor.text.substr(start, cursor.position - start);
            cursor.advance();
            return true;
        }
        if (character == '\\' || static_cast<unsigned char>(character) < 0x20U) {
            return false;
        }
        cursor.advance();
    }
    return false;
}

bool read_number_token(Cursor& cursor, std::string_view& token) noexcept {
    const std::size_t start = cursor.position;
    while (!cursor.done()) {
        const char character = cursor.peek();
        if (character >= '0' && character <= '9') {
            cursor.advance();
        } else if (character == '-' && cursor.position == start) {
            cursor.advance();
        } else {
            break;
        }
    }
    if (cursor.position == start) return false;
    token = cursor.text.substr(start, cursor.position - start);
    return true;
}

bool expect_key(Cursor& cursor, std::string_view key) noexcept {
    skip_whitespace(cursor);
    std::string_view parsed;
    if (!read_json_string(cursor, parsed)) return false;
    return parsed == key;
}

bool expect_number(Cursor& cursor, std::uint64_t limit,
                   std::uint64_t& destination) noexcept {
    skip_whitespace(cursor);
    std::string_view token;
    if (!read_number_token(cursor, token)) return false;
    return parse_unsigned_decimal(token, limit, destination);
}

bool expect_colon(Cursor& cursor) noexcept {
    skip_whitespace(cursor);
    return cursor.literal(":");
}

bool expect_comma(Cursor& cursor) noexcept {
    skip_whitespace(cursor);
    return cursor.literal(",");
}

bool expect_entry_object(Cursor& cursor, Vst3CrashReportEntryV1& entry) noexcept {
    skip_whitespace(cursor);
    if (!cursor.literal("{")) return false;
    if (!expect_key(cursor, "schema_version")) return false;
    if (!expect_colon(cursor)) return false;
    std::uint64_t schema = 0U;
    if (!expect_number(cursor, 0xFFFFFFFFFFFFFFFFULL, schema)) return false;
    if (schema != kVst3CrashReportSchemaVersionV1) return false;
    if (!expect_comma(cursor)) return false;
    if (!expect_key(cursor, "captured_utc")) return false;
    if (!expect_colon(cursor)) return false;
    skip_whitespace(cursor);
    std::string_view signed_token;
    if (!read_number_token(cursor, signed_token)) return false;
    bool negative = false;
    std::string_view digits = signed_token;
    if (!digits.empty() && digits.front() == '-') {
        negative = true;
        digits.remove_prefix(1U);
    }
    std::uint64_t magnitude = 0U;
    if (!parse_unsigned_decimal(digits,
                                negative ? 9223372036854775808ULL
                                         : 9223372036854775807ULL,
                                magnitude)) {
        return false;
    }
    entry.captured_utc = negative ? static_cast<std::int64_t>(0ULL - magnitude)
                                  : static_cast<std::int64_t>(magnitude);
    if (!expect_comma(cursor)) return false;
    if (!expect_key(cursor, "reason")) return false;
    if (!expect_colon(cursor)) return false;
    skip_whitespace(cursor);
    std::string_view reason_token;
    if (!read_json_string(cursor, reason_token)) return false;
    if (!reason_from_name(reason_token, entry.reason)) return false;
    if (!expect_comma(cursor)) return false;
    if (!expect_key(cursor, "exit_code")) return false;
    if (!expect_colon(cursor)) return false;
    std::uint64_t exit_code = 0U;
    if (!expect_number(cursor, 0xFFFFFFFFULL, exit_code)) return false;
    entry.exit_code = static_cast<std::uint32_t>(exit_code);
    if (!expect_comma(cursor)) return false;
    if (!expect_key(cursor, "uptime_ms")) return false;
    if (!expect_colon(cursor)) return false;
    std::uint64_t uptime = 0U;
    if (!expect_number(cursor, 0xFFFFFFFFFFFFFFFFULL, uptime)) return false;
    entry.uptime_ms = uptime;
    if (!expect_comma(cursor)) return false;
    if (!expect_key(cursor, "module_sha256")) return false;
    if (!expect_colon(cursor)) return false;
    skip_whitespace(cursor);
    std::string_view digest_token;
    if (!read_json_string(cursor, digest_token)) return false;
    if (!parse_hex_digest(digest_token, entry.module_sha256)) return false;
    skip_whitespace(cursor);
    return cursor.literal("}");
}

}  // namespace

Vst3Sha256DigestV1 vst3_sha256_v1(std::span<const std::uint8_t> input) noexcept {
    constexpr std::uint32_t kInitialState[8U] = {0x6a09e667U, 0xbb67ae85U,
                                                 0x3c6ef372U, 0xa54ff53aU,
                                                 0x510e527fU, 0x9b05688cU,
                                                 0x1f83d9abU, 0x5be0cd19U};
    std::uint32_t state[8U];
    for (std::size_t index = 0U; index < 8U; ++index) state[index] = kInitialState[index];
    std::size_t offset = 0U;
    while (input.size() - offset >= 64U) {
        std::uint8_t block[64U];
        for (std::size_t index = 0U; index < 64U; ++index) {
            block[index] = input[offset + index];
        }
        sha256_compress(state, block);
        offset += 64U;
    }
    std::uint8_t tail[64U]{};
    const std::size_t remaining = input.size() - offset;
    for (std::size_t index = 0U; index < remaining; ++index) {
        tail[index] = input[offset + index];
    }
    tail[remaining] = 0x80U;
    if (remaining >= 56U) {
        sha256_compress(state, tail);
        for (auto& byte : tail) byte = 0U;
    }
    const std::uint64_t bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        tail[63U - index] = static_cast<std::uint8_t>(bit_length >> (8U * index));
    }
    sha256_compress(state, tail);
    Vst3Sha256DigestV1 digest{};
    for (std::size_t word = 0U; word < 8U; ++word) {
        digest[word * 4U] = static_cast<std::uint8_t>(state[word] >> 24U);
        digest[word * 4U + 1U] = static_cast<std::uint8_t>(state[word] >> 16U);
        digest[word * 4U + 2U] = static_cast<std::uint8_t>(state[word] >> 8U);
        digest[word * 4U + 3U] = static_cast<std::uint8_t>(state[word]);
    }
    return digest;
}

bool validate_vst3_crash_report_entry_v1(
    const Vst3CrashReportEntryV1& entry) noexcept {
    if (entry.schema_version != kVst3CrashReportSchemaVersionV1) return false;
    if (entry.captured_utc == 0) return false;
    if (reason_name(entry.reason) == nullptr) return false;
    bool digest_present = false;
    for (const auto byte : entry.module_sha256) {
        if (byte != 0U) {
            digest_present = true;
            break;
        }
    }
    return digest_present;
}

Vst3CrashReportResultV1 Vst3CrashReportStoreV1::append(
    const Vst3CrashReportEntryV1& entry) noexcept {
    if (!validate_vst3_crash_report_entry_v1(entry)) {
        return Vst3CrashReportResultV1::invalid_argument;
    }
    if (count_ == entries_.size()) {
        for (std::size_t index = 1U; index < entries_.size(); ++index) {
            entries_[index - 1U] = entries_[index];
        }
        entries_[entries_.size() - 1U] = entry;
    } else {
        entries_[count_] = entry;
        ++count_;
    }
    return Vst3CrashReportResultV1::ok;
}

void Vst3CrashReportStoreV1::clear() noexcept { count_ = 0U; }

Vst3CrashReportResultV1 serialize_vst3_crash_report_store_v1(
    const Vst3CrashReportStoreV1& store, std::span<char> destination,
    std::size_t& bytes_written) noexcept {
    Writer writer{destination, 0U, true};
    writer.raw("{\"schema_version\":");
    writer.decimal_u64(kVst3CrashReportSchemaVersionV1);
    writer.raw(",\"entries\":[");
    for (std::size_t index = 0U; index < store.size(); ++index) {
        if (index != 0U) writer.raw(",");
        write_entry(writer, store.entry_at(index));
    }
    writer.raw("]}");
    if (!writer.ok) return Vst3CrashReportResultV1::output_too_small;
    bytes_written = writer.position;
    return Vst3CrashReportResultV1::ok;
}

Vst3CrashReportResultV1 parse_vst3_crash_report_store_v1(
    std::span<const char> document, Vst3CrashReportStoreV1& destination) noexcept {
    if (document.empty() || document.size() > kVst3CrashReportMaxDocumentBytesV1) {
        return Vst3CrashReportResultV1::invalid_argument;
    }
    Cursor cursor{std::string_view{document.data(), document.size()}, 0U};
    skip_whitespace(cursor);
    if (!cursor.literal("{")) return Vst3CrashReportResultV1::invalid_argument;
    if (!expect_key(cursor, "schema_version")) return Vst3CrashReportResultV1::invalid_argument;
    if (!expect_colon(cursor)) return Vst3CrashReportResultV1::invalid_argument;
    std::uint64_t schema = 0U;
    if (!expect_number(cursor, 0xFFFFFFFFFFFFFFFFULL, schema) ||
        schema != kVst3CrashReportSchemaVersionV1) {
        return Vst3CrashReportResultV1::invalid_argument;
    }
    if (!expect_comma(cursor)) return Vst3CrashReportResultV1::invalid_argument;
    if (!expect_key(cursor, "entries")) return Vst3CrashReportResultV1::invalid_argument;
    if (!expect_colon(cursor)) return Vst3CrashReportResultV1::invalid_argument;
    skip_whitespace(cursor);
    if (!cursor.literal("[")) return Vst3CrashReportResultV1::invalid_argument;
    Vst3CrashReportEntryV1 entries[kVst3CrashReportMaxEntriesV1];
    std::size_t count = 0U;
    skip_whitespace(cursor);
    if (!cursor.literal("]")) {
        while (true) {
            if (count >= kVst3CrashReportMaxEntriesV1) {
                return Vst3CrashReportResultV1::invalid_argument;
            }
            if (!expect_entry_object(cursor, entries[count])) {
                return Vst3CrashReportResultV1::invalid_argument;
            }
            ++count;
            skip_whitespace(cursor);
            if (cursor.literal(",")) continue;
            if (cursor.literal("]")) break;
            return Vst3CrashReportResultV1::invalid_argument;
        }
    }
    skip_whitespace(cursor);
    if (!cursor.literal("}")) return Vst3CrashReportResultV1::invalid_argument;
    skip_whitespace(cursor);
    if (!cursor.done()) return Vst3CrashReportResultV1::invalid_argument;
    Vst3CrashReportStoreV1 rebuilt;
    for (std::size_t index = 0U; index < count; ++index) {
        if (rebuilt.append(entries[index]) != Vst3CrashReportResultV1::ok) {
            return Vst3CrashReportResultV1::invalid_argument;
        }
    }
    destination.clear();
    destination = rebuilt;
    return Vst3CrashReportResultV1::ok;
}

}  // namespace hibiki
