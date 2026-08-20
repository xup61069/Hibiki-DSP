// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ipc_pipe.hpp"

#include <array>
#include <limits>
#include <vector>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace hibiki {
namespace {

HANDLE as_handle(const std::uintptr_t value) noexcept { return reinterpret_cast<HANDLE>(value); }
std::uintptr_t as_integer(const HANDLE value) noexcept { return reinterpret_cast<std::uintptr_t>(value); }

bool valid_pipe_name(const std::wstring& name) noexcept {
    constexpr wchar_t prefix[] = L"\\\\.\\pipe\\";
    const auto prefix_length = (sizeof(prefix) / sizeof(prefix[0])) - 1U;
    return name.size() > prefix_length && name.compare(0U, prefix_length, prefix) == 0 &&
           name.find(L'\0') == std::wstring::npos;
}

void write_u32(std::uint8_t* bytes, const std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool transfer(HANDLE pipe,
              const bool write,
              void* const data,
              const std::size_t bytes,
              const std::uint32_t timeout_ms) noexcept {
    if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE || data == nullptr || bytes == 0U ||
        bytes > (std::numeric_limits<DWORD>::max)()) {
        return false;
    }
    auto* raw = static_cast<std::uint8_t*>(data);
    std::size_t offset = 0U;
    while (offset < bytes) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) return false;
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD transferred = 0U;
        const auto remaining = bytes - offset;
        const BOOL immediate = write
                                   ? WriteFile(pipe, raw + offset, static_cast<DWORD>(remaining),
                                               &transferred, &overlapped)
                                   : ReadFile(pipe, raw + offset, static_cast<DWORD>(remaining),
                                              &transferred, &overlapped);
        bool completed = immediate != FALSE;
        if (!completed && GetLastError() == ERROR_IO_PENDING &&
            WaitForSingleObject(event, timeout_ms) == WAIT_OBJECT_0) {
            completed = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
        }
        if (!completed || transferred == 0U) {
            CancelIoEx(pipe, &overlapped);
            CloseHandle(event);
            return false;
        }
        CloseHandle(event);
        offset += transferred;
    }
    return true;
}

bool connect_client(HANDLE pipe, const std::uint32_t timeout_ms) noexcept {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return false;
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    const BOOL immediate = ConnectNamedPipe(pipe, &overlapped);
    if (immediate != FALSE || GetLastError() == ERROR_PIPE_CONNECTED) {
        CloseHandle(event);
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING ||
        WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &overlapped);
        CloseHandle(event);
        DisconnectNamedPipe(pipe);
        return false;
    }
    DWORD transferred = 0U;
    const BOOL completed = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
    CloseHandle(event);
    if (completed == FALSE) {
        DisconnectNamedPipe(pipe);
        return false;
    }
    return true;
}

void close_pipe(HANDLE pipe) noexcept {
    if (pipe != nullptr && pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe, nullptr);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

}  // namespace

IpcNamedPipeServerV1::~IpcNamedPipeServerV1() { stop(); }

bool IpcNamedPipeServerV1::start(const IpcNamedPipeConfigV1& config,
                                 const IpcFrameHandlerV1 handler,
                                 void* const context) noexcept {
    stop();
    if (!valid_pipe_name(config.pipe_name) || handler == nullptr || config.io_timeout_ms == 0U ||
        config.io_timeout_ms > 30000U || config.max_frame_bytes < 20U ||
        config.max_frame_bytes > kIpcMaxPayloadBytes + 20U) {
        return false;
    }
    pipe_name_ = config.pipe_name;
    max_frame_bytes_ = config.max_frame_bytes;
    io_timeout_ms_ = config.io_timeout_ms;
    handler_ = handler;
    handler_context_ = context;
    stop_requested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    try {
        worker_ = std::thread([this] { run(); });
    } catch (...) {
        running_.store(false, std::memory_order_release);
        handler_ = nullptr;
        handler_context_ = nullptr;
        return false;
    }
    return true;
}

void IpcNamedPipeServerV1::cancel_current_io() noexcept {
    const auto value = pipe_handle_.load(std::memory_order_acquire);
    if (value != 0U) CancelIoEx(as_handle(value), nullptr);
}

void IpcNamedPipeServerV1::stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    cancel_current_io();
    if (worker_.joinable()) worker_.join();
    running_.store(false, std::memory_order_release);
    client_connected_.store(false, std::memory_order_release);
    handler_ = nullptr;
    handler_context_ = nullptr;
    pipe_name_.clear();
    max_frame_bytes_ = 0U;
    io_timeout_ms_ = 0U;
}

void IpcNamedPipeServerV1::run() noexcept {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        const auto pipe = CreateNamedPipeW(
            pipe_name_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1U,
            max_frame_bytes_, max_frame_bytes_, io_timeout_ms_, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) break;
        pipe_handle_.store(as_integer(pipe), std::memory_order_release);
        if (!connect_client(pipe, io_timeout_ms_)) {
            pipe_handle_.store(0U, std::memory_order_release);
            close_pipe(pipe);
            if (stop_requested_.load(std::memory_order_acquire)) break;
            continue;
        }
        client_connected_.store(true, std::memory_order_release);
        while (!stop_requested_.load(std::memory_order_acquire)) {
            std::array<std::uint8_t, 4> length_bytes{};
            if (!transfer(pipe, false, length_bytes.data(), length_bytes.size(), io_timeout_ms_)) break;
            const auto frame_bytes = read_u32(length_bytes.data());
            if (frame_bytes < 20U || frame_bytes > max_frame_bytes_) break;
            std::vector<std::uint8_t> packet(frame_bytes);
            if (!transfer(pipe, false, packet.data(), packet.size(), io_timeout_ms_)) break;
            IpcDecodeError decode_error{IpcDecodeError::None};
            const auto request = decode_ipc_frame(packet, decode_error);
            if (!request.has_value()) break;
            IpcFrameV1 response{};
            if (!handler_(*request, response, handler_context_)) {
                response.header.type = IpcMessageType::Error;
                response.header.request_id = request->header.request_id;
                response.payload.clear();
            }
            const auto encoded = encode_ipc_frame(response);
            if (encoded.empty() || encoded.size() > max_frame_bytes_ ||
                encoded.size() > (std::numeric_limits<std::uint32_t>::max)()) {
                break;
            }
            std::array<std::uint8_t, 4> response_length{};
            write_u32(response_length.data(), static_cast<std::uint32_t>(encoded.size()));
            if (!transfer(pipe, true, response_length.data(), response_length.size(), io_timeout_ms_) ||
                !transfer(pipe, true, const_cast<std::uint8_t*>(encoded.data()), encoded.size(),
                          io_timeout_ms_)) {
                break;
            }
        }
        client_connected_.store(false, std::memory_order_release);
        pipe_handle_.store(0U, std::memory_order_release);
        close_pipe(pipe);
    }
    pipe_handle_.store(0U, std::memory_order_release);
    client_connected_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

}  // namespace hibiki

#else

namespace hibiki {

IpcNamedPipeServerV1::~IpcNamedPipeServerV1() { stop(); }

bool IpcNamedPipeServerV1::start(const IpcNamedPipeConfigV1&,
                                 IpcFrameHandlerV1,
                                 void*) noexcept {
    return false;
}

void IpcNamedPipeServerV1::stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    client_connected_.store(false, std::memory_order_release);
}

void IpcNamedPipeServerV1::run() noexcept {}
void IpcNamedPipeServerV1::cancel_current_io() noexcept {}

}  // namespace hibiki

#endif
