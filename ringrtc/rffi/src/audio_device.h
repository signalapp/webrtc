/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_AUDIO_DEVICE_H__
#define RFFI_AUDIO_DEVICE_H__

#include <cstdint>

#include "api/audio/audio_device.h"
#include "api/sequence_checker.h"
#include "rffi/api/audio_device_intf.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {
namespace rffi {

/**
 * RingRTC-specific ADM implementation, which forwards to Rust layer.
 */
class RingRTCAudioDeviceModule : public AudioDeviceModule {
 public:
  ~RingRTCAudioDeviceModule() override;

  // Creates an ADM for usage in production code.
  static scoped_refptr<RingRTCAudioDeviceModule> Create(
      void* adm_borrowed,
      const AudioDeviceCallbacks* callbacks);

  // Retrieve the currently utilized audio layer
  int32_t ActiveAudioLayer(AudioLayer* audio_layer) const override;

  // Full-duplex transportation of PCM audio
  // Note that, as with all other functions in this interface, this must be
  // called on the thread that initialized the object.
  // It also may not be called during recording or playback (as determined by
  // the rust layer's Playing and Recording functions)
  int32_t RegisterAudioCallback(AudioTransport* audio_callback) override;

  // RingRTC function that forwards to the underlying `callback_` to send
  // input data, if a callback has been specified.
  // Used to allow rust code to invoke callback.
  int32_t RecordedDataIsAvailable(
      const void* audio_samples,
      size_t n_samples,
      size_t n_bytes_per_sample,
      size_t n_channels,
      uint32_t samples_per_sec,
      uint32_t total_delay_ms,
      int32_t clock_drift,
      uint32_t current_mic_level,
      bool key_pressed,
      uint32_t& new_mic_level,
      std::optional<int64_t> estimated_capture_time_ns);

  // RingRTC function that forwards to the underlying `callback_` to receive
  // output data, if a callback has been specified.
  // Used to allow rust code to invoke callback.
  int32_t NeedMorePlayData(size_t n_samples,
                           size_t n_bytes_per_sample,
                           size_t n_channels,
                           uint32_t samples_per_sec,
                           void* audio_samples,
                           size_t& n_samples_out,
                           int64_t* elapsed_time_ms,
                           int64_t* ntp_time_ms);

  // Main initialization and termination
  int32_t Init() override;
  // Final so that calling from destructor is safe
  int32_t Terminate() override final;
  bool Initialized() const override;

  // Device enumeration
  int16_t PlayoutDevices() override;
  int16_t RecordingDevices() override;
  int32_t PlayoutDeviceName(uint16_t index,
                            char name[kAdmMaxDeviceNameSize],
                            char guid[kAdmMaxGuidSize]) override;
  int32_t RecordingDeviceName(uint16_t index,
                              char name[kAdmMaxDeviceNameSize],
                              char guid[kAdmMaxGuidSize]) override;

  // Device selection
  int32_t SetPlayoutDevice(uint16_t index) override;
  int32_t SetPlayoutDevice(WindowsDeviceType device) override;
  int32_t SetRecordingDevice(uint16_t index) override;
  int32_t SetRecordingDevice(WindowsDeviceType device) override;

  // Audio transport initialization
  int32_t PlayoutIsAvailable(bool* available) override;
  int32_t InitPlayout() override;
  bool PlayoutIsInitialized() const override;
  int32_t RecordingIsAvailable(bool* available) override;
  int32_t InitRecording() override;
  bool RecordingIsInitialized() const override;

  // Audio transport control
  int32_t StartPlayout() override;
  int32_t StopPlayout() override;
  bool Playing() const override;
  int32_t StartRecording() override;
  int32_t StopRecording() override;
  bool Recording() const override;

  // Audio mixer initialization
  int32_t InitSpeaker() override;
  bool SpeakerIsInitialized() const override;
  int32_t InitMicrophone() override;
  bool MicrophoneIsInitialized() const override;

  // Speaker volume controls
  int32_t SpeakerVolumeIsAvailable(bool* available) override;
  int32_t SetSpeakerVolume(uint32_t volume) override;
  int32_t SpeakerVolume(uint32_t* volume) const override;
  int32_t MaxSpeakerVolume(uint32_t* max_volume) const override;
  int32_t MinSpeakerVolume(uint32_t* min_volume) const override;

  // Microphone volume controls
  int32_t MicrophoneVolumeIsAvailable(bool* available) override;
  int32_t SetMicrophoneVolume(uint32_t volume) override;
  int32_t MicrophoneVolume(uint32_t* volume) const override;
  int32_t MaxMicrophoneVolume(uint32_t* max_volume) const override;
  int32_t MinMicrophoneVolume(uint32_t* min_volume) const override;

  // Speaker mute control
  int32_t SpeakerMuteIsAvailable(bool* available) override;
  int32_t SetSpeakerMute(bool enable) override;
  int32_t SpeakerMute(bool* enabled) const override;

  // Microphone mute control
  int32_t MicrophoneMuteIsAvailable(bool* available) override;
  int32_t SetMicrophoneMute(bool enable) override;
  int32_t MicrophoneMute(bool* enabled) const override;

  // Stereo support
  int32_t StereoPlayoutIsAvailable(bool* available) const override;
  int32_t SetStereoPlayout(bool enable) override;
  int32_t StereoPlayout(bool* enabled) const override;
  int32_t StereoRecordingIsAvailable(bool* available) const override;
  int32_t SetStereoRecording(bool enable) override;
  int32_t StereoRecording(bool* enabled) const override;

  // Playout delay
  int32_t PlayoutDelay(uint16_t* delayMS) const override;

  // Only supported on Android.
  bool BuiltInAECIsAvailable() const override { return false; }
  bool BuiltInAGCIsAvailable() const override { return false; }
  bool BuiltInNSIsAvailable() const override { return false; }

  // Enables the built-in audio effects. Only supported on Android.
  int32_t EnableBuiltInAEC(bool enable) override { return -1; }
  int32_t EnableBuiltInAGC(bool enable) override { return -1; }
  int32_t EnableBuiltInNS(bool enable) override { return -1; }

  // Play underrun count. Only supported on Android.
  int32_t GetPlayoutUnderrunCount() const override { return -1; }

  // Used to generate RTC stats. If not implemented, RTCAudioPlayoutStats will
  // not be present in the stats.
  std::optional<Stats> GetStats() const override { return std::nullopt; }

// Only supported on iOS.
#if defined(WEBRTC_IOS)
  int GetPlayoutAudioParameters(AudioParameters* params) const override {
    return -1;
  }
  int GetRecordAudioParameters(AudioParameters* params) const override {
    return -1;
  }
#endif  // WEBRTC_IOS
 protected:
  RingRTCAudioDeviceModule(void* adm_borrowed,
                           const AudioDeviceCallbacks* callbacks);

 private:
  // Ensures that the class is used on the same thread as it is constructed
  // and destroyed on.
  SequenceChecker thread_checker_;

  void* adm_borrowed_ RTC_GUARDED_BY(&thread_checker_) = nullptr;
  AudioDeviceCallbacks rust_callbacks_ RTC_GUARDED_BY(&thread_checker_);
};

}  // namespace rffi
}  // namespace webrtc

#endif  // RFFI_AUDIO_DEVICE_H__
