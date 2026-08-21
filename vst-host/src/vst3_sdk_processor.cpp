#include "hibiki/vst3_sdk_processor.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace hibiki {
namespace {

using Steinberg::FUnknownPtr;
using Steinberg::IPtr;
using Steinberg::Vst::IAudioProcessor;
using Steinberg::Vst::IComponent;
using Steinberg::Vst::SpeakerArrangement;
using Steinberg::Vst::SpeakerArr::k51;
using Steinberg::Vst::SpeakerArr::k71Cine;
using Steinberg::Vst::SpeakerArr::kMono;
using Steinberg::Vst::SpeakerArr::kStereo;

constexpr bool result_ok(Steinberg::tresult result) noexcept
{
    return result == Steinberg::kResultOk;
}

constexpr std::size_t kMaxPluginStateBytesV1 = 1024U * 1024U;

class BoundedStateStream final
    : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::IBStream>> {
public:
    explicit BoundedStateStream(const std::size_t capacity) : data_(capacity, 0U) {}

    Steinberg::tresult PLUGIN_API read(void* buffer, const Steinberg::int32 num_bytes,
                                       Steinberg::int32* num_read) override {
        if (num_bytes < 0 || buffer == nullptr || cursor_ > size_) return Steinberg::kInvalidArgument;
        const auto available = size_ - cursor_;
        const auto count = std::min<std::size_t>(available, static_cast<std::size_t>(num_bytes));
        if (count != 0U) std::memcpy(buffer, data_.data() + cursor_, count);
        cursor_ += count;
        if (num_read != nullptr) *num_read = static_cast<Steinberg::int32>(count);
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API write(void* buffer, const Steinberg::int32 num_bytes,
                                        Steinberg::int32* num_written) override {
        if (num_bytes < 0 || buffer == nullptr || cursor_ > data_.size()) {
            return Steinberg::kInvalidArgument;
        }
        const auto count = static_cast<std::size_t>(num_bytes);
        if (count > data_.size() - cursor_) {
            overflowed_ = true;
            return Steinberg::kOutOfMemory;
        }
        if (count != 0U) std::memcpy(data_.data() + cursor_, buffer, count);
        cursor_ += count;
        size_ = std::max(size_, cursor_);
        if (num_written != nullptr) *num_written = num_bytes;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API seek(const Steinberg::int64 position,
                                       const Steinberg::int32 mode,
                                       Steinberg::int64* result) override {
        Steinberg::int64 base = 0;
        if (mode == Steinberg::IBStream::kIBSeekCur) {
            base = static_cast<Steinberg::int64>(cursor_);
        } else if (mode == Steinberg::IBStream::kIBSeekEnd) {
            base = static_cast<Steinberg::int64>(size_);
        } else if (mode != Steinberg::IBStream::kIBSeekSet) {
            return Steinberg::kInvalidArgument;
        }
        if ((position > 0 && base > std::numeric_limits<Steinberg::int64>::max() - position) ||
            (position < 0 && base < std::numeric_limits<Steinberg::int64>::min() - position)) {
            return Steinberg::kInvalidArgument;
        }
        const auto next = base + position;
        if (next < 0 || static_cast<std::uint64_t>(next) > data_.size()) {
            return Steinberg::kInvalidArgument;
        }
        cursor_ = static_cast<std::size_t>(next);
        if (result != nullptr) *result = next;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API tell(Steinberg::int64* position) override {
        if (position == nullptr) return Steinberg::kInvalidArgument;
        *position = static_cast<Steinberg::int64>(cursor_);
        return Steinberg::kResultTrue;
    }

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_.data(); }

private:
    std::vector<std::uint8_t> data_;
    std::size_t cursor_{0U};
    std::size_t size_{0U};
    bool overflowed_{false};
};

std::uint32_t layout_channels(Vst3SdkAudioLayoutV1 layout) noexcept
{
    return static_cast<std::uint32_t>(layout);
}

SpeakerArrangement layout_arrangement(Vst3SdkAudioLayoutV1 layout) noexcept
{
    switch (layout) {
    case Vst3SdkAudioLayoutV1::mono:
        return kMono;
    case Vst3SdkAudioLayoutV1::stereo:
        return kStereo;
    case Vst3SdkAudioLayoutV1::surround_51:
        return k51;
    case Vst3SdkAudioLayoutV1::surround_71:
        return k71Cine;
    }
    return 0;
}

void set_error(std::string& error, const char* text)
{
    error = text;
}

} // namespace

struct Vst3SdkProcessorV1::Impl {
    VST3::Hosting::Module::Ptr module;
    IPtr<Steinberg::Vst::HostApplication> host;
    IPtr<IComponent> component;
    FUnknownPtr<IAudioProcessor> processor;
    Steinberg::Vst::AudioBusBuffers input_bus{};
    Steinberg::Vst::AudioBusBuffers output_bus{};
    Steinberg::Vst::ProcessData process_data{};
    Steinberg::Vst::ProcessSetup setup{};
    Steinberg::Vst::ParameterChanges parameter_changes{16};
};

Vst3SdkProcessorV1::~Vst3SdkProcessorV1()
{
    close();
}

bool validate_vst3_sdk_parameter_points_v1(
    const std::span<const Vst3SdkParameterPointV1> points,
    const std::uint32_t frames) noexcept {
    if (frames == 0U || frames > Vst3SdkProcessorV1::kMaxFrames || points.size() > 64U) {
        return false;
    }
    for (std::size_t index = 0U; index < points.size(); ++index) {
        const auto& point = points[index];
        if (point.sample_offset < 0 || static_cast<std::uint32_t>(point.sample_offset) >= frames ||
            !std::isfinite(point.normalized_value) || point.normalized_value < 0.0 ||
            point.normalized_value > 1.0) {
            return false;
        }
        std::size_t same_parameter = 0U;
        bool first_parameter_point = true;
        for (std::size_t prior = 0U; prior <= index; ++prior) {
            if (points[prior].parameter_id == point.parameter_id) {
                ++same_parameter;
                if (prior < index) first_parameter_point = false;
            }
        }
        if (same_parameter > 5U) return false;
        if (first_parameter_point) {
            std::size_t unique_count = 0U;
            for (std::size_t candidate = 0U; candidate <= index; ++candidate) {
                bool seen = false;
                for (std::size_t prior = 0U; prior < candidate; ++prior) {
                    if (points[prior].parameter_id == points[candidate].parameter_id) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) ++unique_count;
            }
            if (unique_count > 16U) return false;
        }
    }
    return true;
}

bool Vst3SdkProcessorV1::open(const std::string& module_path,
                              const std::string& class_id,
                              const Vst3SdkProcessorConfigV1& config,
                              std::string& error)
{
    close();
    error.clear();

    const auto channels = layout_channels(config.layout);
    if (!std::isfinite(config.sample_rate) || config.sample_rate < 8000.0 ||
        config.sample_rate > 384000.0 || config.max_frames == 0 ||
        config.max_frames > kMaxFrames || channels == 0 || channels > kMaxChannels) {
        set_error(error, "invalid VST3 processor configuration");
        return false;
    }
    const auto arrangement = layout_arrangement(config.layout);
    if (arrangement == 0) {
        set_error(error, "unsupported VST3 speaker arrangement");
        return false;
    }

    const auto uid = VST3::UID::fromString(class_id);
    if (!uid) {
        set_error(error, "invalid VST3 class UID");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->module = VST3::Hosting::Module::create(module_path, error);
    if (!impl->module) {
        if (error.empty()) {
            set_error(error, "VST3 module could not be loaded");
        }
        return false;
    }
    impl->host = Steinberg::owned(new Steinberg::Vst::HostApplication());
    if (!impl->host) {
        set_error(error, "VST3 host context allocation failed");
        return false;
    }
    impl->module->getFactory().setHostContext(impl->host);
    impl->component = impl->module->getFactory().createInstance<IComponent>(*uid);
    if (!impl->component || !result_ok(impl->component->initialize(impl->host))) {
        set_error(error, "VST3 component initialization failed");
        return false;
    }
    impl->processor = FUnknownPtr<IAudioProcessor>(impl->component);
    if (!impl->processor) {
        set_error(error, "VST3 component has no audio processor interface");
        impl->component->terminate();
        return false;
    }

    if (impl->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput) != 1 ||
        impl->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput) != 1) {
        set_error(error, "only one main audio input and output bus are supported");
        impl->component->terminate();
        return false;
    }
    Steinberg::Vst::BusInfo input_info{};
    Steinberg::Vst::BusInfo output_info{};
    if (!result_ok(impl->component->getBusInfo(Steinberg::Vst::kAudio,
                                                Steinberg::Vst::kInput, 0, input_info)) ||
        !result_ok(impl->component->getBusInfo(Steinberg::Vst::kAudio,
                                                Steinberg::Vst::kOutput, 0, output_info)) ||
        input_info.channelCount <= 0 || output_info.channelCount <= 0) {
        set_error(error, "VST3 main bus channel count does not match the requested layout");
        impl->component->terminate();
        return false;
    }
    if (!result_ok(impl->component->activateBus(Steinberg::Vst::kAudio,
                                                 Steinberg::Vst::kInput, 0, true)) ||
        !result_ok(impl->component->activateBus(Steinberg::Vst::kAudio,
                                                 Steinberg::Vst::kOutput, 0, true))) {
        set_error(error, "VST3 main audio bus activation failed");
        impl->component->terminate();
        return false;
    }

    SpeakerArrangement input_arrangement = arrangement;
    SpeakerArrangement output_arrangement = arrangement;
    if (!result_ok(impl->processor->setBusArrangements(&input_arrangement, 1,
                                                        &output_arrangement, 1))) {
        set_error(error, "VST3 speaker arrangement rejected");
        impl->component->terminate();
        return false;
    }
    SpeakerArrangement actual_input = 0;
    SpeakerArrangement actual_output = 0;
    if (!result_ok(impl->processor->getBusArrangement(Steinberg::Vst::kInput, 0,
                                                       actual_input)) ||
        !result_ok(impl->processor->getBusArrangement(Steinberg::Vst::kOutput, 0,
                                                       actual_output)) ||
        actual_input != arrangement || actual_output != arrangement) {
        set_error(error, "VST3 plugin did not retain the requested speaker arrangement");
        impl->component->terminate();
        return false;
    }
    impl->setup.processMode = Steinberg::Vst::kRealtime;
    impl->setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    impl->setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(config.max_frames);
    impl->setup.sampleRate = config.sample_rate;
    if (!result_ok(impl->processor->setupProcessing(impl->setup)) ||
        !result_ok(impl->component->setActive(true)) ||
        !result_ok(impl->processor->setProcessing(true))) {
        set_error(error, "VST3 processing setup failed");
        impl->component->setActive(false);
        impl->component->terminate();
        return false;
    }

    impl->input_bus.numChannels = static_cast<Steinberg::int32>(channels);
    impl->output_bus.numChannels = static_cast<Steinberg::int32>(channels);
    impl->input_bus.channelBuffers32 = input_ptrs_.data();
    impl->output_bus.channelBuffers32 = output_ptrs_.data();
    impl->process_data.processMode = Steinberg::Vst::kRealtime;
    impl->process_data.symbolicSampleSize = Steinberg::Vst::kSample32;
    impl->process_data.numInputs = 1;
    impl->process_data.numOutputs = 1;
    impl->process_data.inputs = &impl->input_bus;
    impl->process_data.outputs = &impl->output_bus;
    impl->process_data.inputParameterChanges = nullptr;
    impl->process_data.outputParameterChanges = nullptr;
    impl->process_data.inputEvents = nullptr;
    impl->process_data.outputEvents = nullptr;
    impl->process_data.processContext = nullptr;

    impl_ = impl.release();
    channels_ = channels;
    max_frames_ = config.max_frames;
    latency_samples_ = impl_->processor->getLatencySamples();
    processing_ = true;
    return true;
}

void Vst3SdkProcessorV1::close() noexcept
{
    if (impl_ != nullptr) {
        if (processing_) {
            (void)impl_->processor->setProcessing(false);
            (void)impl_->component->setActive(false);
            processing_ = false;
        }
        (void)impl_->component->terminate();
        delete impl_;
        impl_ = nullptr;
    }
    channels_ = 0;
    max_frames_ = 0;
    latency_samples_ = 0;
    input_ptrs_.fill(nullptr);
    output_ptrs_.fill(nullptr);
}

Vst3SdkProcessResultV1 Vst3SdkProcessorV1::process(const float* input,
                                                   float* output,
                                                   const std::uint32_t frames,
                                                   const std::span<const Vst3SdkParameterPointV1> parameters) noexcept
{
    if (!processing_ || impl_ == nullptr) {
        return Vst3SdkProcessResultV1::not_open;
    }
    if (input == nullptr || output == nullptr) {
        return Vst3SdkProcessResultV1::invalid_buffer;
    }
    if (frames == 0 || frames > max_frames_ || frames > kMaxFrames) {
        return Vst3SdkProcessResultV1::unsupported_block;
    }
    if (!validate_vst3_sdk_parameter_points_v1(parameters, frames)) {
        return Vst3SdkProcessResultV1::invalid_parameter;
    }

    for (std::uint32_t channel = 0; channel < channels_; ++channel) {
        input_ptrs_[channel] = input_planar_[channel].data();
        output_ptrs_[channel] = output_planar_[channel].data();
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const float sample = input[frame * channels_ + channel];
            if (!std::isfinite(sample)) {
                std::fill(output, output + frames * channels_, 0.0F);
                return Vst3SdkProcessResultV1::invalid_buffer;
            }
            input_planar_[channel][frame] = sample;
            output_planar_[channel][frame] = 0.0F;
        }
    }
    impl_->process_data.numSamples = static_cast<Steinberg::int32>(frames);
    impl_->parameter_changes.clearQueue();
    for (const auto& point : parameters) {
        Steinberg::int32 queue_index = 0;
        auto* queue = impl_->parameter_changes.addParameterData(
            static_cast<Steinberg::Vst::ParamID>(point.parameter_id), queue_index);
        if (queue == nullptr) {
            std::fill(output, output + frames * channels_, 0.0F);
            return Vst3SdkProcessResultV1::invalid_parameter;
        }
        Steinberg::int32 point_index = 0;
        if (queue->addPoint(point.sample_offset, point.normalized_value, point_index) !=
            Steinberg::kResultOk) {
            std::fill(output, output + frames * channels_, 0.0F);
            return Vst3SdkProcessResultV1::invalid_parameter;
        }
    }
    impl_->process_data.inputParameterChanges = parameters.empty() ? nullptr : &impl_->parameter_changes;
    const auto result = impl_->processor->process(impl_->process_data);
    if (!result_ok(result)) {
        std::fill(output, output + frames * channels_, 0.0F);
        return Vst3SdkProcessResultV1::plugin_error;
    }
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels_; ++channel) {
            const float sample = output_planar_[channel][frame];
            if (!std::isfinite(sample)) {
                std::fill(output, output + frames * channels_, 0.0F);
                return Vst3SdkProcessResultV1::non_finite_output;
            }
            output[frame * channels_ + channel] = sample;
        }
    }
    return Vst3SdkProcessResultV1::ok;
}

