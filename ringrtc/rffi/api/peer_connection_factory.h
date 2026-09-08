/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_PEER_CONNECTION_FACTORY_H__
#define RFFI_API_PEER_CONNECTION_FACTORY_H__

#include "rffi/api/audio_device_intf.h"
#include "rffi/api/injectable_network.h"
#include "rffi/api/peer_connection_intf.h"
#include "rffi/api/rtp_observer_intf.h"

namespace rtc {
class RTCCertificite;
}

namespace webrtc {
class PeerConnectionInterface;
class PeerConnectionFactoryInterface;
class AudioTrackInterface;
class VideoTrackSourceInterface;

class PeerConnectionFactoryOwner;

namespace rffi {
class PeerConnectionObserverRffi;

typedef struct {
  ptr::Borrowed<const char> username_borrowed;
  ptr::Borrowed<const char> password_borrowed;
  ptr::Borrowed<const char> hostname_borrowed;
  ptr::Borrowed<ptr::Borrowed<const char>> urls_borrowed;
  size_t urls_size;
} RffiIceServer;

typedef struct {
  ptr::Borrowed<const RffiIceServer> servers;
  size_t servers_size;
} RffiIceServers;

enum class RffiPeerConnectionKind : uint8_t {
  kDirect,
  kRelayed,
  kGroupCall,
};

typedef struct {
  bool high_pass_filter_enabled;
  bool aec_enabled;
  bool ns_enabled;
  bool agc_enabled;
  ptr::Borrowed<void> rust_adm_borrowed;
  ptr::Borrowed<AudioDeviceCallbacks> rust_audio_device_callbacks;
  void (*free_adm_cb)(ptr::Borrowed<const void>);
} RffiAudioConfig;

typedef struct {
  int32_t max_packets;
  int32_t min_delay_ms;
  int32_t max_target_delay_ms;
  bool fast_accelerate;
} RffiAudioJitterBufferConfig;

// You can create more than one, but you should probably only have one unless
// you want to test separate endpoints that are as independent as possible.
RUSTEXPORT ptr::OwnedRc<webrtc::PeerConnectionFactoryOwner>
Rust_createPeerConnectionFactory(
    ptr::Borrowed<const RffiAudioConfig> audio_config_borrowed,
    bool use_injectable_network,
    const char* field_trials_string);

RUSTEXPORT ptr::OwnedRc<webrtc::PeerConnectionFactoryOwner>
Rust_createPeerConnectionFactoryWrapper(
    ptr::BorrowedRc<webrtc::PeerConnectionFactoryInterface>
        factory_borrowed_rc);

RUSTEXPORT ptr::Borrowed<webrtc::rffi::InjectableNetwork>
Rust_getInjectableNetwork(ptr::BorrowedRc<webrtc::PeerConnectionFactoryOwner>
                              factory_owner_borrowed_rc);

RUSTEXPORT ptr::OwnedRc<webrtc::PeerConnectionInterface>
Rust_createPeerConnection(
    ptr::BorrowedRc<webrtc::PeerConnectionFactoryOwner>
        factory_owner_borrowed_rc,
    ptr::Borrowed<webrtc::rffi::PeerConnectionObserverRffi> observer_borrowed,
    RffiPeerConnectionKind kind,
    ptr::Borrowed<const RffiAudioJitterBufferConfig>
        audio_jitter_buffer_config_borrowed,
    int32_t audio_rtcp_report_interval_ms,
    ptr::Borrowed<const RffiIceServers> ice_servers_borrowed,
    ptr::BorrowedRc<webrtc::AudioTrackInterface>
        outgoing_audio_track_borrowed_rc,
    ptr::BorrowedRc<webrtc::VideoTrackInterface>
        outgoing_video_track_borrowed_rc);

RUSTEXPORT ptr::OwnedRc<webrtc::AudioTrackInterface> Rust_createAudioTrack(
    ptr::BorrowedRc<webrtc::PeerConnectionFactoryOwner>
        factory_owner_borrowed_rc);

RUSTEXPORT ptr::OwnedRc<webrtc::VideoTrackInterface> Rust_createVideoTrack(
    ptr::BorrowedRc<webrtc::PeerConnectionFactoryOwner>
        factory_owner_borrowed_rc,
    ptr::BorrowedRc<webrtc::VideoTrackSourceInterface> source_borrowed_rc);

}  // namespace rffi
}  // namespace webrtc
#endif /* RFFI_API_PEER_CONNECTION_FACTORY_H__ */
