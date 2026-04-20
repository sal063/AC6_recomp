/**
 ******************************************************************************
 * ReXGlue native WASAPI audio driver                                          *
 ******************************************************************************
 */

#include <native/audio/wasapi/wasapi_audio_driver.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include <native/audio/audio_runtime.h>
#include <native/audio/conversion.h>
#include <native/audio/flags.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define COBJMACROS
#include <Windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

REXCVAR_DECLARE(bool, audio_trace_render_driver_verbose);
REXCVAR_DEFINE_INT32(audio_wasapi_buffer_frames, 480, "Audio",
                     "Requested WASAPI shared-mode period in frames").range(64, 2048);

namespace rex::audio::wasapi {

namespace {

template <typename T>
void SafeRelease(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

uint32_t ClampRequestedFrames() {
  return static_cast<uint32_t>(std::clamp(REXCVAR_GET(audio_wasapi_buffer_frames), 64, 2048));
}

uint32_t RequiredQueueFramesForDevice(const uint32_t device_buffer_frames) {
  const uint32_t buffer_frames = std::max(device_buffer_frames, 1u);
  return std::max(3u, (buffer_frames + kRenderDriverTicSamplesPerFrame - 1) /
                           kRenderDriverTicSamplesPerFrame);
}

REFERENCE_TIME FramesToHundredsOfNanoseconds(const uint32_t frame_count,
                                             const uint32_t sample_rate) {
  return static_cast<REFERENCE_TIME>((10000000ull * frame_count) / sample_rate);
}

WAVEFORMATEXTENSIBLE BuildStereoFloatFormat() {
  WAVEFORMATEXTENSIBLE format = {};
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = 2;
  format.Format.nSamplesPerSec = kAudioFrameSampleRate;
  format.Format.wBitsPerSample = sizeof(float) * 8;
  format.Format.nBlockAlign =
      static_cast<WORD>(format.Format.nChannels * (format.Format.wBitsPerSample / 8));
  format.Format.nAvgBytesPerSec =
      format.Format.nSamplesPerSec * static_cast<uint32_t>(format.Format.nBlockAlign);
  format.Format.cbSize =
      static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
  format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
  format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  return format;
}

}  // namespace

WasapiAudioDriver::WasapiAudioDriver(memory::Memory* memory, AudioRuntime* runtime,
                                     const size_t client_index)
    : AudioDriver(memory), runtime_(runtime), client_index_(client_index) {}

WasapiAudioDriver::~WasapiAudioDriver() {
  Shutdown();
}

bool WasapiAudioDriver::Initialize() {
  shutting_down_.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(init_mutex_);
    init_done_ = false;
    init_success_ = false;
    init_error_.clear();
  }

  render_thread_ = std::thread([this]() { RenderThreadMain(); });

  std::unique_lock<std::mutex> lock(init_mutex_);
  init_cv_.wait(lock, [this]() { return init_done_; });
  const bool success = init_success_;
  lock.unlock();

  if (!success && render_thread_.joinable()) {
    render_thread_.join();
  }

  if (!success) {
    REXAPU_ERROR("WasapiAudioDriver initialization failed for client {}: {}", client_index_,
                 init_error_);
  }
  return success;
}

void WasapiAudioDriver::Shutdown() {
  const bool was_shutting_down = shutting_down_.exchange(true, std::memory_order_acq_rel);
  if (was_shutting_down) {
    if (render_thread_.joinable()) {
      render_thread_.join();
    }
    return;
  }

  if (render_event_) {
    SetEvent(render_event_);
  }

  if (render_thread_.joinable()) {
    render_thread_.join();
  }

  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (!frames_unused_.empty()) {
    delete[] frames_unused_.top();
    frames_unused_.pop();
  }
  while (!frames_queued_.empty()) {
    delete[] frames_queued_.front();
    frames_queued_.pop();
  }
  pending_output_float_count_ = 0;
  pending_output_float_offset_ = 0;
}

void WasapiAudioDriver::SubmitFrame(const uint32_t frame_ptr) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  if (!input_frame) {
    return;
  }

