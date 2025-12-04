/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/src/peer_connection_observer.h"

#include "pc/webrtc_sdp.h"
#include "rffi/src/ptr.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/string_encode.h"

namespace webrtc {
namespace rffi {

PeerConnectionObserverRffi::PeerConnectionObserverRffi(
    void* observer,
    const PeerConnectionObserverCallbacks* callbacks,
    bool enable_frame_encryption,
    bool enable_video_frame_event,
    bool enable_video_frame_content)
    : observer_(observer),
      callbacks_(*callbacks),
      enable_frame_encryption_(enable_frame_encryption),
      enable_video_frame_event_(enable_video_frame_event),
      enable_video_frame_content_(enable_video_frame_content) {
  RTC_LOG(LS_INFO) << "PeerConnectionObserverRffi:ctor(): " << this->observer_;
}

PeerConnectionObserverRffi::~PeerConnectionObserverRffi() {
  RTC_LOG(LS_INFO) << "PeerConnectionObserverRffi:dtor(): " << this->observer_;
}

void PeerConnectionObserverRffi::OnIceCandidate(const IceCandidate* candidate) {
  RustIceCandidate rust_candidate;

  std::string sdp;
  candidate->ToString(&sdp);
  rust_candidate.sdp_borrowed = sdp.c_str();

  rust_candidate.is_relayed =
      (candidate->candidate().type() == IceCandidateType::kRelay);
  rust_candidate.relay_protocol = TransportProtocol::kUnknown;
  if (candidate->candidate().relay_protocol() == UDP_PROTOCOL_NAME) {
    rust_candidate.relay_protocol = TransportProtocol::kUdp;
  } else if (candidate->candidate().relay_protocol() == TCP_PROTOCOL_NAME) {
    rust_candidate.relay_protocol = TransportProtocol::kTcp;
  } else if (candidate->candidate().relay_protocol() == TLS_PROTOCOL_NAME) {
    rust_candidate.relay_protocol = TransportProtocol::kTls;
  }

  callbacks_.onIceCandidate(observer_, &rust_candidate);
}

void PeerConnectionObserverRffi::OnIceCandidateError(
    const std::string& address,
    int port,
    const std::string& url,
    int error_code,
    const std::string& error_text) {
  // Error code 701 is when we have an IPv4 local port trying to reach an IPv6
  // server or vice versa. That's expected to not work, so we don't want to log
  // that all the time.
  if (error_code != 701) {
    RTC_LOG(LS_WARNING) << "Failed to gather local ICE candidate from "
                        << address << ":" << port << " to " << url << "; error "
                        << error_code << ": " << error_text;
  }
}

void PeerConnectionObserverRffi::OnIceCandidateRemoved(
    const IceCandidate* candidate) {
  callbacks_.onIceCandidateRemoved(
      observer_, RtcSocketAddressToIpPort(candidate->address()));
}

void PeerConnectionObserverRffi::OnSignalingChange(
    PeerConnectionInterface::SignalingState new_state) {}

void PeerConnectionObserverRffi::OnIceConnectionChange(
    PeerConnectionInterface::IceConnectionState new_state) {
  callbacks_.onIceConnectionChange(observer_, new_state);
}

void PeerConnectionObserverRffi::OnConnectionChange(
    PeerConnectionInterface::PeerConnectionState new_state) {}

void PeerConnectionObserverRffi::OnIceConnectionReceivingChange(
    bool receiving) {
  RTC_LOG(LS_INFO) << "OnIceConnectionReceivingChange()";
}

void PeerConnectionObserverRffi::OnIceSelectedCandidatePairChanged(
    const CandidatePairChangeEvent& event) {
  auto& local = event.selected_candidate_pair.local_candidate();
  auto& remote = event.selected_candidate_pair.remote_candidate();
  auto local_adapter_type = local.network_type();
  auto local_adapter_type_under_vpn = local.underlying_type_for_vpn();
  bool local_relayed = (local.type() == IceCandidateType::kRelay) ||
                       !local.relay_protocol().empty();
  TransportProtocol local_relay_protocol = TransportProtocol::kUnknown;
  if (local.relay_protocol() == UDP_PROTOCOL_NAME) {
    local_relay_protocol = TransportProtocol::kUdp;
  } else if (local.relay_protocol() == TCP_PROTOCOL_NAME) {
    local_relay_protocol = TransportProtocol::kTcp;
  } else if (local.relay_protocol() == TLS_PROTOCOL_NAME) {
    local_relay_protocol = TransportProtocol::kTls;
  }
  bool remote_relayed = (remote.type() == IceCandidateType::kRelay);
  auto network_route =
      rffi::NetworkRoute{local_adapter_type, local_adapter_type_under_vpn,
                         local_relayed, local_relay_protocol, remote_relayed};

  callbacks_.onIceNetworkRouteChange(observer_, network_route,
                                     SdpSerializeCandidate(local).c_str(),
                                     SdpSerializeCandidate(remote).c_str());
}

void PeerConnectionObserverRffi::OnIceGatheringChange(
    PeerConnectionInterface::IceGatheringState new_state) {
  RTC_LOG(LS_INFO) << "OnIceGatheringChange()";
}

void PeerConnectionObserverRffi::OnAddStream(
    scoped_refptr<MediaStreamInterface> stream) {
  RTC_LOG(LS_INFO) << "OnAddStream()";

  callbacks_.onAddStream(observer_, take_rc(stream));
}

void PeerConnectionObserverRffi::OnRemoveStream(
    scoped_refptr<MediaStreamInterface> stream) {
  RTC_LOG(LS_INFO) << "OnRemoveStream()";
}

void PeerConnectionObserverRffi::OnRenegotiationNeeded() {
  RTC_LOG(LS_INFO) << "OnRenegotiationNeeded()";
}

void PeerConnectionObserverRffi::OnAddTrack(
    scoped_refptr<RtpReceiverInterface> receiver,
    const std::vector<scoped_refptr<MediaStreamInterface>>& streams) {
  RTC_LOG(LS_INFO) << "OnAddTrack()";
}

void PeerConnectionObserverRffi::OnTrack(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  auto receiver = transceiver->receiver();
  auto streams = receiver->streams();

  // Ownership is transferred to the rust call back
  // handler.  Someone must call RefCountInterface::Release()
  // eventually.
  if (receiver->media_type() == MediaType::AUDIO) {
    if (enable_frame_encryption_) {
      uint32_t id = 0;
      if (receiver->stream_ids().size() > 0) {
        FromString(receiver->stream_ids()[0], &id);
      }
      if (id != 0) {
        receiver->SetFrameDecryptor(CreateDecryptor(id));
        callbacks_.onAddAudioRtpReceiver(observer_, take_rc(receiver->track()));
      } else {
        RTC_LOG(LS_WARNING)
            << "Not sending decryptor for RtpReceiver with strange ID: "
            << receiver->track()->id();
      }
    } else {
      callbacks_.onAddAudioRtpReceiver(observer_, take_rc(receiver->track()));
    }
  } else if (receiver->media_type() == MediaType::VIDEO) {
    if (enable_frame_encryption_) {
      uint32_t id = 0;
      if (receiver->stream_ids().size() > 0) {
        FromString(receiver->stream_ids()[0], &id);
      }
      if (id != 0) {
        receiver->SetFrameDecryptor(CreateDecryptor(id));
        AddVideoSink(static_cast<VideoTrackInterface*>(receiver->track().get()),
                     id);
        callbacks_.onAddVideoRtpReceiver(observer_, take_rc(receiver->track()),
                                         id);
      } else {
        RTC_LOG(LS_WARNING)
            << "Not sending decryptor for RtpReceiver with strange ID: "
            << receiver->track()->id();
      }
    } else {
      AddVideoSink(static_cast<VideoTrackInterface*>(receiver->track().get()),
                   0);
      callbacks_.onAddVideoRtpReceiver(observer_, take_rc(receiver->track()),
                                       0);
    }
  }
}

class Encryptor : public FrameEncryptorInterface {
 public:
  // Passed-in observer must live at least as long as the Encryptor,
  // which likely means as long as the PeerConnection.
  Encryptor(void* observer, PeerConnectionObserverCallbacks* callbacks)
      : observer_(observer), callbacks_(callbacks) {}

