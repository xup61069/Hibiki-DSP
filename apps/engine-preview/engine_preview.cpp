// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/driver_stream_ring_v1.h"
#include "hibiki/engine_control.hpp"
#include "hibiki/noise_suppressor.hpp"
#include "hibiki/output_sink.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/session_catalog.hpp"
#include "hibiki/session_command_queue.hpp"
#include "hibiki/session_route_rules.hpp"
#include "hibiki/tab_bridge.hpp"
#include "hibiki/windows_audio_session_route.hpp"
#include "hibiki/windows_device_catalog.hpp"
#include "hibiki/windows_process_loopback_lane.hpp"
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"
#include "hibiki/wav_ir.hpp"
#include "hibiki/plugin_host.hpp"
#include "hibiki/vst3_sandbox.hpp"

#include <Windows.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic_bool g_stop{false};

struct IrPrepareState final {
    hibiki::AudioEngineModel* engine{nullptr};
    hibiki::IrPhaseResolutionV1 resolution{};
    std::string path{};
    bool prepared{false};
};

struct SystemVolumeState final {
    hibiki::WindowsVolumeBroker broker{};
    hibiki::WindowsVolumeLinkV1 link{};
    bool enabled{false};
    bool bound{false};
    bool have_last_engine_state{false};
    hibiki::OutputGroupVolumeStateV1 last_engine_state{};
};

struct SessionRoutingState final {
    hibiki::WindowsAudioSessionRouteCoordinatorV1 coordinator{};
    hibiki::SessionCommandQueueV1 queue{};
    hibiki::SessionCatalogSnapshotStoreV1 catalog_store{};
    hibiki::SessionRouteRuleStoreV1 rules{};
    std::array<wchar_t, 260U> endpoint_id{};
    std::uint16_t endpoint_id_chars{0U};
    bool requested{false};
    bool bound{false};
};

struct WasapiOutputState final {
    hibiki::AudioEngineModel* engine{nullptr};
    hibiki::WasapiOutputConfigV1 config{};
    std::string endpoint_id{};
    std::string display_name{};
    std::uint32_t block_frames{128U};
    bool requested{false};
    bool active{false};
    bool test_tone_enabled{false};
    bool driver_loopback_enabled{false};
    bool wav_source_enabled{false};
};

constexpr std::uint32_t kTabBridgeMaxFrames = 4096U;
constexpr std::uint32_t kTabBridgeMaxOutputChannels = 8U;

struct TabBridgeState final {
    hibiki::TabBridgeServer server{};
    hibiki::TabCaptureQueueV1 queue{};
    hibiki::BasicNoiseSuppressorV1 noise_suppressor{};
    std::vector<float> input_buffer{};
    std::vector<float> output_buffer{};
    std::vector<hibiki::RtLaneInputV1> lane_inputs{};
    hibiki::TabLaneEffectsV1 effects{};
    bool requested{false};
    bool listening{false};
    std::uint64_t received_blocks{0U};
};

constexpr std::uint32_t kTestToneInputChannels = 2U;
constexpr std::uint32_t kTestToneMaxFrames = 4096U;
constexpr std::uint32_t kTestToneMaxOutputChannels = 8U;

struct TestToneState final {
    std::array<float, kTestToneMaxFrames * kTestToneInputChannels> input{};
    std::array<float, kTestToneMaxFrames * kTestToneMaxOutputChannels> output{};
    std::array<hibiki::RtLaneInputV1, 1U> lanes{};
    double phase{0.0};
    std::uint32_t sample_rate{0U};
    std::uint32_t block_frames{0U};
};

constexpr std::uint32_t kDriverLoopbackInputChannels = 2U;
constexpr std::uint32_t kDriverLoopbackMaxFrames = 4096U;
// Bounded ASCII identifier below the 40-byte wire GUID capacity; it never
// leaves this process except inside validated v1 packets.
constexpr char kDriverLoopbackEndpointGuid[] = "hibiki-driver-loopback-v1";

