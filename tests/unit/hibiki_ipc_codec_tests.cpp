#pragma once

// SPDX-License-Identifier: Apache-2.0

// Issue #1850: dedicated unit tests for the IPC wire codec fail-closed
// contract. Every decode error code must be asserted with its exact value,
// and encode-side rejection paths must return an empty buffer without
// producing a partially formed frame.

#include "hibiki/ipc.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#define CHECK(condition)                                                                     \
    do {                                                                                      \
        if (!(condition)) {                                                                   \
            std::fprintf(stderr, "ipc codec check failed: %s (%s:%d)\n", #condition,          \
                         __FILE__, __LINE__);                                                 \
            return 1;                                                                         \
        }                                                                                     \
    } while (false)

namespace {

using hibiki::decode_ipc_frame;
using hibiki::encode_ipc_frame;
using hibiki::IpcDecodeError;
using hibiki::IpcFrameV1;
using hibiki::IpcMessageType;

constexpr std::size_t kHeaderBytes = 20U;

// Build a valid minimal frame and then mutate it for each error scenario.
std::vector<std::uint8_t> make_valid_frame_bytes() {
    IpcFrameV1 frame;
    frame.header.type = IpcMessageType::Ack;
    frame.header.request_id = 7U;
    frame.payload = {0x10U, 0x20U};
    auto bytes = encode_ipc_frame(frame);
    return bytes;
}

void set_version(std::vector<std::uint8_t>& bytes, std::uint16_t version) {
    bytes[4] = static_cast<std::uint8_t>(version & 0xFFU);
    bytes[5] = static_cast<std::uint8_t>((version >> 8U) & 0xFFU);
}

void set_type(std::vector<std::uint8_t>& bytes, std::uint16_t type) {
    bytes[6] = static_cast<std::uint8_t>(type & 0xFFU);
    bytes[7] = static_cast<std::uint8_t>((type >> 8U) & 0xFFU);
}

void set_payload_size(std::vector<std::uint8_t>& bytes, std::uint32_t declared) {
    bytes[8] = static_cast<std::uint8_t>(declared & 0xFFU);
    bytes[9] = static_cast<std::uint8_t>((declared >> 8U) & 0xFFU);
    bytes[10] = static_cast<std::uint8_t>((declared >> 16U) & 0xFFU);
    bytes[11] = static_cast<std::uint8_t>((declared >> 24U) & 0xFFU);
}

}  // namespace

int main() {
    using namespace hibiki;

    // Baseline: a valid frame round-trips cleanly.
    {
        const auto bytes = make_valid_frame_bytes();
        CHECK(bytes.size() == kHeaderBytes + 2U);
        IpcDecodeError error{IpcDecodeError::None};
        const auto decoded = decode_ipc_frame(bytes, error);
        CHECK(decoded.has_value() && error == IpcDecodeError::None);
        CHECK(decoded->header.type == IpcMessageType::Ack &&
              decoded->header.request_id == 7U &&
              decoded->payload.size() == 2U);
    }

    // Truncated: fewer than the 20-byte header.
    {
        const auto bytes = make_valid_frame_bytes();
        for (std::size_t short_length = 0U; short_length < kHeaderBytes; ++short_length) {
            IpcDecodeError error{IpcDecodeError::None};
            const auto decoded =
                decode_ipc_frame(std::span<const std::uint8_t>(bytes.data(), short_length), error);
            CHECK(!decoded.has_value());
            CHECK(error == IpcDecodeError::Truncated);
        }
    }

    // UnsupportedVersion: any version other than 1 is rejected.
    {
        auto bytes = make_valid_frame_bytes();
        set_version(bytes, 0U);
        IpcDecodeError error{IpcDecodeError::None};
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::UnsupportedVersion);

        set_version(bytes, 2U);
        error = IpcDecodeError::None;
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::UnsupportedVersion);

        set_version(bytes, 0xFFFFU);
        error = IpcDecodeError::None;
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::UnsupportedVersion);
    }

    // InvalidType: type 0 and every out-of-range value are rejected.
    {
        auto bytes = make_valid_frame_bytes();
        set_type(bytes, 0U);
        IpcDecodeError error{IpcDecodeError::None};
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::InvalidType);

        set_type(bytes, 23U);  // One past EqVisualSnapshot (=22).
        error = IpcDecodeError::None;
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::InvalidType);

        set_type(bytes, 0xFFFFU);
        error = IpcDecodeError::None;
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::InvalidType);
    }

    // OversizedPayload: header declares more than kIpcMaxPayloadBytes.
    {
        auto bytes = make_valid_frame_bytes();
        set_payload_size(bytes, kIpcMaxPayloadBytes + 1U);
        IpcDecodeError error{IpcDecodeError::None};
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::OversizedPayload);

        set_payload_size(bytes, 0xFFFFFFFFU);
        error = IpcDecodeError::None;
        CHECK(!decode_ipc_frame(bytes, error).has_value());
        CHECK(error == IpcDecodeError::OversizedPayload);
    }

    // LengthMismatch: declared payload size differs from actual trailing bytes.
    {
        auto too_few = make_valid_frame_bytes();
        set_payload_size(too_few, 3U);  // Actual trailing bytes: 2.
        IpcDecodeError error{IpcDecodeError::None};
        CHECK(!decode_ipc_frame(too_few, error).has_value());
        CHECK(error == IpcDecodeError::LengthMismatch);

        auto too_many = make_valid_frame_bytes();
        too_many.push_back(0xAAU);  // Declared: 2, actual: 3.
        error = IpcDecodeError::None;
        CHECK(!decode_ipc_frame(too_many, error).has_value());
        CHECK(error == IpcDecodeError::LengthMismatch);
    }

    // Encode rejection: oversized payload returns empty vector.
    {
        IpcFrameV1 frame;
        frame.header.type = IpcMessageType::Ack;
        frame.header.request_id = 1U;
        frame.payload.assign(kIpcMaxPayloadBytes + 1U, 0x42U);
        CHECK(encode_ipc_frame(frame).empty());
    }

    // Encode rejection: invalid message type returns empty vector.
    {
        IpcFrameV1 frame;
        frame.header.type = static_cast<IpcMessageType>(0U);
        frame.header.request_id = 1U;
        frame.payload = {0x01U};
        CHECK(encode_ipc_frame(frame).empty());

        frame.header.type = static_cast<IpcMessageType>(9999U);
        CHECK(encode_ipc_frame(frame).empty());
    }

    // Boundary: exactly kIpcMaxPayloadBytes is accepted by decode.
    {
        IpcFrameV1 frame;
        frame.header.type = IpcMessageType::Ack;
        frame.header.request_id = 99U;
        frame.payload.assign(kIpcMaxPayloadBytes, 0x33U);
        const auto bytes = encode_ipc_frame(frame);
        CHECK(bytes.size() == kHeaderBytes + kIpcMaxPayloadBytes);
        IpcDecodeError error{IpcDecodeError::None};
        const auto decoded = decode_ipc_frame(bytes, error);
        CHECK(decoded.has_value() && error == IpcDecodeError::None);
        CHECK(decoded->payload.size() == kIpcMaxPayloadBytes);
    }

    return 0;
}

#undef CHECK

