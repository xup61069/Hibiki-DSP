// SPDX-License-Identifier: GPL-3.0-only
//
// Optional source-only ASIO 2.x COM transport. This target deliberately has
// no physical-device code: the Hibiki engine/IPC bridge is the next boundary.

#if !defined(_WIN32) || !defined(HIBIKI_ENABLE_NATIVE_ASIO)
#error "This file is only compiled by the optional Windows ASIO target"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

#include <iasiodrv.h>

namespace {

constexpr CLSID kHibikiAsioClsid = {
    0x2c0e8d4f, 0x7d9f, 0x4aa7, {0xa0, 0x3d, 0x69, 0x0d, 0xcf, 0x5d, 0x5f, 0x8c}};
constexpr long kOutputChannels = 8;
constexpr long kPreferredBuffer = 128;
constexpr long kMinBuffer = 32;
constexpr long kMaxBuffer = 4096;
constexpr std::array<double, 4> kSampleRates{44100.0, 48000.0, 96000.0, 192000.0};

std::atomic<long> g_object_count{0};
HINSTANCE g_module_instance = nullptr;

bool supported_rate(ASIOSampleRate rate) noexcept {
  return std::any_of(kSampleRates.begin(), kSampleRates.end(), [rate](double candidate) {
    return std::abs(rate - candidate) < 0.5;
  });
}

void set_u64(ASIOSamples& value, std::uint64_t number) noexcept {
  value.hi = static_cast<unsigned long>(number >> 32U);
  value.lo = static_cast<unsigned long>(number & 0xffffffffULL);
}

void set_timestamp(ASIOTimeStamp& value, std::uint64_t number) noexcept {
  value.hi = static_cast<unsigned long>(number >> 32U);
  value.lo = static_cast<unsigned long>(number & 0xffffffffULL);
}

class HibikiAsioDriver final : public IASIO {
public:
  HibikiAsioDriver() { g_object_count.fetch_add(1, std::memory_order_relaxed); }

  ~HibikiAsioDriver() {
    disposeBuffers();
    g_object_count.fetch_sub(1, std::memory_order_relaxed);
  }

  HibikiAsioDriver(const HibikiAsioDriver&) = delete;
  HibikiAsioDriver& operator=(const HibikiAsioDriver&) = delete;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    // ASIO hosts pass the driver's class ID as the requested interface ID
    // (see the open-source ASIO host helper), rather than a public IASIO IID.
    if (iid == IID_IUnknown || iid == kHibikiAsioClsid) {
      *object = static_cast<IASIO*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
  }

  ASIOBool init(void* /*sysHandle*/) override { return ASIOTrue; }

  void getDriverName(char* name) override {
    if (name != nullptr) std::strcpy(name, "Hibiki DSP");
  }

  long getDriverVersion() override { return 1000L; }

  void getErrorMessage(char* string) override {
    if (string != nullptr) std::strcpy(string, last_error_);
  }

  ASIOError start() override {
    std::scoped_lock lock(mutex_);
    if (!buffers_ready_ || (callbacks_.bufferSwitch == nullptr && callbacks_.bufferSwitchTimeInfo == nullptr)) {
      return ASE_InvalidMode;
    }
    if (running_) return ASE_OK;
    running_ = true;
    worker_ = std::thread([this] { run_callbacks(); });
    return ASE_OK;
  }

  ASIOError stop() override {
    {
      std::scoped_lock lock(mutex_);
      if (!running_) return ASE_OK;
      running_ = false;
    }
    if (worker_.joinable()) worker_.join();
    return ASE_OK;
  }

  ASIOError getChannels(long* inputs, long* outputs) override {
    if (inputs == nullptr || outputs == nullptr) return ASE_InvalidParameter;
    *inputs = 0;
    *outputs = kOutputChannels;
    return ASE_OK;
  }

  ASIOError getLatencies(long* input, long* output) override {
    if (input == nullptr || output == nullptr) return ASE_InvalidParameter;
    *input = 0;
    *output = buffer_size_;
    return ASE_OK;
  }

  ASIOError getBufferSize(long* minimum, long* maximum, long* preferred, long* granularity) override {
    if (minimum == nullptr || maximum == nullptr || preferred == nullptr || granularity == nullptr) {
      return ASE_InvalidParameter;
    }
    *minimum = kMinBuffer;
    *maximum = kMaxBuffer;
    *preferred = kPreferredBuffer;
    *granularity = 1;
    return ASE_OK;
  }

  ASIOError canSampleRate(ASIOSampleRate rate) override {
    return supported_rate(rate) ? ASE_OK : ASE_NoClock;
  }

  ASIOError getSampleRate(ASIOSampleRate* rate) override {
    if (rate == nullptr) return ASE_InvalidParameter;
    *rate = sample_rate_;
    return ASE_OK;
  }

