// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_worker_pipe.hpp"

#include <array>
#include <cstring>
#include <string_view>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace hibiki {
namespace {

HANDLE as_handle(void* value) noexcept { return static_cast<HANDLE>(value); }
void* as_pointer(HANDLE value) noexcept { return static_cast<void*>(value); }

bool valid_pipe_name(const std::wstring& name) noexcept {
  constexpr wchar_t prefix[] = L"\\\\.\\pipe\\";
  return name.size() > (sizeof(prefix) / sizeof(prefix[0]) - 1U) &&
         name.compare(0U, sizeof(prefix) / sizeof(prefix[0]) - 1U, prefix) == 0;
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

}  // namespace

Vst3WorkerPipeV1::~Vst3WorkerPipeV1() { close(); }

bool Vst3WorkerPipeV1::create_server(const Vst3WorkerPipeConfigV1& config) noexcept {
  close();
  if (!valid_pipe_name(config.pipe_name) || config.max_frame_bytes == 0U ||
      config.max_frame_bytes > 4U * 1024U * 1024U || config.io_timeout_ms == 0U ||
      config.io_timeout_ms > 30000U) {
    return false;
  }
  const auto pipe = CreateNamedPipeW(
      config.pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1U, config.max_frame_bytes,
      config.max_frame_bytes, config.io_timeout_ms, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return false;
  handle_ = as_pointer(pipe);
  max_frame_bytes_ = config.max_frame_bytes;
  io_timeout_ms_ = config.io_timeout_ms;
  connected_ = false;
  return true;
}

bool Vst3WorkerPipeV1::connect_client(const std::wstring_view pipe_name,
                                      const std::uint32_t io_timeout_ms) noexcept {
  close();
  if (!valid_pipe_name(std::wstring(pipe_name)) || io_timeout_ms == 0U ||
      io_timeout_ms > 30000U) {
    return false;
  }
  const std::wstring name(pipe_name);
  const auto deadline = GetTickCount64() + io_timeout_ms;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  while (GetTickCount64() <= deadline) {
    pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    if (GetLastError() != ERROR_PIPE_BUSY) return false;
    const auto remaining = deadline - GetTickCount64();
    if (remaining == 0U || !WaitNamedPipeW(name.c_str(), static_cast<DWORD>(remaining))) {
      return false;
    }
  }
  if (pipe == INVALID_HANDLE_VALUE) return false;
  DWORD mode = PIPE_READMODE_BYTE;
  if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) == FALSE) {
    CloseHandle(pipe);
    return false;
  }
  handle_ = as_pointer(pipe);
  max_frame_bytes_ = 4U * 1024U * 1024U;
  io_timeout_ms_ = io_timeout_ms;
  connected_ = true;
  return true;
}

bool Vst3WorkerPipeV1::wait_for_client(const std::uint32_t timeout_ms) noexcept {
  if (handle_ == nullptr || connected_ || timeout_ms == 0U) return false;
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) return false;
  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  const BOOL connected = ConnectNamedPipe(as_handle(handle_), &overlapped);
  if (connected != FALSE || GetLastError() == ERROR_PIPE_CONNECTED) {
    CloseHandle(event);
    connected_ = true;
    return true;
  }
  if (GetLastError() != ERROR_IO_PENDING ||
      WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0) {
    CancelIoEx(as_handle(handle_), &overlapped);
    CloseHandle(event);
    DisconnectNamedPipe(as_handle(handle_));
    return false;
  }
  DWORD transferred = 0U;
  const BOOL completed = GetOverlappedResult(as_handle(handle_), &overlapped, &transferred, FALSE);
  CloseHandle(event);
  if (completed == FALSE) {
    DisconnectNamedPipe(as_handle(handle_));
    return false;
  }
  connected_ = true;
  return true;
}

bool Vst3WorkerPipeV1::transfer(const bool write, void* const data, const std::size_t bytes) noexcept {
  if (handle_ == nullptr || !connected_ || data == nullptr || bytes == 0U) return false;
  auto* raw = static_cast<std::uint8_t*>(data);
  std::size_t offset = 0U;
  while (offset < bytes) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return false;
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    DWORD transferred = 0U;
    const auto remaining = bytes - offset;
    const BOOL result = write
                            ? WriteFile(as_handle(handle_), raw + offset,
                                        static_cast<DWORD>(remaining), &transferred, &overlapped)
                            : ReadFile(as_handle(handle_), raw + offset,
                                       static_cast<DWORD>(remaining), &transferred, &overlapped);
    bool success = result != FALSE;
    if (!success && GetLastError() == ERROR_IO_PENDING &&
        WaitForSingleObject(event, io_timeout_ms_) == WAIT_OBJECT_0) {
      success = GetOverlappedResult(as_handle(handle_), &overlapped, &transferred, FALSE) != FALSE;
    }
    if (!success || transferred == 0U) {
      if (!success) CancelIoEx(as_handle(handle_), &overlapped);
      CloseHandle(event);
      return false;
    }
    CloseHandle(event);
    offset += transferred;
  }
  return true;
}

bool Vst3WorkerPipeV1::send(const std::span<const std::uint8_t> frame) noexcept {
  if (frame.empty() || frame.size() > max_frame_bytes_ || frame.size() > UINT32_MAX) return false;
  std::array<std::uint8_t, 4> length{};
  write_u32(length.data(), static_cast<std::uint32_t>(frame.size()));
  return transfer(true, length.data(), length.size()) &&
         transfer(true, const_cast<std::uint8_t*>(frame.data()), frame.size());
}

bool Vst3WorkerPipeV1::receive(const std::span<std::uint8_t> destination,
                               std::size_t& bytes_read) noexcept {
  bytes_read = 0U;
  std::array<std::uint8_t, 4> length{};
  if (!transfer(false, length.data(), length.size())) return false;
  const auto size = read_u32(length.data());
  if (size == 0U || size > max_frame_bytes_ || size > destination.size()) return false;
  if (!transfer(false, destination.data(), size)) return false;
  bytes_read = size;
  return true;
}

void Vst3WorkerPipeV1::close() noexcept {
  if (handle_ != nullptr) {
    CancelIoEx(as_handle(handle_), nullptr);
    if (connected_) DisconnectNamedPipe(as_handle(handle_));
    CloseHandle(as_handle(handle_));
  }
  handle_ = nullptr;
  max_frame_bytes_ = 0U;
  io_timeout_ms_ = 0U;
  connected_ = false;
}

}  // namespace hibiki

#else

namespace hibiki {
Vst3WorkerPipeV1::~Vst3WorkerPipeV1() = default;
bool Vst3WorkerPipeV1::create_server(const Vst3WorkerPipeConfigV1&) noexcept { return false; }
bool Vst3WorkerPipeV1::connect_client(std::wstring_view, std::uint32_t) noexcept { return false; }
bool Vst3WorkerPipeV1::wait_for_client(std::uint32_t) noexcept { return false; }
bool Vst3WorkerPipeV1::send(std::span<const std::uint8_t>) noexcept { return false; }
bool Vst3WorkerPipeV1::receive(std::span<std::uint8_t>, std::size_t& bytes_read) noexcept {
  bytes_read = 0U;
  return false;
}
void Vst3WorkerPipeV1::close() noexcept {
  handle_ = nullptr;
  connected_ = false;
}
}  // namespace hibiki

#endif
