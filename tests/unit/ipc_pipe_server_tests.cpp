// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ipc.hpp"
#include "hibiki/ipc_pipe.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::IpcFrameV1;
using hibiki::IpcMessageType;
using hibiki::IpcNamedPipeConfigV1;
using hibiki::IpcNamedPipeServerV1;

bool ack_hello(const hibiki::IpcFrameV1& request, IpcFrameV1& response, void*) noexcept
{
    if (request.header.type != IpcMessageType::Hello) return false;
    response.header.type = IpcMessageType::Ack;
    response.header.request_id = request.header.request_id;
    return true;
}

bool reject_all(const hibiki::IpcFrameV1& request, IpcFrameV1& response, void*) noexcept
{
    // Returning false makes the server synthesize an Error reply.
    (void)response;
    return request.header.type == IpcMessageType::Hello && false;
}

[[nodiscard]] std::wstring unique_pipe_name(const wchar_t* suffix)
{
    std::wstring name = L"\\\\.\\pipe\\HibikiDSP_ipc_pipe_tests_";
    name += suffix;
    name += L"_";
    name += std::to_wstring(_getpid());
    return name;
}

void write_u32(std::uint8_t* bytes, const std::uint32_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] HANDLE connect_with_retry(const std::wstring& pipe_name) noexcept
{
    for (int attempt = 0; attempt < 300; ++attempt) {
        const auto client = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U,
                                        nullptr, OPEN_EXISTING, 0U, nullptr);
        if (client != INVALID_HANDLE_VALUE) return client;
        if (GetLastError() == ERROR_PIPE_BUSY) {
            (void)WaitNamedPipeW(pipe_name.c_str(), 100U);
            continue;
        }
        Sleep(10U);
    }
    return INVALID_HANDLE_VALUE;
}

bool write_exact(HANDLE client, const std::uint8_t* data, const std::size_t size) noexcept
{
    DWORD transferred = 0U;
    return WriteFile(client, data, static_cast<DWORD>(size), &transferred, nullptr) != FALSE &&
           transferred == size;
}

bool read_exact(HANDLE client, std::uint8_t* data, const std::size_t size) noexcept
{
    DWORD transferred = 0U;
    return ReadFile(client, data, static_cast<DWORD>(size), &transferred, nullptr) != FALSE &&
           transferred == size;
}

bool round_trip_frame(HANDLE client,
                      const IpcFrameV1& request,
                      IpcFrameV1& response,
                      hibiki::IpcDecodeError& error)
{
    const auto encoded = hibiki::encode_ipc_frame(request);
    if (encoded.empty()) return false;
    const auto frame_bytes = static_cast<std::uint32_t>(encoded.size());
    std::array<std::uint8_t, 4> length{};
    write_u32(length.data(), frame_bytes);
    if (!write_exact(client, length.data(), length.size())) return false;
    if (!write_exact(client, encoded.data(), encoded.size())) return false;
    if (!read_exact(client, length.data(), length.size())) return false;
    const auto response_bytes = read_u32(length.data());
    std::vector<std::uint8_t> packet(response_bytes);
    if (!read_exact(client, packet.data(), packet.size())) return false;
    const auto decoded = hibiki::decode_ipc_frame(packet, error);
    if (!decoded.has_value()) return false;
    response = *decoded;
    return true;
}

}  // namespace

