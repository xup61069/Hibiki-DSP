// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_wasapi_output.hpp"

#include <cstddef>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>

namespace hibiki {
namespace {

IAudioClient* as_client(void* value) noexcept { return static_cast<IAudioClient*>(value); }
IAudioRenderClient* as_render(void* value) noexcept {
  return static_cast<IAudioRenderClient*>(value);
}
HANDLE as_event(void* value) noexcept { return static_cast<HANDLE>(value); }

bool valid_layout(const std::uint32_t channels) noexcept {
  return channels == 2U || channels == 6U || channels == 8U;
}

bool valid_rate(const std::uint32_t rate) noexcept {
  return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

}  // namespace

WindowsWasapiOutputV1::~WindowsWasapiOutputV1() { unbind(); }

bool WindowsWasapiOutputV1::bind(const WasapiOutputConfigV1& config) noexcept {
  unbind();
  if (!valid_layout(config.channels) || !valid_rate(config.sample_rate) ||
      config.buffer_duration_ms == 0U || config.buffer_duration_ms > 200U) {
    return false;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  WAVEFORMATEX* mix_format = nullptr;
  IAudioClient* client = nullptr;
  IAudioRenderClient* render_client = nullptr;
  HANDLE event_handle = nullptr;
  auto cleanup = [&]() noexcept {
    if (render_client != nullptr) render_client->Release();
    if (client != nullptr) client->Release();
    if (event_handle != nullptr) CloseHandle(event_handle);
    if (mix_format != nullptr) CoTaskMemFree(mix_format);
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
  };

  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator)))) {
    cleanup();
    return false;
  }
  const HRESULT device_result = config.endpoint_id.empty()
                                    ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)
                                    : enumerator->GetDevice(config.endpoint_id.c_str(), &device);
  if (FAILED(device_result) || device == nullptr ||
      FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client))) ||
      client == nullptr || FAILED(client->GetMixFormat(&mix_format)) || mix_format == nullptr) {
    cleanup();
    return false;
  }

  const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
  const bool is_float = mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                        (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                         mix_format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
                         IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
  if (!is_float || mix_format->wBitsPerSample != 32U ||
      mix_format->nChannels != config.channels || mix_format->nSamplesPerSec != config.sample_rate) {
    cleanup();
    return false;
  }

  const REFERENCE_TIME duration = static_cast<REFERENCE_TIME>(config.buffer_duration_ms) * 10000;
  const DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, duration, 0, mix_format,
                                nullptr)) ||
      FAILED(client->GetBufferSize(&buffer_frames_)) ||
      FAILED(client->GetService(IID_PPV_ARGS(&render_client))) || render_client == nullptr ||
      (event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr)) == nullptr ||
      FAILED(client->SetEventHandle(event_handle))) {
    cleanup();
    buffer_frames_ = 0;
    return false;
  }

  client_ = client;
  render_client_ = render_client;
  event_ = event_handle;
  channels_ = config.channels;
  sample_rate_ = config.sample_rate;
  client = nullptr;
  render_client = nullptr;
  event_handle = nullptr;
  cleanup();
  return true;
}

bool WindowsWasapiOutputV1::start() noexcept {
  if (!bound() || started_ || FAILED(as_client(client_)->Start())) return false;
  started_ = true;
  return true;
}

void WindowsWasapiOutputV1::stop() noexcept {
  if (client_ != nullptr && started_) {
    (void)as_client(client_)->Stop();
    (void)as_client(client_)->Reset();
  }
  started_ = false;
}

void WindowsWasapiOutputV1::release_resources() noexcept {
  stop();
  if (render_client_ != nullptr) as_render(render_client_)->Release();
  if (client_ != nullptr) as_client(client_)->Release();
  if (event_ != nullptr) CloseHandle(as_event(event_));
  client_ = nullptr;
  render_client_ = nullptr;
  event_ = nullptr;
  channels_ = 0;
  sample_rate_ = 0;
  buffer_frames_ = 0;
}

void WindowsWasapiOutputV1::unbind() noexcept { release_resources(); }

bool WindowsWasapiOutputV1::render(const float* const interleaved,
                                   const std::uint32_t frames) noexcept {
  if (!started_ || interleaved == nullptr || frames == 0U || frames > buffer_frames_) return false;
  UINT32 padding = 0U;
  if (FAILED(as_client(client_)->GetCurrentPadding(&padding)) ||
      frames > buffer_frames_ - (padding < buffer_frames_ ? padding : buffer_frames_)) {
    return false;
  }
  BYTE* destination = nullptr;
  if (FAILED(as_render(render_client_)->GetBuffer(frames, &destination)) || destination == nullptr) {
    return false;
  }
  std::memcpy(destination, interleaved,
              static_cast<std::size_t>(frames) * channels_ * sizeof(float));
  return SUCCEEDED(as_render(render_client_)->ReleaseBuffer(frames, 0U));
}

}  // namespace hibiki

#else

namespace hibiki {

WindowsWasapiOutputV1::~WindowsWasapiOutputV1() = default;
bool WindowsWasapiOutputV1::bind(const WasapiOutputConfigV1&) noexcept { return false; }
bool WindowsWasapiOutputV1::start() noexcept { return false; }
void WindowsWasapiOutputV1::stop() noexcept { started_ = false; }
void WindowsWasapiOutputV1::unbind() noexcept {
  stop();
  client_ = nullptr;
  render_client_ = nullptr;
  channels_ = 0;
  sample_rate_ = 0;
  buffer_frames_ = 0;
}
bool WindowsWasapiOutputV1::render(const float*, std::uint32_t) noexcept { return false; }
void WindowsWasapiOutputV1::release_resources() noexcept { unbind(); }

}  // namespace hibiki

#endif
