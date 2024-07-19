/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "sdk/android/src/jni/audio_device/audio_device_module.h"

#include "api/make_ref_counted.h"
#include "api/sequence_checker.h"
#include "rtc_base/logging.h"

#include <oboe/Oboe.h>

#include <memory>
#include <utility>
#include <atomic>

// We expect Oboe to use our expected configuration:
// WebRTC is looking for signed 16-bit PCM data at 48000Hz.
constexpr oboe::AudioFormat kSampleFormat = oboe::AudioFormat::I16;
constexpr int32_t kSampleRate = 48000;
// WebRTC audio callbacks handle 10ms chunks, or 480 frames at 48000Hz per channel.
constexpr oboe::ChannelCount kChannelCount = oboe::ChannelCount::Mono;
constexpr size_t kMaxFramesPerCallback = (kSampleRate / 100);

// When delay can't be obtained, use a fairly high latency delay value by default.
constexpr uint16_t kDefaultPlayoutDelayMs = 150;

constexpr int kInvalidAudioSessionId = -1;

// Limit logging values that get updated frequently.
constexpr int kLogEveryN = 100;

namespace webrtc {
namespace jni {

namespace {

// Implements an Audio Device Manager using the Oboe C++ audio library (https://github.com/google/oboe).
class AndroidAudioDeviceModuleOboe :
    public AudioDeviceModule,
    public oboe::AudioStreamDataCallback,
    public oboe::AudioStreamErrorCallback {
 public:
  AndroidAudioDeviceModuleOboe(
    bool use_software_acoustic_echo_canceler,
    bool use_software_noise_suppressor,
    bool use_exclusive_sharing_mode,
    int audio_session_id)
      : use_software_acoustic_echo_canceler_(use_software_acoustic_echo_canceler),
        use_software_noise_suppressor_(use_software_noise_suppressor),
        use_exclusive_sharing_mode_(use_exclusive_sharing_mode),
        audio_session_id_(audio_session_id) {
    RTC_LOG(LS_WARNING) << "AndroidAudioDeviceModuleOboe::AndroidAudioDeviceModuleOboe";
    thread_checker_.Detach();
  }

  ~AndroidAudioDeviceModuleOboe() override {
    RTC_LOG(LS_WARNING) << "AndroidAudioDeviceModuleOboe::~AndroidAudioDeviceModuleOboe";
  }

  // Retrieve the currently utilized audio layer
  // There is only one audio layer as far as this implementation is concerned. Use
  // kAndroidJavaAudio to make sure the default implementation isn't used.
  int32_t ActiveAudioLayer(AudioDeviceModule::AudioLayer* audioLayer) const override {
    RTC_LOG(LS_WARNING) << "ActiveAudioLayer always kAndroidJavaAudio";
    *audioLayer = kAndroidJavaAudio;
    return 0;
  }

  // Full-duplex transportation of PCM audio
  // Invoked when the VoIP Engine is initialized.
  int32_t RegisterAudioCallback(AudioTransport* audioCallback) override {
    RTC_LOG(LS_WARNING) << __FUNCTION__;
    if (is_playing_ || is_recording_) {
      RTC_LOG(LS_ERROR) << "Failed to set audio transport since media was active";
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
    if (!initialized_) {
      return 0;
    }

    if (input_stream_) {
      input_stream_->stop();
      input_stream_->close();
      input_stream_.reset();
    }
    if (output_stream_) {
      output_stream_->stop();
      output_stream_->close();
      latency_tuner_.reset();
      output_stream_.reset();
    }

    playout_initialized_ = false;
    recording_initialized_ = false;

    is_playing_ = false;
    is_recording_ = false;

    initialized_ = false;
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

  int32_t SetPlayoutDevice(AudioDeviceModule::WindowsDeviceType device) override {
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

  int32_t SetRecordingDevice(AudioDeviceModule::WindowsDeviceType device) override {
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
    if (playout_initialized_) {
      RTC_LOG(LS_WARNING) << "Playout is already initialized!";
      return 0;
    }

    if (CreateOutputStream() != 0) {
      return -1;
    }

    playout_initialized_ = true;

    return 0;
  }

  bool PlayoutIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "PlayoutIsInitialized " << playout_initialized_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return playout_initialized_;
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
    if (recording_initialized_) {
      RTC_LOG(LS_WARNING) << "Recording is already initialized!";
      return 0;
    }

    if (CreateInputStream() != 0) {
      return -1;
    }

    recording_initialized_ = true;

    return 0;
  }

  bool RecordingIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "RecordingIsInitialized " << recording_initialized_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return recording_initialized_;
  }

  // Audio transport control
  // Note that the general is_playing_ and is_recording_ flags may not be in sync with
  // the Oboe states, but they are good enough to use as intentions.

  int32_t StartPlayout() override {
    RTC_LOG(LS_WARNING) << "StartPlayout";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (!playout_initialized_) {
      RTC_LOG(LS_ERROR) << "Playout is not initialized!";
      return -1;
    }
    if (is_playing_) {
      RTC_LOG(LS_WARNING) << "Playout is already started!";
      return 0;
    }

    oboe::Result result = output_stream_->requestStart();
    if (result == oboe::Result::OK) {
      is_playing_ = true;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "Failed to start the output stream: " << oboe::convertToText(result);
      return -1;
    }
  }

  int32_t StopPlayout() override {
    RTC_LOG(LS_WARNING) << "StopPlayout";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (!is_playing_) {
      RTC_LOG(LS_WARNING) << "Playout is already stopped!";
      return 0;
    }

    // Blocking call to stop the output stream.
    oboe::Result result = output_stream_->stop();
    // In most error cases, playout is stopped, and we'll return -1, so clear the state flag.
    is_playing_ = false;
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "Failed to stop the output stream: " << oboe::convertToText(result);
      return -1;
    }

    return 0;
  }

  bool Playing() const override {
    RTC_LOG(LS_WARNING) << "Playing " << is_playing_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return is_playing_;
  }

  int32_t StartRecording() override {
    RTC_LOG(LS_WARNING) << "StartRecording";
    RTC_DCHECK_RUN_ON(&thread_checker_);

    if (!initialized_) {
      return -1;
    }
    if (!recording_initialized_) {
      RTC_LOG(LS_ERROR) << "Recording is not initialized!";
      return -1;
    }
    if (is_recording_) {
      RTC_LOG(LS_WARNING) << "Recording is already started!";
      return 0;
    }

    oboe::Result result = input_stream_->requestStart();
    if (result == oboe::Result::OK) {
      is_recording_ = true;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "Failed to start the input stream: " << oboe::convertToText(result);
      return -1;
    }
  }

  int32_t StopRecording() override {
    RTC_LOG(LS_WARNING) << "StopRecording";
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_) {
      return -1;
    }
    if (!is_recording_) {
      RTC_LOG(LS_WARNING) << "Recording is already stopped!";
      return 0;
    }

    // Blocking call to stop the output stream.
    oboe::Result result = input_stream_->stop();
    // In most error cases, recording is stopped, and we'll return -1, so clear the state flag.
    is_recording_ = false;
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "Failed to stop the input stream: " << oboe::convertToText(result);
      return -1;
    }