struct DriverLoopbackState final {
    std::array<float, kDriverLoopbackMaxFrames * kDriverLoopbackInputChannels> input{};
    std::array<float, kTestToneMaxFrames * kTestToneMaxOutputChannels> processed{};
    std::array<float, kTestToneMaxFrames * kTestToneMaxOutputChannels> decode_samples{};
    std::array<float, kTestToneMaxFrames * kTestToneMaxOutputChannels> output{};
    std::array<std::uint8_t, HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1> packet{};
    std::array<std::uint8_t, HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1> popped{};
    std::array<hibiki::RtLaneInputV1, 1U> lanes{};
    hibiki_driver_stream_ring_v1 ring{};
    double phase{0.0};
    std::uint64_t sequence{0U};
    std::uint64_t rendered_blocks{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t block_frames{0U};
    std::uint32_t encode_failures{0U};
    std::uint32_t push_failures{0U};
    std::uint32_t pop_failures{0U};
    std::uint32_t deliver_failures{0U};
};

constexpr std::uint32_t kWavSourceMaxFrames = 4096U;
constexpr std::uint32_t kWavSourceMaxOutputChannels = kTestToneMaxOutputChannels;
constexpr std::uint32_t kOfflineRenderOutputSampleRateV1 = 48000U;
constexpr std::uint16_t kOfflineRenderOutputChannelsV1 = 2U;

struct OfflineRenderResult final {
    std::size_t frames{0U};
    std::uint32_t file_sample_rate{0U};
    std::uint32_t output_sample_rate{0U};
    bool file_rate_matches{false};
    bool resampled{false};
};
constexpr std::size_t kWavSourceResampleFlushInputFramesV1 = 64U;

// Bounded WAV file playback source. The file is decoded once on the control
// plane before streaming starts; the render loop only copies already decoded
// samples into a lane and never touches the filesystem.
struct WavFileSourceState final {
    hibiki::IrWavDataV1 data{};
    std::vector<float> input_block{};
    std::vector<float> output_block{};
    std::array<hibiki::RtLaneInputV1, 1U> lanes{};
    std::size_t next_frame{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t file_sample_rate{0U};
    std::uint32_t block_frames{0U};
    std::uint64_t rendered_blocks{0U};
    std::uint64_t frames_rendered{0U};
    std::uint64_t failed_blocks{0U};
    std::uint32_t consecutive_failures{0U};
    bool requested{false};
    bool loop{false};
    bool prepared{false};
    bool active{false};
    bool eof{false};
};

constexpr std::uint32_t kWavSourceFailureLimit = 100U;

constexpr std::size_t kMaxProcessDeliverySources = 8U;
constexpr std::uint32_t kProcessLoopbackMaxFrames = 4096U;
constexpr std::uint32_t kProcessDeliveryFailureLimit = 100U;

struct ProcessDeliveryLane final {
    hibiki::WindowsProcessLoopbackSourceV1 source{};
    std::string lane_id{};
    std::uint32_t process_id{0U};
    bool active{false};
    std::uint32_t consecutive_failures{0U};
};

struct ProcessDeliveryState final {
    std::array<ProcessDeliveryLane, kMaxProcessDeliverySources> lanes{};
    std::vector<float> input_buffer{};
    std::vector<float> output_buffer{};
    std::vector<hibiki::RtLaneInputV1> lane_inputs{};
    hibiki::GraphConfigV1 graph{};
    hibiki::WindowsProcessLoopbackBlockV1 last_block{};
    std::uint64_t rendered_blocks{0U};
    std::uint64_t dropped_blocks{0U};
    bool graph_ready{false};
};

constexpr std::size_t kVst3LaneRingCapacityFrames = 4096U;

struct Vst3LaneState final {
    hibiki::PluginHostModel host{};
    hibiki::Vst3SandboxProcess sandbox{};
    std::vector<float> ring_storage{};
    std::vector<float> tap_buffer{};
    std::vector<float> worker_output{};
    std::wstring worker_executable{};
    std::wstring plugin_path{};
    std::uint64_t request_id{1U};
    std::uint64_t block_start{0U};
    std::uint64_t processed_blocks{0U};
    std::uint64_t failed_blocks{0U};
    std::uint32_t channels{0U};
    std::size_t block_frames{0U};
    double sample_rate{0.0};
    std::uint64_t tap_sequence{0U};
    bool requested{false};
    bool launched{false};
};

bool supported_wasapi_layout(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
}

bool supported_wasapi_rate(const std::uint32_t sample_rate) noexcept {
    return sample_rate == 44100U || sample_rate == 48000U ||
           sample_rate == 96000U || sample_rate == 192000U;
}

bool start_default_wasapi_output(
    WasapiOutputState& state,
    hibiki::AudioEngineModel& engine,
    const hibiki::PhysicalDeviceCatalogV1& catalog) noexcept {
    state.active = false;
    state.endpoint_id.clear();
    state.display_name.clear();
    state.config = {};
    const auto* const descriptor = catalog.default_device(hibiki::PhysicalDeviceFlowV1::Render);
    if (descriptor == nullptr || descriptor->availability !=
                                     hibiki::PhysicalDeviceAvailabilityV1::Active ||
        descriptor->endpoint_id.empty() || !supported_wasapi_layout(descriptor->channels) ||
        !supported_wasapi_rate(descriptor->sample_rate)) {
        return false;
    }

    const auto block_frames = descriptor->buffer_frames == 0U
                                  ? 128U
                                  : std::clamp(descriptor->buffer_frames, 16U, 4096U);
    hibiki::WasapiOutputConfigV1 config{
        std::wstring(descriptor->endpoint_id.begin(), descriptor->endpoint_id.end()),
        descriptor->channels, descriptor->sample_rate, 20U};
    if (!engine.start_wasapi_output(config, block_frames)) return false;
    state.engine = &engine;
    state.config = std::move(config);
    state.endpoint_id = descriptor->endpoint_id;
    state.display_name = descriptor->display_name;
    state.block_frames = block_frames;
    state.active = true;
    return true;
}

bool prepare_test_tone(TestToneState& tone,
                       WasapiOutputState& output,
                       hibiki::AudioEngineModel& engine) noexcept {
    if (!output.active || output.config.sample_rate == 0U || output.block_frames == 0U ||
        output.block_frames > kTestToneMaxFrames ||
        output.config.channels > kTestToneMaxOutputChannels) {
        return false;
    }

    auto scene = hibiki::make_easy_scene(hibiki::EasySceneKind::Studio, "main");
    scene.graph.output_channels = output.config.channels;
    engine.set_sample_rate(output.config.sample_rate);
    if (!engine.prepare_graph(scene.graph, 1U) || !engine.commit_graph()) {
        engine.rollback_graph();
        return false;
    }

    tone.lanes[0U] = hibiki::RtLaneInputV1{tone.input.data(), kTestToneInputChannels};
    tone.sample_rate = output.config.sample_rate;
    tone.block_frames = output.block_frames;
    output.test_tone_enabled = true;
    return true;
}

bool render_test_tone(TestToneState& tone,
                      hibiki::AudioEngineModel& engine) noexcept {
    constexpr double kFrequencyHz = 440.0;
    constexpr float kAmplitude = 0.1F;  // approximately -20 dBFS
    constexpr double kTwoPi = 6.28318530717958647692;
    if (tone.sample_rate == 0U || tone.block_frames == 0U ||
        tone.block_frames > kTestToneMaxFrames) {
        return false;
    }

    const auto phase_increment = kTwoPi * kFrequencyHz /
                                 static_cast<double>(tone.sample_rate);
    for (std::uint32_t frame = 0U; frame < tone.block_frames; ++frame) {
        const auto sample = kAmplitude * static_cast<float>(std::sin(tone.phase));
        if (!std::isfinite(sample)) return false;
        const auto offset = static_cast<std::size_t>(frame) * kTestToneInputChannels;
        tone.input[offset] = sample;
        tone.input[offset + 1U] = sample;
        tone.phase += phase_increment;
        if (tone.phase >= kTwoPi) tone.phase -= kTwoPi;
    }
    return engine.process_output_group_to_wasapi(
        "main", std::span<const hibiki::RtLaneInputV1>(tone.lanes), tone.output.data(),
        tone.block_frames);
}

bool prepare_driver_loopback(DriverLoopbackState& loopback,
                             WasapiOutputState& output,
                             hibiki::AudioEngineModel& engine) noexcept {
    if (!output.active || output.config.sample_rate == 0U || output.block_frames == 0U ||
        output.block_frames > kDriverLoopbackMaxFrames ||
        output.config.channels > kTestToneMaxOutputChannels) {
        return false;
    }

    auto scene = hibiki::make_easy_scene(hibiki::EasySceneKind::Studio, "main");
    scene.graph.output_channels = output.config.channels;
    engine.set_sample_rate(output.config.sample_rate);
    if (!engine.prepare_graph(scene.graph, 4U) || !engine.commit_graph()) {
        engine.rollback_graph();
        return false;
    }

    if (hibiki_driver_stream_ring_init_v1(
            &loopback.ring,
            sizeof(loopback.ring),
            output.config.channels,
            output.config.sample_rate) != HIBIKI_DRIVER_STREAM_RING_OK_V1) {
        engine.rollback_graph();
        return false;
    }
    loopback.lanes[0U] =
        hibiki::RtLaneInputV1{loopback.input.data(), kDriverLoopbackInputChannels};
    loopback.sample_rate = output.config.sample_rate;
    loopback.block_frames = output.block_frames;
    output.driver_loopback_enabled = true;
    return true;
}

bool render_driver_loopback(DriverLoopbackState& loopback,
                            hibiki::AudioEngineModel& engine) noexcept {
    constexpr double kFrequencyHz = 440.0;
    constexpr float kAmplitude = 0.1F;  // approximately -20 dBFS
    constexpr double kTwoPi = 6.28318530717958647692;
    if (loopback.sample_rate == 0U || loopback.block_frames == 0U ||
        loopback.block_frames > kDriverLoopbackMaxFrames) {
        ++loopback.encode_failures;
        return false;
    }

    // Source block: bounded stereo sine, same level as the existing test tone.
    const auto phase_increment = kTwoPi * kFrequencyHz /
                                 static_cast<double>(loopback.sample_rate);
    for (std::uint32_t frame = 0U; frame < loopback.block_frames; ++frame) {
        const auto sample = kAmplitude * static_cast<float>(std::sin(loopback.phase));
        if (!std::isfinite(sample)) {
            ++loopback.encode_failures;
            return false;
        }
        const auto offset =
            static_cast<std::size_t>(frame) * kDriverLoopbackInputChannels;
        loopback.input[offset] = sample;
        loopback.input[offset + 1U] = sample;
        loopback.phase += phase_increment;
        if (loopback.phase >= kTwoPi) loopback.phase -= kTwoPi;
    }

    // Engine path: graph, Group Master and limiter run exactly once per
    // packet, then the processed block is encoded as a complete v1 packet.
    std::size_t written_bytes = 0U;
    if (!engine.encode_driver_stream_packet_from_lane(
            0U, kDriverLoopbackEndpointGuid, loopback.sequence + 1U,
            /*generation=*/1ULL,
            /*flags=*/0U, loopback.input.data(), kDriverLoopbackInputChannels,
            loopback.block_frames, std::span<hibiki::RtLaneInputV1>(loopback.lanes),
            loopback.processed.data(), std::span<std::uint8_t>(loopback.packet),
            written_bytes)) {
        ++loopback.encode_failures;
        return false;
    }
    ++loopback.sequence;

    const auto push_result = hibiki_driver_stream_ring_push_v1(
        &loopback.ring, sizeof(loopback.ring), loopback.packet.data(), written_bytes);
    if (push_result != HIBIKI_DRIVER_STREAM_RING_OK_V1) {
        ++loopback.push_failures;
        return false;
    }

    std::size_t popped_bytes = 0U;
    std::uint32_t silence = 0U;
    const auto pop_result = hibiki_driver_stream_ring_pop_v1(
        &loopback.ring, sizeof(loopback.ring), loopback.popped.data(),
        loopback.popped.size(), &popped_bytes, &silence);
    if (pop_result != HIBIKI_DRIVER_STREAM_RING_OK_V1) {
        ++loopback.pop_failures;
        return false;
    }

    // Delivery: decode validates the whole packet again before it may enter
    // the lane graph a second time on the way to the WASAPI handoff.
    if (!engine.process_driver_stream_packet_to_wasapi(
            0U, kDriverLoopbackEndpointGuid,
            std::span<const std::uint8_t>(loopback.popped.data(), popped_bytes),
            std::span<float>(loopback.decode_samples),
            std::span<hibiki::RtLaneInputV1>(loopback.lanes), loopback.output.data())) {
        ++loopback.deliver_failures;
        return false;
    }
    ++loopback.rendered_blocks;
    return true;
}

// Decode a bounded WAV file and, when its sample rate differs from the sink,
// convert the whole buffer offline with the shared polyphase bank. Both the
// live WASAPI source and the device-free offline render path use this helper;
// neither converts inside a realtime callback.
bool decode_wav_for_sink_rate(const std::filesystem::path& path,
                              const std::uint32_t sink_rate,
                              hibiki::IrWavDataV1& decoded_out,
                              std::uint32_t& file_rate_out,
                              bool& resampled) {
    resampled = false;
    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        const auto end = file.tellg();
        if (end <= 0 || end > static_cast<std::streamoff>(hibiki::kMaxIrWavBytesV1)) {
            return false;
        }
        const auto size = static_cast<std::size_t>(end);
        std::vector<std::uint8_t> bytes(size);
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(size))) {
            return false;
        }
        auto decoded = hibiki::decode_ir_wav_v1(
            bytes, hibiki::kMaxSourceWavFramesV1);
        if (!decoded.valid) return false;
        if (decoded.data.channels == 0U || decoded.data.frames() == 0U) return false;
        decoded_out = std::move(decoded.data);
        file_rate_out = decoded_out.sample_rate;
        if (decoded_out.sample_rate == sink_rate || sink_rate == 0U) return true;
        // Bounded polyphase conversion (0.25x-4.0x), identical to the live path.
        const double step = static_cast<double>(decoded_out.sample_rate) /
                            static_cast<double>(sink_rate);
        if (!std::isfinite(step) || step < 0.25 || step > 4.0) return false;
        const auto nominal = static_cast<std::size_t>(std::llround(
            static_cast<double>(decoded_out.frames()) *
            static_cast<double>(sink_rate) /
            static_cast<double>(decoded_out.sample_rate)));
        if (nominal == 0U) return false;
        hibiki::PersistentPolyphaseResampler resampler;
        if (!resampler.prepare(decoded_out.channels, step)) return false;
        const auto channel_count = static_cast<std::size_t>(decoded_out.channels);
        // process() only emits centers fully covered by real input; reserve
        // bounded tail room so every flush call has enough output space.
        const auto flush_output_frames = static_cast<std::size_t>(
            (static_cast<double>(kWavSourceResampleFlushInputFramesV1 + 13U) /
             step)) + 1U;
        std::vector<float> converted(
            (nominal + flush_output_frames) * channel_count, 0.0F);
        constexpr std::size_t kChunkFrames = 4096U;
        std::size_t produced = 0U;
        for (std::size_t offset = 0U; offset < decoded_out.frames(); offset += kChunkFrames) {
            const auto chunk = (std::min)(kChunkFrames, decoded_out.frames() - offset);
            float* out_base = converted.data() + produced * channel_count;
            const auto capacity = converted.size() / channel_count - produced;
            std::size_t got = 0U;
            if (!resampler.process(
                    decoded_out.interleaved_samples.data() + offset * channel_count,
                    chunk, out_base, capacity, got)) {
                return false;
            }
            produced += got;
        }
        const std::vector<float> zeros(
            kWavSourceResampleFlushInputFramesV1 * channel_count, 0.0F);
        while (produced < nominal) {
            float* out_base = converted.data() + produced * channel_count;
            const auto capacity = converted.size() / channel_count - produced;
            std::size_t got = 0U;
            if (!resampler.process(zeros.data(), kWavSourceResampleFlushInputFramesV1,
                                   out_base, capacity, got)) {
                return false;
            }
            if (got == 0U) break;
            produced += got;
        }
        if (produced < nominal) return false;
        decoded_out.interleaved_samples.resize(nominal * channel_count);
        decoded_out.sample_rate = sink_rate;
        resampled = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool prepare_wav_file_source(WavFileSourceState& source,
                             const std::filesystem::path& path,
                             WasapiOutputState& output,
                             hibiki::AudioEngineModel& engine) {
    source.active = false;
    if (!output.active || output.config.sample_rate == 0U || output.block_frames == 0U ||
        output.block_frames > kWavSourceMaxFrames ||
        output.config.channels > kWavSourceMaxOutputChannels ||
        !supported_wasapi_layout(output.config.channels) ||
        !supported_wasapi_rate(output.config.sample_rate)) {
        return false;
    }

    hibiki::IrWavDataV1 decoded_data{};
    bool resampled = false;
    std::uint32_t input_file_rate = 0U;
    if (!decode_wav_for_sink_rate(path, output.config.sample_rate, decoded_data,
                                  input_file_rate, resampled)) {
        return false;
    }
    try {
        if (decoded_data.channels != 1U &&
            decoded_data.channels != output.config.channels) {
            return false;
        }
        if (output.config.channels > 2U && decoded_data.channels == 1U) {
            // Mono broadcast is only defined for stereo here; multi-channel
            // sinks keep their explicit layout contract.
            return false;
        }

        auto scene = hibiki::make_easy_scene(hibiki::EasySceneKind::Studio, "main");
        scene.graph.output_channels = output.config.channels;
        engine.set_sample_rate(output.config.sample_rate);
        if (!engine.prepare_graph(scene.graph, 5U) || !engine.commit_graph()) {
            engine.rollback_graph();
            return false;
        }

        source.file_sample_rate = input_file_rate;
        // decode_wav_for_sink_rate already converted to the sink rate when
        // needed, so the live path stays a plain move into the source state.
        source.data = std::move(decoded_data);
        (void)resampled;
        source.input_block.resize(
            static_cast<std::size_t>(kWavSourceMaxFrames) *
            static_cast<std::size_t>(
                source.data.channels == 1U ? 2U : source.data.channels));
        source.output_block.resize(
            static_cast<std::size_t>(kWavSourceMaxFrames) *
            static_cast<std::size_t>(output.config.channels));
        source.lanes[0U] = hibiki::RtLaneInputV1{source.input_block.data(),
                                                 static_cast<std::uint16_t>(
                                                     source.data.channels == 1U
                                                         ? 2U
                                                         : source.data.channels)};
        source.next_frame = 0U;
        source.sample_rate = output.config.sample_rate;
        source.block_frames = output.block_frames;
        source.prepared = true;
        source.active = true;
        output.wav_source_enabled = true;
        return true;
    } catch (...) {
        engine.rollback_graph();
        return false;
    }
}

bool render_wav_file_source(WavFileSourceState& source,
                            hibiki::AudioEngineModel& engine) noexcept {
    if (!source.prepared || !source.active || source.block_frames == 0U) {
        return false;
    }

    const auto source_channels =
        source.data.channels == 1U ? 2U : static_cast<std::uint32_t>(source.data.channels);
    const auto total_frames = source.data.frames();
    const auto start_frame = source.next_frame;
    for (std::uint32_t frame = 0U; frame < source.block_frames; ++frame) {
        if (source.next_frame >= total_frames) {
            if (!source.loop) {
                source.eof = true;
                // Complete this block with silence so the sink receives a
                // well-formed block, then stop scheduling future renders.
                const auto remaining =
                    static_cast<std::size_t>(source.block_frames - frame) *
                    static_cast<std::size_t>(source_channels);
                const auto offset_begin =
                    static_cast<std::size_t>(frame) * static_cast<std::size_t>(source_channels);
                for (std::size_t index = 0U; index < remaining; ++index) {
                    source.input_block[offset_begin + index] = 0.0F;
                }
                break;
            }
            source.next_frame = 0U;
        }
        else if (frame > 0U && source.next_frame == 0U) {
            // Loop wrap: the frame position restarted, so the rest of this
            // block is silence padding and progress reports a full block.
            break;
        }
        const auto offset =
            static_cast<std::size_t>(frame) * static_cast<std::size_t>(source_channels);
        if (source.data.channels == 1U) {
            const auto sample = source.data.interleaved_samples[source.next_frame];
            source.input_block[offset] = sample;
            source.input_block[offset + 1U] = sample;
        } else {
            const auto base = static_cast<std::size_t>(source.next_frame) *
                              static_cast<std::size_t>(source.data.channels);
            for (std::uint32_t channel = 0U; channel < source_channels; ++channel) {
                source.input_block[offset + channel] =
                    source.data.interleaved_samples[base + channel];
            }
        }
        ++source.next_frame;
    }
    source.frames_rendered += source.next_frame - start_frame;
    const auto delivered = engine.process_output_group_to_wasapi(
        "main", std::span<const hibiki::RtLaneInputV1>(source.lanes),
        source.output_block.data(), source.block_frames);
    if (delivered) {
        ++source.rendered_blocks;
        source.consecutive_failures = 0U;
    } else {
        ++source.failed_blocks;
        ++source.consecutive_failures;
        if (source.consecutive_failures >= kWavSourceFailureLimit) {
            source.active = false;
            source.eof = true;
        }
    }
    return delivered;
}

// Device-free end-to-end evidence: decode (and resample when needed), commit
// the same Studio graph used by the live path, then drive every frame through
// process_output_group in bounded blocks. No WASAPI handoff is involved, so
// the result is reproducible on machines without any audio endpoint.
bool render_wav_offline(const std::filesystem::path& input_path,
                        const std::filesystem::path& output_path,
                        OfflineRenderResult& result,
                        std::string& error_detail) {
    hibiki::AudioEngineModel engine;
    hibiki::IrWavDataV1 decoded_data{};
    std::uint32_t input_file_rate = 0U;
    bool resampled = false;
    if (!decode_wav_for_sink_rate(input_path, kOfflineRenderOutputSampleRateV1,
                                  decoded_data, input_file_rate, resampled)) {
        error_detail = "decode failed; file must be a bounded Float32 PCM WAV";
        return false;
    }
    try {
        const auto channels = static_cast<std::uint32_t>(decoded_data.channels);
        if (channels != 1U && channels != kOfflineRenderOutputChannelsV1) {
            error_detail = "unsupported channel count for offline render";
            return false;
        }
        auto scene = hibiki::make_easy_scene(hibiki::EasySceneKind::Studio, "main");
        scene.graph.output_channels = kOfflineRenderOutputChannelsV1;
        engine.set_sample_rate(kOfflineRenderOutputSampleRateV1);
        if (!engine.prepare_graph(scene.graph, 5U) || !engine.commit_graph()) {
            engine.rollback_graph();
            error_detail = "graph prepare/commit failed";
            return false;
        }
        // Bounded output: same cap as the decoder.
        const auto max_frames = hibiki::kMaxIrWavBytesV1 /
                                (sizeof(float) * kOfflineRenderOutputChannelsV1);
        if (decoded_data.frames() > max_frames) {
            error_detail = "converted buffer exceeds the offline render cap";
            return false;
        }
        std::vector<float> rendered(
            decoded_data.frames() * kOfflineRenderOutputChannelsV1, 0.0F);
        // Mono sources must be expanded to a bounded stereo copy before the
        // render loop: the lane contract reads channel_count floats per frame,
        // so pointing a 2-channel lane at a 1-float-per-frame buffer would
        // over-read the heap and misinterpret adjacent samples as L/R.
        std::vector<float> lane_block{};
        if (channels == 1U) {
            lane_block.resize(decoded_data.frames() * 2U);
            for (std::size_t frame = 0U; frame < decoded_data.frames(); ++frame) {
                lane_block[frame * 2U] =
                    decoded_data.interleaved_samples[frame];
                lane_block[frame * 2U + 1U] =
                    decoded_data.interleaved_samples[frame];
            }
        }
        const float* lane_data =
            channels == 1U
                ? lane_block.data()
                : decoded_data.interleaved_samples.data();
        const hibiki::RtLaneInputV1 lane{
            lane_data, static_cast<std::uint16_t>(kOfflineRenderOutputChannelsV1)};
        constexpr std::size_t kBlock = 128U;  // matches kWavSource block cadence
        std::size_t offset = 0U;
        while (offset < decoded_data.frames()) {
            const auto frames = (std::min)(static_cast<std::size_t>(kBlock),
                                           decoded_data.frames() - offset);
            float* out_base = rendered.data() +
                              offset * kOfflineRenderOutputChannelsV1;
            if (!engine.process_output_group(
                    "main", std::span<const hibiki::RtLaneInputV1>(&lane, 1U),
                    out_base, frames)) {
                error_detail = "process_output_group failed at frame " +
                               std::to_string(offset);
                return false;
            }
            offset += frames;
        }
        const auto wav_bytes = hibiki::export_wav_f32_ir(
            std::span<const float>(rendered.data(), rendered.size()),
            kOfflineRenderOutputSampleRateV1,
            kOfflineRenderOutputChannelsV1);
        if (wav_bytes.empty()) {
            error_detail = "wav encode failed";
            return false;
        }
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error_detail = "cannot open output path for writing";
            return false;
        }
        out.write(reinterpret_cast<const char*>(wav_bytes.data()),
                  static_cast<std::streamsize>(wav_bytes.size()));
        if (!out) {
            error_detail = "output write failed";
            return false;
        }
        result.frames = decoded_data.frames();
        result.file_sample_rate = input_file_rate;
        result.output_sample_rate = kOfflineRenderOutputSampleRateV1;
        result.file_rate_matches = !resampled;
        result.resampled = resampled;
        return true;
    } catch (...) {
        error_detail = "unexpected exception during offline render";
        return false;
    }
}