  float* output_frame = nullptr;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[kAudioFrameTotalSamples];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::memcpy(output_frame, input_frame, sizeof(float) * kAudioFrameTotalSamples);
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }

  const uint32_t submitted = submitted_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t queued_depth = queued_depth_.fetch_add(1, std::memory_order_relaxed) + 1;
  uint32_t previous_peak = peak_queued_depth_.load(std::memory_order_relaxed);
  while (queued_depth > previous_peak &&
         !peak_queued_depth_.compare_exchange_weak(previous_peak, queued_depth,
                                                   std::memory_order_relaxed)) {
  }

  if (REXCVAR_GET(audio_trace_render_driver_verbose) &&
      (submitted <= 24 || (submitted % 60) == 0 || queued_depth <= 1)) {
    REXAPU_DEBUG(
        "WasapiAudioDriver::SubmitFrame frame_ptr={:08X} submitted={} consumed={} queued_depth={} peak={} underruns={}",
        frame_ptr, submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed));
  }
}

void WasapiAudioDriver::SubmitSilenceFrame() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  float* output_frame = nullptr;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[kAudioFrameTotalSamples];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::fill_n(output_frame, kAudioFrameTotalSamples, 0.0f);
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
  }

  const uint32_t submitted = submitted_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t queued_depth = queued_depth_.fetch_add(1, std::memory_order_relaxed) + 1;
  silence_injections_.fetch_add(1, std::memory_order_relaxed);
  uint32_t previous_peak = peak_queued_depth_.load(std::memory_order_relaxed);
  while (queued_depth > previous_peak &&
         !peak_queued_depth_.compare_exchange_weak(previous_peak, queued_depth,
                                                   std::memory_order_relaxed)) {
  }

  if (REXCVAR_GET(audio_trace_render_driver_verbose) &&
      (submitted <= 24 || (submitted % 60) == 0 || queued_depth <= 1)) {
    REXAPU_DEBUG(
        "WasapiAudioDriver::SubmitSilenceFrame submitted={} consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
        submitted, consumed_frames_.load(std::memory_order_relaxed), queued_depth,
        peak_queued_depth_.load(std::memory_order_relaxed),
        underrun_count_.load(std::memory_order_relaxed),
        silence_injections_.load(std::memory_order_relaxed));
  }
}

AudioDriverTelemetry WasapiAudioDriver::GetTelemetry() const {
  return AudioDriverTelemetry{
      submitted_frames_.load(std::memory_order_relaxed),
      consumed_frames_.load(std::memory_order_relaxed),
      underrun_count_.load(std::memory_order_relaxed),
      silence_injections_.load(std::memory_order_relaxed),
      queued_depth_.load(std::memory_order_relaxed),
      peak_queued_depth_.load(std::memory_order_relaxed),
  };
}

uint32_t WasapiAudioDriver::queue_low_water_frames() const {
  return std::max(1u, queue_target_frames() - 1);
}

uint32_t WasapiAudioDriver::queue_target_frames() const {
  return RequiredQueueFramesForDevice(device_buffer_frames_);
}

void WasapiAudioDriver::RenderThreadMain() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool co_initialized = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    SignalInitResult(false, "CoInitializeEx failed");
    return;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioClient* audio_client = nullptr;
#ifdef __IAudioClient2_INTERFACE_DEFINED__
  IAudioClient2* audio_client2 = nullptr;
#endif
#ifdef __IAudioClient3_INTERFACE_DEFINED__
  IAudioClient3* audio_client3 = nullptr;
#endif
  IAudioRenderClient* render_client = nullptr;

  do {
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
      SignalInitResult(false, "CoCreateInstance(MMDeviceEnumerator) failed");
      break;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
      SignalInitResult(false, "GetDefaultAudioEndpoint failed");
      break;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
                          reinterpret_cast<void**>(&audio_client));
    if (FAILED(hr)) {
      SignalInitResult(false, "IMMDevice::Activate(IAudioClient) failed");
      break;
    }

#ifdef __IAudioClient2_INTERFACE_DEFINED__
    hr = audio_client->QueryInterface(__uuidof(IAudioClient2),
                                      reinterpret_cast<void**>(&audio_client2));
    if (SUCCEEDED(hr) && audio_client2) {
      AudioClientProperties properties = {};
      properties.cbSize = sizeof(properties);
      properties.eCategory = AudioCategory_GameMedia;
      properties.Options = AUDCLNT_STREAMOPTIONS_NONE;
      audio_client2->SetClientProperties(&properties);
    }
