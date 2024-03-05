/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_PEER_CONNECTION_FACTORY_H__
#define RFFI_API_PEER_CONNECTION_FACTORY_H__

#include "rffi/api/peer_connection_intf.h"

#include "rffi/api/injectable_network.h"
#include "rtc_base/ref_count.h"

namespace rtc {
  class RTCCertificite;
}

namespace webrtc {
  class PeerConnectionInterface;
  class PeerConnectionFactoryInterface;
  class AudioSourceInterface;
  class AudioTrackInterface;
  class AudioDeviceModule;

  // This little indirection is needed so that we can have something
  // that owns the signaling thread (and other threads).
  // We could make our owner implement the PeerConnectionFactoryInterface,
  // but it's not worth the trouble.  This is easier.
  class PeerConnectionFactoryOwner : public rtc::RefCountInterface {
    public:
    virtual ~PeerConnectionFactoryOwner() {}
    virtual PeerConnectionFactoryInterface* peer_connection_factory() = 0;
    // If we are using an injectable network, this is it.
    virtual rffi::InjectableNetwork* injectable_network() {
      return nullptr;
    }
    virtual int16_t AudioPlayoutDevices() {
      return 0;
    }
    virtual int32_t AudioPlayoutDeviceName(uint16_t index, char* name_out, char* uuid_out) {
      return -1;
    }
    virtual bool SetAudioPlayoutDevice(uint16_t index) {
      return false;
    }
    virtual int16_t AudioRecordingDevices() {
      return 0;
    }
    virtual int32_t AudioRecordingDeviceName(uint16_t index, char* name_out, char* uuid_out) {
      return -1;
    }
    virtual bool SetAudioRecordingDevice(uint16_t index) {
      return false;
    }
  };

  namespace rffi {
    class PeerConnectionObserverRffi;
  }
}

typedef struct {
  const char* username_borrowed;
  const char* password_borrowed;
  const char* hostname_borrowed;
  const char** urls_borrowed;
  size_t urls_size;
} RffiIceServer;

typedef struct {
  const RffiIceServer* servers;
  size_t servers_size;
} RffiIceServers;

enum class RffiPeerConnectionKind: uint8_t {
  kDirect,
  kRelayed,
  kGroupCall,
};

enum RffiAudioDeviceModuleType {
  kRffiAudioDeviceModuleDefault = 0,
  kRffiAudioDeviceModuleFile = 1,
};

typedef struct {
  RffiAudioDeviceModuleType audio_device_module_type;
  const char* input_file_borrowed;
  const char* output_file_borrowed;
  bool high_pass_filter_enabled;
  bool aec_enabled;
  bool ns_enabled;
  bool agc_enabled;
} RffiAudioConfig;

typedef struct {
  int32_t max_packets;
  int32_t min_delay_ms;
  int32_t max_target_delay_ms;
  bool fast_accelerate;
} RffiAudioJitterBufferConfig;

// Returns an owned RC.
// You can create more than one, but you should probably only have one unless
// you want to test separate endpoints that are as independent as possible.
RUSTEXPORT webrtc::PeerConnectionFactoryOwner* Rust_createPeerConnectionFactory(
  const RffiAudioConfig* audio_config_borrowed,
  bool use_injectable_network);

// Returns an owned RC.
RUSTEXPORT webrtc::PeerConnectionFactoryOwner* Rust_createPeerConnectionFactoryWrapper(
  webrtc::PeerConnectionFactoryInterface* factory_borrowed_rc);

// Returns a borrowed pointer.
RUSTEXPORT webrtc::rffi::InjectableNetwork* Rust_getInjectableNetwork(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc);

// Returns an owned RC.
RUSTEXPORT webrtc::PeerConnectionInterface* Rust_createPeerConnection(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
  webrtc::rffi::PeerConnectionObserverRffi* observer_borrowed,
  RffiPeerConnectionKind kind,
  const RffiAudioJitterBufferConfig* audio_jitter_buffer_config_borrowed,
  int32_t audio_rtcp_report_interval_ms,
  const RffiIceServers* ice_servers_borrowed,
  webrtc::AudioTrackInterface* outgoing_audio_track_borrowed_rc,
  webrtc::VideoTrackInterface* outgoing_video_track_borrowed_rc);

// Returns an owned RC.
RUSTEXPORT webrtc::AudioTrackInterface* Rust_createAudioTrack(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc);

// Returns an owned RC.
RUSTEXPORT webrtc::rffi::VideoSource* Rust_createVideoSource();

// Returns an owned RC.
RUSTEXPORT webrtc::VideoTrackInterface* Rust_createVideoTrack(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
  webrtc::VideoTrackSourceInterface* source_borrowed_rc);

RUSTEXPORT int16_t Rust_getAudioPlayoutDevices(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc);

RUSTEXPORT int32_t Rust_getAudioPlayoutDeviceName(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc, 
  uint16_t index, 
  char* name_out, 
  char* uuid_out);

RUSTEXPORT bool Rust_setAudioPlayoutDevice(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
  uint16_t index);

RUSTEXPORT int16_t Rust_getAudioRecordingDevices(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc);

RUSTEXPORT int32_t Rust_getAudioRecordingDeviceName(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc, 
  uint16_t index, 
  char* name_out, 
  char* uuid_out);

RUSTEXPORT bool Rust_setAudioRecordingDevice(
  webrtc::PeerConnectionFactoryOwner* factory_owner_borrowed_rc, 
  uint16_t index);

#endif /* RFFI_API_PEER_CONNECTION_FACTORY_H__ */
