/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_AUDIO_DEVICE_INTF_H__
#define RFFI_API_AUDIO_DEVICE_INTF_H__

#include <cstdint>

#include "rffi/api/rffi_defs.h"

using webrtc::rffi::ptr::Borrowed;

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
  int32_t (*activeAudioLayer)(Borrowed<void> adm_borrowed,
                              Borrowed<void> audio_layer);
  // Main initialization and termination
  int32_t (*init)(Borrowed<void> adm_borrowed,
                  uintptr_t audio_transport_ptr_ptr);
  int32_t (*terminate)(Borrowed<void> adm_borrowed);
  bool (*initialized)(Borrowed<void> adm_borrowed);

  // Device enumeration
  int16_t (*playoutDevices)(Borrowed<void> adm_borrowed);
  int16_t (*recordingDevices)(Borrowed<void> adm_borrowed);
  int32_t (*playoutDeviceName)(Borrowed<void> adm_borrowed,
                               uint16_t index,
                               Borrowed<char> name,
                               Borrowed<char> guid);
  int32_t (*recordingDeviceName)(Borrowed<void> adm_borrowed,
                                 uint16_t index,
                                 Borrowed<char> name,
                                 Borrowed<char> guid);

  // Audio transport initialization
  int32_t (*playoutIsAvailable)(Borrowed<void> adm_borrowed,
                                Borrowed<bool> available);
  int32_t (*initPlayout)(Borrowed<void> adm_borrowed);
  bool (*playoutIsInitialized)(Borrowed<void> adm_borrowed);
  int32_t (*recordingIsAvailable)(Borrowed<void> adm_borrowed,
                                  Borrowed<bool> available);
  int32_t (*initRecording)(Borrowed<void> adm_borrowed);
  bool (*recordingIsInitialized)(Borrowed<void> adm_borrowed);

  // Audio transport control
  int32_t (*startPlayout)(Borrowed<void> adm_borrowed);
  int32_t (*stopPlayout)(Borrowed<void> adm_borrowed);
  bool (*playing)(Borrowed<void> adm_borrowed);
  int32_t (*startRecording)(Borrowed<void> adm_borrowed);
  int32_t (*stopRecording)(Borrowed<void> adm_borrowed);
  bool (*recording)(Borrowed<void> adm_borrowed);

  // Audio mixer initialization
  int32_t (*initSpeaker)(Borrowed<void> adm_borrowed);
  bool (*speakerIsInitialized)(Borrowed<void> adm_borrowed);
  int32_t (*initMicrophone)(Borrowed<void> adm_borrowed);
  bool (*microphoneIsInitialized)(Borrowed<void> adm_borrowed);

  // Speaker volume controls
  int32_t (*speakerVolumeIsAvailable)(Borrowed<void> adm_borrowed,
                                      Borrowed<bool> available);
  int32_t (*setSpeakerVolume)(Borrowed<void> adm_borrowed, uint32_t volume);
  int32_t (*speakerVolume)(Borrowed<void> adm_borrowed,
                           Borrowed<uint32_t> volume);
  int32_t (*maxSpeakerVolume)(Borrowed<void> adm_borrowed,
                              Borrowed<uint32_t> max_volume);
  int32_t (*minSpeakerVolume)(Borrowed<void> adm_borrowed,
                              Borrowed<uint32_t> min_volume);

  // Microphone volume controls
  int32_t (*microphoneVolumeIsAvailable)(Borrowed<void> adm_borrowed,
                                         Borrowed<bool> available);
  int32_t (*setMicrophoneVolume)(Borrowed<void> adm_borrowed, uint32_t volume);
  int32_t (*microphoneVolume)(Borrowed<void> adm_borrowed,
                              Borrowed<uint32_t> volume);
  int32_t (*maxMicrophoneVolume)(Borrowed<void> adm_borrowed,
                                 Borrowed<uint32_t> max_volume);
  int32_t (*minMicrophoneVolume)(Borrowed<void> adm_borrowed,
                                 Borrowed<uint32_t> min_volume);

  // Speaker mute control
  int32_t (*speakerMuteIsAvailable)(Borrowed<void> adm_borrowed,
                                    Borrowed<bool> available);
  int32_t (*setSpeakerMute)(Borrowed<void> adm_borrowed, bool enable);
  int32_t (*speakerMute)(Borrowed<void> adm_borrowed, Borrowed<bool> enabled);

  // Microphone mute control
  int32_t (*microphoneMuteIsAvailable)(Borrowed<void> adm_borrowed,
                                       Borrowed<bool> available);
  int32_t (*setMicrophoneMute)(Borrowed<void> adm_borrowed, bool enable);
  int32_t (*microphoneMute)(Borrowed<void> adm_borrowed,
                            Borrowed<bool> enabled);

  // Stereo support
  int32_t (*stereoPlayoutIsAvailable)(Borrowed<void> adm_borrowed,
                                      Borrowed<bool> available);
  int32_t (*setStereoPlayout)(Borrowed<void> adm_borrowed, bool enable);
  int32_t (*stereoPlayout)(Borrowed<void> adm_borrowed, Borrowed<bool> enabled);
  int32_t (*stereoRecordingIsAvailable)(Borrowed<void> adm_borrowed,
                                        Borrowed<bool> available);
  int32_t (*setStereoRecording)(Borrowed<void> adm_borrowed, bool enable);
  int32_t (*stereoRecording)(Borrowed<void> adm_borrowed,
                             Borrowed<bool> enabled);

  // Playout delay
  int32_t (*playoutDelay)(Borrowed<void> adm_borrowed,
                          Borrowed<uint16_t> delayMS);
} AudioDeviceCallbacks;

#endif  // RFFI_API_AUDIO_DEVICE_INTF_H__