// Rebuild the engine graph from the coordinator's current session-route plan.
// Called only from the control loop (never the RT callback); prepare/commit
// is transactional so a failed rebuild keeps the previous graph intact.
bool rebuild_delivery_graph(ProcessDeliveryState& delivery,
                            hibiki::AudioEngineModel& engine,
                            const WasapiOutputState& output,
                            const hibiki::WindowsAudioSessionRouteCoordinatorV1& coordinator) noexcept {
    if (!output.active || !coordinator.bound()) {
        delivery.graph_ready = false;
        return false;
    }
    hibiki::GraphConfigV1 candidate{};
    if (!coordinator.copy_graph(candidate)) return false;
    if (candidate.lanes.empty()) {
        // No routed sessions: keep the previous graph (which may be the
        // test-tone graph or a prior delivery graph) so the sink stays valid.
        delivery.graph_ready = false;
        return true;  // No routed sessions yet; not an error.
    }
    for (const auto& lane : candidate.lanes) {
        if (!supported_wasapi_layout(lane.channel_count)) return false;
    }
    candidate.output_channels = output.config.channels;
    engine.set_sample_rate(output.config.sample_rate);
    // Revision 2: revision 1 was used by prepare_test_tone for its own graph;
    // delivery uses a separate monotonic value so both transactions stay valid.
    if (!engine.prepare_graph(candidate, 2U)) {
        engine.rollback_graph();
        return false;
    }
    if (!engine.commit_graph()) {
        engine.rollback_graph();
        return false;
    }
    try {
        delivery.graph = std::move(candidate);
    } catch (...) {
        return false;
    }
    delivery.lane_inputs.assign(delivery.graph.lanes.size(), hibiki::RtLaneInputV1{});
    delivery.graph_ready = true;
    return true;
}

// Synchronise process-loopback capture sources with the coordinator's plan.
// Stops sources whose PID/lane pair disappeared, then starts new ones in free
// slots. Sources are worker-owned COM objects; this runs on the control thread.
void sync_delivery_sources(ProcessDeliveryState& delivery,
                           const hibiki::WindowsAudioSessionRouteCoordinatorV1& coordinator) noexcept {
    hibiki::ProcessLoopbackPlanV1 plan{};
    const auto result = coordinator.copy_process_loopback_plan(plan);
    if (result != hibiki::ProcessLoopbackPlanResultV1::Applied &&
        result != hibiki::ProcessLoopbackPlanResultV1::NoRoutes) {
        return;  // Ambiguous/capacity failures keep existing sources unchanged.
    }

    // Deactivate sources no longer present in the current plan.
    for (auto& entry : delivery.lanes) {
        if (!entry.active) continue;
        bool found = false;
        for (std::size_t i = 0U; i < plan.size; ++i) {
            if (plan.entries[i].process_id == entry.process_id &&
                plan.entries[i].lane_id == entry.lane_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            entry.source.stop();
            entry.active = false;
            entry.process_id = 0U;
            entry.consecutive_failures = 0U;
        }
    }

    // Start new sources for requests that do not already have an active slot.
    std::size_t next_slot = 0U;
    for (std::size_t i = 0U; i < plan.size && next_slot < kMaxProcessDeliverySources; ++i) {
        const auto& request = plan.entries[i];
        bool already_active = false;
        for (auto& entry : delivery.lanes) {
            if (entry.active && entry.process_id == request.process_id &&
                entry.lane_id == request.lane_id) {
                already_active = true;
                break;
            }
        }
        if (already_active) continue;
        while (next_slot < kMaxProcessDeliverySources && delivery.lanes[next_slot].active) {
            ++next_slot;
        }
        if (next_slot >= kMaxProcessDeliverySources) break;
        auto& slot = delivery.lanes[next_slot];
        hibiki::WindowsProcessLoopbackConfigV1 config{};
        config.process_id = request.process_id;
        config.include_process_tree = request.include_process_tree;
        if (SUCCEEDED(slot.source.start(config))) {
            slot.lane_id = request.lane_id;
            slot.process_id = request.process_id;
            slot.active = true;
            slot.consecutive_failures = 0U;
        }
        ++next_slot;
    }
}

// Poll each active source once per control-loop iteration. Each successful
// read feeds one block through the lane graph into the WASAPI sink handoff.
// This never blocks: source.read() is non-blocking by contract.
void poll_and_deliver(ProcessDeliveryState& delivery,
                      hibiki::AudioEngineModel& engine,
                      const SessionRoutingState& routing) noexcept {
    if (!delivery.graph_ready || !routing.requested || !routing.bound) return;
    for (auto& entry : delivery.lanes) {
        if (!entry.active) continue;
        std::size_t lane_index = SIZE_MAX;
        for (std::size_t li = 0U; li < delivery.graph.lanes.size(); ++li) {
            if (delivery.graph.lanes[li].id == entry.lane_id) {
                lane_index = li;
                break;
            }
        }
        if (lane_index == SIZE_MAX) continue;
        const auto snapshot_before = entry.source.snapshot();
        if (snapshot_before.state != hibiki::WindowsProcessLoopbackStateV1::Running) {
            ++entry.consecutive_failures;
            if (entry.consecutive_failures > kProcessDeliveryFailureLimit) {
                entry.source.stop();
                entry.active = false;
                entry.process_id = 0U;
            }
            continue;
        }
        const bool ok = hibiki::process_windows_process_loopback_lane_v1(
            engine, entry.source, lane_index,
            delivery.input_buffer.data(), kProcessLoopbackMaxFrames,
            std::span<hibiki::RtLaneInputV1>(delivery.lane_inputs),
            delivery.output_buffer.data(), kProcessLoopbackMaxFrames,
            delivery.last_block, /*to_wasapi=*/true);
        if (ok) {
            ++delivery.rendered_blocks;
            entry.consecutive_failures = 0U;
        } else {
            ++delivery.dropped_blocks;
        }
    }
}

bool has_rendered_blocks(const hibiki::WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    return snapshot.primary.rendered_blocks > 0U || snapshot.secondary.rendered_blocks > 0U;
}

hibiki::ControlRouteHealthStateV1 wasapi_route_state(
    const WasapiOutputState& state,
    const hibiki::WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    if (!state.requested) return hibiki::ControlRouteHealthStateV1::Unavailable;
    if (!state.active) return hibiki::ControlRouteHealthStateV1::Pending;
    if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Degraded ||
        snapshot.primary.degraded || snapshot.secondary.degraded) {
        return hibiki::ControlRouteHealthStateV1::Degraded;
    }
    if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Synced &&
        snapshot.primary.endpoint_ready) {
        return hibiki::ControlRouteHealthStateV1::Ready;
    }
    return hibiki::ControlRouteHealthStateV1::Pending;
}

std::string truncate_utf8_prefix(const std::string& text, const std::size_t max_bytes) {
    if (text.size() <= max_bytes) return text;
    auto prefix = text.substr(0U, max_bytes);
    while (!prefix.empty()) {
        const auto byte = static_cast<unsigned char>(prefix.back());
        if ((byte & 0x80U) == 0U) break;
        std::size_t lead_position = prefix.size() - 1U;
        while (lead_position > 0U &&
               (static_cast<unsigned char>(prefix[lead_position]) & 0xC0U) == 0x80U) {
            --lead_position;
        }
        const auto lead = static_cast<unsigned char>(prefix[lead_position]);
        std::size_t expected_length = 1U;
        if ((lead & 0xE0U) == 0xC0U) expected_length = 2U;
        else if ((lead & 0xF0U) == 0xE0U) expected_length = 3U;
        else if ((lead & 0xF8U) == 0xF0U) expected_length = 4U;
        if (expected_length > 1U && prefix.size() - lead_position >= expected_length) break;
        prefix.resize(lead_position);
    }
    return prefix;
}

std::string wasapi_route_detail(
    const WasapiOutputState& state,
    const hibiki::WasapiSinkHandoffSnapshotV1& snapshot) {
    // Bounded device-name prefix. The descriptor name is capped at 128 bytes by
    // the physical-device catalog contract, so this stays well inside the
    // 120-byte route detail budget after trimming.
    std::string prefix;
    if (!state.display_name.empty()) {
        prefix = truncate_utf8_prefix(state.display_name, 40U);
        while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
    }
    const char* body = [&] {
        if (!state.requested) {
            return "physical catalog ready; shared-mode WASAPI sink disabled by default.";
        }
        if (!state.active) {
            return "WASAPI sink requested; no supported active default render endpoint; output remains muted.";
        }
        if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Degraded ||
            snapshot.primary.degraded || snapshot.secondary.degraded) {
            return "WASAPI sink degraded; fail-closed until a supported endpoint is available.";
        }
        if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Synced &&
            snapshot.primary.endpoint_ready) {
            if (state.test_tone_enabled && has_rendered_blocks(snapshot)) {
                return "test tone rendering.";
            }
            return "shared-mode WASAPI sink ready; graph/ASIO delivery remains an explicit source boundary.";
        }
        return "WASAPI sink warming; no audio is reported ready until the worker confirms the endpoint.";
    }();
    if (prefix.empty() || !state.requested) return std::string(body);
    prefix.push_back(':');
    prefix.push_back(' ');
    prefix.append(body);
    return prefix;
}

