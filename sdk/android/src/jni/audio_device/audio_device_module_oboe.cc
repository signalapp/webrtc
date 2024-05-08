/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "sdk/android/src/jni/audio_device/audio_device_module.h"

#include "api/make_ref_counted.h"
#include "api/sequence_checker.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "rtc_base/logging.h"

#include <oboe/Oboe.h>

#include <memory>
#include <utility>

namespace webrtc {
namespace jni {

namespace {

// Implements an Audio Device Manager using the Oboe C++ audio library (https://github.com/google/oboe).
class AndroidAudioDeviceModuleOboe :
    public AudioDeviceModule,
    public oboe::AudioStreamDataCallback,
    public oboe::AudioStreamErrorCallback {
 public:
  AndroidAudioDeviceModuleOboe()
      : task_queue_factory_(CreateDefaultTaskQueueFactory()) {
    RTC_LOG(LS_WARNING) << "JimX: AndroidAudioDeviceModuleOboe::AndroidAudioDeviceModuleOboe";
    thread_checker_.Detach();
  }

  ~AndroidAudioDeviceModuleOboe() override {
    RTC_LOG(LS_WARNING) << "JimX: AndroidAudioDeviceModuleOboe::~AndroidAudioDeviceModuleOboe";
  }

  // Retrieve the currently utilized audio layer
  // There is only one audio layer as far as this implementation is concerned. Use
  // kAndroidJavaAudio to make sure the default implementation isn't used.
  int32_t ActiveAudioLayer(AudioDeviceModule::AudioLayer* audioLayer) const override {
    RTC_LOG(LS_WARNING) << "JimX: ActiveAudioLayer always kAndroidJavaAudio";
    *audioLayer = kAndroidJavaAudio;
    return 0;
  }

  // Full-duplex transportation of PCM audio
  // Invoked when the VoIP Engine is initialized.
  int32_t RegisterAudioCallback(AudioTransport* audioCallback) override {
    if (audioCallback != nullptr) {
      RTC_LOG(LS_WARNING) << "JimX: RegisterAudioCallback";
    } else {
      RTC_LOG(LS_WARNING) << "JimX: RegisterAudioCallback null";
    }

    audio_callback_ = audioCallback;
    return 0;
  }

  // Main initialization and termination

  // Perform general initialization, generally when a call is going to be established,
  // but before the user should start sending and receiving audio.
  int32_t Init() override {
    RTC_LOG(LS_WARNING) << "JimX: Init, using Oboe version: " << oboe::Version::Text;
    RTC_DCHECK(thread_checker_.IsCurrent());

    initialized_ = true;

    return 0;
  }

  int32_t Terminate() override {
    RTC_LOG(LS_WARNING) << "JimX: Terminate";
    if (!initialized_) {
      return 0;
    }

    RTC_DCHECK(thread_checker_.IsCurrent());

    if (input_stream_) {
      input_stream_->close();
      input_stream_ = nullptr;
    }
    if (output_stream_) {
      output_stream_->close();
      output_stream_ = nullptr;
    }

    initialized_ = false;
    thread_checker_.Detach();

    return 0;
  }

  bool Initialized() const override {
    RTC_LOG(LS_WARNING) << "JimX: Initialized " << initialized_;
    return initialized_;
  }

  // Device enumeration
  // TODO: Figure out what to say here, that for Android this is not necessary.
  // Or that the default is used, or ...

  // This implementation only supports one playout device.
  int16_t PlayoutDevices() override {
    RTC_LOG(LS_WARNING) << "JimX: PlayoutDevices always 1";
    return 1;
  }

  // This implementation only supports one playout device.
  int16_t RecordingDevices() override {
    RTC_LOG(LS_WARNING) << "JimX: RecordingDevices always 1";
    return 1;
  }

  int32_t PlayoutDeviceName(uint16_t index,
                            char name[kAdmMaxDeviceNameSize],
                            char guid[kAdmMaxGuidSize]) override {
    RTC_LOG(LS_WARNING) << "JimX: PlayoutDeviceName (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t RecordingDeviceName(uint16_t index,
                              char name[kAdmMaxDeviceNameSize],
                              char guid[kAdmMaxGuidSize]) override {
    RTC_LOG(LS_WARNING) << "JimX: RecordingDeviceName (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  // Device selection

  int32_t SetPlayoutDevice(uint16_t index) override {
    // OK to use but it has no effect currently since device selection is done
    // using Android APIs instead.
    RTC_LOG(LS_WARNING) << "JimX: SetPlayoutDevice " << index << ", (no effect!)";
    return 0;
  }

  int32_t SetPlayoutDevice(AudioDeviceModule::WindowsDeviceType device) override {
    RTC_LOG(LS_WARNING) << "JimX: SetPlayoutDevice (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t SetRecordingDevice(uint16_t index) override {
    // OK to use but it has no effect currently since device selection is done
    // using Android APIs instead.
    RTC_LOG(LS_WARNING) << "JimX: SetRecordingDevice " << index << ", (no effect!)";
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << index << ")";
    return 0;
  }

  int32_t SetRecordingDevice(AudioDeviceModule::WindowsDeviceType device) override {
    RTC_LOG(LS_WARNING) << "JimX: SetRecordingDevice (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  // Audio transport initialization

  int32_t PlayoutIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "JimX: PlayoutIsAvailable always true";
    *available = true;
    return 0;
  }

  int32_t InitPlayout() override {
    RTC_LOG(LS_WARNING) << "JimX: InitPlayout";
    if (!initialized_) {
      return -1;
    }
    if (playout_initialized_) {
      return 0;
    }

    // Create a builder for an Oboe output audio stream.
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);

    // WebRTC is looking for signed 16-bit PCM data at 48000Hz.
    builder.setFormat(oboe::AudioFormat::I16);
    builder.setFormatConversionAllowed(true);
    builder.setSampleRate(48000);
    // TODO: Consider setSampleRateConversionQuality()

    // For communications we only want to use 1 channel (mono).
    builder.setChannelCount(oboe::ChannelCount::Mono);
    // And allow Oboe to perform conversions if necessary.
    builder.setChannelConversionAllowed(true);

    // WebRTC audio callbacks want 10ms chunks, or 480 frames at 48000Hz.
    // Let Oboe manage this.
    builder.setFramesPerDataCallback(480);

    // Attempt to use Exclusive sharing mode for the lowest possible latency.
    // TODO: We don't seem to be getting it. How can we see it in action?
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    // TODO: Consider passing the mode in from the Java layer, we might need sharing mode for Group Calls.

    // Set the performance mode to get the lowest possible latency.
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);

    // Specifying usage and contentType might result in better volume and routing decisions.
    builder.setUsage(oboe::Usage::VoiceCommunication);
    // TODO: Doesn't seem like content type is going to be useful. Check it out.
    //builder.setContentType(oboe::Usage::VoiceCommunication);

    // TODO: See if we should be calling setDeviceId() at all (after testing to see the contrary hopefully).

    // Set callbacks for handling PCM data and other underlying API notifications.
    builder.setDataCallback(this);
    builder.setErrorCallback(this);

    oboe::Result result = builder.openStream(&output_stream_);
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "JimX: Failed to open the output stream.";
      return -1;
    }

