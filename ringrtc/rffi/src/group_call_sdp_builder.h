/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_GROUP_CALL_SDP_BUILDER_H_
#define RFFI_GROUP_CALL_SDP_BUILDER_H_

#include <algorithm>
#include <string>

#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/video_codecs/vp9_profile.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_video_layers_allocation_extension.h"
#include "pc/media_session.h"
#include "pc/session_description.h"
#include "rffi/api/peer_connection_intf.h"
#include "rffi/src/constants.h"

namespace webrtc {
namespace rffi {

class GroupCallSsrcGenerator final {
 public:
  enum class Ssrc : int {
    kAudio = 0,
    // Skip audio RTX
    kVideo1 = 2,
    kVideo1Rtx = 3,
    kVideo2 = 4,
    kVideo2Rtx = 5,
    kVideo3 = 6,
    kVideo3Rtx = 7,
    kVideoSvc = 8,
    kVideoSvcRtx = 9,
  };

  explicit GroupCallSsrcGenerator(uint32_t demux_id) {
    RTC_DCHECK_EQ(demux_id & 0xF, 0u);
    demux_id_ = demux_id;
  }

  uint32_t GenerateSsrc(Ssrc target_ssrc) const {
    return demux_id_ | static_cast<int>(target_ssrc);
  }

 private:
  uint32_t demux_id_;
};

class GroupCallSessionDescriptionBuilder {
 public:
  GroupCallSessionDescriptionBuilder& with_ice_credentials(
      const std::string_view ufrag,
      const std::string_view pwd) {
    ice_ufrag_ = ufrag;
    ice_pwd_ = pwd;
    return *this;
  }

  GroupCallSessionDescriptionBuilder& with_remote_demux_ids(
      const std::span<const uint32_t> remote_demux_ids) {
    remote_demux_ids_ = remote_demux_ids;
    return *this;
  }

  GroupCallSessionDescriptionBuilder& with_remote_demux_ids_require_svc(
      const std::span<const uint32_t> remote_demux_ids_require_svc) {
    remote_demux_ids_require_svc_ = remote_demux_ids_require_svc;
    return *this;
  }

  GroupCallSessionDescriptionBuilder& enable_svc_encode(bool enable_svc) {
    enable_svc_encode_ = enable_svc;
    return *this;
  }

  SessionDescriptionInterface* Build() const;

 protected:
  GroupCallSessionDescriptionBuilder(bool local,
                                     uint32_t local_demux_id,
                                     const RffiSrtpKey& srtp_key);

  void SetRtpParameters(MediaContentDescription& media) const;
  void SetupAudioStream(AudioContentDescription& audio,
                        uint32_t demux_id) const;
  void SetupVideoStream(VideoContentDescription& video,
                        uint32_t demux_id,
                        bool is_svc) const;
  static void SetVideoCodecFeedbackParameters(Codec& codec);
  static void SetOpusParameters(Codec& opus);

  bool is_svc(uint32_t demux_id) const {
    return std::find(remote_demux_ids_require_svc_.begin(),
                     remote_demux_ids_require_svc_.end(),
                     demux_id) != remote_demux_ids_require_svc_.end();
  }

  bool is_local() const {
    return local_direction_ == RtpTransceiverDirection::kSendOnly;
  }

 private:
  // This must stay in sync with PeerConnectionFactory.createAudioTrack
  static constexpr char kLocalAudioTrackId[] = "audio1";
  // This must stay in sync with PeerConnectionFactory.createVideoTrack
  static constexpr char kLocalVideoTrackId[] = "video1";

  static constexpr char kLocalAudioContentName[] = "local-audio0";
  static constexpr char kLocalVideoContentName[] = "local-video0";

  // The VP9 profile to use for all VP9 video streams
  static constexpr auto kVP9Profile = VP9Profile::kProfile0;

  // TransportCC V2 is now enabled by default, but the difference is that V2
  // doesn't send periodic updates and instead waits for feedback requests.
  // Since the SFU doesn't currently send feedback requests, we can't enable V2.
  // We'd have to add it to the SFU to move from V1 to V2.
  const RtpExtension rtp_ext_transport_cc1_{
      TransportSequenceNumber::Uri(),
      RtpHeaderExtensionId(TRANSPORT_CC1_EXT_ID)};
  const RtpExtension rtp_ext_video_orientation_{
      VideoOrientation::Uri(), RtpHeaderExtensionId(VIDEO_ORIENTATION_EXT_ID)};
  const RtpExtension rtp_ext_dependency_descriptor_{
      RtpDependencyDescriptorExtension::Uri(),
      RtpHeaderExtensionId(DEPENDENCY_DESCRIPTOR_EXT_ID)};
  const RtpExtension rtp_ext_video_layers_allocation_{
      RtpVideoLayersAllocationExtension::Uri(),
      RtpHeaderExtensionId(VIDEO_LAYERS_ALLOCATION_EXT_ID)};
  const RtpExtension rtp_ext_audio_level_{
      AudioLevelExtension::Uri(), RtpHeaderExtensionId(AUDIO_LEVEL_EXT_ID)};

  // If we're generating an offer, the local direction is sendonly. Otherwise,
  // we are generating a response, and it is marked as recvonly.
  RtpTransceiverDirection local_direction_;
  // If we're generating an offer, the remote direction, for all remote
  // streams, is marked as recvonly. Otherwise, we're generating a response,
  // and they will be marked as sendonly.
  RtpTransceiverDirection remote_direction_;
  std::string_view ice_ufrag_;
  std::string_view ice_pwd_;
  CryptoParams crypto_params_;
  uint32_t local_demux_id_;
  std::span<const uint32_t> remote_demux_ids_;
  std::span<const uint32_t> remote_demux_ids_require_svc_;
  bool enable_svc_encode_ = false;
};

struct LocalGroupCallSessionDescriptionBuilder final
    : GroupCallSessionDescriptionBuilder {
  LocalGroupCallSessionDescriptionBuilder(uint32_t demux_id,
                                          const RffiSrtpKey& srtp_key)
      : GroupCallSessionDescriptionBuilder(true, demux_id, srtp_key) {}
};

struct RemoteGroupCallSessionDescriptionBuilder final
    : GroupCallSessionDescriptionBuilder {
  RemoteGroupCallSessionDescriptionBuilder(uint32_t demux_id,
                                           const RffiSrtpKey& srtp_key)
      : GroupCallSessionDescriptionBuilder(false, demux_id, srtp_key) {}
};

}  // namespace rffi
}  // namespace webrtc

#endif  // RFFI_GROUP_CALL_SDP_BUILDER_H_