bool has_command_line_flag(const int argc,
                           wchar_t* const* argv,
                           const std::wstring_view flag) noexcept {
    if (argv == nullptr || flag.empty()) return false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && std::wstring_view(argv[index]) == flag) return true;
    }
    return false;
}

HRESULT bind_default_volume(IMMDeviceEnumerator* const enumerator,
                            hibiki::WindowsVolumeBroker& broker) noexcept {
    if (enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = broker.bind(device);
    device->Release();
    return bind_result;
}

HRESULT rebind_default_volume_if_changed(IMMDeviceEnumerator* const enumerator,
                                         hibiki::WindowsVolumeBroker& broker,
                                         const bool currently_bound) noexcept {
    if (enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = currently_bound ? broker.bind_if_changed(device)
                                             : broker.bind(device);
    device->Release();
    return bind_result;
}

HRESULT acquire_default_render_device(IMMDeviceEnumerator* const enumerator,
                                      IMMDevice** const device,
                                      std::array<wchar_t, 260U>& endpoint_id,
                                      std::uint16_t& endpoint_id_chars) noexcept {
    endpoint_id.fill(L'\0');
    endpoint_id_chars = 0U;
    if (enumerator == nullptr || device == nullptr) return E_INVALIDARG;
    *device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device);
    if (FAILED(result) || *device == nullptr) return FAILED(result) ? result : E_FAIL;
    LPWSTR raw_id = nullptr;
    const auto id_result = (*device)->GetId(&raw_id);
    if (FAILED(id_result) || raw_id == nullptr) {
        if (raw_id != nullptr) CoTaskMemFree(raw_id);
        (*device)->Release();
        *device = nullptr;
        return FAILED(id_result) ? id_result : E_FAIL;
    }
    const auto length = std::wcslen(raw_id);
    if (length == 0U || length >= endpoint_id.size()) {
        CoTaskMemFree(raw_id);
        (*device)->Release();
        *device = nullptr;
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
    std::copy_n(raw_id, length, endpoint_id.data());
    endpoint_id[length] = L'\0';
    endpoint_id_chars = static_cast<std::uint16_t>(length);
    CoTaskMemFree(raw_id);
    return S_OK;
}

bool same_endpoint_id(const std::array<wchar_t, 260U>& left,
                      const std::uint16_t left_chars,
                      const std::array<wchar_t, 260U>& right,
                      const std::uint16_t right_chars) noexcept {
    return left_chars == right_chars &&
           std::equal(left.begin(), left.begin() + left_chars, right.begin());
}

HRESULT bind_default_session_route_if_changed(IMMDeviceEnumerator* const enumerator,
                                               SessionRoutingState& state) noexcept {
    IMMDevice* device = nullptr;
    std::array<wchar_t, 260U> endpoint_id{};
    std::uint16_t endpoint_id_chars = 0U;
    const auto acquire_result = acquire_default_render_device(
        enumerator, &device, endpoint_id, endpoint_id_chars);
    if (FAILED(acquire_result)) {
        state.coordinator.unbind();
        state.bound = false;
        state.endpoint_id.fill(L'\0');
        state.endpoint_id_chars = 0U;
        return acquire_result;
    }
    if (state.bound && same_endpoint_id(state.endpoint_id, state.endpoint_id_chars,
                                        endpoint_id, endpoint_id_chars)) {
        device->Release();
        return S_FALSE;
    }
    const auto bind_result = state.coordinator.bind(device);
    device->Release();
    if (FAILED(bind_result)) {
        state.bound = false;
        return bind_result;
    }
    state.endpoint_id = endpoint_id;
    state.endpoint_id_chars = endpoint_id_chars;
    state.bound = true;
    (void)state.coordinator.set_rules(state.rules);
    return S_OK;
}

bool publish_session_catalog(SessionRoutingState& state) noexcept {
    if (!state.requested || !state.bound) return false;
    const auto previous = state.catalog_store.sequence();
    if (previous == UINT64_MAX) return false;
    hibiki::SessionCatalogSnapshotV1 candidate{};
    if (!state.coordinator.make_session_catalog_snapshot(previous + 1U, candidate) ||
        !state.catalog_store.publish(candidate)) {
        return false;
    }
    return true;
}

bool enqueue_session_volume_command(const hibiki::SessionVolumeCommandV1& request,
                                    void* const context) noexcept {
    auto* state = static_cast<SessionRoutingState*>(context);
    return state != nullptr && state->requested && state->bound &&
           state->queue.try_push_volume(request);
}

bool enqueue_session_route_command(const hibiki::SessionRouteCommandV1& request,
                                   void* const context) noexcept {
    auto* state = static_cast<SessionRoutingState*>(context);
    return state != nullptr && state->requested && state->bound &&
           state->queue.try_push_route(request);
}

bool enqueue_session_route_rule_command(const hibiki::SessionRouteRuleCommandV1& request,
                                        void* const context) noexcept {
    auto* state = static_cast<SessionRoutingState*>(context);
    if (state == nullptr || !state->requested || !state->bound) return false;
    const auto encoded = hibiki::encode_session_route_rule_command_v1(request);
    if (encoded[0U] == 0U) return false;
    return state->queue.try_push_route_rule(request);
}

bool drain_session_commands(SessionRoutingState& state) noexcept {
    if (!state.requested || !state.bound) return false;
    std::uint64_t expected_sequence = state.catalog_store.sequence();
    bool catalog_dirty = false;
    bool any_processed = false;
    hibiki::SessionCommandWorkItemV1 item{};
    while (state.queue.try_pop(item)) {
        any_processed = true;
        if (item.kind == hibiki::SessionCommandKindV1::Volume) {
            if (item.volume.catalog_sequence != expected_sequence) continue;
            const auto result = state.coordinator.write_session_volume_handle(
                item.volume.handle, hibiki::q16_16_to_db(item.volume.requested_db_q16_16),
                item.volume.mute != 0U, hibiki::WindowsVolumeEventContextsV1::session());
            catalog_dirty = catalog_dirty || SUCCEEDED(result);
        } else if (item.kind == hibiki::SessionCommandKindV1::Route) {
            if (item.route.catalog_sequence != expected_sequence) continue;
            const auto result = state.coordinator.bind_session_route_handle(
                item.route.handle,
                std::string_view(item.route.lane.data(), item.route.lane_bytes),
                std::string_view(item.route.output_group.data(), item.route.output_group_bytes));
            if (SUCCEEDED(result)) {
                catalog_dirty = true;
                if (expected_sequence != UINT64_MAX) ++expected_sequence;
            }
        } else if (item.kind == hibiki::SessionCommandKindV1::RouteRule) {
            if (item.route_rule.catalog_sequence != expected_sequence) continue;
            try {
                auto candidate_rules = state.rules;
                const auto operation = item.route_rule.operation;
                hibiki::SessionRouteRuleResultV1 rule_result =
                    hibiki::SessionRouteRuleResultV1::invalid_argument;
                if (operation == hibiki::SessionRouteRuleOperationV1::Upsert) {
                    hibiki::SessionRouteRuleV1 rule{};
                    rule.schema_version = item.route_rule.schema_version;
                    rule.rule_id.assign(item.route_rule.rule_id.data(),
                                        item.route_rule.rule_id_bytes);
                    rule.priority = item.route_rule.priority;
                    rule.enabled = item.route_rule.enabled != 0U;
                    rule.app_id.assign(item.route_rule.app_id.data(), item.route_rule.app_id_bytes);
                    rule.display_name_contains.assign(item.route_rule.display_name.data(),
                                                      item.route_rule.display_name_bytes);
                    rule.lane_id.assign(item.route_rule.lane.data(), item.route_rule.lane_bytes);
                    rule.output_group.assign(item.route_rule.output_group.data(),
                                             item.route_rule.output_group_bytes);
                    rule.gain_owner = item.route_rule.gain_owner ==
                                              hibiki::SessionRouteRuleGainOwnerV1::HibikiInternal
                                          ? hibiki::SessionGainOwner::HibikiInternal
                                          : hibiki::SessionGainOwner::WindowsSession;
                    rule.makeup_gain_db = hibiki::q16_16_to_db(
                        item.route_rule.makeup_gain_q16_16);
                    rule_result = candidate_rules.upsert(rule);
                } else if (operation == hibiki::SessionRouteRuleOperationV1::Remove) {
                    rule_result = candidate_rules.remove(
                                      std::string_view(item.route_rule.rule_id.data(),
                                                       item.route_rule.rule_id_bytes))
                                      ? hibiki::SessionRouteRuleResultV1::applied
                                      : hibiki::SessionRouteRuleResultV1::no_match;
                } else if (operation == hibiki::SessionRouteRuleOperationV1::Clear) {
                    candidate_rules.clear();
                    rule_result = hibiki::SessionRouteRuleResultV1::applied;
                }
                if (rule_result == hibiki::SessionRouteRuleResultV1::applied) {
                    const auto refresh_result = state.coordinator.set_rules_and_refresh(candidate_rules);
                    const bool applied = refresh_result ==
                                             hibiki::WindowsAudioSessionRouteRefreshResultV1::Applied ||
                                         refresh_result ==
                                             hibiki::WindowsAudioSessionRouteRefreshResultV1::NoRoutes;
                    if (applied) {
                        state.rules = std::move(candidate_rules);
                        catalog_dirty = true;
                        if (expected_sequence != UINT64_MAX) ++expected_sequence;
                    }
                }
            } catch (...) {
                // Keep the previous rule store when a bounded control update fails.
            }
        }
    }
    if (catalog_dirty) (void)publish_session_catalog(state);
    return any_processed;
}

bool prepare_ir_file(const hibiki::IrPrepareCommandV1& request, void* const context) noexcept {
    auto* state = static_cast<IrPrepareState*>(context);
    if (state == nullptr || state->engine == nullptr || request.path_bytes == 0U ||
        request.path_bytes > request.path.size() ||
        request.mode > 3U || request.strength_q16_16 < 0 || request.strength_q16_16 > 65536 ||
        (request.mode == 3U && request.strength_q16_16 != 0)) {
        return false;
    }
    try {
        const std::string path(request.path.data(), request.path_bytes);
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!file) return false;
        const auto end = file.tellg();
        if (end <= 0 || end > static_cast<std::streamoff>(hibiki::kMaxIrWavBytesV1)) return false;
        const auto size = static_cast<std::size_t>(end);
        std::vector<std::uint8_t> bytes(size);
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
            return false;
        }
        const auto decoded = hibiki::decode_ir_wav_v1(
            bytes, hibiki::kMaxRealtimeIrTapsV1);
        if (!decoded.valid ||
            (request.expected_sample_rate != 0U &&
             decoded.data.sample_rate != request.expected_sample_rate) ||
            (request.expected_channels != 0U && decoded.data.channels != request.expected_channels)) {
            return false;
        }
        const auto policy = hibiki::IrPhasePolicyV1{
            1U, static_cast<hibiki::IrPhaseMode>(request.mode),
            static_cast<double>(request.strength_q16_16) / 65536.0};
        const auto resolution = hibiki::resolve_ir_phase_policy(policy);
        if (!resolution.valid || resolution.mode == hibiki::IrPhaseMode::Bypass) return false;
        if (!state->engine->prepare_ir("main", decoded.data, resolution) ||
            !state->engine->commit_ir()) {
            state->engine->rollback_ir();
            return false;
        }
        state->resolution = resolution;
        state->path = path;
        state->prepared = true;
        return true;
    } catch (...) {
        return false;
    }
}

void set_route(hibiki::ControlRouteHealthEntryV1& route,
               const std::string_view id,
               const std::string_view name,
               const std::string_view detail,
               const hibiki::ControlRouteHealthStateV1 state,
               const std::uint16_t flags = 0U) noexcept {
    route = {};
    route.id_bytes = static_cast<std::uint8_t>(id.size());
    route.name_bytes = static_cast<std::uint16_t>(name.size());
    // Defensive bound: callers compose details from device prefixes and
    // telemetry counters; never let a longer composition overflow the wire
    // entry even though current call sites stay inside the budget.
    route.detail_bytes = static_cast<std::uint16_t>(
        std::min<std::size_t>(detail.size(), route.detail.size()));
    route.state = state;
    route.flags = flags;
    std::copy(id.begin(), id.end(), route.id.begin());
    std::copy(name.begin(), name.end(), route.name.begin());
    const auto detail_copy_bytes = std::min<std::size_t>(
        detail.size(), route.detail.size());
    std::copy(detail.begin(),
              detail.begin() + static_cast<std::ptrdiff_t>(detail_copy_bytes),
              route.detail.begin());
}

