/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <utility>

#include "api/make_ref_counted.h"
#include "api/sequence_checker.h"
#include "rtc_base/logging.h"
#include "sdk/android/src/jni/audio_device/audio_device_module.h"

// We expect Oboe to use our expected configuration:
// WebRTC is looking for signed 16-bit PCM data at 48000Hz.
constexpr oboe::AudioFormat kSampleFormat = oboe::AudioFormat::I16;
constexpr int32_t kSampleRate = 48000;
// WebRTC audio callbacks handle 10ms chunks, or 480 frames at 48000Hz per
// channel.
constexpr oboe::ChannelCount kChannelCount = oboe::ChannelCount::Mono;
constexpr size_t kMaxFramesPerCallback = (kSampleRate / 100);

// When delay can't be obtained, use a fairly high latency delay value by
// default.
constexpr uint16_t kDefaultPlayoutDelayMs = 150;

// Limit logging values that get updated frequently.
constexpr int kLogEveryN = 250;

namespace webrtc {
namespace jni {

namespace {

class OboeStream : public std::enable_shared_from_this<OboeStream>,
                   public oboe::AudioStreamDataCallback,
                   public oboe::AudioStreamErrorCallback {
 public:
  OboeStream(AudioTransport* audio_callback,
             oboe::Direction direction,
             bool use_exclusive_sharing_mode,
             bool use_input_low_latency,
             bool use_input_voice_comm_preset)
      : audio_callback_(audio_callback),
        direction_(direction),
        use_exclusive_sharing_mode_(use_exclusive_sharing_mode),
        use_input_low_latency_(use_input_low_latency),
        use_input_voice_comm_preset_(use_input_voice_comm_preset) {
    RTC_LOG(LS_WARNING) << "OboeStream constructed for "
                        << oboe::convertToText(direction_);
  }

  ~OboeStream() {
    RTC_LOG(LS_WARNING) << "OboeStream destructor called for "
                        << oboe::convertToText(direction_);
  }

  int32_t LockedCreate() {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    return Create();
  }

  void Terminate() {
    RTC_LOG(LS_WARNING) << "Terminate " << oboe::convertToText(direction_);

    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!initialized_) {
      return;
    }

    initialized_ = false;

    should_start_ = false;
    stream_->close();

    stream_.reset();
    latency_tuner_.reset();
  }

  int32_t Start() {
    RTC_LOG(LS_WARNING) << "Start " << oboe::convertToText(direction_);

    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!initialized_) {
      return -1;
    }
    if (should_start_) {
      RTC_LOG(LS_WARNING) << oboe::convertToText(direction_)
                          << " is already started!";
      return 0;
    }
    if (!stream_) {
      RTC_LOG(LS_ERROR) << oboe::convertToText(direction_)
                        << " stream is null!";
      return -1;
    }

    oboe::Result result = stream_->requestStart();
    if (result == oboe::Result::OK) {
      should_start_ = true;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "Failed to start the "
                        << oboe::convertToText(direction_)
                        << " stream: " << oboe::convertToText(result);
      return -1;
    }
  }

  // The ADM can get the playout delay from output streams.
  uint16_t GetPlayoutDelay() { return playout_delay_ms_; }

  // The ADM can get the playout underrun count from output streams.
  int32_t GetPlayoutUnderrunCount() { return playout_underrun_count_; }

  // Used for input streams so that the playout delay can be used to calculate a
  // total delay value.
  void SetPlayoutDelay(uint16_t playout_delay_ms) {
    playout_delay_ms_ = playout_delay_ms;
  }

  // Oboe Callbacks

  void onErrorAfterClose(oboe::AudioStream* audioStream,
                         oboe::Result error) override {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!initialized_) {
      RTC_LOG(LS_WARNING) << "onErrorAfterClose: Module not initialized for "
                          << oboe::convertToText(direction_)
                          << ". Error: " << oboe::convertToText(error);
      return;
    }

    if (audioStream != stream_.get()) {
      RTC_LOG(LS_ERROR) << "onErrorAfterClose: Unknown stream: "
                        << oboe::convertToText(error);
      return;
    }

