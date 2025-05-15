/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_AUDIO_DEVICE_INTF_H__
#define RFFI_API_AUDIO_DEVICE_INTF_H__

#include <cstdint>

#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/include/audio_device_defines.h"
#include "rffi/api/rffi_defs.h"

/**
 * Rust friendly wrapper for creating objects that implement the
 * AudioDevice interface.
 */

RUSTEXPORT int32_t
Rust_recordedDataIsAvailable(webrtc::AudioTransport* audio_callback,
                             const void* audio_samples,
                             size_t n_samples,
                             size_t n_bytes_per_sample,
                             size_t n_channels,
                             uint32_t samples_per_sec,
                             uint32_t total_delay_ms,
                             int32_t clock_drift,
                             uint32_t current_mic_level,
                             bool key_pressed,
                             uint32_t* new_mic_level,
                             int64_t estimated_capture_time_ns);

RUSTEXPORT int32_t Rust_needMorePlayData(webrtc::AudioTransport* audio_callback,
                                         size_t n_samples,
                                         size_t n_bytes_per_sample,
                                         size_t n_channels,
                                         uint32_t samples_per_sec,
                                         void* audio_samples,
                                         size_t* n_samples_out,
                                         int64_t* elapsed_time_ms,
                                         int64_t* ntp_time_ms);

typedef struct {
  int32_t (*activeAudioLayer)(
      void* adm_borrowed,
      webrtc::AudioDeviceModule::AudioLayer* audio_layer);
  int32_t (*registerAudioCallback)(void* adm_borrowed,
                                   webrtc::AudioTransport* audio_callback);
  // Main initialization and termination
  int32_t (*init)(void* adm_borrowed);
  int32_t (*terminate)(void* adm_borrowed);
  bool (*initialized)(void* adm_borrowed);

  // Device enumeration
  int16_t (*playoutDevices)(void* adm_borrowed);
  int16_t (*recordingDevices)(void* adm_borrowed);
  int32_t (*playoutDeviceName)(void* adm_borrowed,
                               uint16_t index,
                               char name[webrtc::kAdmMaxDeviceNameSize],
                               char guid[webrtc::kAdmMaxGuidSize]);
  int32_t (*recordingDeviceName)(void* adm_borrowed,
                                 uint16_t index,
                                 char name[webrtc::kAdmMaxDeviceNameSize],
                                 char guid[webrtc::kAdmMaxGuidSize]);

  // Device selection
  int32_t (*setPlayoutDevice)(void* adm_borrowed, uint16_t index);
  int32_t (*setPlayoutDeviceWin)(
      void* adm_borrowed,
      webrtc::AudioDeviceModule::WindowsDeviceType device);
  int32_t (*setRecordingDevice)(void* adm_borrowed, uint16_t index);
  int32_t (*setRecordingDeviceWin)(
      void* adm_borrowed,
      webrtc::AudioDeviceModule::WindowsDeviceType device);

  // Audio transport initialization
  int32_t (*playoutIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*initPlayout)(void* adm_borrowed);
  bool (*playoutIsInitialized)(void* adm_borrowed);
  int32_t (*recordingIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*initRecording)(void* adm_borrowed);
  bool (*recordingIsInitialized)(void* adm_borrowed);

  // Audio transport control
  int32_t (*startPlayout)(void* adm_borrowed);
  int32_t (*stopPlayout)(void* adm_borrowed);
  bool (*playing)(void* adm_borrowed);
  int32_t (*startRecording)(void* adm_borrowed);
  int32_t (*stopRecording)(void* adm_borrowed);
  bool (*recording)(void* adm_borrowed);

  // Audio mixer initialization
  int32_t (*initSpeaker)(void* adm_borrowed);
  bool (*speakerIsInitialized)(void* adm_borrowed);
  int32_t (*initMicrophone)(void* adm_borrowed);
  bool (*microphoneIsInitialized)(void* adm_borrowed);

  // Speaker volume controls
  int32_t (*speakerVolumeIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*setSpeakerVolume)(void* adm_borrowed, uint32_t volume);
  int32_t (*speakerVolume)(void* adm_borrowed, uint32_t* volume);
  int32_t (*maxSpeakerVolume)(void* adm_borrowed, uint32_t* max_volume);
  int32_t (*minSpeakerVolume)(void* adm_borrowed, uint32_t* min_volume);

  // Microphone volume controls
  int32_t (*microphoneVolumeIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*setMicrophoneVolume)(void* adm_borrowed, uint32_t volume);
  int32_t (*microphoneVolume)(void* adm_borrowed, uint32_t* volume);
  int32_t (*maxMicrophoneVolume)(void* adm_borrowed, uint32_t* max_volume);
  int32_t (*minMicrophoneVolume)(void* adm_borrowed, uint32_t* min_volume);

  // Speaker mute control
  int32_t (*speakerMuteIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*setSpeakerMute)(void* adm_borrowed, bool enable);
  int32_t (*speakerMute)(void* adm_borrowed, bool* enabled);

  // Microphone mute control
  int32_t (*microphoneMuteIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*setMicrophoneMute)(void* adm_borrowed, bool enable);
  int32_t (*microphoneMute)(void* adm_borrowed, bool* enabled);

  // Stereo support
  int32_t (*stereoPlayoutIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*setStereoPlayout)(void* adm_borrowed, bool enable);
  int32_t (*stereoPlayout)(void* adm_borrowed, bool* enabled);
  int32_t (*stereoRecordingIsAvailable)(void* adm_borrowed, bool* available);
  int32_t (*setStereoRecording)(void* adm_borrowed, bool enable);
  int32_t (*stereoRecording)(void* adm_borrowed, bool* enabled);

  // Playout delay
  int32_t (*playoutDelay)(void* adm_borrowed, uint16_t* delayMS);
} AudioDeviceCallbacks;

#endif  // RFFI_API_AUDIO_DEVICE_INTF_H__
