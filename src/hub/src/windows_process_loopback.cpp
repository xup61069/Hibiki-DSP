// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_process_loopback.hpp"

#if defined(_WIN32)

#include <audioclientactivationparams.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <wrl/implements.h>

#include <cstring>
#include <memory>
#include <new>

namespace hibiki {
namespace {

struct ActivationState final {
    ActivationState() : done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationState() {
        if (client != nullptr) client->Release();
        if (done != nullptr) CloseHandle(done);
    }

    HANDLE done{nullptr};
    HRESULT result{E_PENDING};
    IAudioClient* client{nullptr};
};

class ActivationHandler final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase,
                                          IActivateAudioInterfaceCompletionHandler> {
public:
    explicit ActivationHandler(std::shared_ptr<ActivationState> state) noexcept
        : state_(std::move(state)) {}

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activation_result = E_FAIL;
        IUnknown* activated = nullptr;
        if (operation == nullptr || FAILED(operation->GetActivateResult(&activation_result, &activated))) {
            activation_result = E_FAIL;
        }
        if (SUCCEEDED(activation_result) && activated != nullptr) {
            activation_result = activated->QueryInterface(__uuidof(IAudioClient),
                                                            reinterpret_cast<void**>(&state_->client));
        }
        if (activated != nullptr) activated->Release();
        state_->result = activation_result;
        if (state_->done != nullptr) SetEvent(state_->done);
        return S_OK;
    }

private:
    std::shared_ptr<ActivationState> state_;
};

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

}  // namespace

WindowsProcessLoopbackSourceV1::~WindowsProcessLoopbackSourceV1() { stop(); }

void WindowsProcessLoopbackSourceV1::set_degraded(const HRESULT error) noexcept {
    state_ = WindowsProcessLoopbackStateV1::Degraded;
    last_error_ = error;
}

HRESULT WindowsProcessLoopbackSourceV1::start(
    const WindowsProcessLoopbackConfigV1& config) {
    stop();
    if (config.process_id == 0U) {
        set_degraded(E_INVALIDARG);
        return E_INVALIDARG;
    }
    config_ = config;
    state_ = WindowsProcessLoopbackStateV1::Activating;
    last_error_ = S_OK;

    auto activation = std::make_shared<ActivationState>();
    if (activation->done == nullptr) {
        set_degraded(HRESULT_FROM_WIN32(GetLastError()));
        return last_error_;
    }
    Microsoft::WRL::ComPtr<ActivationHandler> handler;
    try {
        handler = Microsoft::WRL::Make<ActivationHandler>(activation);
    } catch (const std::bad_alloc&) {
        set_degraded(E_OUTOFMEMORY);
        return last_error_;
    }
    if (handler == nullptr) {
        set_degraded(E_OUTOFMEMORY);
        return last_error_;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS activation_params{};
    activation_params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activation_params.ProcessLoopbackParams.TargetProcessId = config.process_id;
    activation_params.ProcessLoopbackParams.ProcessLoopbackMode =
        config.include_process_tree ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
                                     : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT property;
    PropVariantInit(&property);
    property.vt = VT_BLOB;
    property.blob.cbSize = sizeof(activation_params);
    property.blob.pBlobData = reinterpret_cast<BYTE*>(&activation_params);

    IActivateAudioInterfaceAsyncOperation* operation = nullptr;
    HRESULT result = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &property, handler.Get(),
        &operation);
    if (operation != nullptr) operation->Release();
    if (FAILED(result)) {
        set_degraded(result);
        return result;
    }
    const DWORD wait_result = WaitForSingleObject(activation->done, 2000U);
    if (wait_result != WAIT_OBJECT_0) {
        set_degraded(wait_result == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                                                 : HRESULT_FROM_WIN32(GetLastError()));
        return last_error_;
    }
    if (FAILED(activation->result) || activation->client == nullptr) {
        set_degraded(FAILED(activation->result) ? activation->result : E_NOINTERFACE);
        return last_error_;
    }
    audio_client_ = activation->client;
    activation->client = nullptr;

    WAVEFORMATEX* mix_format = nullptr;
    result = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(result) || mix_format == nullptr || !is_float32(*mix_format)) {
        if (mix_format != nullptr) CoTaskMemFree(mix_format);
        set_degraded(FAILED(result) ? result : AUDCLNT_E_UNSUPPORTED_FORMAT);
        stop();
        return last_error_;
    }
    sample_rate_ = mix_format->nSamplesPerSec;
    channels_ = mix_format->nChannels;
    if ((config.requested_sample_rate != 0U && config.requested_sample_rate != sample_rate_) ||
        (config.requested_channels != 0U && config.requested_channels != channels_)) {
        CoTaskMemFree(mix_format);
        set_degraded(AUDCLNT_E_UNSUPPORTED_FORMAT);
        stop();
        return last_error_;
    }
    event_handle_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_handle_ == nullptr) {
        CoTaskMemFree(mix_format);
        set_degraded(HRESULT_FROM_WIN32(GetLastError()));
        stop();
        return last_error_;
    }
    constexpr REFERENCE_TIME kBufferDuration = 200000;  // 20 ms.
    result = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        kBufferDuration, 0, mix_format, nullptr);
    CoTaskMemFree(mix_format);
    if (FAILED(result)) {
        set_degraded(result);
        stop();
        return last_error_;
    }
    result = audio_client_->SetEventHandle(event_handle_);
    if (SUCCEEDED(result)) {
        result = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                            reinterpret_cast<void**>(&capture_client_));
    }
    if (SUCCEEDED(result)) result = audio_client_->GetBufferSize(&frames_per_buffer_);
    if (SUCCEEDED(result)) result = audio_client_->Start();
    if (FAILED(result)) {
        set_degraded(result);
        stop();
        return last_error_;
    }
    state_ = WindowsProcessLoopbackStateV1::Running;
    return S_OK;
}