    RTC_LOG(LS_WARNING) << "onErrorAfterClose: "
                        << oboe::convertToText(direction_)
                        << " stream: " << oboe::convertToText(error);
    if (error != oboe::Result::ErrorDisconnected) {
      RTC_LOG(LS_ERROR) << "onErrorAfterClose: Unhandled stream error";
      return;
    }

    if (Create() != 0) {
      RTC_LOG(LS_ERROR) << "onErrorAfterClose: Failed to recreate the stream!";
      initialized_ = false;
      should_start_ = false;
    } else {
      if (should_start_) {
        oboe::Result result = stream_->requestStart();
        if (result != oboe::Result::OK) {
          RTC_LOG(LS_ERROR) << "onErrorAfterClose: Failed to start the stream.";
          should_start_ = false;
        }
      }
    }
  }

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream,
                                        void* audioData,
                                        int32_t numFrames) override {
    if (!stream_mutex_.try_lock()) {
      RTC_LOG(LS_WARNING)
          << "onAudioReady: Unable to acquire lock, skipping callback";
      if (direction_ == oboe::Direction::Output) {
        std::fill(static_cast<int16_t*>(audioData),
                  static_cast<int16_t*>(audioData) + numFrames, 0);
      }
      return oboe::DataCallbackResult::Continue;
    }

    // We've acquired the lock, adopt it to scope.
    std::lock_guard<std::mutex> lock(stream_mutex_, std::adopt_lock);

    AudioTransport* audio_callback = audio_callback_;
    bool return_with_stop = false;

    if (!initialized_) {
      RTC_LOG(LS_WARNING) << "onAudioReady: initialized_ is false!";
      return_with_stop = true;
    } else if (!audio_callback) {
      RTC_LOG(LS_WARNING) << "onAudioReady: Audio callback is not set!";
      return_with_stop = true;
    } else if (audioStream != stream_.get()) {
      RTC_LOG(LS_ERROR) << "onAudioReady: Unknown stream!";
      return_with_stop = true;
    }

    if (return_with_stop) {
      if (direction_ == oboe::Direction::Output) {
        std::fill(static_cast<int16_t*>(audioData),
                  static_cast<int16_t*>(audioData) + numFrames, 0);
      }
      return oboe::DataCallbackResult::Stop;
    }

    if (direction_ == oboe::Direction::Output) {
      size_t num_samples_out = 0;

      // Retrieve new 16-bit PCM audio data using the audio transport instance.
      int64_t elapsed_time_ms = -1;
      int64_t ntp_time_ms = -1;
      int32_t result = audio_callback->NeedMorePlayData(
          /* samples_per_channel */ numFrames,
          /* bytes_per_sample */ sizeof(int16_t),
          /* number_of_channels */ kChannelCount,
          /* samples_per_second */ kSampleRate,
          /* audio_samples */ static_cast<int16_t*>(audioData),
          /* samples_out */ num_samples_out,
          /* elapsed_time_ms */ &elapsed_time_ms,
          /* ntp_time_ms */ &ntp_time_ms);
      if (result != 0) {
        RTC_LOG(LS_ERROR)
            << "onAudioReady: NeedMorePlayData failed with error: " << result;
        std::fill(static_cast<int16_t*>(audioData),
                  static_cast<int16_t*>(audioData) + numFrames, 0);
      }

      auto latencyResult = audioStream->calculateLatencyMillis();
      if (latencyResult) {
        playout_delay_ms_ = static_cast<uint16_t>(
            std::clamp(latencyResult.value(), static_cast<double>(0),
                       static_cast<double>(UINT16_MAX)));
      }

      auto countResult = audioStream->getXRunCount();
      if (countResult) {
        playout_underrun_count_ = countResult.value();
      }

      if (auto tuner = latency_tuner_.get()) {
        oboe::Result tuneResult = tuner->tune();
        if (tuneResult != oboe::Result::OK) {
          RTC_LOG(LS_WARNING) << "onAudioReady: LatencyTuner::tune failed: "
                              << oboe::convertToText(tuneResult);
        }
      }
    } else {  // direction_ == oboe::Direction::Input
      uint32_t new_mic_level_dummy = 0;
      uint32_t total_delay_ms = playout_delay_ms_;

      auto latencyResult = audioStream->calculateLatencyMillis();
      if (latencyResult) {
        total_delay_ms += static_cast<uint16_t>(
            std::clamp(latencyResult.value(), static_cast<double>(0),
                       static_cast<double>(UINT16_MAX)));
      }

      int32_t result = audio_callback->RecordedDataIsAvailable(
          /* audio_data */ static_cast<int16_t*>(audioData),
          /* number_of_frames */ numFrames,
          /* bytes_per_sample */ sizeof(int16_t),
          /* number_of_channels */ kChannelCount,
          /* sample_rate */ kSampleRate,
          /* audio_delay_milliseconds */ total_delay_ms,
          /* clock_drift */ 0,
          /* volume */ 0,
          /* key_pressed */ false,
          /* new_mic_volume */ new_mic_level_dummy);
      if (result != 0) {
        RTC_LOG(LS_ERROR)
            << "onAudioReady: RecordedDataIsAvailable failed with error: "
            << result;
      }
    }

    return oboe::DataCallbackResult::Continue;
  }

 private:
  void LogStreamConfiguration() {
    RTC_LOG(LS_WARNING)
        << "OboeStream Config: "
        << "direction: " << oboe::convertToText(stream_->getDirection())
        << ", audioApi: " << oboe::convertToText(stream_->getAudioApi())
        << ", deviceId: " << stream_->getDeviceId()
        << ", sessionId: " << stream_->getSessionId()
        << ", format: " << oboe::convertToText(stream_->getFormat())
        << ", sampleRate: " << stream_->getSampleRate()
        << ", channelCount: " << stream_->getChannelCount()
        << ", sharingMode: " << oboe::convertToText(stream_->getSharingMode())
        << ", performanceMode: "
        << oboe::convertToText(stream_->getPerformanceMode()) << "  mmap used: "
        << ((stream_->getAudioApi() == oboe::AudioApi::AAudio)
                ? (oboe::OboeExtensions::isMMapUsed(stream_.get()) ? "true"
                                                                   : "false")
                : "n/a")
        << ", framesPerBurst/Capacity/Size: " << stream_->getFramesPerBurst()
        << "/" << stream_->getBufferCapacityInFrames() << "/"
        << stream_->getBufferSizeInFrames()
        << (stream_->getDirection() == oboe::Direction::Output
                ? (", usage: " +
                   std::string(oboe::convertToText(stream_->getUsage())) +
                   ", contentType: " +
                   std::string(oboe::convertToText(stream_->getContentType())))
                : (stream_->getDirection() == oboe::Direction::Input
                       ? ", inputPreset: " + std::string(oboe::convertToText(
                                                 stream_->getInputPreset()))
                       : ""));
  }

  void ConfigureStreamBuilder(oboe::AudioStreamBuilder& builder) {
    builder.setDirection(direction_);

    // Keep using OpenSL-ES on Android 8.1 due to problems with AAudio.
    if (oboe::getSdkVersion() == __ANDROID_API_O_MR1__) {
      builder.setAudioApi(oboe::AudioApi::OpenSLES);
    }

    // Let Oboe manage the configuration we want.
    builder.setFormat(kSampleFormat);
    builder.setSampleRate(kSampleRate);
    builder.setChannelCount(kChannelCount);
    builder.setFramesPerDataCallback(kMaxFramesPerCallback);

    // And allow Oboe to perform conversions if necessary.
    builder.setFormatConversionAllowed(true);
    // Use medium to balance performance, quality, and latency.
    builder.setSampleRateConversionQuality(
        oboe::SampleRateConversionQuality::Medium);
    builder.setChannelConversionAllowed(true);

    if (use_exclusive_sharing_mode_) {
      // Attempt to use Exclusive sharing mode for the lowest possible latency.
      builder.setSharingMode(oboe::SharingMode::Exclusive);
    }

    // Set callbacks for handling audio data and errors, providing shared
    // pointers to this OboeStream instance.
    builder.setDataCallback(shared_from_this());
    builder.setErrorCallback(shared_from_this());

    if (direction_ == oboe::Direction::Output) {
      // Set the performance mode to get the lowest possible latency.
      builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);

      // Specifying usage and contentType should result in better volume and
      // routing decisions.
      builder.setUsage(oboe::Usage::VoiceCommunication);
      builder.setContentType(oboe::ContentType::Speech);
    } else {
      if (use_input_low_latency_) {
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
      } else {
        builder.setPerformanceMode(oboe::PerformanceMode::None);
      }

      // Specifying an inputPreset should result in better volume and routing
      // decisions (and privacy).
      if (use_input_voice_comm_preset_) {
        builder.setInputPreset(oboe::InputPreset::VoiceCommunication);
      } else {
        builder.setInputPreset(oboe::InputPreset::VoiceRecognition);
      }
    }
  }

  int32_t Create() {
    RTC_LOG(LS_WARNING) << "Create " << oboe::convertToText(direction_);

    oboe::AudioStreamBuilder builder;
    ConfigureStreamBuilder(builder);

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "Failed to open the stream: "
                        << oboe::convertToText(result);
      return -1;
    }

    initialized_ = true;

    if (direction_ == oboe::Direction::Output &&
        stream_->getAudioApi() == oboe::AudioApi::AAudio) {
      latency_tuner_ = std::make_unique<oboe::LatencyTuner>(*stream_);
    }

    LogStreamConfiguration();

    return 0;
  }

  std::atomic<bool> initialized_{false};
  std::atomic<bool> should_start_{false};

  std::shared_ptr<oboe::AudioStream> stream_;

  std::atomic<AudioTransport*> audio_callback_{nullptr};

  const oboe::Direction direction_;
  const bool use_exclusive_sharing_mode_;
  const bool use_input_low_latency_;
  const bool use_input_voice_comm_preset_;

  std::atomic<uint16_t> playout_delay_ms_{kDefaultPlayoutDelayMs};

  // For output streams.
  std::atomic<int32_t> playout_underrun_count_{0};
  std::unique_ptr<oboe::LatencyTuner> latency_tuner_;

  std::mutex stream_mutex_;
};