    return 0;
  }

  bool Recording() const override {
    RTC_LOG(LS_WARNING) << "Recording " << is_recording_;
    RTC_DCHECK_RUN_ON(&thread_checker_);
    return is_recording_;
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

  // None of our models support stereo playout for communication. Speech is always captured
  // in mono and the playout device should up-mix to all applicable output emitters.
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

  // None of our models support stereo recording for communication. Speech is always
  // captured in mono.
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

    // Return the latest value set by the data callback.
    *delay_ms = playout_delay_ms_;

    // Limit logging of the playout delay.
    if (++delay_log_counter_ % kLogEveryN == 0) {
      RTC_LOG(LS_WARNING) << "playout_delay_ms_: " << *delay_ms;
    }
    return 0;
  }

  // Only supported on Android.

  bool BuiltInAECIsAvailable() const override {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (!initialized_)
      return false;

    RTC_LOG(LS_WARNING) << "BuiltInAECIsAvailable " << !use_software_acoustic_echo_canceler_;
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

    RTC_LOG(LS_WARNING) << "BuiltInNSIsAvailable " << !use_software_noise_suppressor_;
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
    RTC_LOG(LS_ERROR) << "EnableBuiltInAGC " << enable << ", (should not be reached)";
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

    // Return the latest value set by the data callback.
    if (playout_underrun_count_ != 0) {
      // Limit logging of the playout underrun count.
      if (++underrun_log_counter_ % kLogEveryN == 0) {
        RTC_LOG(LS_WARNING) << "playout_underrun_count_: " << playout_underrun_count_;
      }
    }
    return playout_underrun_count_;
  }

