#include "hibiki/vst3_sdk_processor.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

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
};

Vst3SdkProcessorV1::~Vst3SdkProcessorV1()
{
    close();
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
                                                   std::uint32_t frames) noexcept
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

} // namespace hibiki