// Implements an Audio Device Manager using the Oboe C++ audio library
// (https://github.com/google/oboe).
class AndroidAudioDeviceModuleOboe : public AudioDeviceModule {
 public:
  AndroidAudioDeviceModuleOboe(bool use_software_acoustic_echo_canceler,
                               bool use_software_noise_suppressor,
                               bool use_exclusive_sharing_mode,
                               bool use_input_low_latency,
                               bool use_input_voice_comm_preset)
      : use_software_acoustic_echo_canceler_(
            use_software_acoustic_echo_canceler),
        use_software_noise_suppressor_(use_software_noise_suppressor),
        use_exclusive_sharing_mode_(use_exclusive_sharing_mode),
        use_input_low_latency_(use_input_low_latency),
        use_input_voice_comm_preset_(use_input_voice_comm_preset) {
    RTC_LOG(LS_WARNING) << "AndroidAudioDeviceModuleOboe constructed";
    thread_checker_.Detach();
  }

  ~AndroidAudioDeviceModuleOboe() override {
    RTC_LOG(LS_WARNING) << "AndroidAudioDeviceModuleOboe destructor called";
  }

  // Retrieve the currently utilized audio layer
  // There is only one audio layer as far as this implementation is concerned.
  // Use kAndroidJavaAudio to make sure the default implementation isn't used.
  int32_t ActiveAudioLayer(
      AudioDeviceModule::AudioLayer* audioLayer) const override {
    RTC_LOG(LS_WARNING) << "ActiveAudioLayer always kAndroidJavaAudio";
    *audioLayer = kAndroidJavaAudio;
    return 0;
  }