  ASIOError setSampleRate(ASIOSampleRate rate) override {
    if (!supported_rate(rate)) return ASE_NoClock;
    ASIOCallbacks callbacks_copy{};
    {
      std::scoped_lock lock(mutex_);
      if (running_) return ASE_InvalidMode;
      sample_rate_ = rate;
      callbacks_copy = callbacks_;
    }
    if (callbacks_copy.sampleRateDidChange != nullptr) callbacks_copy.sampleRateDidChange(rate);
    return ASE_OK;
  }

  ASIOError getClockSources(ASIOClockSource* clocks, long* count) override {
    if (count == nullptr) return ASE_InvalidParameter;
    if (clocks == nullptr || *count < 1) {
      *count = 1;
      return ASE_OK;
    }
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    std::strcpy(clocks[0].name, "Hibiki virtual clock");
    *count = 1;
    return ASE_OK;
  }

  ASIOError setClockSource(long reference) override { return reference == 0 ? ASE_OK : ASE_InvalidParameter; }

  ASIOError getSamplePosition(ASIOSamples* position, ASIOTimeStamp* timestamp) override {
    if (position == nullptr || timestamp == nullptr) return ASE_InvalidParameter;
    const auto samples = sample_position_.load(std::memory_order_relaxed);
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    set_u64(*position, samples);
    set_timestamp(*timestamp, static_cast<std::uint64_t>(now));
    return ASE_OK;
  }

  ASIOError getChannelInfo(ASIOChannelInfo* info) override {
    if (info == nullptr || info->isInput != ASIOFalse || info->channel < 0 || info->channel >= kOutputChannels) {
      return ASE_InvalidParameter;
    }
    info->isActive = ASIOTrue;
    info->channelGroup = info->channel / 2;
    info->type = ASIOSTFloat32LSB;
    std::snprintf(info->name, sizeof(info->name), "Hibiki Out %ld", info->channel + 1);
    return ASE_OK;
  }

  ASIOError createBuffers(ASIOBufferInfo* infos, long count, long buffer_size, ASIOCallbacks* callbacks) override {
    if (infos == nullptr || callbacks == nullptr || count != kOutputChannels ||
        buffer_size < kMinBuffer || buffer_size > kMaxBuffer) {
      return ASE_InvalidParameter;
    }
    for (long i = 0; i < count; ++i) {
      if (infos[i].isInput != ASIOFalse || infos[i].channelNum != i) return ASE_InvalidMode;
    }
    disposeBuffers();
    try {
      std::array<std::unique_ptr<float[]>, kOutputChannels> first{};
      std::array<std::unique_ptr<float[]>, kOutputChannels> second{};
      for (long channel = 0; channel < kOutputChannels; ++channel) {
        first[static_cast<std::size_t>(channel)] = std::make_unique<float[]>(buffer_size);
        second[static_cast<std::size_t>(channel)] = std::make_unique<float[]>(buffer_size);
        infos[channel].buffers[0] = first[static_cast<std::size_t>(channel)].get();
        infos[channel].buffers[1] = second[static_cast<std::size_t>(channel)].get();
      }
      std::scoped_lock lock(mutex_);
      for (long channel = 0; channel < kOutputChannels; ++channel) {
        buffers_[static_cast<std::size_t>(channel)][0] = std::move(first[static_cast<std::size_t>(channel)]);
        buffers_[static_cast<std::size_t>(channel)][1] = std::move(second[static_cast<std::size_t>(channel)]);
      }
      buffer_size_ = buffer_size;
      callbacks_ = *callbacks;
      buffers_ready_ = true;
    } catch (...) {
      return ASE_NoMemory;
    }
    return ASE_OK;
  }

  ASIOError disposeBuffers() override {
    stop();
    std::scoped_lock lock(mutex_);
    for (auto& channel : buffers_) {
      channel[0].reset();
      channel[1].reset();
    }
    buffers_ready_ = false;
    return ASE_OK;
  }

  ASIOError controlPanel() override { return ASE_NotPresent; }

  ASIOError future(long selector, void* /*opt*/) override {
    switch (selector) {
      case kAsioCanTimeInfo:
      case kAsioCanTimeCode:
        return ASE_SUCCESS;
      default:
        return ASE_NotPresent;
    }
  }

  ASIOError outputReady() override { return ASE_OK; }

private:
  using BufferPair = std::array<std::unique_ptr<float[]>, 2>;