void publish_eq_visual_snapshot(const hibiki::EqVisualSnapshotV1& snapshot,
                                void* const context) noexcept {
    auto* store = static_cast<hibiki::EqVisualSnapshotStoreV1*>(context);
    if (store != nullptr) (void)store->publish(snapshot);
}

hibiki::ControlStatusSnapshotV1 make_initial_status(
    const hibiki::OutputGroupVolumeStateV1 volume,
    const std::string_view main_output_detail,
    const WasapiOutputState& wasapi_output,
    const hibiki::WasapiSinkHandoffSnapshotV1& wasapi_snapshot,
    const bool system_volume_enabled,
    const std::string_view system_volume_detail,
    const bool session_routing_enabled,
    const std::string_view session_routing_detail,
    const std::string_view process_loopback_detail,
    const bool driver_loopback_enabled,
    const bool driver_loopback_ready,
    const std::string_view driver_loopback_detail,
    const bool wav_source_enabled, const std::string_view wav_source_detail,
    const bool tab_bridge_enabled, const std::string_view tab_bridge_detail,
    const bool vst3_lane_enabled) noexcept {
    hibiki::ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = 1U;
    snapshot.volume = volume;
    snapshot.route_count = vst3_lane_enabled ? 8U
        : (wav_source_enabled || driver_loopback_enabled || tab_bridge_enabled)
                              ? 7U
                              : 6U;
    set_route(snapshot.routes[0U], "engine-control", "引擎控制面",
              "named pipe 已啟動；目前為本機 user-space preview。",
              hibiki::ControlRouteHealthStateV1::Ready);
    set_route(snapshot.routes[1U], "main-output", "主輸出",
              main_output_detail,
              wasapi_output.requested ? wasapi_route_state(wasapi_output, wasapi_snapshot)
                                      : hibiki::ControlRouteHealthStateV1::Unavailable,
              wasapi_output.requested ? 0U : 1U);
    set_route(snapshot.routes[2U], "windows-volume", "Windows 音量",
              system_volume_detail,
              system_volume_enabled ? hibiki::ControlRouteHealthStateV1::Ready
                                     : hibiki::ControlRouteHealthStateV1::Unavailable,
              system_volume_enabled ? 0U : 1U);
    set_route(snapshot.routes[3U], "vendor-asio", "廠商 ASIO",
              "Strict Direct／廠商 ASIO 不會被透明攔截。",
              hibiki::ControlRouteHealthStateV1::Bypassed);
    set_route(snapshot.routes[4U], "windows-session", "Windows App／Session",
              session_routing_detail,
              session_routing_enabled ? hibiki::ControlRouteHealthStateV1::Ready
                                       : hibiki::ControlRouteHealthStateV1::Unavailable,
              session_routing_enabled ? 0U : 1U);
    set_route(snapshot.routes[5U], "process-loopback", "Process Loopback",
              process_loopback_detail,
              session_routing_enabled ? hibiki::ControlRouteHealthStateV1::Pending
                                       : hibiki::ControlRouteHealthStateV1::Unavailable,
              session_routing_enabled ? 0U : 1U);
    // The last status slot is the explicit audio-source slot: exactly one of
    // the mutually exclusive sources fills it, so the initial snapshot must
    // already name the requested mode instead of publishing a wrong route id.
    if (wav_source_enabled) {
        set_route(snapshot.routes[6U], "wav-source", "WAV 檔案音源",
                  wav_source_detail,
                  hibiki::ControlRouteHealthStateV1::Pending, 0U);
    } else if (driver_loopback_enabled) {
        set_route(snapshot.routes[6U], "driver-loopback", "Driver Stream Loopback",
                  driver_loopback_detail,
                  driver_loopback_ready
                      ? hibiki::ControlRouteHealthStateV1::Pending
                      : hibiki::ControlRouteHealthStateV1::Degraded,
                  driver_loopback_ready ? 0U : 1U);
    } else {
        set_route(snapshot.routes[6U], "browser-tab", "瀏覽器分頁",
                  tab_bridge_detail,
                  tab_bridge_enabled
                      ? hibiki::ControlRouteHealthStateV1::Pending
                      : hibiki::ControlRouteHealthStateV1::Unavailable,
                  tab_bridge_enabled ? 0U : 1U);
    }
    if (vst3_lane_enabled) {
        set_route(snapshot.routes[7U], "vst3-lane", "VST3 Lane",
                  "vst3 lane armed; waiting for first processed block.",
                  hibiki::ControlRouteHealthStateV1::Pending);
    }
    return snapshot;
}

bool same_volume(const hibiki::OutputGroupVolumeStateV1& left,
                 const hibiki::OutputGroupVolumeStateV1& right) noexcept {
    return left.schema_version == right.schema_version &&
           left.requested_db == right.requested_db &&
           left.safety_ceiling_db == right.safety_ceiling_db &&
           left.effective_db == right.effective_db && left.mute == right.mute &&
           left.generation == right.generation && left.origin == right.origin &&
           left.actuator == right.actuator;
}

bool same_route(const hibiki::ControlRouteHealthEntryV1& left,
                const hibiki::ControlRouteHealthEntryV1& right) noexcept {
    return left.id_bytes == right.id_bytes && left.name_bytes == right.name_bytes &&
           left.detail_bytes == right.detail_bytes && left.state == right.state &&
           left.flags == right.flags && left.id == right.id && left.name == right.name &&
           left.detail == right.detail;
}