  // Full-duplex transportation of PCM audio
  // Invoked when the VoIP Engine is initialized.
  int32_t RegisterAudioCallback(AudioTransport* audioCallback) override {
    RTC_LOG(LS_WARNING) << __FUNCTION__;
    if (should_play_ || should_record_) {
      RTC_LOG(LS_ERROR)
          << "Failed to set audio transport since media was active";
      return -1;
    }

    audio_callback_ = audioCallback;

    return 0;
  }

  // Main initialization and termination

  // Perform general initialization, when a call is going to be established, but
  // before the user should start sending and receiving audio.
  int32_t Init() override {
    RTC_LOG(LS_WARNING) << "Init, using Oboe version: " << oboe::Version::Text;
    RTC_DCHECK_RUN_ON(&thread_checker_);

    initialized_ = true;

    return 0;
  }

  int32_t Terminate() override {
    RTC_LOG(LS_WARNING) << __FUNCTION__;
    RTC_DCHECK_RUN_ON(&thread_checker_);

    if (initialized_) {
      initialized_ = false;
      should_play_ = false;
      should_record_ = false;

      if (output_stream_) {
        output_stream_->Terminate();
        output_stream_.reset();
      }

      if (input_stream_) {
        input_stream_->Terminate();
        input_stream_.reset();
      }
    }

    thread_checker_.Detach();

    return 0;
  }