  void run_callbacks() noexcept {
    long buffer_index = 0;
    const auto block_duration = std::chrono::duration<double>(
        static_cast<double>(buffer_size_) / sample_rate_);
    while (running_.load(std::memory_order_acquire)) {
      ASIOCallbacks callbacks_copy{};
      {
        std::scoped_lock lock(mutex_);
        callbacks_copy = callbacks_;
      }
      if (callbacks_copy.bufferSwitchTimeInfo != nullptr) {
        ASIOTime time{};
        time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
        time.timeInfo.speed = 1.0;
        time.timeInfo.sampleRate = sample_rate_;
        set_u64(time.timeInfo.samplePosition, sample_position_.load(std::memory_order_relaxed));
        set_timestamp(time.timeInfo.systemTime, static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()));
        callbacks_copy.bufferSwitchTimeInfo(&time, buffer_index, ASIOFalse);
      } else if (callbacks_copy.bufferSwitch != nullptr) {
        callbacks_copy.bufferSwitch(buffer_index, ASIOFalse);
      }
      sample_position_.fetch_add(static_cast<std::uint64_t>(buffer_size_), std::memory_order_relaxed);
      buffer_index = 1 - buffer_index;
      std::this_thread::sleep_for(block_duration);
    }
  }

  std::atomic<ULONG> references_{1};
  std::mutex mutex_;
  ASIOCallbacks callbacks_{};
  std::array<BufferPair, kOutputChannels> buffers_{};
  std::atomic<bool> running_{false};
  bool buffers_ready_{false};
  long buffer_size_{kPreferredBuffer};
  ASIOSampleRate sample_rate_{48000.0};
  std::atomic<std::uint64_t> sample_position_{0};
  char last_error_[64]{""};
  std::thread worker_;
};

class HibikiAsioClassFactory final : public IClassFactory {
public:
  HibikiAsioClassFactory() { g_object_count.fetch_add(1, std::memory_order_relaxed); }
  ~HibikiAsioClassFactory() { g_object_count.fetch_sub(1, std::memory_order_relaxed); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IClassFactory) {
      *object = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
    if (outer != nullptr) return CLASS_E_NOAGGREGATION;
    auto* driver = new (std::nothrow) HibikiAsioDriver();
    if (driver == nullptr) return E_OUTOFMEMORY;
    const HRESULT result = driver->QueryInterface(iid, object);
    driver->Release();
    return result;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
    if (lock != FALSE) g_object_count.fetch_add(1, std::memory_order_relaxed);
    else g_object_count.fetch_sub(1, std::memory_order_relaxed);
    return S_OK;
  }

private:
  std::atomic<ULONG> references_{1};
};

HRESULT register_driver() noexcept {
  wchar_t module_path[MAX_PATH]{};
  if (g_module_instance == nullptr || GetModuleFileNameW(g_module_instance, module_path, MAX_PATH) == 0) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  const auto clsid = L"{2C0E8D4F-7D9F-4AA7-A03D-690DCF5D5F8C}";
  const auto set_value = [](HKEY key, const wchar_t* name, const wchar_t* value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                          static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t)));
  };
  HKEY clsid_key = nullptr;
  auto result = RegCreateKeyExW(HKEY_CLASSES_ROOT,
                                L"CLSID\\{2C0E8D4F-7D9F-4AA7-A03D-690DCF5D5F8C}\\InprocServer32", 0,
                                nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &clsid_key, nullptr);
  if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
  const auto path_result = set_value(clsid_key, nullptr, module_path);
  const auto threading_result = set_value(clsid_key, L"ThreadingModel", L"Both");
  RegCloseKey(clsid_key);
  if (path_result != ERROR_SUCCESS || threading_result != ERROR_SUCCESS) {
    return E_ACCESSDENIED;
  }

  HKEY asio_key = nullptr;
  result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Hibiki DSP", 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &asio_key, nullptr);
  if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
  const auto clsid_result = set_value(asio_key, L"clsid", clsid);
  const auto description_result = set_value(asio_key, L"description", L"Hibiki DSP ASIO");
  RegCloseKey(asio_key);
  return (clsid_result == ERROR_SUCCESS && description_result == ERROR_SUCCESS) ? S_OK : E_ACCESSDENIED;
}

}  // namespace

STDAPI DllCanUnloadNow() {
  return g_object_count.load(std::memory_order_acquire) == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** object) {
  if (object == nullptr) return E_POINTER;
  *object = nullptr;
  if (clsid != kHibikiAsioClsid) return CLASS_E_CLASSNOTAVAILABLE;
  auto* factory = new (std::nothrow) HibikiAsioClassFactory();
  if (factory == nullptr) return E_OUTOFMEMORY;
  const HRESULT result = factory->QueryInterface(iid, object);
  factory->Release();
  return result;
}

STDAPI DllRegisterServer() { return register_driver(); }

STDAPI DllUnregisterServer() {
  const auto asio_result = RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Hibiki DSP");
  const auto clsid_result = RegDeleteTreeW(HKEY_CLASSES_ROOT,
                                           L"CLSID\\{2C0E8D4F-7D9F-4AA7-A03D-690DCF5D5F8C}");
  const bool asio_ok = asio_result == ERROR_SUCCESS || asio_result == ERROR_FILE_NOT_FOUND;
  const bool clsid_ok = clsid_result == ERROR_SUCCESS || clsid_result == ERROR_FILE_NOT_FOUND;
  return asio_ok && clsid_ok ? S_OK : E_ACCESSDENIED;
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module_instance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}