  // Used to generate RTC stats.
  absl::optional<Stats> GetStats() const override {
    // Returning nullopt because stats are not supported in this implementation.
    return absl::nullopt;
  }

  // Oboe Callbacks

  bool onError(oboe::AudioStream *audioStream, oboe::Result error) override {
    if (audioStream == output_stream_.get()) {
      RTC_LOG(LS_WARNING) << "onError on output stream: " << oboe::convertToText(error);
      if (error == oboe::Result::ErrorDisconnected) {
        oboe::Result result = output_stream_->close();
        if (result != oboe::Result::OK) {
            RTC_LOG(LS_WARNING) << "Failed to close the output stream: " << oboe::convertToText(result);
        }

        latency_tuner_.reset();
        output_stream_.reset();

        if (playout_initialized_) {
          if (CreateOutputStream() != 0) {
            RTC_LOG(LS_ERROR) << "Failed to recreate the output stream!";
            playout_initialized_ = false;
            is_playing_ = false;
          } else {
            if (is_playing_) {
              result = output_stream_->requestStart();
              if (result != oboe::Result::OK) {
                RTC_LOG(LS_ERROR) << "Failed to start the output stream.";
                is_playing_ = false;
              }
            }
          }
        }

        return true;
      } else {
        RTC_LOG(LS_ERROR) << "Unhandled output stream error";
        return false;
      }
    } else if (audioStream == input_stream_.get()) {
      RTC_LOG(LS_WARNING) << "onError on input stream: " << oboe::convertToText(error);
      if (error == oboe::Result::ErrorDisconnected) {
        oboe::Result result = input_stream_->close();
        if (result != oboe::Result::OK) {
            RTC_LOG(LS_WARNING) << "Failed to close the input stream: " << oboe::convertToText(result);
        }

        input_stream_.reset();

        if (recording_initialized_) {
          if (CreateInputStream() != 0) {
            RTC_LOG(LS_ERROR) << "Failed to recreate the input stream!";
            recording_initialized_ = false;
            is_recording_ = false;
          } else {
            if (is_recording_) {
              result = input_stream_->requestStart();
              if (result != oboe::Result::OK) {
                RTC_LOG(LS_ERROR) << "Failed to start the input stream.";
                is_recording_ = false;
              }
            }
          }
        }

        return true;
      } else {
        RTC_LOG(LS_ERROR) << "Unhandled input stream error";
        return false;
      }
    } else {
      RTC_LOG(LS_ERROR) << "onError for unknown stream: " << oboe::convertToText(error);
      return false;
    }
  }

  void onErrorBeforeClose(oboe::AudioStream *audioStream, oboe::Result error) override {
    RTC_LOG(LS_ERROR) << "onErrorBeforeClose invoked! " << oboe::convertToText(error);
  }