    // TODO: For now just log the parameters that we might not get. But change this to fail if we don't get what we want.
    RTC_LOG(LS_WARNING) << "JimX: output_stream_:";
    RTC_LOG(LS_WARNING) << "JimX:   audioApi: " << oboe::convertToText(output_stream_->getAudioApi());
    RTC_LOG(LS_WARNING) << "JimX:   format: " << oboe::convertToText(output_stream_->getFormat());
    RTC_LOG(LS_WARNING) << "JimX:   sampleRate: " << output_stream_->getSampleRate();
    RTC_LOG(LS_WARNING) << "JimX:   channelCount: " << output_stream_->getChannelCount();
    RTC_LOG(LS_WARNING) << "JimX:   sharingMode: " << oboe::convertToText(output_stream_->getSharingMode());

    playout_initialized_ = true;

    return 0;
  }

  bool PlayoutIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "JimX: PlayoutIsInitialized " << playout_initialized_;
    return playout_initialized_;
  }

  int32_t RecordingIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "JimX: RecordingIsAvailable always true";
    *available = true;
    return 0;
  }

  int32_t InitRecording() override {
    RTC_LOG(LS_WARNING) << "JimX: InitRecording";
    if (!initialized_) {
      return -1;
    }
    if (recording_initialized_) {
      return 0;
    }

    // Create a builder for an Oboe input audio stream.
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input);

    // WebRTC is looking for signed 16-bit PCM data at 48000Hz.
    builder.setFormat(oboe::AudioFormat::I16);
    builder.setFormatConversionAllowed(true);
    builder.setSampleRate(48000);
    // TODO: Consider setSampleRateConversionQuality()

    // For communications we only want to use 1 channel (mono).
    builder.setChannelCount(oboe::ChannelCount::Mono);
    // And allow Oboe to perform conversions if necessary.
    builder.setChannelConversionAllowed(true);

    // WebRTC audio callbacks want 10ms chunks, or 480 frames at 48000Hz.
    // Let Oboe manage this.
    builder.setFramesPerDataCallback(480);

    // Attempt to use Exclusive sharing mode for the lowest possible latency.
    // TODO: We don't seem to be getting it. How can we see it in action?
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    // TODO: Consider passing the mode in from the Java layer, we might need sharing mode for Group Calls.

    // Set the performance mode to get the lowest possible latency.
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);

    // Specifying an inputPreset might result in better volume and routing decisions (and privacy).
    builder.setInputPreset(oboe::InputPreset::VoiceCommunication);

    // TODO: See if we should be calling setDeviceId() at all (after testing to see the contrary hopefully).

    // Set callbacks for handling PCM data and other underlying API notifications.
    builder.setDataCallback(this);
    builder.setErrorCallback(this);

    oboe::Result result = builder.openStream(&input_stream_);
    if (result != oboe::Result::OK) {
      RTC_LOG(LS_ERROR) << "JimX: Failed to open the input stream.";
      return -1;
    }

    // TODO: For now just log the parameters that we might not get. But change this to fail if we don't get what we want.
    RTC_LOG(LS_WARNING) << "JimX: input_stream_:";
    RTC_LOG(LS_WARNING) << "JimX:   audioApi: " << oboe::convertToText(input_stream_->getAudioApi());
    RTC_LOG(LS_WARNING) << "JimX:   format: " << oboe::convertToText(input_stream_->getFormat());
    RTC_LOG(LS_WARNING) << "JimX:   sampleRate: " << input_stream_->getSampleRate();
    RTC_LOG(LS_WARNING) << "JimX:   channelCount: " << input_stream_->getChannelCount();
    RTC_LOG(LS_WARNING) << "JimX:   sharingMode: " << oboe::convertToText(input_stream_->getSharingMode());

    recording_initialized_ = true;

    return 0;
  }

  bool RecordingIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "JimX: RecordingIsInitialized " << recording_initialized_;
    return recording_initialized_;
  }

  // Audio transport control
  // Note: We generally choose non-blocking operations when starting/stopping streams.
  // Not that the general is_playing_ and is_recording_ flags may not be in sync with
  // the Oboe states, but they are good enough as intentions.

  int32_t StartPlayout() override {
    RTC_LOG(LS_WARNING) << "JimX: StartPlayout";
    if (!initialized_) {
      return -1;
    }
    if (!playout_initialized_) {
      return -1;
    }
    if (is_playing_) {
      return 0;
    }

    oboe::Result result = output_stream_->requestStart();
    if (result == oboe::Result::OK) {
      is_playing_ = true;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "JimX: Failed to start the output stream.";
      return -1;
    }
  }

  int32_t StopPlayout() override {
    RTC_LOG(LS_WARNING) << "JimX: StopPlayout";
    if (!initialized_) {
      return -1;
    }
    if (!is_playing_) {
      return 0;
    }

    oboe::Result result = output_stream_->requestStop();
    if (result == oboe::Result::OK) {
      is_playing_ = false;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "JimX: Failed to stop the output stream.";
      // TODO: This might mean it was stopped due to error. In which case is it an error?
      return -1;
    }
  }

  bool Playing() const override {
    RTC_LOG(LS_WARNING) << "JimX: Playing " << is_playing_;
    return is_playing_;
  }

  int32_t StartRecording() override {
    RTC_LOG(LS_WARNING) << "JimX: StartRecording";

    if (!initialized_) {
      return -1;
    }
    if (!recording_initialized_) {
      return -1;
    }
    if (is_recording_) {
      return 0;
    }

    oboe::Result result = input_stream_->requestStart();
    if (result == oboe::Result::OK) {
      is_recording_ = true;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "JimX: Failed to start the input stream.";
      return -1;
    }
  }

  int32_t StopRecording() override {
    RTC_LOG(LS_WARNING) << "JimX: StopRecording";
    if (!initialized_) {
      return -1;
    }
    if (!is_recording_) {
      return 0;
    }

    oboe::Result result = input_stream_->requestStop();
    if (result == oboe::Result::OK) {
      is_recording_ = false;
      return 0;
    } else {
      RTC_LOG(LS_ERROR) << "JimX: Failed to stop the input stream.";
      // TODO: This might mean it was stopped due to error. In which case is it an error?
      return -1;
    }
  }

  bool Recording() const override {
    RTC_LOG(LS_WARNING) << "JimX: Recording " << is_recording_;
    return is_recording_;
  }

  // Audio mixer initialization
  // Use the module initialization to indicate device readiness.

  int32_t InitSpeaker() override {
    RTC_LOG(LS_WARNING) << "JimX: InitSpeaker " << (initialized_ ? 0 : -1);
    return initialized_ ? 0 : -1;
  }

  bool SpeakerIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "JimX: SpeakerIsInitialized " << initialized_;
    return initialized_;
  }

  int32_t InitMicrophone() override {
    RTC_LOG(LS_WARNING) << "JimX: InitMicrophone " << (initialized_ ? 0 : -1);
    return initialized_ ? 0 : -1;
  }

  bool MicrophoneIsInitialized() const override {
    RTC_LOG(LS_WARNING) << "JimX: MicrophoneIsInitialized " << initialized_;
    return initialized_;
  }

  // Speaker volume controls
  // TODO: Ascertain if we need to support these or not.

  int32_t SpeakerVolumeIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "JimX: SpeakerVolumeIsAvailable always false";
    if (!initialized_) {
      return -1;
    }
    *available = false;
    return 0;
  }

  int32_t SetSpeakerVolume(uint32_t volume) override {
    RTC_LOG(LS_WARNING) << "JimX: SetSpeakerVolume always success";
    if (!initialized_) {
      return -1;
    }
    return 0;
  }

  int32_t SpeakerVolume(uint32_t* output_volume) const override {
    RTC_LOG(LS_WARNING) << "JimX: SpeakerVolume always 0";
    if (!initialized_) {
      return -1;
    }
    *output_volume = 0;
    return 0;
  }

  int32_t MaxSpeakerVolume(uint32_t* output_max_volume) const override {
    RTC_LOG(LS_WARNING) << "JimX: MaxSpeakerVolume always 0";
    if (!initialized_) {
      return -1;
    }
    *output_max_volume = 0;
    return 0;
  }

  int32_t MinSpeakerVolume(uint32_t* output_min_volume) const override {
    RTC_LOG(LS_WARNING) << "JimX: MinSpeakerVolume always 0";
    if (!initialized_) {
      return -1;
    }
    *output_min_volume = 0;
    return 0;
  }

  // Microphone volume controls
  // TODO: Ascertain if we need to support these or not.

  int32_t MicrophoneVolumeIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "JimX: MicrophoneVolumeIsAvailable always false";
    *available = false;
    return -1;
  }

  int32_t SetMicrophoneVolume(uint32_t volume) override {
    RTC_LOG(LS_WARNING) << "JimX: SetMicrophoneVolume (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t MicrophoneVolume(uint32_t* volume) const override {
    RTC_LOG(LS_WARNING) << "JimX: MicrophoneVolume (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override {
    RTC_LOG(LS_WARNING) << "JimX: MaxMicrophoneVolume (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t MinMicrophoneVolume(uint32_t* minVolume) const override {
    RTC_LOG(LS_WARNING) << "JimX: MinMicrophoneVolume (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  // Speaker mute control

  int32_t SpeakerMuteIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "JimX: SpeakerMuteIsAvailable (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t SetSpeakerMute(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: SetSpeakerMute (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t SpeakerMute(bool* enabled) const override {
    RTC_LOG(LS_WARNING) << "JimX: SpeakerMute (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  // Microphone mute control

  int32_t MicrophoneMuteIsAvailable(bool* available) override {
    RTC_LOG(LS_WARNING) << "JimX: MicrophoneMuteIsAvailable (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t SetMicrophoneMute(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: SetMicrophoneMute (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  int32_t MicrophoneMute(bool* enabled) const override {
    RTC_LOG(LS_WARNING) << "JimX: MicrophoneMute (unreachable)";
    RTC_CHECK_NOTREACHED();
  }

  // Stereo support

  // None of our models support stereo playout for communication. Speech is always captured
  // in mono and the playout device should up-mix to all applicable output emitters.
  int32_t StereoPlayoutIsAvailable(bool* available) const override {
    RTC_LOG(LS_WARNING) << "JimX: StereoPlayoutIsAvailable always false";
    *available = false;
    return 0;
  }

  // We don't expect stereo to be enabled, especially on-the-fly.
  int32_t SetStereoPlayout(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: SetStereoPlayout " << enable;
    if (enable) {
      return -1;
    }
    return 0;
  }

  int32_t StereoPlayout(bool* enabled) const override {
    RTC_LOG(LS_WARNING) << "JimX: StereoPlayout always false";
    *enabled = false;
    return 0;
  }

  // None of our models support stereo recording for communication. Speech is always
  // captured in mono.
  int32_t StereoRecordingIsAvailable(bool* available) const override {
    RTC_LOG(LS_WARNING) << "JimX: StereoRecordingIsAvailable always false";
    *available = false;
    return 0;
  }

  // We don't expect stereo to be enabled, especially on-the-fly.
  int32_t SetStereoRecording(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: SetStereoRecording " << enable;
    if (enable) {
      return -1;
    }
    return 0;
  }

  int32_t StereoRecording(bool* enabled) const override {
    RTC_LOG(LS_WARNING) << "JimX: StereoRecording always false";
    *enabled = false;
    return 0;
  }

  // Playout delay calculation using Oboe.
  int32_t PlayoutDelay(uint16_t* delay_ms) const override {
    // Set a default value for latency.
    // TODO: Define this as a properly named constant.
    *delay_ms = 150;

    if (!is_playing_) {
      return -1;
    }

    auto latencyResult = output_stream_->calculateLatencyMillis();
    if (latencyResult) {
      *delay_ms = static_cast<uint16_t>(
        std::clamp(latencyResult.value(), static_cast<double>(0), static_cast<double>(UINT16_MAX)));
    }

    // Note: Disabling to reduce log noise.
    RTC_LOG(LS_WARNING) << "JimX: PlayoutDelay " << *delay_ms;
    return 0;
  }

  // Only supported on Android.
  // TODO: Figure out this section.

  // Returns true if the device both supports built in AEC and the device
  // is not blocklisted.
  // Currently, if OpenSL ES is used in both directions, this method will still
  // report the correct value and it has the correct effect. As an example:
  // a device supports built in AEC and this method returns true. Libjingle
  // will then disable the WebRTC based AEC and that will work for all devices
  // (mainly Nexus) even when OpenSL ES is used for input since our current
  // implementation will enable built-in AEC by default also for OpenSL ES.
  // The only "bad" thing that happens today is that when Libjingle calls
  // OpenSLESRecorder::EnableBuiltInAEC() it will not have any real effect and
  // a "Not Implemented" log will be filed. This non-perfect state will remain
  // until I have added full support for audio effects based on OpenSL ES APIs.
  bool BuiltInAECIsAvailable() const override {
    if (!initialized_)
      return false;

//    bool isAvailable = input_->IsAcousticEchoCancelerSupported();
    bool isAvailable = true;
    RTC_LOG(LS_WARNING) << "JimX: BuiltInAECIsAvailable " << isAvailable;
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return isAvailable;
  }

  // Not implemented for any input device on Android.
  bool BuiltInAGCIsAvailable() const override {
    RTC_LOG(LS_WARNING) << "JimX: BuiltInAGCIsAvailable always false";
    RTC_DLOG(LS_INFO) << "output: " << false;
    return false;
  }

  // Returns true if the device both supports built in NS and the device
  // is not blocklisted.
  // TODO(henrika): add implementation for OpenSL ES based audio as well.
  // In addition, see comments for BuiltInAECIsAvailable().
  bool BuiltInNSIsAvailable() const override {
    if (!initialized_)
      return false;

//    bool isAvailable = input_->IsNoiseSuppressorSupported();
    bool isAvailable = true;
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    RTC_LOG(LS_WARNING) << "JimX: BuiltInNSIsAvailable " << isAvailable;
    return isAvailable;
  }

  // Enables the built-in audio effects. Only supported on Android.
  // TODO: Figure out this section.

  // TODO(henrika): add implementation for OpenSL ES based audio as well.
  int32_t EnableBuiltInAEC(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: EnableBuiltInAEC " << enable;
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    if (!initialized_)
      return -1;
    RTC_CHECK(BuiltInAECIsAvailable()) << "HW AEC is not available";

//    int32_t result = input_->EnableBuiltInAEC(enable);
    int32_t result = 0;

    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_LOG(LS_WARNING) << "JimX: EnableBuiltInAEC result " << result;
    return result;
  }

  int32_t EnableBuiltInAGC(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: EnableBuiltInAGC " << enable << ", (unreachable)";
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    RTC_CHECK_NOTREACHED();
  }

  // TODO(henrika): add implementation for OpenSL ES based audio as well.
  int32_t EnableBuiltInNS(bool enable) override {
    RTC_LOG(LS_WARNING) << "JimX: EnableBuiltInNS " << enable;
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    if (!initialized_)
      return -1;
    RTC_CHECK(BuiltInNSIsAvailable()) << "HW NS is not available";

//    int32_t result = input_->EnableBuiltInNS(enable);
    int32_t result = 0;

    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_LOG(LS_WARNING) << "JimX: EnableBuiltInNS result " << result;
    return result;
  }

  // Playout underrun count. Only supported on Android.
  int32_t GetPlayoutUnderrunCount() const override {
    if (!is_playing_) {
      return -1;
    }

    int32_t count = -1;

    // TODO: Is the isXRunCountSupported() check really necessary? Probably not.
    if (output_stream_->isXRunCountSupported()) {
      auto countResult = output_stream_->getXRunCount();
      if (countResult) {
        count = countResult.value();
      }
    }

    // Note: Cutting down on noise, only show if non-zero.
    if (count != 0) {
      RTC_LOG(LS_WARNING) << "JimX: GetPlayoutUnderrunCount " << count;
    }
    return count;
  }

  // Used to generate RTC stats. For now, return absl::nullopt so that it won't be present
  // in the stats.
  absl::optional<Stats> GetStats() const override {
    // Note: Disabling to reduce log noise.
    //RTC_LOG(LS_WARNING) << "JimX: GetStats";
    return absl::nullopt;
  }

  // Oboe Callbacks

  bool onError(oboe::AudioStream *audioStream, oboe::Result error) override {
    if (audioStream == output_stream_) {
      RTC_LOG(LS_WARNING) << "JimX: onError on output stream: " << oboe::convertToText(error);
    } else if (audioStream == input_stream_) {
      RTC_LOG(LS_WARNING) << "JimX: onError on input stream: " << oboe::convertToText(error);
    } else {
      RTC_LOG(LS_WARNING) << "JimX: onError on unknown stream: " << oboe::convertToText(error);
    }

    // TODO: Return true if the stream has been stopped and closed, false if not.
    return false;
  }

  void onErrorBeforeClose(oboe::AudioStream *audioStream, oboe::Result error) override {
    if (audioStream == output_stream_) {
      RTC_LOG(LS_WARNING) << "JimX: onErrorBeforeClose on output stream: " << oboe::convertToText(error);
    } else if (audioStream == input_stream_) {
      RTC_LOG(LS_WARNING) << "JimX: onErrorBeforeClose on input stream: " << oboe::convertToText(error);
    } else {
      RTC_LOG(LS_WARNING) << "JimX: onErrorBeforeClose on unknown stream: " << oboe::convertToText(error);
    }
  }

  void onErrorAfterClose(oboe::AudioStream *audioStream, oboe::Result error) override {
    if (audioStream == output_stream_) {
      RTC_LOG(LS_WARNING) << "JimX: onErrorAfterClose on output stream: " << oboe::convertToText(error);
    } else if (audioStream == input_stream_) {
      RTC_LOG(LS_WARNING) << "JimX: onErrorAfterClose on input stream: " << oboe::convertToText(error);
    } else {
      RTC_LOG(LS_WARNING) << "JimX: onErrorAfterClose on unknown stream: " << oboe::convertToText(error);
    }
  }

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) override {
    if (audio_callback_ != nullptr) {
      if (audioStream == output_stream_) {
          size_t play_channels = audioStream->getChannelCount();
          size_t total_samples = play_channels * numFrames;

          // This allocation is temporary. Switch to a pre-allocated model.
          int16_t* temp_buffer = (int16_t*)malloc(total_samples * sizeof(int16_t));

          // audio_device_buffer.cc sets these to -1, maybe to indicate not used?
          int64_t elapsed_time_ms = -1;
          int64_t ntp_time_ms = -1;

          size_t num_samples_out(0);
          const size_t bytes_per_frame = play_channels * sizeof(int16_t);

          // also, this returns int32_t... handle it
          audio_callback_->NeedMorePlayData(
            numFrames,
            bytes_per_frame,
            play_channels,
            audioStream->getSampleRate(),
            &temp_buffer[0],
            num_samples_out,
            &elapsed_time_ms,
            &ntp_time_ms);

          memcpy(audioData, &temp_buffer[0], numFrames * bytes_per_frame);
          free(temp_buffer);
      } else if (audioStream == input_stream_) {
          int numChannels = audioStream->getChannelCount();
          int numSamples = numFrames * numChannels;

          int16_t tempBuffer[numSamples];
          memcpy(tempBuffer, audioData, numFrames * numChannels * sizeof(int16_t));

          uint32_t new_mic_level = 0;

          // also, this returns int32_t... handle it
          audio_callback_->RecordedDataIsAvailable(
            tempBuffer,
            numSamples,
            numChannels * sizeof(int16_t) * 8,
            numChannels,
            audioStream->getSampleRate(),
            0,
            0,
            0,
            false,
            new_mic_level);
      }
    } else {
      // TODO: Implement default handling (like passing output buffer to 0)? Or an error for the DataCallbackResult?
    }
    return oboe::DataCallbackResult::Continue;
  }

 private:
  SequenceChecker thread_checker_;
  const std::unique_ptr<TaskQueueFactory> task_queue_factory_;

  oboe::AudioStream *input_stream_ = nullptr;
  oboe::AudioStream *output_stream_ = nullptr;

  webrtc::AudioTransport* audio_callback_ = nullptr;

  bool initialized_ = false;

  bool playout_initialized_ = false;
  bool recording_initialized_ = false;

  bool is_playing_ = false;
  bool is_recording_ = false;
};

}  // namespace

rtc::scoped_refptr<AudioDeviceModule> CreateAudioDeviceModuleOboe(
    AudioDeviceModule::AudioLayer audio_layer,
    bool is_stereo_playout_supported,
    bool is_stereo_record_supported,
    uint16_t playout_delay_ms) {
  RTC_LOG(LS_WARNING) << "JimX: CreateAudioDeviceModuleOboe";
  RTC_LOG(LS_WARNING) << "JimX:   audio_layer " << audio_layer;
  RTC_LOG(LS_WARNING) << "JimX:   is_stereo_playout_supported " << is_stereo_playout_supported;
  RTC_LOG(LS_WARNING) << "JimX:   is_stereo_record_supported " << is_stereo_record_supported;
  RTC_LOG(LS_WARNING) << "JimX:   playout_delay_ms " << playout_delay_ms;
  return rtc::make_ref_counted<AndroidAudioDeviceModuleOboe>();
}

}  // namespace jni
}  // namespace webrtc