  bool Initialized() const override {
    RTC_LOG(LS_WARNING) << "Initialized " << initialized_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return initialized_;
  }

  // Device enumeration

  // This implementation only supports one playout device.
  int16_t PlayoutDevices() override {
    RTC_LOG(LS_WARNING) << "PlayoutDevices always 1";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return 1;
  }

  // This implementation only supports one playout device.
  int16_t RecordingDevices() override {
    RTC_LOG(LS_WARNING) << "RecordingDevices always 1";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return 1;
  }

  int32_t PlayoutDeviceName(uint16_t index,
                            char name[kAdmMaxDeviceNameSize],
                            char guid[kAdmMaxGuidSize]) override {
    RTC_LOG(LS_ERROR) << "PlayoutDeviceName (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t RecordingDeviceName(uint16_t index,
                              char name[kAdmMaxDeviceNameSize],
                              char guid[kAdmMaxGuidSize]) override {
    RTC_LOG(LS_ERROR) << "RecordingDeviceName (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  // Device selection

  int32_t SetPlayoutDevice(uint16_t index) override {
    // OK to use but it has no effect currently since device selection is done
    // using Android APIs instead.
    RTC_LOG(LS_WARNING) << "SetPlayoutDevice " << index << ", (no effect!)";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return 0;
  }

  int32_t SetPlayoutDevice(
      AudioDeviceModule::WindowsDeviceType device) override {
    RTC_LOG(LS_ERROR) << "SetPlayoutDevice (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t SetRecordingDevice(uint16_t index) override {
    // OK to use but it has no effect currently since device selection is done
    // using Android APIs instead.
    RTC_LOG(LS_WARNING) << "SetRecordingDevice " << index << ", (no effect!)";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return 0;
  }

  int32_t SetRecordingDevice(
      AudioDeviceModule::WindowsDeviceType device) override {
    RTC_LOG(LS_ERROR) << "SetRecordingDevice (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  // Audio transport initialization

  int32_t PlayoutIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "PlayoutIsAvailable always true";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *available = true;
    return 0;
  }

  int32_t InitPlayout() override {
    RTC_LOG(LS_WARNING) << "InitPlayout";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }

    if (output_stream_) {
      RTC_LOG(LS_WARNING) << "Playout is already initialized!";
      return 0;
    }

    if (CreateOutputStream() != 0) {
      return -1;
    }

    return 0;
  }