Vst3SdkStateResultV1 Vst3SdkProcessorV1::save_state(
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written) {
    bytes_written = 0U;
    if (!processing_ || impl_ == nullptr) return Vst3SdkStateResultV1::not_open;
    try {
        BoundedStateStream stream(kMaxPluginStateBytesV1);
        const auto result = impl_->component->getState(&stream);
        if (!result_ok(result)) {
            return stream.overflowed() ? Vst3SdkStateResultV1::state_too_large
                                       : Vst3SdkStateResultV1::plugin_error;
        }
        if (stream.overflowed()) return Vst3SdkStateResultV1::state_too_large;
        if (destination.size() < stream.size()) return Vst3SdkStateResultV1::invalid_buffer;
        if (stream.size() != 0U) std::memcpy(destination.data(), stream.data(), stream.size());
        bytes_written = stream.size();
        return Vst3SdkStateResultV1::ok;
    } catch (const std::bad_alloc&) {
        return Vst3SdkStateResultV1::allocation_failed;
    }
}

Vst3SdkStateResultV1 Vst3SdkProcessorV1::load_state(
    const std::span<const std::uint8_t> state_bytes) {
    if (!processing_ || impl_ == nullptr) return Vst3SdkStateResultV1::not_open;
    if (state_bytes.size() > kMaxPluginStateBytesV1) return Vst3SdkStateResultV1::state_too_large;
    try {
        BoundedStateStream stream(kMaxPluginStateBytesV1);
        if (!state_bytes.empty()) {
            if (stream.write(const_cast<std::uint8_t*>(state_bytes.data()),
                             static_cast<Steinberg::int32>(state_bytes.size()), nullptr) !=
                Steinberg::kResultTrue ||
                stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) != Steinberg::kResultTrue) {
                return Vst3SdkStateResultV1::invalid_buffer;
            }
        }
        return result_ok(impl_->component->setState(&stream))
                   ? Vst3SdkStateResultV1::ok
                   : Vst3SdkStateResultV1::plugin_error;
    } catch (const std::bad_alloc&) {
        return Vst3SdkStateResultV1::allocation_failed;
    }
}

} // namespace hibiki