void WindowsProcessLoopbackSourceV1::stop() noexcept {
    if (audio_client_ != nullptr) {
        (void)audio_client_->Stop();
    }
    if (capture_client_ != nullptr) {
        capture_client_->Release();
        capture_client_ = nullptr;
    }
    if (audio_client_ != nullptr) {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (event_handle_ != nullptr) {
        CloseHandle(event_handle_);
        event_handle_ = nullptr;
    }
    if (state_ != WindowsProcessLoopbackStateV1::Degraded) {
        state_ = WindowsProcessLoopbackStateV1::Stopped;
    }
    sample_rate_ = 0U;
    channels_ = 0U;
    frames_per_buffer_ = 0U;
    captured_frames_ = 0U;
    dropped_frames_ = 0U;
    config_ = {};
}

bool WindowsProcessLoopbackSourceV1::read(float* const interleaved,
                                          const std::uint32_t capacity_frames,
                                          std::uint32_t& frames_read) noexcept {
    frames_read = 0U;
    if (state_ != WindowsProcessLoopbackStateV1::Running || capture_client_ == nullptr ||
        interleaved == nullptr || capacity_frames == 0U || channels_ == 0U) {
        return false;
    }
    UINT32 packet_frames = 0U;
    HRESULT result = capture_client_->GetNextPacketSize(&packet_frames);
    if (FAILED(result)) {
        set_degraded(result);
        return false;
    }
    if (packet_frames == 0U) return true;
    if (packet_frames > capacity_frames) {
        if (SUCCEEDED(capture_client_->ReleaseBuffer(packet_frames))) {
            dropped_frames_ += packet_frames;
        } else {
            set_degraded(E_INVALIDARG);
        }
        return false;
    }
    BYTE* data = nullptr;
    UINT32 frames = 0U;
    DWORD flags = 0U;
    result = capture_client_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
    if (FAILED(result)) {
        set_degraded(result);
        return false;
    }
    if (frames > capacity_frames || frames > packet_frames || data == nullptr) {
        (void)capture_client_->ReleaseBuffer(frames);
        set_degraded(E_INVALIDARG);
        return false;
    }
    const auto sample_count = static_cast<std::size_t>(frames) * channels_;
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0U) {
        std::memset(interleaved, 0, sample_count * sizeof(float));
    } else {
        std::memcpy(interleaved, data, sample_count * sizeof(float));
    }
    result = capture_client_->ReleaseBuffer(frames);
    if (FAILED(result)) {
        set_degraded(result);
        return false;
    }
    frames_read = frames;
    captured_frames_ += frames;
    return true;
}

WindowsProcessLoopbackSnapshotV1 WindowsProcessLoopbackSourceV1::snapshot() const noexcept {
    return WindowsProcessLoopbackSnapshotV1{state_, config_.process_id, sample_rate_, channels_,
                                            frames_per_buffer_, captured_frames_, dropped_frames_,
                                            last_error_};
}

}  // namespace hibiki

#endif  // defined(_WIN32)