  // This is called just before Encrypt to get the size of the ciphertext
  // buffer that will be given to Encrypt.
  size_t GetMaxCiphertextByteSize(MediaType media_type,
                                  size_t plaintext_size) override {
    bool is_audio = (media_type == MediaType::AUDIO);
    bool is_video = (media_type == MediaType::VIDEO);
    if (!is_audio && !is_video) {
      RTC_LOG(LS_WARNING)
          << "GetMaxCiphertextByteSize called with weird media type: "
          << media_type;
      return 0;
    }
    return callbacks_->getMediaCiphertextBufferSize(observer_, is_audio,
                                                    plaintext_size);
  }

  int Encrypt(MediaType media_type,
              // Our encryption mechanism is the same regardless of SSRC
              uint32_t _ssrc,
              // This is not supported by our SFU currently, so don't bother
              // trying to use it.
              ArrayView<const uint8_t> _generic_video_header,
              ArrayView<const uint8_t> plaintext,
              ArrayView<uint8_t> ciphertext_buffer,
              size_t* ciphertext_size) override {
    bool is_audio = (media_type == MediaType::AUDIO);
    bool is_video = (media_type == MediaType::VIDEO);
    if (!is_audio && !is_video) {
      RTC_LOG(LS_WARNING) << "Encrypt called with weird media type: "
                          << media_type;
      return -1;  // Error
    }
    if (!callbacks_->encryptMedia(observer_, plaintext.data(), plaintext.size(),
                                  ciphertext_buffer.data(),
                                  ciphertext_buffer.size(), ciphertext_size)) {
      return -2;  // Error
    }
    return 0;  // No error
  }