  void onErrorAfterClose(oboe::AudioStream *audioStream, oboe::Result error) override {
    RTC_LOG(LS_ERROR) << "onErrorAfterClose invoked! " << oboe::convertToText(error);
  }

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) override {
    if (!audio_callback_) {
      RTC_LOG(LS_ERROR) << "Audio callback is not set!";
      std::fill(static_cast<int16_t*>(audioData), static_cast<int16_t*>(audioData) + numFrames, 0);
      return oboe::DataCallbackResult::Stop;
    }

    if (audioStream == output_stream_.get()) {
      size_t num_samples_out = 0;

      // Retrieve new 16-bit PCM audio data using the audio transport instance.
      int64_t elapsed_time_ms = -1;
      int64_t ntp_time_ms = -1;
      int32_t result = audio_callback_->NeedMorePlayData(
        /* samples_per_channel */ numFrames,
        /* bytes_per_sample */ sizeof(int16_t),
        /* number_of_channels */ kChannelCount,
        /* samples_per_second */ kSampleRate,
        /* audio_samples */ static_cast<int16_t*>(audioData),
        /* samples_out */ num_samples_out,
        /* elapsed_time_ms */ &elapsed_time_ms,
        /* ntp_time_ms */ &ntp_time_ms);
      if (result != 0) {
        RTC_LOG(LS_ERROR) << "NeedMorePlayData failed with error: " << result;
        std::fill(static_cast<int16_t*>(audioData), static_cast<int16_t*>(audioData) + numFrames, 0);
      }

      // Update the delay of the playout.
      auto latencyResult = output_stream_->calculateLatencyMillis();
      if (latencyResult) {
        playout_delay_ms_ = static_cast<uint16_t>(
          std::clamp(latencyResult.value(), static_cast<double>(0), static_cast<double>(UINT16_MAX))
        );
      }

      // Update the playout underrun count.
      auto countResult = output_stream_->getXRunCount();
      if (countResult) {
        playout_underrun_count_ = countResult.value();
      }

      if (latency_tuner_) {
        oboe::Result tuneResult = latency_tuner_->tune();
        if (tuneResult != oboe::Result::OK) {
          RTC_LOG(LS_WARNING) << "LatencyTuner::tune failed: " << oboe::convertToText(tuneResult);
        }
      }
    } else if (audioStream == input_stream_.get()) {
      uint32_t new_mic_level_dummy = 0;
      uint32_t total_delay_ms = playout_delay_ms_;

      auto latencyResult = input_stream_->calculateLatencyMillis();
      if (latencyResult) {
        total_delay_ms += static_cast<uint16_t>(
          std::clamp(latencyResult.value(), static_cast<double>(0), static_cast<double>(UINT16_MAX))
        );
      }

      int32_t result = audio_callback_->RecordedDataIsAvailable(
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
        RTC_LOG(LS_ERROR) << "RecordedDataIsAvailable failed with error: " << result;
        std::fill(static_cast<int16_t*>(audioData), static_cast<int16_t*>(audioData) + numFrames, 0);
      }
    } else {
      RTC_LOG(LS_ERROR) << "onAudioReady on unknown stream!";
      std::fill(static_cast<int16_t*>(audioData), static_cast<int16_t*>(audioData) + numFrames, 0);
    }

    return oboe::DataCallbackResult::Continue;
  }

 private:

  void LogStreamConfiguration(const std::shared_ptr<oboe::AudioStream>& stream) {
    RTC_LOG(LS_WARNING) << "Oboe Stream Config: "
      << "direction: " << oboe::convertToText(stream->getDirection())
      << ", audioApi: " << oboe::convertToText(stream->getAudioApi())
      << ", deviceId: " << stream->getDeviceId()
      << ", format: " << oboe::convertToText(stream->getFormat())
      << ", sampleRate: " << stream->getSampleRate()
      << ", channelCount: " << stream->getChannelCount()
      << ", sharingMode: " << oboe::convertToText(stream->getSharingMode())
      << ", performanceMode: " << oboe::convertToText(stream->getPerformanceMode())
      << "  mmap used: " << ((stream->getAudioApi() == oboe::AudioApi::AAudio) ?
          (oboe::OboeExtensions::isMMapUsed(stream.get()) ? "true" : "false") : "n/a")
      << ", framesPerBurst/Capacity/Size: "
          << stream->getFramesPerBurst() << "/"
          << stream->getBufferCapacityInFrames() << "/"
          << stream->getBufferSizeInFrames()
      << (stream->getDirection() == oboe::Direction::Output ?
          (", usage: " + std::string(oboe::convertToText(stream->getUsage())) +
           ", contentType: " + std::string(oboe::convertToText(stream->getContentType()))) :
          (stream->getDirection() == oboe::Direction::Input ?
           ", inputPreset: " + std::string(oboe::convertToText(stream->getInputPreset())) :
           ""));
  }

  void ConfigureCommonStreamSettings(oboe::AudioStreamBuilder& builder, oboe::Direction direction) {
    builder.setDirection(direction);

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
    // Use medium to balance performance and quality.
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
    builder.setChannelConversionAllowed(true);

    if (use_exclusive_sharing_mode_) {
      // Attempt to use Exclusive sharing mode for the lowest possible latency.
      builder.setSharingMode(oboe::SharingMode::Exclusive);
    }

    // Set the performance mode to get the lowest possible latency.
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);