  bool PlayoutIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "PlayoutIsInitialized "
                        << (output_stream_ != nullptr);
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return output_stream_ != nullptr;
  }

  int32_t RecordingIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "RecordingIsAvailable always true";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *available = true;
    return 0;
  }

  int32_t InitRecording() override {
    RTC_LOG(LS_WARNING) << "InitRecording";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }

    if (input_stream_) {
      RTC_LOG(LS_WARNING) << "Recording is already initialized!";
      return 0;
    }

    if (CreateInputStream() != 0) {
      return -1;
    }

    return 0;
  }

  bool RecordingIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "RecordingIsInitialized "
                        << (input_stream_ != nullptr);
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return input_stream_ != nullptr;
  }

  // Audio transport control

  int32_t StartPlayout() override {
    RTC_LOG(LS_WARNING) << "StartPlayout";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (should_play_) {
      RTC_LOG(LS_WARNING) << "Playout is already started!";
      return 0;
    }

    if (output_stream_) {
      int32_t result = output_stream_->Start();
      if (result == 0) {
        should_play_ = true;
      }
      return result;
    } else {
      RTC_LOG(LS_ERROR) << "Output stream is null!";
      return -1;
    }
  }

  int32_t StopPlayout() override {
    RTC_LOG(LS_WARNING) << "StopPlayout";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (!should_play_) {
      RTC_LOG(LS_WARNING) << "Playout is already stopped!";
      return 0;
    }

    if (output_stream_) {
      // Stop and close the output stream, returning Playout to an uninitialized
      // state.
      output_stream_->Terminate();
      output_stream_.reset();
      should_play_ = false;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "Output stream is null!";
      return -1;
    }
  }

  bool Playing() const override {
    RTC_LOG(LS_WARNING) << "Playing " << should_play_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return should_play_;
  }

  int32_t StartRecording() override {
    RTC_LOG(LS_WARNING) << "StartRecording";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (should_record_) {
      RTC_LOG(LS_WARNING) << "Recording is already started!";
      return 0;
    }

    if (input_stream_) {
      int32_t result = input_stream_->Start();
      if (result == 0) {
        should_record_ = true;
      }
      return result;
    } else {
      RTC_LOG(LS_ERROR) << "Input stream is null!";
      return -1;
    }
  }

  int32_t StopRecording() override {
    RTC_LOG(LS_WARNING) << "StopRecording";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (!should_record_) {
      RTC_LOG(LS_WARNING) << "Recording is already stopped!";
      return 0;
    }

    if (input_stream_) {
      // Stop and close the input stream, returning Recording to an
      // uninitialized state.
      input_stream_->Terminate();
      input_stream_.reset();
      should_record_ = false;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "Input stream is null!";
      return -1;
    }
  }

  bool Recording() const override {
    RTC_LOG(LS_WARNING) << "Recording " << should_record_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return should_record_;
  }

  // Audio mixer initialization
  // Use the module initialization to indicate device readiness.

  int32_t InitSpeaker() override {
    RTC_LOG(LS_WARNING) << "InitSpeaker " << (initialized_ ? 0 : -1);
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return initialized_ ? 0 : -1;
  }

  bool SpeakerIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "SpeakerIsInitialized " << initialized_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return initialized_;
  }

  int32_t InitMicrophone() override {
    RTC_LOG(LS_WARNING) << "InitMicrophone " << (initialized_ ? 0 : -1);
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return initialized_ ? 0 : -1;
  }

  bool MicrophoneIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "MicrophoneIsInitialized " << initialized_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return initialized_;
  }

  // Speaker volume controls

  int32_t SpeakerVolumeIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "SpeakerVolumeIsAvailable always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    *available = false;
    return 0;
  }

  int32_t SetSpeakerVolume(uint32_t volume) override {
    RTC_LOG(LS_WARNING) << "SetSpeakerVolume always success";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    return 0;
  }

  int32_t SpeakerVolume(uint32_t* output_volume) const override {
    RTC_LOG(LS_WARNING) << "SpeakerVolume always 0";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    *output_volume = 0;
    return 0;
  }

  int32_t MaxSpeakerVolume(uint32_t* output_max_volume) const override {
    RTC_LOG(LS_WARNING) << "MaxSpeakerVolume always 0";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    *output_max_volume = 0;
    return 0;
  }

  int32_t MinSpeakerVolume(uint32_t* output_min_volume) const override {
    RTC_LOG(LS_WARNING) << "MinSpeakerVolume always 0";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    *output_min_volume = 0;
    return 0;
  }

  // Microphone volume controls

  int32_t MicrophoneVolumeIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "MicrophoneVolumeIsAvailable always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *available = false;
    return -1;
  }

  int32_t SetMicrophoneVolume(uint32_t volume) override {
    RTC_LOG(LS_ERROR) << "SetMicrophoneVolume (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t MicrophoneVolume(uint32_t* volume) const override {
    RTC_LOG(LS_ERROR) << "MicrophoneVolume (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override {
    RTC_LOG(LS_ERROR) << "MaxMicrophoneVolume (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t MinMicrophoneVolume(uint32_t* minVolume) const override {
    RTC_LOG(LS_ERROR) << "MinMicrophoneVolume (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  // Speaker mute control

  int32_t SpeakerMuteIsAvailable(bool* available) override {
    RTC_LOG(LS_ERROR) << "SpeakerMuteIsAvailable (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t SetSpeakerMute(bool enable) override {
    RTC_LOG(LS_ERROR) << "SetSpeakerMute (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t SpeakerMute(bool* enabled) const override {
    RTC_LOG(LS_ERROR) << "SpeakerMute (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  // Microphone mute control

  int32_t MicrophoneMuteIsAvailable(bool* available) override {
    RTC_LOG(LS_ERROR) << "MicrophoneMuteIsAvailable (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t SetMicrophoneMute(bool enable) override {
    RTC_LOG(LS_ERROR) << "SetMicrophoneMute (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t MicrophoneMute(bool* enabled) const override {
    RTC_LOG(LS_ERROR) << "MicrophoneMute (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  // Stereo support

  // None of our models support stereo playout for communication. Speech is
  // always captured in mono and the playout device should up-mix to all
  // applicable output emitters.
  int32_t StereoPlayoutIsAvailable(bool* available) const override {
    RTC_LOG(LS_WARNING) << "StereoPlayoutIsAvailable always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *available = false;
    return 0;
  }

  // We don't expect stereo to be enabled, especially on-the-fly.
  int32_t SetStereoPlayout(bool enable) override {
    RTC_LOG(LS_WARNING) << "SetStereoPlayout " << enable;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (enable) {
      return -1;
    }
    return 0;
  }

  int32_t StereoPlayout(bool* enabled) const override {
    RTC_LOG(LS_WARNING) << "StereoPlayout always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *enabled = false;
    return 0;
  }

  // None of our models support stereo recording for communication. Speech is
  // always captured in mono.
  int32_t StereoRecordingIsAvailable(bool* available) const override {
    RTC_LOG(LS_WARNING) << "StereoRecordingIsAvailable always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *available = false;
    return 0;
  }

  // We don't expect stereo to be enabled, especially on-the-fly.
  int32_t SetStereoRecording(bool enable) override {
    RTC_LOG(LS_WARNING) << "SetStereoRecording " << enable;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (enable) {
      return -1;
    }
    return 0;
  }

  int32_t StereoRecording(bool* enabled) const override {
    RTC_LOG(LS_WARNING) << "StereoRecording always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    *enabled = false;
    return 0;
  }

  // Playout delay calculation.
  int32_t PlayoutDelay(uint16_t* delay_ms) const override {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (output_stream_) {
      // Return the latest value set by the data callback.
      *delay_ms = output_stream_->GetPlayoutDelay();

      // We'll use a best effort approach to keep the input stream updated.
      // Just do it every time WebRTC requests it.
      if (input_stream_) {
        input_stream_->SetPlayoutDelay(*delay_ms);
      }

      // Limit logging of the playout delay.
      if (++delay_log_counter_ % kLogEveryN == 0) {
        RTC_LOG(LS_WARNING) << "playout_delay_ms_: " << *delay_ms;
      }

      return 0;
    } else {
      *delay_ms = 0;
      return -1;
    }
  }

  // Only supported on Android.

  bool BuiltInAECIsAvailable() const override {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_)
      return false;

    RTC_LOG(LS_WARNING) << "BuiltInAECIsAvailable "
                        << !use_software_acoustic_echo_canceler_;
    return !use_software_acoustic_echo_canceler_;
  }

  // Not implemented for any input device on Android.
  bool BuiltInAGCIsAvailable() const override {
    RTC_LOG(LS_WARNING) << "BuiltInAGCIsAvailable always false";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return false;
  }

  bool BuiltInNSIsAvailable() const override {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_)
      return false;

    RTC_LOG(LS_WARNING) << "BuiltInNSIsAvailable "
                        << !use_software_noise_suppressor_;
    return !use_software_noise_suppressor_;
  }

  // Enables the built-in audio effects. Only supported on Android.

  int32_t EnableBuiltInAEC(bool enable) override {
    RTC_LOG(LS_WARNING) << "EnableBuiltInAEC " << enable;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_)
      return -1;

    // This is a noop for us.
    return 0;
  }

  int32_t EnableBuiltInAGC(bool enable) override {
    RTC_LOG(LS_ERROR) << "EnableBuiltInAGC " << enable
                      << ", (should not be reached)";
    RTC_DCHECK_NOTREACHED();
    return -1;
  }

  int32_t EnableBuiltInNS(bool enable) override {
    RTC_LOG(LS_WARNING) << "EnableBuiltInNS " << enable;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_)
      return -1;

    // This is a noop for us.
    return 0;
  }

  // Playout underrun count. Only supported on Android.
  int32_t GetPlayoutUnderrunCount() const override {
    RTC_DCHECK_RUN_ON(&thread_checker_);

    int32_t playout_underrun_count = 0;

    // Return the latest value set by the data callback for the output stream.
    if (output_stream_) {
      playout_underrun_count = output_stream_->GetPlayoutUnderrunCount();
      // Limit the logging of the playout underrun count.
      if (++underrun_log_counter_ % kLogEveryN == 0) {
        if (playout_underrun_count != 0) {
          RTC_LOG(LS_WARNING)
              << "playout_underrun_count_: " << playout_underrun_count;
        }
      }
    }

    return playout_underrun_count;
  }

  // Used to generate RTC stats.
  std::optional<Stats> GetStats() const override {
    // Returning nullopt because stats are not supported in this implementation.
    return std::nullopt;
  }

 private:
  int32_t CreateOboeStream(oboe::Direction direction) {
    RTC_LOG(LS_WARNING) << "CreateOboeStream: "
                        << oboe::convertToText(direction);

    std::shared_ptr<OboeStream>& stream =
        (direction == oboe::Direction::Output) ? output_stream_ : input_stream_;

    stream = std::make_shared<OboeStream>(
        audio_callback_.load(), direction, use_exclusive_sharing_mode_,
        use_input_low_latency_, use_input_voice_comm_preset_);

    int32_t result = stream->LockedCreate();
    if (result != 0) {
      stream.reset();
      return result;
    }

    RTC_LOG(LS_WARNING) << "OboeStream created, "
                        << oboe::convertToText(direction)
                        << " with use count: " << stream.use_count();
    return 0;
  }

  int32_t CreateOutputStream() {
    return CreateOboeStream(oboe::Direction::Output);
  }

  int32_t CreateInputStream() {
    return CreateOboeStream(oboe::Direction::Input);
  }

  SequenceChecker thread_checker_;

  const bool use_software_acoustic_echo_canceler_;
  const bool use_software_noise_suppressor_;
  const bool use_exclusive_sharing_mode_;
  const bool use_input_low_latency_;
  const bool use_input_voice_comm_preset_;

  std::shared_ptr<OboeStream> input_stream_;
  std::shared_ptr<OboeStream> output_stream_;

  std::atomic<AudioTransport*> audio_callback_{nullptr};

  std::atomic<bool> initialized_{false};
  std::atomic<bool> should_play_{false};
  std::atomic<bool> should_record_{false};

  mutable std::atomic<uint32_t> delay_log_counter_{0};
  mutable std::atomic<uint32_t> underrun_log_counter_{0};
};

}  // namespace

scoped_refptr<AudioDeviceModule> CreateAudioDeviceModuleOboe(
    bool use_software_acoustic_echo_canceler,
    bool use_software_noise_suppressor,
    bool use_exclusive_sharing_mode,
    bool use_input_low_latency,
    bool use_input_voice_comm_preset) {
  RTC_LOG(LS_WARNING) << "CreateAudioDeviceModuleOboe";
  return make_ref_counted<AndroidAudioDeviceModuleOboe>(
      use_software_acoustic_echo_canceler, use_software_noise_suppressor,
      use_exclusive_sharing_mode, use_input_low_latency,
      use_input_voice_comm_preset);
}

}  // namespace jni
}  // namespace webrtc
