/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_PEER_CONNECTION_OBSERVER_H__
#define RFFI_PEER_CONNECTION_OBSERVER_H__

#include "rffi/api/peer_connection_observer_intf.h"

/**
 * Adapter between the C++ PeerConnectionObserver interface and the
 * Rust PeerConnection.Observer interface.  Wraps an instance of the
 * Rust interface and dispatches C++ callbacks to Rust.
 */

namespace webrtc {
namespace rffi {

class VideoSink;

class PeerConnectionObserverRffi : public PeerConnectionObserver {
 public:
  // Passed-in observer must live at least as long as the
  // PeerConnectionObserverRffi.
  PeerConnectionObserverRffi(void* observer,
                             const PeerConnectionObserverCallbacks* callbacks,
                             bool enable_frame_encryption,
                             bool enable_video_frame_event,
                             bool enable_video_frame_content);
  ~PeerConnectionObserverRffi() override;

  // If enabled, the PeerConnection will be configured to encrypt and decrypt
  // media frames using PeerConnectionObserverCallbacks.
  bool enable_frame_encryption() { return enable_frame_encryption_; }
  // These will be a passed into RtpSenders and will be implemented
  // with callbacks to PeerConnectionObserverCallbacks.
  scoped_refptr<FrameEncryptorInterface> CreateEncryptor();
  // These will be a passed into RtpReceivers and will be implemented
  // with callbacks to PeerConnectionObserverCallbacks.
  scoped_refptr<FrameDecryptorInterface> CreateDecryptor(uint32_t track_id);

  // Implementation of PeerConnectionObserver interface, which propagates
  // the callbacks to the Rust observer.
  void OnIceCandidate(const IceCandidate* candidate) override;
  void OnIceCandidateError(const std::string& address,
                           int port,
                           const std::string& url,
                           int error_code,
                           const std::string& error_text) override;
  void OnIceCandidateRemoved(const IceCandidate* candidate) override;
  void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) override;
  void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) override;
  void OnConnectionChange(
      PeerConnectionInterface::PeerConnectionState new_state) override;
  void OnIceConnectionReceivingChange(bool receiving) override;
  void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) override;
  void OnIceSelectedCandidatePairChanged(
      const CandidatePairChangeEvent& event) override;
  void OnAddStream(scoped_refptr<MediaStreamInterface> stream) override;
  void OnRemoveStream(scoped_refptr<MediaStreamInterface> stream) override;
  void OnDataChannel(scoped_refptr<DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override;
  void OnAddTrack(
      scoped_refptr<RtpReceiverInterface> receiver,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams) override;
  void OnTrack(scoped_refptr<RtpTransceiverInterface> transceiver) override;

  // Called by the VideoSinks in video_sinks_.
  void OnVideoFrame(uint32_t demux_id, const webrtc::VideoFrame& frame);

 private:
  // Add a VideoSink to the video_sinks_ for ownership and pass
  // a borrowed pointer to the track.
  void AddVideoSink(VideoTrackInterface* track, uint32_t demux_id);

  void* observer_;
  PeerConnectionObserverCallbacks callbacks_;
  bool enable_frame_encryption_ = false;
  bool enable_video_frame_event_ = false;
  bool enable_video_frame_content_ = false;
  std::vector<std::unique_ptr<VideoSink>> video_sinks_;
};

// A simple implementation of a VideoSinkInterface which passes video frames
// back to the PeerConnectionObserver with a demux_id.
class VideoSink : public VideoSinkInterface<webrtc::VideoFrame> {
 public:
  VideoSink(uint32_t demux_id, PeerConnectionObserverRffi*);
  ~VideoSink() override = default;

  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  uint32_t demux_id_;
  PeerConnectionObserverRffi* pc_observer_;
};

}  // namespace rffi
}  // namespace webrtc

#endif /* RFFI_PEER_CONNECTION_OBSERVER_H__ */
