#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace hibiki {

// This interface is compiled only for the optional, locally supplied VST3 SDK
// target.  It is deliberately a worker/control-plane adapter: the caller must
// keep it out of the Hibiki RT render callback and put it behind the sandbox
// watchdog/IPC boundary.
enum class Vst3SdkAudioLayoutV1 : std::uint8_t {
    mono = 1,
    stereo = 2,
    surround_51 = 6,
    surround_71 = 8,
};

struct Vst3SdkProcessorConfigV1 {
    double sample_rate{48000.0};
    std::uint32_t max_frames{4096};
    Vst3SdkAudioLayoutV1 layout{Vst3SdkAudioLayoutV1::stereo};
};

enum class Vst3SdkProcessResultV1 : std::uint8_t {
    ok,
    not_open,
    invalid_buffer,
    unsupported_block,
    plugin_error,
    non_finite_output,
};

class Vst3SdkProcessorV1 final {
public:
    static constexpr std::uint32_t kMaxChannels = 8;
    static constexpr std::uint32_t kMaxFrames = 4096;

    Vst3SdkProcessorV1() = default;
    ~Vst3SdkProcessorV1();

    Vst3SdkProcessorV1(const Vst3SdkProcessorV1&) = delete;
    Vst3SdkProcessorV1& operator=(const Vst3SdkProcessorV1&) = delete;

    // module_path and class_id are control-plane inputs.  The class id is the
    // canonical 32-hex-character VST3 UID emitted by the SDK catalog.
    bool open(const std::string& module_path,
              const std::string& class_id,
              const Vst3SdkProcessorConfigV1& config,
              std::string& error);
    void close() noexcept;

    bool is_open() const noexcept { return processing_; }
    std::uint32_t channels() const noexcept { return channels_; }
    std::uint32_t max_frames() const noexcept { return max_frames_; }
    std::uint32_t latency_samples() const noexcept { return latency_samples_; }

    // Caller owns both interleaved buffers.  No allocations, locks or waits
    // are performed here; plugin code itself is still untrusted and therefore
    // must run in the sandbox worker, never in Hibiki's RT graph.
    Vst3SdkProcessResultV1 process(const float* input,
                                   float* output,
                                   std::uint32_t frames) noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
    std::array<std::array<float, kMaxFrames>, kMaxChannels> input_planar_{};
    std::array<std::array<float, kMaxFrames>, kMaxChannels> output_planar_{};
    std::array<float*, kMaxChannels> input_ptrs_{};
    std::array<float*, kMaxChannels> output_ptrs_{};
    std::uint32_t channels_{0};
    std::uint32_t max_frames_{0};
    std::uint32_t latency_samples_{0};
    bool processing_{false};
};

} // namespace hibiki