 private:
  void* observer_;
  PeerConnectionObserverCallbacks* callbacks_;
};

scoped_refptr<FrameEncryptorInterface>
PeerConnectionObserverRffi::CreateEncryptor() {
  // The PeerConnectionObserverRffi outlives the Encryptor because it outlives
  // the PeerConnection, which outlives the RtpSender, which owns the Encryptor.
  // So we know the PeerConnectionObserverRffi outlives the Encryptor.
  return make_ref_counted<Encryptor>(observer_, &callbacks_);
}

void PeerConnectionObserverRffi::AddVideoSink(VideoTrackInterface* track,
                                              uint32_t demux_id) {
  if (!enable_video_frame_event_ || !track) {
    return;
  }

  auto sink = std::make_unique<VideoSink>(demux_id, this);

  VideoSinkWants wants;
  // Note: this causes frames to be dropped, not rotated.
  // So don't set it to true, even if it seems to make sense!
  wants.rotation_applied = false;

  // The sink gets stored in the track, but never destroys it.
  // The sink must live as long as the track, which is why we
  // stored it in the PeerConnectionObserverRffi.
  track->AddOrUpdateSink(sink.get(), wants);
  video_sinks_.push_back(std::move(sink));
}

VideoSink::VideoSink(uint32_t demux_id, PeerConnectionObserverRffi* pc_observer)
    : demux_id_(demux_id), pc_observer_(pc_observer) {}

void VideoSink::OnFrame(const VideoFrame& frame) {
  pc_observer_->OnVideoFrame(demux_id_, frame);
}

void PeerConnectionObserverRffi::OnVideoFrame(uint32_t demux_id,
                                              const VideoFrame& frame) {
  RffiVideoFrameMetadata metadata = {};
  metadata.width = frame.width();
  metadata.height = frame.height();
  metadata.rotation = frame.rotation();
  // We can't keep a reference to the buffer around or it will slow down the
  // video decoder. This introduces a copy, but only in the case where we aren't
  // rotated, and it's a copy of i420 and not RGBA (i420 is smaller than RGBA).
  // TODO: Figure out if we can make the decoder have a larger frame output pool
  // so that we don't need to do this.
  auto* buffer_owned_rc =
      enable_video_frame_content_
          ? Rust_copyAndRotateVideoFrameBuffer(frame.video_frame_buffer().get(),
                                               frame.rotation())
          : nullptr;
  // If we rotated the frame, we need to update metadata as well
  if ((metadata.rotation == kVideoRotation_90) ||
      (metadata.rotation == kVideoRotation_270)) {
    metadata.width = frame.height();
    metadata.height = frame.width();
  }
  metadata.rotation = kVideoRotation_0;

  callbacks_.onVideoFrame(observer_, demux_id, metadata, buffer_owned_rc);
}

class Decryptor : public FrameDecryptorInterface {
 public:
  // Passed-in observer must live at least as long as the Decryptor,
  // which likely means as long as the PeerConnection.
  Decryptor(uint32_t track_id,
            void* observer,
            PeerConnectionObserverCallbacks* callbacks)
      : track_id_(track_id), observer_(observer), callbacks_(callbacks) {}