int main()
{
    // ---- start() config validation fails closed --------------------------
    {
        IpcNamedPipeServerV1 server;
        CHECK(!server.start(IpcNamedPipeConfigV1{L"", 1024U, 100U}, ack_hello, nullptr));
        CHECK(!server.start(IpcNamedPipeConfigV1{L"\\\\badpipe\\name", 1024U, 100U}, ack_hello, nullptr));
        std::wstring nul_name = unique_pipe_name(L"nul");
        nul_name.push_back(L'\0');
        nul_name += L"suffix";
        CHECK(!server.start(IpcNamedPipeConfigV1{nul_name, 1024U, 100U}, ack_hello, nullptr));
        CHECK(!server.start(
            IpcNamedPipeConfigV1{unique_pipe_name(L"nohandler"), 1024U, 100U}, nullptr, nullptr));
        CHECK(!server.start(IpcNamedPipeConfigV1{unique_pipe_name(L"zero_timeout"), 1024U, 0U},
                            ack_hello, nullptr));
        CHECK(!server.start(IpcNamedPipeConfigV1{unique_pipe_name(L"huge_timeout"), 1024U, 30001U},
                            ack_hello, nullptr));
        CHECK(!server.start(IpcNamedPipeConfigV1{unique_pipe_name(L"tiny_frame"), 19U, 100U},
                            ack_hello, nullptr));
        CHECK(!server.start(
            IpcNamedPipeConfigV1{unique_pipe_name(L"oversized_frame"),
                                 hibiki::kIpcMaxPayloadBytes + 21U, 100U},
            ack_hello, nullptr));
        CHECK(!server.running());
    }

    // ---- duplicate first-instance ownership fails closed ------------------
    {
        const auto owned_name = unique_pipe_name(L"duplicate");
        IpcNamedPipeServerV1 owner;
        IpcNamedPipeServerV1 duplicate;
        IpcNamedPipeConfigV1 config{};
        config.pipe_name = owned_name;
        config.require_first_pipe_instance = true;
        CHECK(owner.start(config, ack_hello, nullptr));
        CHECK(!duplicate.start(config, ack_hello, nullptr) && !duplicate.running());
        owner.stop();
        CHECK(!owner.running());
    }

    // ---- malformed wire frames are rejected without hanging ---------------
    {
        const auto pipe_name = unique_pipe_name(L"malformed");
        IpcNamedPipeServerV1 server;
        IpcNamedPipeConfigV1 config{pipe_name, 1024U, 1000U};
        CHECK(server.start(config, ack_hello, nullptr));
        auto client = connect_with_retry(pipe_name);
        CHECK(client != INVALID_HANDLE_VALUE);

        std::array<std::uint8_t, 4> length{};

        // Declared length below the minimum frame size disconnects.
        write_u32(length.data(), 10U);
        CHECK(write_exact(client, length.data(), length.size()));
        std::array<std::uint8_t, 4> probe{};
        DWORD transferred = 0U;
        (void)ReadFile(client, probe.data(), static_cast<DWORD>(probe.size()), &transferred, nullptr);

        // Reconnect: declared length above max_frame_bytes disconnects too.
        CloseHandle(client);
        client = connect_with_retry(pipe_name);
        CHECK(client != INVALID_HANDLE_VALUE);
        write_u32(length.data(), 1025U);
        CHECK(write_exact(client, length.data(), length.size()));
        (void)ReadFile(client, probe.data(), static_cast<DWORD>(probe.size()), &transferred, nullptr);

        CloseHandle(client);
        server.stop();
        CHECK(!server.running());
    }

    // ---- handler rejection produces an Error reply with preserved id -----
    {
        const auto pipe_name = unique_pipe_name(L"handler_error");
        IpcNamedPipeServerV1 server;
        CHECK(server.start(IpcNamedPipeConfigV1{pipe_name, 1024U, 1000U}, reject_all, nullptr));
        auto client = connect_with_retry(pipe_name);
        CHECK(client != INVALID_HANDLE_VALUE);

        auto hello = IpcFrameV1{};
        hello.header.type = IpcMessageType::Hello;
        hello.header.request_id = 4242U;
        auto response = IpcFrameV1{};
        auto error = hibiki::IpcDecodeError::None;
        CHECK(round_trip_frame(client, hello, response, error));
        CHECK(response.header.type == IpcMessageType::Error &&
              response.header.request_id == 4242U && response.payload.empty());

        CloseHandle(client);
        server.stop();
        CHECK(!server.running());
    }

    // ---- happy path, idle timeout recovery and sequential reconnect ------
    {
        const auto pipe_name = unique_pipe_name(L"idle");
        IpcNamedPipeServerV1 server;
        IpcNamedPipeConfigV1 config{};
        config.pipe_name = pipe_name;
        config.max_frame_bytes = 1024U;
        config.io_timeout_ms = 300U;
        CHECK(server.start(config, ack_hello, nullptr));
        auto client = connect_with_retry(pipe_name);
        CHECK(client != INVALID_HANDLE_VALUE);

        auto hello = IpcFrameV1{};
        hello.header.type = IpcMessageType::Hello;
        hello.header.request_id = 7U;
        auto response = IpcFrameV1{};
        auto error = hibiki::IpcDecodeError::None;
        CHECK(round_trip_frame(client, hello, response, error));
        CHECK(response.header.type == IpcMessageType::Ack && response.header.request_id == 7U);

        // Idle longer than io_timeout_ms must not drop the connection.
        Sleep(config.io_timeout_ms + 250U);
        hello.header.request_id = 8U;
        CHECK(round_trip_frame(client, hello, response, error));
        CHECK(response.header.type == IpcMessageType::Ack && response.header.request_id == 8U);

        // A second client must be served after the first one leaves.
        CloseHandle(client);
        auto second_client = connect_with_retry(pipe_name);
        CHECK(second_client != INVALID_HANDLE_VALUE);
        hello.header.request_id = 9U;
        CHECK(round_trip_frame(second_client, hello, response, error));
        CHECK(response.header.type == IpcMessageType::Ack && response.header.request_id == 9U);

        CloseHandle(second_client);
        server.stop();
        CHECK(!server.running());
    }

    std::fputs("ipc pipe server tests passed\n", stdout);
    return 0;
}

#else

int main()
{
    std::fputs("ipc pipe server tests skipped (non-Windows)\n", stdout);
    return 0;
}

#endif
