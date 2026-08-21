// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live WASAPI handoff probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <array>
#include <cstdint>
#include <cstdio>

#include "hibiki/windows_wasapi_handoff.hpp"

int main() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n",
                     static_cast<unsigned long>(init));
        return 2;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    WAVEFORMATEX* mix_format = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(result) && enumerator != nullptr) {
        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (SUCCEEDED(result) && device != nullptr) {
        IAudioClient* client = nullptr;
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&client));
        if (SUCCEEDED(result) && client != nullptr) result = client->GetMixFormat(&mix_format);
        if (client != nullptr) client->Release();
    }
    DWORD format_mask = 0U;
    if (mix_format != nullptr && mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        mix_format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        format_mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format)->dwChannelMask;
    }
    const bool usable_format = SUCCEEDED(result) && mix_format != nullptr &&
                               (mix_format->nChannels == 2U || mix_format->nChannels == 6U ||
                                mix_format->nChannels == 8U) &&
                               (mix_format->nSamplesPerSec == 44100U ||
                                mix_format->nSamplesPerSec == 48000U ||
                                mix_format->nSamplesPerSec == 96000U ||
                                mix_format->nSamplesPerSec == 192000U);
    if (!usable_format) {
        std::printf("wasapi=unavailable stage=format channels=%u rate=%u tag=%u bits=%u mask=0x%lx\n",
                    mix_format != nullptr ? mix_format->nChannels : 0U,
                    mix_format != nullptr ? mix_format->nSamplesPerSec : 0U,
                    mix_format != nullptr ? mix_format->wFormatTag : 0U,
                    mix_format != nullptr ? mix_format->wBitsPerSample : 0U,
                    static_cast<unsigned long>(format_mask));
        if (mix_format != nullptr) CoTaskMemFree(mix_format);
        if (device != nullptr) device->Release();
        if (enumerator != nullptr) enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }

    hibiki::WasapiOutputConfigV1 config;
    config.channels = mix_format->nChannels;
    config.sample_rate = mix_format->nSamplesPerSec;
    config.buffer_duration_ms = 20U;
    std::printf("format channels=%u rate=%u tag=%u bits=%u mask=0x%lx\n",
                config.channels, config.sample_rate, mix_format->wFormatTag,
                mix_format->wBitsPerSample, static_cast<unsigned long>(format_mask));
    CoTaskMemFree(mix_format);
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();

    hibiki::WindowsWasapiSinkHandoffV1 handoff;
    if (!handoff.start_initial(config, 128U)) {
        std::printf("wasapi=unavailable stage=start_initial\n");
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }

    bool initial_ready = false;
    for (std::uint32_t attempt = 0U; attempt < 200U; ++attempt) {
        const auto snapshot = handoff.snapshot();
        if (snapshot.primary.endpoint_ready) {
            initial_ready = true;
            break;
        }
        if (!snapshot.primary.running && snapshot.primary.degraded) break;
        Sleep(10U);
    }
    const auto initial_snapshot = handoff.snapshot();
    const bool began = initial_ready && handoff.begin(config, 128U, 30U);
    if (!began) {
        handoff.stop();
        std::printf("wasapi=unavailable stage=initial_ready_or_begin ready=%s running=%s degraded=%s\n",
                    initial_ready ? "pass" : "fail",
                    initial_snapshot.primary.running ? "pass" : "fail",
                    initial_snapshot.primary.degraded ? "pass" : "fail");
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }

    bool candidate_ready = false;
    for (std::uint32_t attempt = 0U; attempt < 200U; ++attempt) {
        if (handoff.prepare()) {
            candidate_ready = true;
            break;
        }
        const auto snapshot = handoff.snapshot();
        if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::RolledBack ||
            snapshot.state == hibiki::WasapiSinkHandoffStateV1::Degraded) {
            break;
        }
        Sleep(10U);
    }
    if (!candidate_ready) {
        handoff.stop();
        std::printf("wasapi=unavailable stage=candidate_ready\n");
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }

    std::array<float, 8U * 128U> silence{};
    constexpr std::uint32_t kFadeBlocks = 32U;
    bool rollback_pass = false;
    if (handoff.process(silence.data(), 128U, config.channels)) {
        handoff.rollback();
        const auto rollback_snapshot = handoff.snapshot();
        rollback_pass = rollback_snapshot.state == hibiki::WasapiSinkHandoffStateV1::RolledBack &&
                        rollback_snapshot.primary.running &&
                        !rollback_snapshot.secondary.running;
    }
    const bool retry_began = rollback_pass && handoff.begin(config, 128U, 30U);
    bool retry_ready = false;
    for (std::uint32_t attempt = 0U; retry_began && attempt < 200U; ++attempt) {
        if (handoff.prepare()) {
            retry_ready = true;
            break;
        }
        const auto snapshot = handoff.snapshot();
        if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::RolledBack ||
            snapshot.state == hibiki::WasapiSinkHandoffStateV1::Degraded) {
            break;
        }
        Sleep(10U);
    }
    bool submitted = true;
    for (std::uint32_t block = 0U; retry_ready && block < kFadeBlocks; ++block) {
        if (handoff.snapshot().state != hibiki::WasapiSinkHandoffStateV1::Fading) break;
        if (!handoff.process(silence.data(), 128U, config.channels)) submitted = false;
        Sleep(8U);
    }
    const auto before_commit = handoff.snapshot();
    const bool committed = retry_ready && submitted &&
                           before_commit.state == hibiki::WasapiSinkHandoffStateV1::ReadyToCommit &&
                           handoff.commit();
    const auto final_snapshot = handoff.snapshot();
    const bool rendered = final_snapshot.primary.rendered_blocks > 0U ||
                          final_snapshot.secondary.rendered_blocks > 0U;
    handoff.stop();
    if (SUCCEEDED(init)) CoUninitialize();
    std::printf("wasapi=%s initial=pass candidate=pass rollback=%s retry=%s fade=%s commit=%s rendered=%s state=%u submitted_blocks=%llu/%llu dropped=%u/%u\n",
                (committed && rendered && rollback_pass) ? "pass" : "fail",
                rollback_pass ? "pass" : "fail", retry_ready ? "pass" : "fail",
                submitted ? "pass" : "fail", committed ? "pass" : "fail",
                rendered ? "pass" : "fail",
                static_cast<unsigned>(before_commit.state),
                static_cast<unsigned long long>(before_commit.primary.submitted_blocks),
                static_cast<unsigned long long>(before_commit.secondary.submitted_blocks),
                before_commit.primary.dropped_blocks, before_commit.secondary.dropped_blocks);
    return committed && rendered ? 0 : 5;
}
