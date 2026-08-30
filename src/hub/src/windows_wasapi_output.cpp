// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_wasapi_output.hpp"

#include <algorithm>
#include <cmath>
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
IAudioClock* as_clock(void* value) noexcept { return static_cast<IAudioClock*>(value); }
HANDLE as_event(void* value) noexcept { return static_cast<HANDLE>(value); }

bool valid_layout(const std::uint32_t channels) noexcept {
  return channels == 2U || channels == 6U || channels == 8U;
}

bool valid_rate(const std::uint32_t rate) noexcept {
  return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

DWORD expected_channel_mask(const std::uint32_t channels) noexcept;
bool channel_mask_allowed(std::uint32_t channels, DWORD mask) noexcept;

bool parse_sample_format(const WAVEFORMATEX* const format,
                         const std::uint32_t expected_channels,
                         const std::uint32_t expected_rate,
                         WasapiSampleEncodingV1& encoding,
                         std::uint32_t& bytes_per_sample) noexcept {
  if (format == nullptr || format->nChannels != expected_channels ||
      format->nSamplesPerSec != expected_rate) {
    return false;
  }
  GUID subformat{};
  std::uint32_t bits = format->wBitsPerSample;
  DWORD channel_mask = expected_channel_mask(expected_channels);
  if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
      format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    subformat = extensible->SubFormat;
    channel_mask = extensible->dwChannelMask;
    if (extensible->Samples.wValidBitsPerSample != 0U &&
        extensible->Samples.wValidBitsPerSample > bits) {
      return false;
    }
  } else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    subformat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  } else if (format->wFormatTag == WAVE_FORMAT_PCM) {
    subformat = KSDATAFORMAT_SUBTYPE_PCM;
  } else {
    return false;
  }
  if (!channel_mask_allowed(expected_channels, channel_mask)) return false;
  if (IsEqualGUID(subformat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && bits == 32U) {
    encoding = WasapiSampleEncodingV1::Float32;
    bytes_per_sample = 4U;
    return true;
  }
  if (!IsEqualGUID(subformat, KSDATAFORMAT_SUBTYPE_PCM)) return false;
  switch (bits) {
    case 16U:
      encoding = WasapiSampleEncodingV1::Pcm16;
      bytes_per_sample = 2U;
      return true;
    case 24U:
      encoding = WasapiSampleEncodingV1::Pcm24;
      bytes_per_sample = 3U;
      return true;
    case 32U:
      encoding = WasapiSampleEncodingV1::Pcm32;
      bytes_per_sample = 4U;
      return true;
    default:
      return false;
  }
}

template <typename Integer>
Integer float_to_pcm(const float sample, const double maximum, const Integer minimum) noexcept {
  if (!std::isfinite(sample)) return 0;
  const auto limited = std::clamp(static_cast<double>(sample), -1.0, 1.0);
  if (limited <= -1.0) return minimum;
  return static_cast<Integer>(std::llround(limited * maximum));
}

DWORD expected_channel_mask(const std::uint32_t channels) noexcept {
  switch (channels) {
    case 2U: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    case 6U: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                     SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    case 8U: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                     SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
                     SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
    default: return 0U;
  }
}

bool channel_mask_allowed(const std::uint32_t channels, const DWORD mask) noexcept {
  if (mask == expected_channel_mask(channels)) return true;
  if (channels == 6U) {
    constexpr DWORD kFiveOneSideMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
                                       SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY |
                                       SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
    return mask == kFiveOneSideMask;
  }
  return false;
}

WasapiOutputFailureV1 classify_failure(const HRESULT result) noexcept {
  if (result == AUDCLNT_E_DEVICE_INVALIDATED) {
    return WasapiOutputFailureV1::DeviceInvalidated;
  }
  if (result == AUDCLNT_E_SERVICE_NOT_RUNNING) {
    return WasapiOutputFailureV1::ServiceNotRunning;
  }
  return WasapiOutputFailureV1::Other;
}

}  // namespace

