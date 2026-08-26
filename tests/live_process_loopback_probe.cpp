// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live process-loopback probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <thread>
#include <cstdio>

#include "hibiki/windows_process_loopback.hpp"

namespace {

bool is_float32(const WAVEFORMATEX& format) noexcept {
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32U) {
        return true;
    }
    if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE || format.wBitsPerSample != 32U ||
        format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return false;
    }
    const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
    return IsEqualGUID(extensible.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
}

HRESULT render_tone(IMMDevice* const device,
                    const std::uint32_t expected_rate,
                    const std::uint32_t expected_channels) {
    if (device == nullptr) return E_INVALIDARG;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* format = nullptr;
    HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(&client));
    if (SUCCEEDED(result)) result = client->GetMixFormat(&format);
    if (SUCCEEDED(result) &&
        (format == nullptr || !is_float32(*format) || format->nSamplesPerSec != expected_rate ||
         format->nChannels != expected_channels)) {
        result = AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    if (SUCCEEDED(result)) {
        result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                    200000, 0, format, nullptr);
    }
    UINT32 buffer_frames = 0U;
    if (SUCCEEDED(result)) result = client->GetBufferSize(&buffer_frames);
    if (SUCCEEDED(result)) {
        result = client->GetService(__uuidof(IAudioRenderClient),
                                    reinterpret_cast<void**>(&render));
    }
    if (SUCCEEDED(result)) result = client->Start();
    const double phase_step = 2.0 * 3.14159265358979323846 * 220.0 /
                              static_cast<double>(expected_rate);
    double phase = 0.0;
    for (std::uint32_t iteration = 0U; SUCCEEDED(result) && iteration < 120U; ++iteration) {
        UINT32 padding = 0U;
        result = client->GetCurrentPadding(&padding);
        if (FAILED(result)) break;
        const UINT32 available = buffer_frames > padding ? buffer_frames - padding : 0U;
        if (available == 0U) {
            Sleep(5U);
            continue;
        }
        BYTE* data = nullptr;
        result = render->GetBuffer(available, &data);
        if (SUCCEEDED(result) && data != nullptr) {
            auto* samples = reinterpret_cast<float*>(data);
            for (UINT32 frame = 0U; frame < available; ++frame) {
                const float value = static_cast<float>(0.001 * std::sin(phase));
                phase += phase_step;
                if (phase > 2.0 * 3.14159265358979323846) phase -= 2.0 * 3.14159265358979323846;
                for (UINT32 channel = 0U; channel < expected_channels; ++channel) {
                    samples[static_cast<std::size_t>(frame) * expected_channels + channel] = value;
                }
            }
            result = render->ReleaseBuffer(available, 0U);
        }
        Sleep(5U);
    }
    if (client != nullptr) (void)client->Stop();
    if (render != nullptr) render->Release();
    if (client != nullptr) client->Release();
    if (format != nullptr) CoTaskMemFree(format);
    return result;
}

}  // namespace

int main() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return 2;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    WAVEFORMATEX* format = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(result)) result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    IAudioClient* inspect_client = nullptr;
    if (SUCCEEDED(result)) {
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&inspect_client));
    }
    if (SUCCEEDED(result)) result = inspect_client->GetMixFormat(&format);
    const bool usable = SUCCEEDED(result) && format != nullptr && is_float32(*format) &&
                        format->nChannels > 0U && format->nChannels <= 8U &&
                        format->nSamplesPerSec >= 44100U && format->nSamplesPerSec <= 192000U;
    if (!usable) {
        std::printf("loopback=unavailable stage=format\n");
        if (format != nullptr) CoTaskMemFree(format);
        if (inspect_client != nullptr) inspect_client->Release();
        if (device != nullptr) device->Release();
        if (enumerator != nullptr) enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }
    const std::uint32_t channels = format->nChannels;
    const std::uint32_t sample_rate = format->nSamplesPerSec;
    CoTaskMemFree(format);
    if (inspect_client != nullptr) inspect_client->Release();

    hibiki::WindowsProcessLoopbackSourceV1 source;
    const auto start_result = source.start(
        hibiki::WindowsProcessLoopbackConfigV1{GetCurrentProcessId(), true, sample_rate, channels});
    if (FAILED(start_result)) {
        std::printf("loopback=unavailable stage=activate error=0x%08lx\n",
                    static_cast<unsigned long>(start_result));
        if (device != nullptr) device->Release();
        if (enumerator != nullptr) enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }
    std::array<float, 8U * 4096U> samples{};
    std::uint64_t captured = 0U;
    bool finite = true;
    bool nonzero = false;

    HRESULT render_result = E_FAIL;
    std::atomic<bool> render_done{false};
    std::thread render_thread([&]() {
        const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        render_result = render_tone(device, sample_rate, channels);
        if (SUCCEEDED(com_hr)) CoUninitialize();
        render_done.store(true);
    });

    for (std::uint32_t attempt = 0U; attempt < 600U; ++attempt) {
        if (nonzero && captured >= static_cast<std::uint64_t>(sample_rate / 20U)) break;
        if (!render_done.load() || captured < static_cast<std::uint64_t>(sample_rate / 20U)) {
            if (source.event_handle() != nullptr) {
                (void)WaitForSingleObject(source.event_handle(), 2U);
            }
            std::uint32_t frames = 0U;
            if (!source.read(samples.data(), 4096U, frames)) {
                if (render_done.load()) break;
                continue;
            }
            captured += frames;
            for (std::size_t index = 0U;
                 index < static_cast<std::size_t>(frames) * channels; ++index) {
                finite = finite && std::isfinite(samples[index]);
                nonzero = nonzero || std::abs(samples[index]) > 1.0e-7F;
            }
        } else {
            break;
        }
    }
    render_thread.join();
    const auto snapshot = source.snapshot();
    source.stop();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    const bool pass = SUCCEEDED(render_result) && snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Running &&
                      captured > 0U && finite && nonzero;
    std::printf("loopback=%s render=%s frames=%llu channels=%u rate=%u finite=%s nonzero=%s\n",
                pass ? "pass" : "fail", SUCCEEDED(render_result) ? "pass" : "fail",
                static_cast<unsigned long long>(captured), channels, sample_rate,
                finite ? "pass" : "fail", nonzero ? "pass" : "fail");
    return pass ? 0 : 5;
}
