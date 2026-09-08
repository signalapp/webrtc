/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_PEER_CONNECTION_OBSERVER_INTF_H__
#define RFFI_API_PEER_CONNECTION_OBSERVER_INTF_H__

#include "rffi/api/injectable_network.h"
#include "rffi/api/media.h"
#include "rffi/api/network.h"
#include "rffi/api/rffi_defs.h"
#include "rffi/api/webrtc_common.h"

/**
 * Rust friendly wrapper around a custom class that implements the
 * webrtc::PeerConnectionObserver interface.
 *
 */

namespace webrtc {
class PeerConnectionInterface;
class MediaStreamInterface;
class MediaStreamTrackInterface;
namespace rffi {
class PeerConnectionObserverRffi;

/* NetworkRoute structure passed between Rust and C++ */
typedef struct {
  AdapterType local_adapter_type;
  AdapterType local_adapter_type_under_vpn;
  bool local_relayed;
  TransportProtocol local_relay_protocol;
  bool remote_relayed;
} NetworkRoute;

/* Peer Connection Observer callback function pointers */
typedef struct {
  // ICE events
  void (*onIceCandidate)(
      ptr::Borrowed<void> observer_borrowed,
      ptr::Borrowed<const RustIceCandidate> candidate_borrowed);
  void (*onIceCandidateRemoved)(ptr::Borrowed<void> observer_borrowed,
                                const webrtc::rffi::IpPort address_borrowed);
  void (*onIceConnectionChange)(ptr::Borrowed<void> observer_borrowed,
                                webrtc::rffi::IceConnectionState);
  void (*onIceNetworkRouteChange)(
      ptr::Borrowed<void> observer_borrowed,
      webrtc::rffi::NetworkRoute,
      ptr::Borrowed<const char> local_description_borrowed,
      ptr::Borrowed<const char> remote_description_borrowed);

  // Media events
  void (*onAddStream)(
      ptr::Borrowed<void> observer_borrowed,
      ptr::OwnedRc<webrtc::MediaStreamInterface> stream_owned_rc);
  void (*onAddAudioRtpReceiver)(
      ptr::Borrowed<void> observer_borrowed,
      ptr::OwnedRc<webrtc::MediaStreamTrackInterface> track_owned_rc);
  void (*onAddVideoRtpReceiver)(
      ptr::Borrowed<void> observer_borrowed,
      ptr::OwnedRc<webrtc::MediaStreamTrackInterface> track_owned_rc,
      uint32_t demux_id);
  void (*onVideoFrame)(
      ptr::Borrowed<void> observer_borrowed,
      uint32_t track_id,
      RffiVideoFrameMetadata metadata,
      ptr::OwnedRc<webrtc::VideoFrameBuffer> frame_buffer_owned_rc);

  // Frame encryption
  size_t (*getMediaCiphertextBufferSize)(ptr::Borrowed<void> observer_borrowed,
                                         bool,
                                         size_t);
  bool (*encryptMedia)(ptr::Borrowed<void> observer_borrowed,
                       ptr::Borrowed<const uint8_t> plaintext_borrowed,
                       size_t,
                       uint8_t* ciphertext_out,
                       size_t,
                       size_t* ciphertext_size_out);
  size_t (*getMediaPlaintextBufferSize)(ptr::Borrowed<void> observer_borrowed,
                                        uint32_t,
                                        bool,
                                        size_t);
  bool (*decryptMedia)(ptr::Borrowed<void> observer_borrowed,
                       uint32_t,
                       ptr::Borrowed<const uint8_t> ciphertext_borrowed,
                       size_t,
                       uint8_t* plaintext_out,
                       size_t,
                       size_t* plaintext_size_out);
} PeerConnectionObserverCallbacks;

// Passed-in observer must live at least as long as the
// PeerConnectionObserverRffi, which is at least as long as the PeerConnection.
RUSTEXPORT webrtc::rffi::PeerConnectionObserverRffi*
Rust_createPeerConnectionObserver(
    ptr::Borrowed<void> observer_borrowed,
    ptr::Borrowed<const PeerConnectionObserverCallbacks> callbacks_borrowed,
    bool enable_frame_encryption,
    bool enable_video_frame_event,
    bool enable_video_frame_content);

RUSTEXPORT void Rust_deletePeerConnectionObserver(
    ptr::Owned<webrtc::rffi::PeerConnectionObserverRffi> observer_owned);

}  // namespace rffi
}  // namespace webrtc
#endif /* RFFI_API_PEER_CONNECTION_OBSERVER_INTF_H__ */