WindowsWasapiOutputV1::~WindowsWasapiOutputV1() { unbind(); }

bool WindowsWasapiOutputV1::bind(const WasapiOutputConfigV1& config) noexcept {
  unbind();
  failure_ = WasapiOutputFailureV1::None;
  if (!valid_layout(config.channels) || !valid_rate(config.sample_rate) ||
      config.buffer_duration_ms == 0U || config.buffer_duration_ms > 200U) {
    return false;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  WAVEFORMATEX* mix_format = nullptr;
  IAudioClient* client = nullptr;
  IAudioRenderClient* render_client = nullptr;
  IAudioClock* clock = nullptr;
  HANDLE event_handle = nullptr;
  auto cleanup = [&]() noexcept {
    if (render_client != nullptr) render_client->Release();
    if (clock != nullptr) clock->Release();
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

  WasapiSampleEncodingV1 encoding{};
  std::uint32_t bytes_per_sample = 0U;
  if (!parse_sample_format(mix_format, config.channels, config.sample_rate, encoding,
                           bytes_per_sample)) {
    cleanup();
    return false;
  }

  const REFERENCE_TIME duration = static_cast<REFERENCE_TIME>(config.buffer_duration_ms) * 10000;
  const DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, duration, 0, mix_format,
                                nullptr)) ||
      FAILED(client->GetBufferSize(&buffer_frames_)) ||
      FAILED(client->GetService(IID_PPV_ARGS(&render_client))) || render_client == nullptr ||
      FAILED(client->GetService(IID_PPV_ARGS(&clock))) || clock == nullptr ||
      (event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr)) == nullptr ||
      FAILED(client->SetEventHandle(event_handle))) {
    cleanup();
    buffer_frames_ = 0;
    return false;
  }

  client_ = client;
  render_client_ = render_client;
  clock_ = clock;
  event_ = event_handle;
  channels_ = config.channels;
  sample_rate_ = config.sample_rate;
  encoding_ = encoding;
  bytes_per_sample_ = bytes_per_sample;
  client = nullptr;
  render_client = nullptr;
  clock = nullptr;
  event_handle = nullptr;
  cleanup();
  return true;
}

bool WindowsWasapiOutputV1::start() noexcept {
  if (!bound() || started_) {
    failure_ = WasapiOutputFailureV1::Other;
    return false;
  }
  const auto result = as_client(client_)->Start();
  if (FAILED(result)) {
    failure_ = classify_failure(result);
    return false;
  }
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
  if (clock_ != nullptr) as_clock(clock_)->Release();
  if (client_ != nullptr) as_client(client_)->Release();
  if (event_ != nullptr) CloseHandle(as_event(event_));
  client_ = nullptr;
  render_client_ = nullptr;
  clock_ = nullptr;
  event_ = nullptr;
  channels_ = 0;
  sample_rate_ = 0;
  buffer_frames_ = 0;
  encoding_ = WasapiSampleEncodingV1::Float32;
  failure_ = WasapiOutputFailureV1::None;
  bytes_per_sample_ = 0;
}

void WindowsWasapiOutputV1::unbind() noexcept { release_resources(); }