    // Set callbacks for handling PCM data and other underlying API notifications.
    // To avoid an abstraction, we'll set the shared pointer with a no-op deleter.
    builder.setDataCallback(std::shared_ptr<oboe::AudioStreamDataCallback>(
      this,
      [](oboe::AudioStreamDataCallback*) {}
    ));
    builder.setErrorCallback(std::shared_ptr<oboe::AudioStreamErrorCallback>(
      this,
      [](oboe::AudioStreamErrorCallback*) {}
    ));
  }

  int32_t CreateOutputStream() {
    RTC_LOG(LS_WARNING) << "CreateOutputStream";

    // Create a builder for an Oboe output audio stream.
    oboe::AudioStreamBuilder builder;
    ConfigureCommonStreamSettings(builder, oboe::Direction::Output);

    // Specifying usage and contentType should result in better volume and routing decisions.
    // These settings are specific to the output stream.
    builder.setUsage(oboe::Usage::VoiceCommunication);
    builder.setContentType(oboe::ContentType::Speech);

    oboe::Result result = builder.openStream(output_stream_);
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "Failed to open the output stream: " << oboe::convertToText(result);
      return -1;
    }

    if (output_stream_->getAudioApi() == oboe::AudioApi::AAudio) {
      latency_tuner_ = std::make_unique<oboe::LatencyTuner>(*output_stream_.get());
      if (!latency_tuner_) {
        RTC_LOG(LS_WARNING) << "Could not create LatencyTuner, continue without one";
      }
    }

    LogStreamConfiguration(output_stream_);

    return 0;
  }

  int32_t CreateInputStream() {
    RTC_LOG(LS_WARNING) << "CreateInputStream";

    oboe::AudioStreamBuilder builder;
    ConfigureCommonStreamSettings(builder, oboe::Direction::Input);

    // If provided, attach the sessionId from the AudioManager. While not always
    // strictly necessary, it oftentimes improves audio quality and experience.
    if (audio_session_id_ != kInvalidAudioSessionId) {
      RTC_LOG(LS_WARNING) << "Setting session_id: " << audio_session_id_;
      builder.setSessionId((oboe::SessionId) audio_session_id_);
    }

    // Specifying an inputPreset should result in better volume and routing
    // decisions (and privacy).
    builder.setInputPreset(oboe::InputPreset::VoiceCommunication);

    oboe::Result result = builder.openStream(input_stream_);
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "Failed to open the input stream: " << oboe::convertToText(result);
      return -1;
    }

    LogStreamConfiguration(input_stream_);

    return 0;
  }

  SequenceChecker thread_checker_;

  const bool use_software_acoustic_echo_canceler_;
  const bool use_software_noise_suppressor_;
  const bool use_exclusive_sharing_mode_;
  const int32_t audio_session_id_;

  std::shared_ptr<oboe::AudioStream> input_stream_;
  std::shared_ptr<oboe::AudioStream> output_stream_;
  std::unique_ptr<oboe::LatencyTuner> latency_tuner_;

  webrtc::AudioTransport* audio_callback_ = nullptr;

  std::atomic<bool> initialized_ { false };

  std::atomic<bool> playout_initialized_ { false };
  std::atomic<bool> recording_initialized_ { false };

  std::atomic<bool> is_playing_ { false };
  std::atomic<bool> is_recording_ { false };

  std::atomic<uint16_t> playout_delay_ms_ { kDefaultPlayoutDelayMs };
  std::atomic<int32_t> playout_underrun_count_ { 0 };

  mutable std::atomic<uint32_t> delay_log_counter_ { 0 };
  mutable std::atomic<uint32_t> underrun_log_counter_ { 0 };
};

}  // namespace

rtc::scoped_refptr<AudioDeviceModule> CreateAudioDeviceModuleOboe(
    bool use_software_acoustic_echo_canceler,
    bool use_software_noise_suppressor,
    bool use_exclusive_sharing_mode,
    int audio_session_id) {
  RTC_LOG(LS_WARNING) << "CreateAudioDeviceModuleOboe";
  return rtc::make_ref_counted<AndroidAudioDeviceModuleOboe>(
    use_software_acoustic_echo_canceler,
    use_software_noise_suppressor,
    use_exclusive_sharing_mode,
    audio_session_id);
}

}  // namespace jni
}  // namespace webrtc