#endif

    const WAVEFORMATEXTENSIBLE requested_format = BuildStereoFloatFormat();
    WAVEFORMATEX* closest_match = nullptr;
    hr = audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                         reinterpret_cast<const WAVEFORMATEX*>(&requested_format),
                                         &closest_match);
    if (closest_match) {
      CoTaskMemFree(closest_match);
      closest_match = nullptr;
    }

    const DWORD base_stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    const uint32_t requested_frames = ClampRequestedFrames();
    uint32_t initialized_frames = requested_frames;
    bool initialized = false;

#ifdef __IAudioClient3_INTERFACE_DEFINED__
    hr = audio_client->QueryInterface(__uuidof(IAudioClient3),
                                      reinterpret_cast<void**>(&audio_client3));
    if (SUCCEEDED(hr) && audio_client3) {
      UINT32 default_period = 0;
      UINT32 fundamental_period = 0;
      UINT32 min_period = 0;
      UINT32 max_period = 0;
      hr = audio_client3->GetSharedModeEnginePeriod(
          reinterpret_cast<const WAVEFORMATEX*>(&requested_format), &default_period,
          &fundamental_period, &min_period, &max_period);
      if (SUCCEEDED(hr) && fundamental_period != 0) {
        initialized_frames =
            std::clamp(((requested_frames + fundamental_period - 1) / fundamental_period) *
                           fundamental_period,
                       min_period, max_period);
        hr = audio_client3->InitializeSharedAudioStream(
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, initialized_frames,
            reinterpret_cast<const WAVEFORMATEX*>(&requested_format), nullptr);
        initialized = SUCCEEDED(hr);
      }
    }