bool WindowsWasapiOutputV1::render(const float* const interleaved,
                                   const std::uint32_t frames) noexcept {
  failure_ = WasapiOutputFailureV1::None;
  if (!started_ || interleaved == nullptr || frames == 0U || frames > buffer_frames_) {
    failure_ = WasapiOutputFailureV1::Other;
    return false;
  }
  UINT32 padding = 0U;
  const auto padding_result = as_client(client_)->GetCurrentPadding(&padding);
  if (FAILED(padding_result)) {
    failure_ = classify_failure(padding_result);
    return false;
  }
  if (frames > buffer_frames_ - (padding < buffer_frames_ ? padding : buffer_frames_)) {
    return false;
  }
  const auto samples = static_cast<std::size_t>(frames) * channels_;
  for (std::size_t index = 0U; index < samples; ++index) {
    if (!std::isfinite(interleaved[index])) return false;
  }
  BYTE* destination = nullptr;
  const auto get_buffer_result = as_render(render_client_)->GetBuffer(frames, &destination);
  if (FAILED(get_buffer_result)) {
    // A failed acquisition does not own a render buffer. Releasing it here
    // would violate the WASAPI GetBuffer/ReleaseBuffer pairing contract.
    failure_ = classify_failure(get_buffer_result);
    return false;
  }
  if (destination == nullptr) {
    // GetBuffer succeeded, so the acquisition must still be balanced even
    // though the COM boundary returned an unusable destination pointer.
    const auto release_result = as_render(render_client_)->ReleaseBuffer(frames, 0U);
    failure_ = FAILED(release_result) ? classify_failure(release_result)
                                      : WasapiOutputFailureV1::Other;
    return false;
  }
  if (encoding_ == WasapiSampleEncodingV1::Float32) {
    std::memcpy(destination, interleaved, samples * sizeof(float));
  } else if (encoding_ == WasapiSampleEncodingV1::Pcm16) {
    auto* output = reinterpret_cast<std::int16_t*>(destination);
    for (std::size_t index = 0U; index < samples; ++index) {
      output[index] = float_to_pcm<std::int16_t>(interleaved[index], 32767.0, INT16_MIN);
    }
  } else if (encoding_ == WasapiSampleEncodingV1::Pcm24) {
    for (std::size_t index = 0U; index < samples; ++index) {
      const auto value = float_to_pcm<std::int32_t>(interleaved[index], 8388607.0, INT32_MIN);
      const auto offset = index * 3U;
      destination[offset] = static_cast<BYTE>(value & 0xFF);
      destination[offset + 1U] = static_cast<BYTE>((value >> 8U) & 0xFF);
      destination[offset + 2U] = static_cast<BYTE>((value >> 16U) & 0xFF);
    }
  } else {
    auto* output = reinterpret_cast<std::int32_t*>(destination);
    for (std::size_t index = 0U; index < samples; ++index) {
      output[index] = float_to_pcm<std::int32_t>(interleaved[index], 2147483647.0, INT32_MIN);
    }
  }
  const auto release_result = as_render(render_client_)->ReleaseBuffer(frames, 0U);
  if (FAILED(release_result)) failure_ = classify_failure(release_result);
  return SUCCEEDED(release_result);
}

bool WindowsWasapiOutputV1::wait_for_buffer(const std::uint32_t timeout_ms) noexcept {
  if (!started_ || event_ == nullptr) return false;
  const auto wait_result = WaitForSingleObject(as_event(event_), timeout_ms);
  if (wait_result == WAIT_TIMEOUT) {
    failure_ = WasapiOutputFailureV1::Timeout;
    return false;
  }
  if (wait_result == WAIT_FAILED) {
    failure_ = WasapiOutputFailureV1::Other;
    return false;
  }
  failure_ = WasapiOutputFailureV1::None;
  return wait_result == WAIT_OBJECT_0;
}

bool WindowsWasapiOutputV1::read_clock(WasapiClockSampleV1& sample) const noexcept {
  sample = {};
  if (!bound() || clock_ == nullptr) return false;
  UINT64 device_position = 0U;
  UINT64 qpc_position = 0U;
  if (FAILED(as_clock(clock_)->GetPosition(&device_position, &qpc_position))) return false;
  sample.device_position = device_position;
  sample.qpc_position = qpc_position;
  return true;
}

WindowsWasapiSinkWorkerV1::~WindowsWasapiSinkWorkerV1() { stop(); }