  // This is called just before Decrypt to get the size of the plaintext
  // buffer that will be given to Decrypt.
  size_t GetMaxPlaintextByteSize(MediaType media_type,
                                 size_t ciphertext_size) override {
    bool is_audio = (media_type == MediaType::AUDIO);
    bool is_video = (media_type == MediaType::VIDEO);
    if (!is_audio && !is_video) {
      RTC_LOG(LS_WARNING)
          << "GetMaxPlaintextByteSize called with weird media type: "
          << media_type;
      return 0;
    }
    return callbacks_->getMediaPlaintextBufferSize(observer_, track_id_,
                                                   is_audio, ciphertext_size);
  }

  FrameDecryptorInterface::Result Decrypt(
      MediaType media_type,
      // Our encryption mechanism is the same regardless of CSRCs
      const std::vector<uint32_t>& _csrcs,
      // This is not supported by our SFU currently, so don't bother trying to
      // use it.
      ArrayView<const uint8_t> _generic_video_header,
      ArrayView<const uint8_t> ciphertext,
      ArrayView<uint8_t> plaintext_buffer) override {
    bool is_audio = (media_type == MediaType::AUDIO);
    bool is_video = (media_type == MediaType::VIDEO);
    if (!is_audio && !is_video) {
      RTC_LOG(LS_WARNING) << "Decrypt called with weird media type: "
                          << media_type;
      return FrameDecryptorInterface::Result(
          FrameDecryptorInterface::Status::kUnknown, 0);
    }
    size_t plaintext_size = 0;
    if (!callbacks_->decryptMedia(observer_, track_id_, ciphertext.data(),
                                  ciphertext.size(), plaintext_buffer.data(),
                                  plaintext_buffer.size(), &plaintext_size)) {
      return FrameDecryptorInterface::Result(
          FrameDecryptorInterface::Status::kFailedToDecrypt, 0);
    }
    return FrameDecryptorInterface::Result(FrameDecryptorInterface::Status::kOk,
                                           plaintext_size);
  }

 private:
  uint32_t track_id_;
  void* observer_;
  PeerConnectionObserverCallbacks* callbacks_;
};

scoped_refptr<FrameDecryptorInterface>
PeerConnectionObserverRffi::CreateDecryptor(uint32_t track_id) {
  // The PeerConnectionObserverRffi outlives the Decryptor because it outlives
  // the PeerConnection, which outlives the RtpReceiver, which owns the
  // Decryptor. So we know the PeerConnectionObserverRffi outlives the
  // Decryptor.
  return make_ref_counted<Decryptor>(track_id, observer_, &callbacks_);
}

// Returns an owned pointer.
// Passed-in observer must live at least as long as the returned value,
// which in turn must live at least as long as the PeerConnection.
RUSTEXPORT PeerConnectionObserverRffi* Rust_createPeerConnectionObserver(
    void* observer_borrowed,
    const PeerConnectionObserverCallbacks* callbacks_borrowed,
    bool enable_frame_encryption,
    bool enable_video_frame_event,
    bool enable_video_frame_content) {
  return new PeerConnectionObserverRffi(
      observer_borrowed, callbacks_borrowed, enable_frame_encryption,
      enable_video_frame_event, enable_video_frame_content);
}

RUSTEXPORT void Rust_deletePeerConnectionObserver(
    PeerConnectionObserverRffi* observer_owned) {
  delete observer_owned;
}

}  // namespace rffi
}  // namespace webrtc
