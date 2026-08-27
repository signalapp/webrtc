/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_AUDIO_DEVICE_INTF_H__
#define RFFI_API_AUDIO_DEVICE_INTF_H__

#include <cstdint>

#include "rffi/api/rffi_defs.h"

/**
 * Rust friendly wrapper for creating objects that implement the
 * AudioDevice interface.
 */
constexpr int kRffiAdmMaxDeviceNameSize = 128;
constexpr int kRffiAdmMaxGuidSize = 128;

// Here and in the next method signature, audio_transport_ptr_ptr is the
// uintptr_t representation of a pointer to a field in the AudioDevice class
// that in turn is a pointer to the AudioTransport class where data will flow
// to and from these callbacks.
//
// Specifically, it is a `std::atomic<AudioTransport*>*` converted to uintptr_t.
//
// Why a uintptr_t? Because the rust layer does not need to use this as a
// pointer, just to hold onto it and pass it back to the RFFI. If we passed it
// as a pointer, the fact that in Rust, pointers are not Send or Sync would
// make it painful to pass this value to the rust audio callbacks.
//
// Why a pointer to a pointer? So that the RingRTC ADM only needs to set the
// pointer once, at initialization time, rather than also needing to handle
// updates to it later. This way, the C++ layer can handle any updates without
// the FFI call.
RUSTEXPORT int32_t
Rust_recordedDataIsAvailable(uintptr_t audio_transport_ptr_ptr,
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

RUSTEXPORT int32_t Rust_needMorePlayData(uintptr_t audio_transport_ptr_ptr,
                                         size_t n_samples,
                                         size_t n_bytes_per_sample,
                                         size_t n_channels,
                                         uint32_t samples_per_sec,
                                         void* audio_samples,
                                         size_t* n_samples_out,
                                         int64_t* elapsed_time_ms,
                                         int64_t* ntp_time_ms);

typedef struct {
  // This method is effectively unimplemented, so we do not need to include
  // the AudioLayer enum in the API.
  // TODO: Delete this method and the other unimplemented ones.
  int32_t (*activeAudioLayer)(void* adm_borrowed, void* audio_layer);
  // Main initialization and termination
  int32_t (*init)(void* adm_borrowed, uintptr_t audio_transport_ptr_ptr);
  int32_t (*terminate)(void* adm_borrowed);
  bool (*initialized)(void* adm_borrowed);

  // Device enumeration
  int16_t (*playoutDevices)(void* adm_borrowed);
  int16_t (*recordingDevices)(void* adm_borrowed);
  int32_t (*playoutDeviceName)(void* adm_borrowed,
                               uint16_t index,
                               char name[kRffiAdmMaxDeviceNameSize],
                               char guid[kRffiAdmMaxGuidSize]);
  int32_t (*recordingDeviceName)(void* adm_borrowed,
                                 uint16_t index,
                                 char name[kRffiAdmMaxDeviceNameSize],
                                 char guid[kRffiAdmMaxGuidSize]);

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