BOOL WINAPI on_console_control(const DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        g_stop.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int wmain(const int argc, wchar_t* const* argv) {
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\HibikiDSP_v1_control";
    if (SetConsoleCtrlHandler(on_console_control, TRUE) == FALSE) return 2;
    std::filesystem::path wav_source_path{};
    bool wav_source_loop_requested = false;
    bool wav_source_path_seen = false;
    std::filesystem::path offline_render_path{};
    bool offline_render_requested = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument = argv[index] != nullptr
                                               ? std::wstring_view(argv[index])
                                               : std::wstring_view{};
        if (argument == L"--render-offline" && index + 1 < argc &&
            argv[index + 1] != nullptr) {
            offline_render_path = std::filesystem::path(argv[index + 1]);
            offline_render_requested = true;
            ++index;
            continue;
        }
        if (argument == L"--enable-wav-source") {
            // The shared requested flag below owns the enable decision.
            continue;
        }
        if (argument == L"--enable-wav-loop") {
            wav_source_loop_requested = true;
            continue;
        }
        if (argument == L"--wav-source-path" && index + 1 < argc &&
            argv[index + 1] != nullptr) {
            wav_source_path = std::filesystem::path(argv[index + 1]);
            wav_source_path_seen = true;
            ++index;
            continue;
        }
    }
    const bool system_volume_requested =
        has_command_line_flag(argc, argv, L"--enable-system-volume");
    const bool session_routing_requested =
        has_command_line_flag(argc, argv, L"--enable-session-routing");
    const bool wasapi_output_requested =
        has_command_line_flag(argc, argv, L"--enable-wasapi-output");
    const bool test_tone_requested =
        has_command_line_flag(argc, argv, L"--enable-test-tone");
    const bool process_delivery_requested =
        has_command_line_flag(argc, argv, L"--enable-process-delivery");
    const bool tab_bridge_requested =
        has_command_line_flag(argc, argv, L"--enable-tab-bridge");
    const bool tab_bass_correction_requested =
        has_command_line_flag(argc, argv, L"--enable-tab-bass-correction");
    const bool tab_noise_suppressor_requested =
        has_command_line_flag(argc, argv, L"--enable-tab-noise-suppressor");
    const bool driver_loopback_requested =
        has_command_line_flag(argc, argv, L"--enable-driver-loopback");
    const bool wav_source_requested =
        has_command_line_flag(argc, argv, L"--enable-wav-source");
    const bool vst3_lane_requested =
        has_command_line_flag(argc, argv, L"--enable-vst3-lane");
    std::wstring vst3_module_path{};
    std::wstring vst3_class_id{};
    std::wstring vst3_worker_path{};
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument = argv[index] != nullptr
                                               ? std::wstring_view(argv[index])
                                               : std::wstring_view{};
        if (argument == L"--vst3-module-path" && index + 1 < argc &&
            argv[index + 1] != nullptr) {
            vst3_module_path = argv[index + 1];
            ++index;
        }
        if (argument == L"--vst3-class-id" && index + 1 < argc &&
            argv[index + 1] != nullptr) {
            vst3_class_id = argv[index + 1];
            ++index;
        }
        if (argument == L"--vst3-worker-path" && index + 1 < argc &&
            argv[index + 1] != nullptr) {
            vst3_worker_path = argv[index + 1];
            ++index;
        }
    }
    if (tab_noise_suppressor_requested && !tab_bridge_requested) {
        // Fail-closed: the basic suppressor only has meaning on the tab lane;
        // refuse to start rather than silently running an unused effect.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (process_delivery_requested && (!wasapi_output_requested || !session_routing_requested)) {
        // Fail-closed: process delivery needs both a WASAPI sink and session
        // routing to be meaningful. Refuse to start rather than silently
        // running with a half-enabled path.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (tab_bridge_requested && !wasapi_output_requested) {
        // Fail-closed: tab bridge requires a WASAPI sink to be meaningful.
        // Refuse to start rather than silently running with a half-enabled path.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (tab_bass_correction_requested && !tab_bridge_requested) {
        // Fail-closed: the adaptive tab policy only has meaning for the
        // explicitly requested browser-tab source.
        std::fwprintf(stderr,
                      L"error: --enable-tab-bass-correction requires --enable-tab-bridge.\n");
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (driver_loopback_requested && !wasapi_output_requested) {
        // Fail-closed: the loopback source exists to prove the packet chain
        // reaches a sink; without a sink it has no meaningful behavior.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (tab_bridge_requested && test_tone_requested) {
        // Fail-closed: one explicit audio source per preview run. A test tone
        // would fight the tab lane for the same output-group graph.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (driver_loopback_requested &&
        (test_tone_requested || tab_bridge_requested || process_delivery_requested)) {
        // Fail-closed: one explicit audio source per preview run. The loopback
        // owns lane 0 and must not fight another source for the same graph.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (tab_bridge_requested && session_routing_requested) {
        // Fail-closed: process-loopback delivery owns its own lane set and
        // graph rebuilds; this slice does not mix browser capture into it.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (wav_source_loop_requested && !wav_source_requested) {
        // Fail-closed: loop only has meaning with an enabled WAV source.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (offline_render_requested &&
        (wasapi_output_requested || test_tone_requested || session_routing_requested ||
         process_delivery_requested || tab_bridge_requested || driver_loopback_requested ||
         system_volume_requested || wav_source_loop_requested)) {
        // Fail-closed: offline render is device-free evidence; it must not
        // share state with any live-delivery path or start the control pipe.
        std::fwprintf(stderr, L"error: --render-offline rejects all live-delivery flags.\n");
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (offline_render_requested && (!wav_source_requested || !wav_source_path_seen ||
                                     offline_render_path.empty())) {
        // Fail-closed: the offline render needs exactly one bounded WAV input.
        std::fwprintf(stderr,
                      L"error: --render-offline requires --enable-wav-source and --wav-source-path.\n");
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (wav_source_requested && !offline_render_requested &&
        (!wasapi_output_requested || !wav_source_path_seen)) {
        // Fail-closed: the source needs a sink and an explicit bounded file.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (wav_source_requested &&
        (test_tone_requested || tab_bridge_requested || process_delivery_requested ||
         driver_loopback_requested)) {
        // Fail-closed: one explicit audio source per preview run.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (vst3_lane_requested && !wasapi_output_requested) {
        // Fail-closed: the VST3 lane needs a sink to be meaningful.
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (vst3_lane_requested && vst3_class_id.empty()) {
        // Fail-closed: the SDK-backed sandbox worker requires a non-empty
        // VST3 class ID to load the requested processor. Refuse to start
        // rather than launching a worker that cannot succeed.
        std::fwprintf(stderr,
                      L"error: --enable-vst3-lane requires --vst3-class-id.\n");
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }
    if (vst3_lane_requested && vst3_module_path.empty()) {
        std::fwprintf(stderr,
                      L"error: --enable-vst3-lane requires --vst3-module-path.\n");
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return 2;
    }

    if (offline_render_requested) {
        OfflineRenderResult result;
        std::string error_detail;
        const bool rendered = render_wav_offline(
            wav_source_path, offline_render_path, result, error_detail);
        if (rendered) {
            std::fwprintf(stdout,
                          L"offline render complete: frames=%llu; resampled %lu->%lu;"
                          L" output=%ls\n",
                          static_cast<unsigned long long>(result.frames),
                          static_cast<unsigned long>(result.file_sample_rate),
                          static_cast<unsigned long>(result.output_sample_rate),
                          offline_render_path.c_str());
        } else {
            std::fwprintf(stderr, L"error: offline render failed: %hs\n",
                          error_detail.c_str());
        }
        (void)SetConsoleCtrlHandler(on_console_control, FALSE);
        return rendered ? 0 : 1;
    }

    hibiki::AudioEngineModel engine;
    hibiki::EngineControlWorkerV1 control_worker{engine};
    // Custom scene cards arrive over control IPC; the preview host owns the
    // fixed-capacity catalog for the lifetime of the worker.
    control_worker.ensure_owned_scene_catalog();
    IrPrepareState ir_state{&engine};
    control_worker.set_ir_prepare_handler(prepare_ir_file, &ir_state);
    SystemVolumeState system_volume;
    system_volume.enabled = system_volume_requested;
    SessionRoutingState session_routing;
    session_routing.requested = session_routing_requested;
    WasapiOutputState wasapi_output;
    wasapi_output.engine = &engine;
    wasapi_output.requested = wasapi_output_requested;
    TestToneState test_tone;
    DriverLoopbackState driver_loopback;
    WavFileSourceState wav_source;
    wav_source.requested = wav_source_requested;
    wav_source.loop = wav_source_loop_requested;
    ProcessDeliveryState process_delivery;
    TabBridgeState tab_bridge;
    Vst3LaneState vst3_lane;
    tab_bridge.requested = tab_bridge_requested;
    vst3_lane.requested = vst3_lane_requested;
    if (process_delivery_requested) {
        process_delivery.input_buffer.resize(
            static_cast<std::size_t>(kProcessLoopbackMaxFrames) * 2U);
        process_delivery.output_buffer.resize(
            static_cast<std::size_t>(kProcessLoopbackMaxFrames) *
            static_cast<std::size_t>(kTestToneMaxOutputChannels));
    }
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    hibiki::WindowsPhysicalDeviceCatalogServiceV1 physical_catalog;
    bool physical_catalog_ready = false;
    HRESULT catalog_result = com_result;
    if (com_initialized) {
        catalog_result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&device_enumerator));
        if (SUCCEEDED(catalog_result) && device_enumerator != nullptr) {
            catalog_result = physical_catalog.bind(device_enumerator);
            if (SUCCEEDED(catalog_result)) {
                catalog_result = physical_catalog.refresh_now();
                physical_catalog_ready = SUCCEEDED(catalog_result);
            }
        }
    }
    const auto wasapi_started = wasapi_output_requested && physical_catalog_ready
                                    ? start_default_wasapi_output(wasapi_output, engine,
                                                                 physical_catalog.catalog())
                                    : false;
    if (wasapi_started && test_tone_requested) {
        (void)prepare_test_tone(test_tone, wasapi_output, engine);
    }
    bool driver_loopback_ready = false;
    if (wasapi_started && driver_loopback_requested) {
        driver_loopback_ready = prepare_driver_loopback(driver_loopback, wasapi_output, engine);
    }
    bool wav_source_ready = false;
    if (wasapi_started && wav_source_requested) {
        wav_source_ready = prepare_wav_file_source(wav_source, wav_source_path,
                                                   wasapi_output, engine);
        if (!wav_source_ready) {
            // Honest diagnostics: a silently skipped source looks identical
            // to a working pipeline downstream (e.g. VST3 tap never gets
            // published), so report the failed preparation explicitly.
            std::fwprintf(
                stderr,
                L"error: wav source prepare failed; path=%ls\n",
                wav_source_path.wstring().c_str());
        }
    }
    if (wasapi_started && vst3_lane_requested) {
        vst3_lane.channels = wasapi_output.config.channels;
        // The worker lane must accept exactly the block size the sink will
        // render; a smaller prepared max silently degrades on the first tap
        // read when the endpoint reports larger buffer frames.
        vst3_lane.block_frames =
            static_cast<std::size_t>(wasapi_output.block_frames);
        vst3_lane.sample_rate = static_cast<double>(wasapi_output.config.sample_rate);
        if (!engine.has_active_graph()) {
            // The VST3 lane rides on the same immutable graph as an explicit
            // source. When no source has committed a graph yet, create a
            // minimal one so process_output_group can render and publish the
            // VST3 tap; otherwise preserve the source-owned graph contract.
            auto vst3_scene = hibiki::make_easy_scene(
                hibiki::EasySceneKind::Studio, "main");
            vst3_scene.graph.output_channels = vst3_lane.channels;
            engine.set_sample_rate(wasapi_output.config.sample_rate);
            if (!engine.prepare_graph(vst3_scene.graph, 1U) ||
                !engine.commit_graph()) {
                engine.rollback_graph();
            }
        }
        vst3_lane.ring_storage.resize(kVst3LaneRingCapacityFrames *
                                       static_cast<std::size_t>(vst3_lane.channels));
        vst3_lane.tap_buffer.resize(
            kVst3LaneRingCapacityFrames * static_cast<std::size_t>(vst3_lane.channels));
        vst3_lane.worker_output.resize(
            kVst3LaneRingCapacityFrames * static_cast<std::size_t>(vst3_lane.channels));
    }
    if (wasapi_started && session_routing_requested && process_delivery_requested) {
        (void)rebuild_delivery_graph(process_delivery, engine, wasapi_output,
                                     session_routing.coordinator);
    }
    std::string tab_bridge_detail = tab_bridge_requested
        ? "tab bridge requested; binding loopback listener..."
        : "tab bridge disabled; Preview will not accept browser packets.";
    bool tab_bass_correction_active = false;
    if (wasapi_started && tab_bridge_requested) {
        // Dedicated tab-capture lane routed to the main output group. This is
        // the graph the merged host was missing: without it the adapter
        // rejects every block and no tab audio can ever reach the sink.
        //
        // The graph transaction is the only mutable engine state this mode
        // owns; prepare/commit is atomic, so a failed rebuild leaves the
        // previously committed graph untouched and the host exits fail-closed.
        hibiki::GraphConfigV1 tab_graph{};
        hibiki::LaneConfigV1 tab_lane{};
        tab_lane.id = "tab-capture";
        tab_lane.output_group = "main";
        tab_lane.channel_count = 2U;
        tab_graph.lanes.push_back(tab_lane);
        tab_graph.output_channels = wasapi_output.config.channels;
        engine.set_sample_rate(wasapi_output.config.sample_rate);
        const bool graph_ready = engine.prepare_graph(tab_graph, 3U) && engine.commit_graph();
        if (!graph_ready) {
            engine.rollback_graph();
            (void)SetConsoleCtrlHandler(on_console_control, FALSE);
            return 2;
        }
        if (tab_bass_correction_requested) {
            // Keep this attachment inside AudioEngineModel rather than on the
            // bridge adapter. That makes the same committed controller own
            // both the RT bass correction and the confirmed source=2 EQ
            // visual snapshot published by EngineControlWorkerV1.
            hibiki::ProgramAwareLevelPolicyV1 tab_bass_policy{};
            tab_bass_policy.enabled = true;
            tab_bass_policy.target_dbfs = -23.0;
            tab_bass_policy.max_boost_db = 0.0;
            tab_bass_policy.max_cut_db = 0.0;
            tab_bass_policy.analysis_window_ms = 2000.0;
            tab_bass_policy.max_rate_db_per_second = 6.0;
            tab_bass_policy.silence_gate_dbfs = -70.0;
            tab_bass_policy.bass_correction_enabled = true;
            tab_bass_policy.bass_max_cut_db = 4.0;
            tab_bass_policy.night_compression_enabled = false;
            if (!engine.prepare_program_aware("main", tab_bass_policy) ||
                !engine.commit_program_aware()) {
                engine.rollback_program_aware();
                (void)SetConsoleCtrlHandler(on_console_control, FALSE);
                return 2;
            }
            tab_bass_correction_active = true;
        }
    }
    const bool tab_bridge_started = wasapi_started && tab_bridge.requested;
    std::string tab_bridge_route_detail{};
    bool suppressor_active = false;
    if (tab_bridge_started) {
        hibiki::TabBridgeServerConfigV1 tab_config{};
        if (tab_noise_suppressor_requested) {
            // Basic high-pass + downward-gate; explicitly not ML denoising.
            const hibiki::BasicNoiseSuppressorPolicyV1 kTabNoisePolicy{
                1U, true, -55.0, -24.0, 8.0, 120.0, 80.0};
            if (tab_bridge.noise_suppressor.configure(
                    kTabNoisePolicy, wasapi_output.config.sample_rate, 2U)) {
                tab_bridge.effects = {nullptr, nullptr,
                                      &tab_bridge.noise_suppressor, nullptr};
                suppressor_active = true;
            }
        }
        if (tab_bridge.server.start(tab_config, hibiki::enqueue_tab_capture_packet_v1,
                                    &tab_bridge.queue)) {
            tab_bridge.listening = true;
            tab_bridge.input_buffer.resize(
                static_cast<std::size_t>(kTabBridgeMaxFrames) * 2U);
            tab_bridge.output_buffer.resize(
                static_cast<std::size_t>(kTabBridgeMaxFrames) *
                static_cast<std::size_t>(kTestToneMaxOutputChannels));
            tab_bridge.lane_inputs.assign(1U, hibiki::RtLaneInputV1{});
            if (suppressor_active) {
                tab_bridge_route_detail =
                    "loopback listener bound; basic suppressor active (stereo).";
            } else {
                tab_bridge_route_detail =
                    "loopback listener bound; waiting for browser capture.";
            }
            if (tab_bass_correction_active) {
                tab_bridge_route_detail +=
                    " adaptive bass correction armed; curve follows telemetry.";
            }
            tab_bridge_detail = tab_bridge_route_detail;
        } else {
            tab_bridge_detail = "loopback listener bind failed; tab bridge disabled.";
        }
    }
    const auto initial_wasapi_snapshot = engine.wasapi_output_snapshot();
    std::string driver_loopback_detail = driver_loopback_requested
        ? (driver_loopback_ready
               ? "driver-stream loopback armed; waiting for first rendered packet."
               : "driver-stream loopback unavailable; sink or graph setup failed.")
        : "driver-stream loopback disabled; no in-process packets are published.";
    std::string wav_source_detail = wav_source_requested
        ? (wav_source_ready
               ? "wav file source armed; waiting for first rendered block."
               : "wav file source unavailable; file, sink or graph setup failed.")
        : "wav file source disabled; no file playback is scheduled.";
    const std::string catalog_detail = physical_catalog_ready
        ? "physical catalog ready; Preview sink disabled unless WASAPI opt-in is requested."
        : "physical catalog unavailable; safe Preview retained.";
    std::string system_volume_detail = system_volume_requested
        ? "system volume link requested; binding Windows endpoint..."
        : "system volume link disabled; Preview will not write Windows volume.";
    if (system_volume_requested && device_enumerator != nullptr) {
        const auto volume_result = bind_default_volume(device_enumerator, system_volume.broker);
        system_volume.bound = SUCCEEDED(volume_result);
        if (system_volume.bound) {
            hibiki::OutputGroupVolumeStateV1 initial_volume{};
            if (SUCCEEDED(system_volume.broker.read_state(initial_volume))) {
                const hibiki::VolumeNotificationV1 notification{
                    initial_volume.requested_db, initial_volume.mute, initial_volume.generation};
                if (engine.apply_windows_volume("main", notification) ==
                    hibiki::VolumeNotificationResult::Accepted) {
                    system_volume.last_engine_state = engine.volume();
                    system_volume.have_last_engine_state = true;
                    system_volume_detail =
                        "system endpoint volume linked; write-through explicitly enabled.";
                } else {
                    system_volume.broker.unbind();
                    system_volume.bound = false;
                    system_volume_detail =
                        "system endpoint read failed; write-through disabled.";
                }
            } else {
                system_volume.broker.unbind();
                system_volume.bound = false;
                system_volume_detail = "system endpoint read failed; write-through disabled.";
            }
        } else {
            system_volume_detail = "system endpoint unavailable; write-through disabled.";
        }
    }
    std::string session_routing_detail = session_routing_requested
        ? "session routing requested; binding Windows App sessions..."
        : "session routing disabled; Preview will not enumerate Apps.";
    std::string process_loopback_detail = session_routing_requested
        ? "session source is worker-owned; process delivery remains unverified."
        : "session routing disabled; process source is not bound.";
    if (session_routing_requested) {
        hibiki::SessionCatalogSnapshotV1 initial_session_catalog{};
        initial_session_catalog.sequence = 1U;
        if (!session_routing.catalog_store.publish(initial_session_catalog)) {
            session_routing_detail = "session catalog store unavailable; routing disabled.";
        }
        if (device_enumerator != nullptr) {
            const auto bind_result = bind_default_session_route_if_changed(
                device_enumerator, session_routing);
            if (SUCCEEDED(bind_result)) {
                const auto refresh_result = session_routing.coordinator.refresh();
                if (refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded &&
                    publish_session_catalog(session_routing)) {
                    session_routing_detail =
                        "session catalog linked; per-App controls enabled; delivery unverified.";
                } else {
                    session_routing.coordinator.unbind();
                    session_routing.bound = false;
                    session_routing_detail =
                        "session enumeration failed; per-App controls disabled.";
                }
            } else {
                session_routing_detail =
                    "default render session endpoint unavailable; routing disabled.";
            }
        } else {
            session_routing_detail = "Windows session endpoint unavailable; routing disabled.";
        }
    }
    const bool system_volume_active = system_volume_requested && system_volume.bound;
    const bool session_routing_active = session_routing_requested && session_routing.bound;
    if (!session_routing_active && session_routing_requested) {
        process_loopback_detail = "session route unavailable; process source remains unbound.";
    }
    control_worker.set_session_volume_handler(enqueue_session_volume_command, &session_routing);
    control_worker.set_session_route_handler(enqueue_session_route_command, &session_routing);
    control_worker.set_session_route_rule_handler(enqueue_session_route_rule_command,
                                                  &session_routing);
    hibiki::EqVisualSnapshotStoreV1 eq_visual_store;
    control_worker.set_eq_visual_publisher(publish_eq_visual_snapshot, &eq_visual_store);
    hibiki::ControlStatusSnapshotStoreV1 status_store;
    const std::string initial_main_output_detail = wasapi_output.requested
        ? wasapi_route_detail(wasapi_output, initial_wasapi_snapshot)
        : catalog_detail;
    auto status = make_initial_status(engine.volume(), initial_main_output_detail,
                                      wasapi_output,
                                      initial_wasapi_snapshot, system_volume_active,
                                      system_volume_detail, session_routing_active,
                                      session_routing_detail, process_loopback_detail,
                                      wasapi_output.driver_loopback_enabled,
                                      driver_loopback_ready,
                                      driver_loopback_detail,
                                      wasapi_output.wav_source_enabled, wav_source_detail,
                                      tab_bridge.listening, tab_bridge_detail,
                                      vst3_lane_requested);
    if (!status_store.publish(status)) return 4;
    hibiki::ControlPlaneHostV1 host;
    hibiki::IpcNamedPipeConfigV1 pipe_config{kPipeName, 64U * 1024U, 1000U};
    pipe_config.require_first_pipe_instance = true;
    if (!host.start_with_queue(pipe_config,
                               physical_catalog_ready ? physical_catalog.snapshot_store() : nullptr,
                               &status_store,
                               session_routing_requested ? &session_routing.catalog_store
                                                         : nullptr,
                               &eq_visual_store)) {
        engine.stop_wasapi_output();
        physical_catalog.unbind();
        session_routing.coordinator.unbind();
        system_volume.broker.unbind();
        if (device_enumerator != nullptr) device_enumerator->Release();
        if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
        return 3;
    }

    auto next_catalog_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    auto next_session_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    auto next_delivery_sync = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
    auto next_volume_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
    auto next_volume_write = std::chrono::steady_clock::now();
    auto next_driver_loopback_render = std::chrono::steady_clock::now();
    auto next_test_tone_render = std::chrono::steady_clock::now();
    auto next_wav_source_render = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (session_routing_requested && device_enumerator != nullptr &&
            now >= next_session_poll) {
            const auto bind_result = bind_default_session_route_if_changed(
                device_enumerator, session_routing);
            if (bind_result == S_OK) {
                const auto refresh_result = session_routing.coordinator.refresh();
                if (refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded) {
                    (void)publish_session_catalog(session_routing);
                } else {
                    session_routing.coordinator.unbind();
                    session_routing.bound = false;
                }
            } else if (bind_result == S_FALSE && session_routing.bound) {
                const auto refresh_result = session_routing.coordinator.poll_and_refresh();
                if (refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::NoChange &&
                    refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded &&
                    refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Unbound) {
                    (void)publish_session_catalog(session_routing);
                }
                if (refresh_result == hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded) {
                    session_routing.bound = false;
                }
            } else if (FAILED(bind_result)) {
                session_routing.coordinator.unbind();
                session_routing.bound = false;
            }
            next_session_poll = now + std::chrono::milliseconds{250};
        }
        if (process_delivery_requested && now >= next_delivery_sync) {
            (void)rebuild_delivery_graph(process_delivery, engine, wasapi_output,
                                         session_routing.coordinator);
            sync_delivery_sources(process_delivery, session_routing.coordinator);
            next_delivery_sync = now + std::chrono::milliseconds{100};
        }
        if (system_volume_requested && device_enumerator != nullptr && now >= next_volume_poll) {
            const auto bind_result = rebind_default_volume_if_changed(
                device_enumerator, system_volume.broker, system_volume.bound);
            if (bind_result == S_OK) {
                system_volume.bound = true;
                hibiki::OutputGroupVolumeStateV1 rebound{};
                if (SUCCEEDED(system_volume.broker.read_state(rebound))) {
                    const hibiki::VolumeNotificationV1 notification{
                        rebound.requested_db, rebound.mute, rebound.generation};
                    if (engine.apply_windows_volume("main", notification) ==
                        hibiki::VolumeNotificationResult::Accepted) {
                        system_volume.last_engine_state = engine.volume();
                        system_volume.have_last_engine_state = true;
                    }
                }
            } else if (FAILED(bind_result)) {
                system_volume.broker.unbind();
                system_volume.bound = false;
            }
            next_volume_poll = now + std::chrono::milliseconds{250};
        }
        if (system_volume_requested && system_volume.bound) {
            hibiki::WindowsVolumeNotificationSnapshotV1 notification{};
            if (system_volume.broker.poll(notification)) {
                (void)system_volume.link.apply(engine, "main", notification);
                system_volume.last_engine_state = engine.volume();
                system_volume.have_last_engine_state = true;
            }
        }
        (void)control_worker.drain(host.command_queue());
        (void)drain_session_commands(session_routing);
        if (process_delivery_requested) {
            poll_and_deliver(process_delivery, engine, session_routing);
        }
        if (system_volume_requested && system_volume.bound &&
            system_volume.have_last_engine_state && now >= next_volume_write) {
            const auto current_engine_state = engine.volume();
            if (!same_volume(current_engine_state, system_volume.last_engine_state)) {
                const auto write_result = system_volume.broker.write(
                    current_engine_state, hibiki::WindowsVolumeEventContextsV1::ui());
                if (SUCCEEDED(write_result)) {
                    system_volume.last_engine_state = current_engine_state;
                    next_volume_write = now;
                } else {
                    next_volume_write = now + std::chrono::milliseconds{100};
                    if (write_result == AUDCLNT_E_DEVICE_INVALIDATED) {
                        system_volume.broker.unbind();
                        system_volume.bound = false;
                    }
                }
            }
        }
        if (physical_catalog_ready && std::chrono::steady_clock::now() >= next_catalog_poll) {
            (void)physical_catalog.poll_and_refresh();
            next_catalog_poll = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds{250};
        }
        if (wasapi_output.test_tone_enabled && now >= next_test_tone_render) {
            (void)render_test_tone(test_tone, engine);
            next_test_tone_render = now + std::chrono::milliseconds{10};
        }
        if (wasapi_output.driver_loopback_enabled && now >= next_driver_loopback_render) {
            (void)render_driver_loopback(driver_loopback, engine);
            next_driver_loopback_render = now + std::chrono::milliseconds{10};
        }
        if (wav_source.prepared && !wav_source.eof && now >= next_wav_source_render) {
            (void)render_wav_file_source(wav_source, engine);
            next_wav_source_render = now + std::chrono::milliseconds{10};
        }
        if (tab_bridge.listening) {
            hibiki::TabCaptureBlockV1 block{};
            const bool delivered = hibiki::process_tab_capture_lane_to_wasapi_v1(
                engine, 0U, tab_bridge.queue,
                tab_bridge.input_buffer.data(), kTabBridgeMaxFrames,
                std::span<hibiki::RtLaneInputV1>(tab_bridge.lane_inputs),
                tab_bridge.output_buffer.data(), kTabBridgeMaxFrames,
                block, tab_noise_suppressor_requested ? &tab_bridge.effects : nullptr);
            if (delivered) {
                ++tab_bridge.received_blocks;
            }
        }
        if (vst3_lane.requested && wasapi_started) {
            // Launch the sandbox worker on the first iteration.
            if (!vst3_lane.launched) {
                if (!vst3_worker_path.empty()) {
                    vst3_lane.worker_executable = vst3_worker_path;
                } else {
                    vst3_lane.worker_executable =
                        L".local\\vst3-build\\vst-host\\Release\\hibiki_vst3_sdk_worker.exe";
                }
                vst3_lane.plugin_path = vst3_module_path;
                hibiki::Vst3SandboxLaunchV1 launch{};
                launch.worker_executable = vst3_lane.worker_executable;
                launch.plugin_path = vst3_lane.plugin_path;
                launch.vst3_class_id = vst3_class_id;
                launch.watchdog_timeout_ms = 250U;
                launch.start_time_ms = 1U;
                launch.worker_pipe_name = L"\\\\.\\pipe\\HibikiDSP_preview_vst3_worker_v1";
                launch.worker_pipe_timeout_ms = 1000U;
                launch.vst3_sample_rate = vst3_lane.sample_rate;
                launch.vst3_channels = vst3_lane.channels;
                if (hibiki::validate_vst3_sandbox_launch_v1(launch) &&
                    vst3_lane.sandbox.launch(launch)) {
                    (void)vst3_lane.host.start(hibiki::PluginDescriptorV1{
                        "preview-lane", vst3_lane.channels,
                        vst3_lane.channels, 0U, true, 250U, true, 1U});
                    if (vst3_lane.host.prepare_worker_session(
                            vst3_lane.sandbox, vst3_lane.sample_rate,
                            static_cast<std::uint32_t>(vst3_lane.block_frames))) {
                        const bool worker_connected = vst3_lane.sandbox.wait_for_worker(1000U);
                        if (!worker_connected) {
                            std::fwprintf(
                                stderr,
                                L"error: vst3 worker did not connect within 1s.\n");
                            ++vst3_lane.failed_blocks;
                        } else {
                            const auto hs = vst3_lane.host.handshake_worker(vst3_lane.request_id++);
                            if (hs != hibiki::Vst3WorkerExchangeResultV1::ok) {
                                std::fwprintf(
                                    stderr,
                                    L"error: vst3 worker handshake failed result=%d.\n",
                                    static_cast<int>(hs));
                            }
                        }
                        vst3_lane.launched = true;
                        // Prepare the lane ring so apply_vst3_lanes can pop.
                        (void)engine.prepare_vst3_lane("main", vst3_lane.channels,
                                                       std::span<float>(vst3_lane.ring_storage));
                        (void)engine.commit_vst3_lane();
                    }
                } else {
                    ++vst3_lane.failed_blocks;
                    vst3_lane.requested = false;  // Don't retry indefinitely.
                }
            }
            // Read tap -> process in worker -> push into ring.
            if (vst3_lane.launched && !vst3_lane.host.can_process()) {
                static bool diag_host_dead_printed = false;
                if (!diag_host_dead_printed) {
                    diag_host_dead_printed = true;
                    std::fwprintf(
                        stderr,
                        L"error: vst3 host cannot process after launch.\n");
                }
                ++vst3_lane.failed_blocks;
            }
            if (vst3_lane.launched && vst3_lane.host.can_process()) {
                std::uint32_t tap_channels = 0U;
                std::size_t tap_frames = 0U;
                std::uint64_t tap_sequence = 0U;
                const bool tap_read = engine.read_vst3_tap(
                    "main", vst3_lane.tap_buffer.data(),
                    kVst3LaneRingCapacityFrames,
                    tap_channels, tap_frames, tap_sequence);
                if (tap_read) {
                    if (tap_sequence == vst3_lane.tap_sequence) {
                        // Same snapshot already consumed; skip duplicate work.
                    } else {
                    const std::size_t sample_count =
                        tap_frames * static_cast<std::size_t>(tap_channels);
                    auto result = vst3_lane.host.process_worker_block(
                        vst3_lane.request_id++, vst3_lane.block_start,
                        static_cast<std::uint32_t>(tap_frames),
                        std::span<const float>(vst3_lane.tap_buffer.data(), sample_count),
                        std::span<float>(vst3_lane.worker_output.data(), sample_count));
                    if (result == hibiki::Vst3WorkerExchangeResultV1::ok) {
                        vst3_lane.block_start += tap_frames;
                        // Push processed audio into the ring for RT consumption.
                        (void)engine.push_vst3_lane("main", vst3_lane.worker_output.data(),
                                                    tap_frames);
                        ++vst3_lane.processed_blocks;
                        vst3_lane.tap_sequence = tap_sequence;
                    } else {
                        ++vst3_lane.failed_blocks;
                    }
                    }
                }
            }
        }
        const auto wasapi_snapshot = engine.wasapi_output_snapshot();
        const auto volume = engine.volume();
        bool status_changed = false;
        if (!same_volume(volume, status.volume)) {
            status.volume = volume;
            status_changed = true;
        }
        const std::string main_output_detail = wasapi_output.requested
            ? wasapi_route_detail(wasapi_output, wasapi_snapshot)
            : (physical_catalog_ready
                   ? std::string("physical catalog ready; shared-mode WASAPI sink disabled by default.")
                   : std::string("physical catalog unavailable; safe Preview retained."));
        const auto previous_main_output_route = status.routes[1U];
        set_route(status.routes[1U], "main-output", "主輸出",
                  main_output_detail,
                  wasapi_output.requested
                      ? wasapi_route_state(wasapi_output, wasapi_snapshot)
                      : hibiki::ControlRouteHealthStateV1::Unavailable,
                  wasapi_output.requested ? 0U : 1U);
        if (!same_route(previous_main_output_route, status.routes[1U])) status_changed = true;
        if (wasapi_output.driver_loopback_enabled) {
            const bool loopback_ready = driver_loopback.rendered_blocks > 0U &&
                                        has_rendered_blocks(wasapi_snapshot);
            const auto failures = driver_loopback.encode_failures +
                                  driver_loopback.push_failures +
                                  driver_loopback.pop_failures +
                                  driver_loopback.deliver_failures;
            std::string detail;
            if (loopback_ready) {
                detail = "driver-stream loopback rendering; packets=" +
                         std::to_string(driver_loopback.rendered_blocks);
                detail += "; sink=" +
                          std::to_string(wasapi_snapshot.primary.submitted_blocks) +
                          "/" + std::to_string(wasapi_snapshot.primary.rendered_blocks);
                if (failures > 0U) {
                    detail += "; failed=" + std::to_string(failures);
                }
                if (driver_loopback.ring.overrun_count != 0U ||
                    driver_loopback.ring.underrun_count != 0U) {
                    detail += "; ring_overrun=" +
                              std::to_string(driver_loopback.ring.overrun_count) +
                              "; ring_underrun=" +
                              std::to_string(driver_loopback.ring.underrun_count);
                }
                detail += "; user-space only.";
            } else if (failures > 0U) {
                detail = "driver-stream loopback degraded; packets=" +
                         std::to_string(driver_loopback.rendered_blocks) +
                         "; failures=" + std::to_string(failures);
            } else {
                detail = "driver-stream loopback armed; waiting for first rendered packet.";
            }
            driver_loopback_detail = std::move(detail);
        }
        const bool volume_route_ready = system_volume_requested && system_volume.bound;
        const auto volume_detail = volume_route_ready
            ? std::string_view("system endpoint volume linked; write-through explicitly enabled.")
            : (system_volume_requested
                   ? std::string_view("system endpoint unavailable; write-through disabled.")
                   : std::string_view("system volume link disabled; Preview will not write Windows volume."));
        const auto previous_volume_route = status.routes[2U];
        set_route(status.routes[2U], "windows-volume", "Windows 音量", volume_detail,
                  volume_route_ready ? hibiki::ControlRouteHealthStateV1::Ready
                                      : hibiki::ControlRouteHealthStateV1::Unavailable,
                  volume_route_ready ? 0U : 1U);
        if (!same_route(previous_volume_route, status.routes[2U])) status_changed = true;
        const auto session_snapshot = session_routing.coordinator.snapshot();
        const bool session_route_ready = session_routing_requested && session_routing.bound &&
                                         !session_snapshot.degraded;
        const auto session_route_state = session_snapshot.degraded
            ? hibiki::ControlRouteHealthStateV1::Degraded
            : session_route_ready && session_snapshot.has_graph
                ? hibiki::ControlRouteHealthStateV1::Ready
                : session_route_ready && session_snapshot.session_count > 0U
                    ? hibiki::ControlRouteHealthStateV1::Pending
                    : hibiki::ControlRouteHealthStateV1::Unavailable;
        const auto wasapi_snapshot_now = engine.wasapi_output_snapshot();
        const bool wasapi_actually_rendering = has_rendered_blocks(wasapi_snapshot_now);
        const auto process_route_state = session_snapshot.degraded
            ? hibiki::ControlRouteHealthStateV1::Degraded
            : session_route_ready && session_snapshot.routed_count > 0U &&
                  process_delivery.rendered_blocks > 0U && wasapi_actually_rendering
                ? hibiki::ControlRouteHealthStateV1::Ready
                : session_route_ready && session_snapshot.routed_count > 0U
                    ? hibiki::ControlRouteHealthStateV1::Pending
                    : hibiki::ControlRouteHealthStateV1::Unavailable;
        const auto session_detail = !session_routing_requested
            ? std::string_view("session routing disabled; Preview will not enumerate Apps.")
            : !session_routing.bound
                ? std::string_view("session endpoint unavailable; per-App controls disabled.")
                : session_snapshot.degraded
                    ? std::string_view("session enumeration degraded; controls fail closed.")
                    : std::string_view("session catalog linked; per-App controls enabled; delivery unverified.");
        static thread_local std::string process_detail_buffer;
        if (!session_routing_requested) {
            process_detail_buffer =
                "session routing disabled; process source is not bound.";
        } else if (!process_delivery_requested) {
            process_detail_buffer =
                "process-tree source remains worker-owned; physical delivery unverified.";
        } else if (process_route_state == hibiki::ControlRouteHealthStateV1::Ready) {
            process_detail_buffer =
                "per-App delivery active: " +
                std::to_string(process_delivery.rendered_blocks) + " block(s) rendered, " +
                std::to_string(wasapi_snapshot_now.primary.rendered_blocks +
                               wasapi_snapshot_now.secondary.rendered_blocks) +
                " WASAPI sink block(s).";
        } else if (process_delivery.rendered_blocks > 0U) {
            process_detail_buffer =
                "per-App capture running (" +
                std::to_string(process_delivery.rendered_blocks) +
                " block(s)) but WASAPI sink has not rendered.";
        } else {
            process_detail_buffer =
                "per-App process delivery enabled; waiting for captured audio.";
        }
        const auto previous_session_route = status.routes[4U];
        const auto previous_process_route = status.routes[5U];
        set_route(status.routes[4U], "windows-session", "Windows App／Session", session_detail,
                  session_route_state, session_routing_requested && session_routing.bound ? 0U : 1U);
        set_route(status.routes[5U], "process-loopback", "Process Loopback", process_detail_buffer,
                  process_route_state, session_routing_requested && session_routing.bound ? 0U : 1U);
        if (!same_route(previous_session_route, status.routes[4U]) ||
            !same_route(previous_process_route, status.routes[5U])) {
            status_changed = true;
        }
        const auto previous_tab_route = status.routes[6U];
        if (driver_loopback_requested) {
            const auto previous_loopback_route = status.routes[6U];
            const bool loopback_ready = driver_loopback.rendered_blocks > 0U &&
                                        has_rendered_blocks(wasapi_snapshot);
            set_route(status.routes[6U], "driver-loopback", "Driver Stream Loopback",
                      driver_loopback_detail,
                      loopback_ready ? hibiki::ControlRouteHealthStateV1::Ready
                                     : (wasapi_output.driver_loopback_enabled
                                            ? hibiki::ControlRouteHealthStateV1::Pending
                                            : hibiki::ControlRouteHealthStateV1::Degraded),
                      wasapi_output.driver_loopback_enabled ? 0U : 1U);
            if (!same_route(previous_loopback_route, status.routes[6U])) {
                status_changed = true;
            }
        }
        if (!tab_bridge_route_detail.empty()) {
            const auto dropped = tab_bridge.queue.dropped_blocks();
            if (dropped > 0U) {
                tab_bridge_route_detail =
                    "receiving; dropped " + std::to_string(dropped) +
                    " block(s) while the 4-slot capture queue was full.";
            }
        }
        if (!driver_loopback_requested && !wav_source_requested) {
            if (!tab_bridge.listening) {
                set_route(status.routes[6U], "browser-tab", "瀏覽器分頁", tab_bridge_detail,
                          hibiki::ControlRouteHealthStateV1::Unavailable, 1U);
            } else if (tab_bridge.received_blocks > 0U &&
                       has_rendered_blocks(wasapi_snapshot)) {
                set_route(status.routes[6U], "browser-tab", "瀏覽器分頁",
                          !tab_bridge_route_detail.empty()
                              ? std::string_view(tab_bridge_route_detail)
                              : std::string_view(
                                    "receiving user-gesture tab capture; rendered through WASAPI sink."),
                          hibiki::ControlRouteHealthStateV1::Ready, 0U);
            } else {
                set_route(status.routes[6U], "browser-tab", "瀏覽器分頁", tab_bridge_detail,
                          hibiki::ControlRouteHealthStateV1::Pending, 0U);
            }
            if (!same_route(previous_tab_route, status.routes[6U])) status_changed = true;
        }
        if (wav_source_requested) {
            const bool wav_ready = wav_source.rendered_blocks > 0U &&
                                   has_rendered_blocks(wasapi_snapshot);
            std::string detail;
            if (wav_source.eof && !wav_source.loop) {
                detail = "wav file source finished; blocks=" +
                         std::to_string(wav_source.rendered_blocks);
            } else if (wav_ready) {
                detail = "wav file source rendering; blocks=" +
                         std::to_string(wav_source.rendered_blocks);
                detail += "; frames=" + std::to_string(wav_source.frames_rendered) +
                          "/" + std::to_string(wav_source.data.frames());
                if (wav_source.loop) {
                    detail += "; loop=on";
                }
                if (wav_source.file_sample_rate != 0U &&
                    wav_source.file_sample_rate != wasapi_output.config.sample_rate) {
                    detail += "; resampled " + std::to_string(wav_source.file_sample_rate) +
                              "->" + std::to_string(wasapi_output.config.sample_rate);
                }
                if (wav_source.failed_blocks != 0U) {
                    detail += "; failed=" +
                              std::to_string(wav_source.failed_blocks);
                }
            } else if (!wav_source.prepared) {
                detail =
                    "wav file source unavailable; file, sink or graph setup failed.";
            } else {
                detail = "wav file source armed; waiting for first rendered block.";
            }
            wav_source_detail = std::move(detail);
            const auto previous_wav_route = status.routes[6U];
            set_route(status.routes[6U], "wav-source", "WAV 檔案音源",
                      wav_source_detail,
                      wav_ready ? hibiki::ControlRouteHealthStateV1::Ready
                                : hibiki::ControlRouteHealthStateV1::Pending,
                      wav_source.prepared ? 0U : 1U);
            if (!same_route(previous_wav_route, status.routes[6U])) status_changed = true;
        }
        if (vst3_lane_requested) {
            static thread_local std::string vst3_detail_buffer;
            const auto previous_vst3_route = status.routes[7U];
            if (!vst3_lane.launched && vst3_lane.failed_blocks > 0U) {
                set_route(status.routes[7U], "vst3-lane", "VST3 Lane",
                          "vst3 lane unavailable; worker or plugin setup failed.",
                          hibiki::ControlRouteHealthStateV1::Degraded, 1U);
            } else if (vst3_lane.processed_blocks > 0U) {
                vst3_detail_buffer =
                    "vst3 lane rendering; blocks=" +
                    std::to_string(vst3_lane.processed_blocks);
                if (vst3_lane.failed_blocks != 0U) {
                    vst3_detail_buffer +=
                        "; failed=" + std::to_string(vst3_lane.failed_blocks);
                }
                set_route(status.routes[7U], "vst3-lane", "VST3 Lane",
                          std::string_view(vst3_detail_buffer),
                          hibiki::ControlRouteHealthStateV1::Ready, 0U);
            } else {
                set_route(status.routes[7U], "vst3-lane", "VST3 Lane",
                          "vst3 lane armed; waiting for first processed block.",
                          hibiki::ControlRouteHealthStateV1::Pending, 0U);
            }
            if (!same_route(previous_vst3_route, status.routes[7U])) status_changed = true;
        }
        if (status_changed) {
            ++status.sequence;
            (void)status_store.publish(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    host.stop();
    tab_bridge.server.stop();
    engine.stop_wasapi_output();
    physical_catalog.unbind();
    session_routing.coordinator.unbind();
    system_volume.broker.unbind();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return 0;
}
