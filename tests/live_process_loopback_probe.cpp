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
#include <cstdint>
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

struct RenderContext {
    IAudioClient* client{nullptr};
    IAudioRenderClient* render{nullptr};
    UINT32 buffer_frames{0U};
    double phase{0.0};
    double phase_step{0.0};
    std::uint32_t channels{2U};
};

HRESULT start_render(IMMDevice* const device, RenderContext& ctx,
                     const std::uint32_t expected_rate,
                     const std::uint32_t expected_channels) {
    if (device == nullptr) return E_INVALIDARG;
    WAVEFORMATEX* format = nullptr;
    HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(&ctx.client));
    if (SUCCEEDED(result)) result = ctx.client->GetMixFormat(&format);
    if (SUCCEEDED(result) &&
        (format == nullptr || !is_float32(*format) || format->nSamplesPerSec != expected_rate ||
         format->nChannels != expected_channels)) {
        result = AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    if (SUCCEEDED(result)) {
        result = ctx.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                        200000, 0, format, nullptr);
    }
    if (SUCCEEDED(result)) result = ctx.client->GetBufferSize(&ctx.buffer_frames);
    if (SUCCEEDED(result)) {
        result = ctx.client->GetService(__uuidof(IAudioRenderClient),
                                        reinterpret_cast<void**>(&ctx.render));
    }
    if (SUCCEEDED(result)) result = ctx.client->Start();
    ctx.phase_step = 2.0 * 3.14159265358979323846 * 220.0 /
                     static_cast<double>(expected_rate);
    ctx.channels = expected_channels;
    if (format != nullptr) CoTaskMemFree(format);
    return result;
}

HRESULT render_one_block(RenderContext& ctx) {
    UINT32 padding = 0U;
    HRESULT result = ctx.client->GetCurrentPadding(&padding);
    if (FAILED(result)) return result;
    const UINT32 available = ctx.buffer_frames > padding ? ctx.buffer_frames - padding : 0U;
    if (available == 0U) return S_OK;
    BYTE* data = nullptr;
    result = ctx.render->GetBuffer(available, &data);
    if (SUCCEEDED(result) && data != nullptr) {
        auto* samples = reinterpret_cast<float*>(data);
        for (UINT32 frame = 0U; frame < available; ++frame) {
            const float value = static_cast<float>(0.5 * std::sin(ctx.phase));
            ctx.phase += ctx.phase_step;
            if (ctx.phase > 2.0 * 3.14159265358979323846)
                ctx.phase -= 2.0 * 3.14159265358979323846;
            for (UINT32 channel = 0U; channel < ctx.channels; ++channel) {
                samples[static_cast<std::size_t>(frame) * ctx.channels + channel] = value;
            }
        }
        result = ctx.render->ReleaseBuffer(available, 0U);
    }
    return result;
}

void stop_render(RenderContext& ctx) {
    if (ctx.client != nullptr) (void)ctx.client->Stop();
    if (ctx.render != nullptr) ctx.render->Release();
    if (ctx.client != nullptr) ctx.client->Release();
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

    // Interleaved: start render first, then alternate render/read so the
    // loopback capture sees live audio rather than post-stop silence.
    RenderContext render_ctx{};
    const HRESULT start_render_result = start_render(device, render_ctx, sample_rate, channels);
    HRESULT render_result = start_render_result;

    std::array<float, 8U * 4096U> samples{};
    std::uint64_t captured = 0U;
    bool finite = true;
    bool nonzero = false;
    for (std::uint32_t attempt = 0U; attempt < 240U; ++attempt) {
        if (SUCCEEDED(render_result)) render_result = render_one_block(render_ctx);
        if (source.event_handle() != nullptr) (void)WaitForSingleObject(source.event_handle(), 5U);
        std::uint32_t frames = 0U;
        bool has_data = false;
        while (source.read(samples.data(), 4096U, frames)) {
            has_data = frames > 0U;
            if (frames > 0U) {
                captured += frames;
                for (std::size_t index = 0U; index < static_cast<std::size_t>(frames) * channels; ++index) {
                    finite = finite && std::isfinite(samples[index]);
                    nonzero = nonzero || std::abs(samples[index]) > 1.0e-7F;
                }
            }
            break;
        }
        if (!has_data) Sleep(3U);
        if (nonzero && captured >= static_cast<std::uint64_t>(sample_rate / 20U)) break;
        Sleep(3U);
    }
    stop_render(render_ctx);

    const auto snapshot = source.snapshot();
    source.stop();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    const bool pass = SUCCEEDED(start_render_result) && snapshot.state == hibiki::WindowsProcessLoopbackStateV1::Running &&
                      captured > 0U && finite && nonzero;
    std::printf("loopback=%s render=%s frames=%llu channels=%u rate=%u finite=%s nonzero=%s\n",
                pass ? "pass" : "fail", SUCCEEDED(start_render_result) ? "pass" : "fail",
                static_cast<unsigned long long>(captured), channels, sample_rate,
                finite ? "pass" : "fail", nonzero ? "pass" : "fail");
    return pass ? 0 : 5;
}