#endif

    if (!initialized) {
      initialized_frames = requested_frames;
      hr = audio_client->Initialize(
          AUDCLNT_SHAREMODE_SHARED, base_stream_flags,
          FramesToHundredsOfNanoseconds(initialized_frames, kAudioFrameSampleRate), 0,
          reinterpret_cast<const WAVEFORMATEX*>(&requested_format), nullptr);
      if (FAILED(hr)) {
        SignalInitResult(false, "IAudioClient initialization failed");
        break;
      }
    }

    render_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!render_event_) {
      SignalInitResult(false, "CreateEventW failed");
      break;
    }

    hr = audio_client->SetEventHandle(render_event_);
    if (FAILED(hr)) {
      SignalInitResult(false, "IAudioClient::SetEventHandle failed");
      break;
    }

    hr = audio_client->GetService(__uuidof(IAudioRenderClient),
                                  reinterpret_cast<void**>(&render_client));
    if (FAILED(hr)) {
      SignalInitResult(false, "IAudioClient::GetService(IAudioRenderClient) failed");
      break;
    }

    UINT32 buffer_frame_count = 0;
    hr = audio_client->GetBufferSize(&buffer_frame_count);
    if (FAILED(hr)) {
      SignalInitResult(false, "IAudioClient::GetBufferSize failed");
      break;
    }
    device_buffer_frames_ = buffer_frame_count;

    BYTE* initial_buffer = nullptr;
    hr = render_client->GetBuffer(buffer_frame_count, &initial_buffer);
    if (FAILED(hr)) {
      SignalInitResult(false, "IAudioRenderClient::GetBuffer initial fill failed");
      break;
    }
    std::memset(initial_buffer, 0, sizeof(float) * 2 * buffer_frame_count);
    hr = render_client->ReleaseBuffer(buffer_frame_count, 0);
    if (FAILED(hr)) {
      SignalInitResult(false, "IAudioRenderClient::ReleaseBuffer initial fill failed");
      break;
    }

    hr = audio_client->Start();
    if (FAILED(hr)) {
      SignalInitResult(false, "IAudioClient::Start failed");
      break;
    }

    REXAPU_INFO(
        "WasapiAudioDriver initialized: client={} channels=2 freq={} requested_frames={} initialized_frames={} buffer_frames={}",
        client_index_, kAudioFrameSampleRate, requested_frames, initialized_frames,
        buffer_frame_count);
    SignalInitResult(true);

    while (!shutting_down_.load(std::memory_order_acquire)) {
      const DWORD wait_result = WaitForSingleObject(render_event_, 10);
      if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_TIMEOUT) {
        break;
      }

      UINT32 padding_frames = 0;
      hr = audio_client->GetCurrentPadding(&padding_frames);
      if (FAILED(hr) || padding_frames > buffer_frame_count) {
        continue;
      }

      UINT32 available_frames = buffer_frame_count - padding_frames;
      while (available_frames > 0 && !shutting_down_.load(std::memory_order_acquire)) {
        if (pending_output_float_offset_ == pending_output_float_count_) {
          pending_output_float_count_ = 0;
          pending_output_float_offset_ = 0;

          float* buffer = nullptr;
          {
            std::unique_lock<std::mutex> guard(frames_mutex_);
            if (!frames_queued_.empty()) {
              buffer = frames_queued_.front();
              frames_queued_.pop();
              queued_depth_.fetch_sub(1, std::memory_order_relaxed);
            }
          }

          if (buffer) {
            if (!REXCVAR_GET(audio_mute)) {
              conversion::render_driver_6_BE_to_interleaved_2_LE(
                  pending_output_frame_.data(), buffer, kRenderDriverTicSamplesPerFrame);
            } else {
              std::memset(pending_output_frame_.data(), 0, sizeof(float) * pending_output_frame_.size());
            }
            pending_output_float_count_ = pending_output_frame_.size();
            {
              std::unique_lock<std::mutex> guard(frames_mutex_);
              frames_unused_.push(buffer);
            }
          }
        }

        const UINT32 pending_frames =
            static_cast<UINT32>((pending_output_float_count_ - pending_output_float_offset_) / 2);
        const UINT32 frames_to_write =
            pending_frames != 0 ? std::min(available_frames, pending_frames) : available_frames;

        BYTE* target = nullptr;
        hr = render_client->GetBuffer(frames_to_write, &target);
        if (FAILED(hr)) {
          break;
        }

        if (pending_frames == 0) {
          ++underrun_count_;
          ++silence_injections_;
          std::memset(target, 0, sizeof(float) * 2 * frames_to_write);
          if (runtime_) {
            runtime_->ReportSamplesConsumedForClient(client_index_, frames_to_write);
            runtime_->WakeWorker();
          }
        } else {
          const size_t float_count = static_cast<size_t>(frames_to_write) * 2;
          std::memcpy(target, pending_output_frame_.data() + pending_output_float_offset_,
                      sizeof(float) * float_count);
          pending_output_float_offset_ += float_count;
          if (runtime_) {
            runtime_->ReportSamplesConsumedForClient(client_index_, frames_to_write);
          }
          if (pending_output_float_offset_ == pending_output_float_count_) {
            pending_output_float_count_ = 0;
            pending_output_float_offset_ = 0;
            consumed_frames_.fetch_add(1, std::memory_order_relaxed);
            if (runtime_) {
              runtime_->ConsumeQueuedFramesForClient(client_index_, 1);
              runtime_->WakeWorker();
            }
          }
        }

        hr = render_client->ReleaseBuffer(frames_to_write, 0);
        if (FAILED(hr)) {
          break;
        }
        available_frames -= frames_to_write;
      }
    }

    audio_client->Stop();
  } while (false);

  if (render_event_) {
    CloseHandle(render_event_);
    render_event_ = nullptr;
  }
  SafeRelease(render_client);
#ifdef __IAudioClient3_INTERFACE_DEFINED__
  SafeRelease(audio_client3);
#endif
#ifdef __IAudioClient2_INTERFACE_DEFINED__
  SafeRelease(audio_client2);
#endif
  SafeRelease(audio_client);
  SafeRelease(device);
  SafeRelease(enumerator);
  if (co_initialized) {
    CoUninitialize();
  }
}

void WasapiAudioDriver::SignalInitResult(const bool success, std::string error_message) {
  std::lock_guard<std::mutex> lock(init_mutex_);
  init_success_ = success;
  init_done_ = true;
  init_error_ = std::move(error_message);
  init_cv_.notify_all();
}

}  // namespace rex::audio::wasapi