bool WindowsWasapiSinkWorkerV1::start(const WasapiOutputConfigV1& config,
                                      const std::uint32_t block_frames) noexcept {
  stop();
  if (!valid_layout(config.channels) || !valid_rate(config.sample_rate) || block_frames == 0U ||
      block_frames > kMaxFrames) {
    return false;
  }
  if (!slots_) {
    try {
      slots_ = std::make_unique<std::array<Slot, kSlotCount>>();
    } catch (...) {
      degraded_.store(true, std::memory_order_release);
      return false;
    }
  }
  for (auto& slot : *slots_) {
    slot.ready_sequence.store(0U, std::memory_order_relaxed);
    slot.frames = 0U;
    slot.channels = 0U;
  }
  channels_ = config.channels;
  sample_rate_ = config.sample_rate;
  block_frames_ = block_frames;
  producer_sequence_.store(0U, std::memory_order_release);
  consumer_sequence_.store(0U, std::memory_order_release);
  dropped_blocks_.store(0U, std::memory_order_release);
  submitted_blocks_.store(0U, std::memory_order_release);
  rendered_blocks_.store(0U, std::memory_order_release);
  clock_request_sequence_.store(0U, std::memory_order_release);
  clock_source_frames_.store(0.0, std::memory_order_release);
  clock_sink_frames_.store(0.0, std::memory_order_release);
  clock_elapsed_seconds_.store(0.0, std::memory_order_release);
  source_step_.store(1.0, std::memory_order_release);
  drift_ppm_.store(0.0, std::memory_order_release);
  endpoint_ready_.store(false, std::memory_order_release);
  degraded_.store(false, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  try {
    worker_ = std::thread([this, config, block_frames] { run(config, block_frames); });
  } catch (...) {
    running_.store(false, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
    return false;
  }
  return true;
}

void WindowsWasapiSinkWorkerV1::stop() noexcept {
  stop_requested_.store(true, std::memory_order_release);
  if (worker_.joinable()) worker_.join();
  running_.store(false, std::memory_order_release);
  endpoint_ready_.store(false, std::memory_order_release);
}

bool WindowsWasapiSinkWorkerV1::recover_output(
    WindowsWasapiOutputV1& output,
    const WasapiOutputConfigV1& config) noexcept {
  const auto failure = output.failure();
  if (failure != WasapiOutputFailureV1::DeviceInvalidated &&
      failure != WasapiOutputFailureV1::ServiceNotRunning) {
    return false;
  }
  endpoint_ready_.store(false, std::memory_order_release);
  output.unbind();
  constexpr std::uint32_t kRecoveryAttempts = 10U;
  for (std::uint32_t attempt = 0U;
       attempt < kRecoveryAttempts && !stop_requested_.load(std::memory_order_acquire);
       ++attempt) {
    if (output.bind(config) && output.start()) {
      endpoint_ready_.store(true, std::memory_order_release);
      return true;
    }
    Sleep(100U);
  }
  degraded_.store(true, std::memory_order_release);
  return false;
}

void WindowsWasapiSinkWorkerV1::observe_clock(const double source_frames,
                                              const double sink_frames,
                                              const double elapsed_seconds) noexcept {
  if (!std::isfinite(source_frames) || !std::isfinite(sink_frames) ||
      !std::isfinite(elapsed_seconds) || source_frames <= 0.0 || sink_frames <= 0.0 ||
      elapsed_seconds <= 0.0) {
    return;
  }

  auto claimed_sequence = clock_request_sequence_.load(std::memory_order_relaxed);
  if ((claimed_sequence & 1U) != 0U ||
      !clock_request_sequence_.compare_exchange_strong(
          claimed_sequence, claimed_sequence + 1U, std::memory_order_acq_rel,
          std::memory_order_relaxed)) {
    return;
  }
  clock_source_frames_.store(source_frames, std::memory_order_relaxed);
  clock_sink_frames_.store(sink_frames, std::memory_order_relaxed);
  clock_elapsed_seconds_.store(elapsed_seconds, std::memory_order_relaxed);
  clock_request_sequence_.store(claimed_sequence + 2U, std::memory_order_release);
}

bool WindowsWasapiSinkWorkerV1::take_clock_request(
    std::uint64_t& applied_sequence, double& source_frames, double& sink_frames,
    double& elapsed_seconds) noexcept {
  const auto before = clock_request_sequence_.load(std::memory_order_acquire);
  if (before == applied_sequence || (before & 1U) != 0U) return false;

  const auto source = clock_source_frames_.load(std::memory_order_relaxed);
  const auto sink = clock_sink_frames_.load(std::memory_order_relaxed);
  const auto elapsed = clock_elapsed_seconds_.load(std::memory_order_relaxed);
  const auto after = clock_request_sequence_.load(std::memory_order_acquire);
  if (before != after || (after & 1U) != 0U) return false;

  source_frames = source;
  sink_frames = sink;
  elapsed_seconds = elapsed;
  applied_sequence = after;
  return true;
}

bool WindowsWasapiSinkWorkerV1::submit(const float* const interleaved,
                                       const std::uint32_t frames,
                                       const std::uint32_t channels) noexcept {
  return submit_scaled(interleaved, frames, channels, 1.0F);
}

bool WindowsWasapiSinkWorkerV1::submit_scaled(const float* const interleaved,
                                              const std::uint32_t frames,
                                              const std::uint32_t channels,
                                              const float gain) noexcept {
  if (!running_.load(std::memory_order_acquire) || interleaved == nullptr || frames == 0U ||
      frames > kMaxFrames || channels != channels_ || !std::isfinite(gain) || gain < 0.0F ||
      gain > 1.0F) {
    return false;
  }
  const auto samples = static_cast<std::size_t>(frames) * channels;
  for (std::size_t index = 0U; index < samples; ++index) {
    if (!std::isfinite(interleaved[index])) return false;
  }
  const auto producer = producer_sequence_.load(std::memory_order_relaxed);
  const auto consumer = consumer_sequence_.load(std::memory_order_acquire);
  if (producer - consumer >= kSlotCount) {
    dropped_blocks_.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  if (!slots_) return false;
  auto& slot = (*slots_)[producer % kSlotCount];
  if (gain == 1.0F) {
    std::copy_n(interleaved, samples, slot.samples.data());
  } else {
    for (std::size_t index = 0U; index < samples; ++index) {
      slot.samples[index] = interleaved[index] * gain;
    }
  }
  slot.frames = frames;
  slot.channels = channels;
  slot.ready_sequence.store(producer + 1U, std::memory_order_release);
  producer_sequence_.store(producer + 1U, std::memory_order_release);
  submitted_blocks_.fetch_add(1U, std::memory_order_relaxed);
  return true;
}

bool WindowsWasapiSinkWorkerV1::pop(float* const interleaved,
                                    const std::uint32_t output_capacity_frames,
                                    std::uint32_t& frames,
                                    std::uint32_t& channels) noexcept {
  frames = 0U;
  channels = 0U;
  if (interleaved == nullptr) return false;
  const auto consumer = consumer_sequence_.load(std::memory_order_relaxed);
  const auto producer = producer_sequence_.load(std::memory_order_acquire);
  if (consumer == producer) return false;
  if (!slots_) return false;
  auto& slot = (*slots_)[consumer % kSlotCount];
  if (slot.ready_sequence.load(std::memory_order_acquire) != consumer + 1U ||
      slot.frames == 0U || slot.frames > output_capacity_frames || slot.channels != channels_) {
    return false;
  }
  const auto samples = static_cast<std::size_t>(slot.frames) * slot.channels;
  std::copy_n(slot.samples.data(), samples, interleaved);
  frames = slot.frames;
  channels = slot.channels;
  consumer_sequence_.store(consumer + 1U, std::memory_order_release);
  return true;
}

void WindowsWasapiSinkWorkerV1::run(WasapiOutputConfigV1 config,
                                    const std::uint32_t block_frames) noexcept {
  HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_initialized = SUCCEEDED(com_result);
  if (!com_initialized && com_result != RPC_E_CHANGED_MODE) {
    degraded_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    return;
  }
  WindowsWasapiOutputV1 output;
  if (!output.bind(config) || !output.start()) {
    degraded_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    if (com_initialized) CoUninitialize();
    return;
  }
  endpoint_ready_.store(true, std::memory_order_release);
  std::array<float, kMaxChannels * kMaxFrames> block{};
  std::array<float, kMaxChannels * kMaxFrames> rendered{};
  if (!sink_model_.prepare(config.channels, 1.0)) {
    degraded_.store(true, std::memory_order_release);
    output.stop();
    endpoint_ready_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    if (com_initialized) CoUninitialize();
    return;
  }
  std::uint64_t applied_clock_sequence = 0U;
  std::uint64_t source_position = 0U;
  std::uint64_t previous_source_position = 0U;
  WasapiClockSampleV1 previous_clock{};
  bool have_previous_clock = false;
  LARGE_INTEGER qpc_frequency{};
  const bool have_qpc_frequency = QueryPerformanceFrequency(&qpc_frequency) != FALSE &&
                                  qpc_frequency.QuadPart > 0;
  while (!stop_requested_.load(std::memory_order_acquire)) {
    double requested_source_frames = 0.0;
    double requested_sink_frames = 0.0;
    double requested_elapsed_seconds = 0.0;
    if (take_clock_request(applied_clock_sequence, requested_source_frames,
                           requested_sink_frames, requested_elapsed_seconds)) {
      sink_model_.observe_clock(requested_source_frames, requested_sink_frames,
                                requested_elapsed_seconds);
      source_step_.store(sink_model_.snapshot().source_step, std::memory_order_release);
      drift_ppm_.store(sink_model_.snapshot().drift_ppm, std::memory_order_release);
    }
    if (!output.wait_for_buffer(10U)) {
      const auto failure = output.failure();
      if (failure == WasapiOutputFailureV1::DeviceInvalidated ||
          failure == WasapiOutputFailureV1::ServiceNotRunning) {
        if (!recover_output(output, config)) break;
        have_previous_clock = false;
        previous_clock = {};
        previous_source_position = source_position;
      }
      continue;
    }
    std::uint32_t frames = 0U;
    std::uint32_t channels = 0U;
    if (!pop(block.data(), kMaxFrames, frames, channels)) {
      frames = block_frames;
      channels = channels_;
      std::fill_n(block.data(), static_cast<std::size_t>(frames) * channels, 0.0F);
    }
    source_position += frames;
    std::size_t rendered_frames = 0U;
    if (!sink_model_.process(block.data(), frames, rendered.data(), kMaxFrames, rendered_frames)) {
      degraded_.store(true, std::memory_order_release);
      continue;
    }
    if (rendered_frames > 0U && output.render(rendered.data(), static_cast<std::uint32_t>(rendered_frames))) {
      rendered_blocks_.fetch_add(1U, std::memory_order_relaxed);
    } else {
      const auto failure = output.failure();
      if (failure == WasapiOutputFailureV1::DeviceInvalidated ||
          failure == WasapiOutputFailureV1::ServiceNotRunning) {
        if (!recover_output(output, config)) break;
        have_previous_clock = false;
        previous_clock = {};
        previous_source_position = source_position;
        continue;
      }
      degraded_.store(true, std::memory_order_release);
    }
    WasapiClockSampleV1 current_clock{};
    if (have_qpc_frequency && output.read_clock(current_clock)) {
      if (have_previous_clock && current_clock.device_position >= previous_clock.device_position &&
          current_clock.qpc_position > previous_clock.qpc_position &&
          source_position >= previous_source_position) {
        const auto sink_delta = current_clock.device_position - previous_clock.device_position;
        const auto source_delta = source_position - previous_source_position;
        const auto qpc_delta = current_clock.qpc_position - previous_clock.qpc_position;
        const double elapsed = static_cast<double>(qpc_delta) /
                               static_cast<double>(qpc_frequency.QuadPart);
        if (sink_delta > 0U && source_delta > 0U && elapsed > 0.0) {
          sink_model_.observe_clock(static_cast<double>(source_delta),
                                    static_cast<double>(sink_delta), elapsed);
          source_step_.store(sink_model_.snapshot().source_step, std::memory_order_release);
          drift_ppm_.store(sink_model_.snapshot().drift_ppm, std::memory_order_release);
        }
      }
      previous_clock = current_clock;
      previous_source_position = source_position;
      have_previous_clock = true;
    }
  }
  output.stop();
  endpoint_ready_.store(false, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  if (com_initialized) CoUninitialize();
}

WasapiSinkWorkerSnapshotV1 WindowsWasapiSinkWorkerV1::snapshot() const noexcept {
  return WasapiSinkWorkerSnapshotV1{
      running_.load(std::memory_order_acquire),
      endpoint_ready_.load(std::memory_order_acquire),
      degraded_.load(std::memory_order_acquire),
      channels_,
      sample_rate_,
      block_frames_,
      dropped_blocks_.load(std::memory_order_acquire),
      submitted_blocks_.load(std::memory_order_acquire),
      rendered_blocks_.load(std::memory_order_acquire),
      source_step_.load(std::memory_order_acquire),
      drift_ppm_.load(std::memory_order_acquire)};
}

}  // namespace hibiki

#else

namespace hibiki {

WindowsWasapiOutputV1::~WindowsWasapiOutputV1() = default;
bool WindowsWasapiOutputV1::bind(const WasapiOutputConfigV1&) noexcept { return false; }
bool WindowsWasapiOutputV1::start() noexcept { return false; }
void WindowsWasapiOutputV1::stop() noexcept { started_ = false; }
void WindowsWasapiOutputV1::release_resources() noexcept {
  stop();
  client_ = nullptr;
  render_client_ = nullptr;
  clock_ = nullptr;
  channels_ = 0;
  sample_rate_ = 0;
  buffer_frames_ = 0;
  encoding_ = WasapiSampleEncodingV1::Float32;
  failure_ = WasapiOutputFailureV1::None;
  bytes_per_sample_ = 0;
}
bool WindowsWasapiOutputV1::render(const float*, std::uint32_t) noexcept { return false; }
bool WindowsWasapiOutputV1::wait_for_buffer(std::uint32_t) noexcept { return false; }
bool WindowsWasapiOutputV1::read_clock(WasapiClockSampleV1&) const noexcept { return false; }
void WindowsWasapiOutputV1::unbind() noexcept { release_resources(); }

WindowsWasapiSinkWorkerV1::~WindowsWasapiSinkWorkerV1() { stop(); }
bool WindowsWasapiSinkWorkerV1::start(const WasapiOutputConfigV1&, std::uint32_t) noexcept {
  stop();
  return false;
}
void WindowsWasapiSinkWorkerV1::stop() noexcept {
  stop_requested_.store(true, std::memory_order_release);
  if (worker_.joinable()) worker_.join();
  running_.store(false, std::memory_order_release);
}
bool WindowsWasapiSinkWorkerV1::submit(const float*, std::uint32_t, std::uint32_t) noexcept {
  return false;
}
bool WindowsWasapiSinkWorkerV1::submit_scaled(const float*, std::uint32_t, std::uint32_t,
                                              float) noexcept {
  return false;
}
void WindowsWasapiSinkWorkerV1::observe_clock(double, double, double) noexcept {}
WasapiSinkWorkerSnapshotV1 WindowsWasapiSinkWorkerV1::snapshot() const noexcept { return {}; }

}  // namespace hibiki

#endif
