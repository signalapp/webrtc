/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/src/group_call_sdp_builder.h"

namespace webrtc {
namespace rffi {

GroupCallSessionDescriptionBuilder::GroupCallSessionDescriptionBuilder(
    bool local,
    uint32_t local_demux_id,
    const RffiSrtpKey& srtp_key)
    : local_demux_id_(local_demux_id) {
  // Use SRTP master key material instead
  crypto_params_.crypto_suite = srtp_key.suite;
  crypto_params_.key_params.SetData(srtp_key.key_borrowed, srtp_key.key_len);
  crypto_params_.key_params.AppendData(srtp_key.salt_borrowed,
                                       srtp_key.salt_len);
  if (local) {
    local_direction_ = RtpTransceiverDirection::kSendOnly;
    remote_direction_ = RtpTransceiverDirection::kRecvOnly;
  } else {
    local_direction_ = RtpTransceiverDirection::kRecvOnly;
    remote_direction_ = RtpTransceiverDirection::kSendOnly;
  }
}

void GroupCallSessionDescriptionBuilder::SetRtpParameters(
    MediaContentDescription& media) const {
  media.set_protocol(kMediaProtocolSavpf);
  media.set_rtcp_mux(true);
  media.set_manually_specify_keys(true);
  media.set_crypto(crypto_params_);
}

void GroupCallSessionDescriptionBuilder::SetupAudioStream(
    AudioContentDescription& audio,
    uint32_t demux_id) const {
  auto audio_ssrc = demux_id;
  auto demux_id_str = std::to_string(demux_id);
  StreamParams audio_stream;
  audio_stream.id = is_local() ? kLocalAudioTrackId : demux_id_str;
  audio_stream.cname = demux_id_str;
  audio_stream.add_ssrc(audio_ssrc);
  audio_stream.set_stream_ids({demux_id_str});
  audio.AddStream(audio_stream);
}

void GroupCallSessionDescriptionBuilder::SetupVideoStream(
    VideoContentDescription& video,
    uint32_t demux_id,
    bool is_svc) const {
  auto demux_id_str = std::to_string(demux_id);
  StreamParams video_stream;
  GroupCallSsrcGenerator ssrc_gen(demux_id);

  if (is_svc) {
    const auto video_ssrc =
        ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideoSvc);
    const auto video_rtx_ssrc =
        ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideoSvcRtx);
    video_stream.add_ssrc(video_ssrc);
    video_stream.AddFidSsrc(video_ssrc, video_rtx_ssrc);
  } else {
    const auto video1_ssrc =
        ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideo1);
    const auto video1_rtx_ssrc =
        ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideo1Rtx);
    video_stream.add_ssrc(video1_ssrc);
    if (is_local()) {
      const auto video2_ssrc =
          ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideo2);
      const auto video2_rtx_ssrc =
          ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideo2Rtx);
      const auto video3_ssrc =
          ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideo3);
      const auto video3_rtx_ssrc =
          ssrc_gen.GenerateSsrc(GroupCallSsrcGenerator::Ssrc::kVideo3Rtx);
      video_stream.add_ssrc(video2_ssrc);
      video_stream.add_ssrc(video3_ssrc);
      video_stream.ssrc_groups.push_back(
          {kSimSsrcGroupSemantics, video_stream.ssrcs});
      video_stream.AddFidSsrc(video2_ssrc, video2_rtx_ssrc);
      video_stream.AddFidSsrc(video3_ssrc, video3_rtx_ssrc);
    }
    video_stream.AddFidSsrc(video1_ssrc, video1_rtx_ssrc);
    // This makes screen share use 2 layers of the highest resolution
    // (but different quality/framerate) rather than 3 layers of
    // differing resolution.
    video.set_conference_mode(true);
  }

  video_stream.id = is_local() ? kLocalVideoTrackId : demux_id_str;
  video_stream.cname = demux_id_str;
  video_stream.set_stream_ids({demux_id_str});
  video.AddStream(video_stream);
}

// static
void GroupCallSessionDescriptionBuilder::SetVideoCodecFeedbackParameters(
    Codec& codec) {
  RTC_DCHECK_EQ(codec.type, Codec::Type::kVideo);
  codec.AddFeedbackParam({kRtcpFbParamTransportCc, kParamValueEmpty});
  codec.AddFeedbackParam({kRtcpFbParamCcm, kRtcpFbCcmParamFir});
  codec.AddFeedbackParam({kRtcpFbParamNack, kParamValueEmpty});
  codec.AddFeedbackParam({kRtcpFbParamNack, kRtcpFbNackParamPli});
  codec.AddFeedbackParam({kRtcpFbParamRemb, kParamValueEmpty});
}

// static
void GroupCallSessionDescriptionBuilder::SetOpusParameters(Codec& opus) {
  RTC_DCHECK_EQ(opus.name, kOpusCodecName);
  // These are the current defaults for WebRTC
  // We set them explicitly to avoid having the defaults change on us.
  opus.SetParam("stereo", "0");  // "1" would cause non-VOIP mode to be used
  opus.SetParam("ptime", "60");
  opus.SetParam("minptime", "60");
  opus.SetParam("maxptime", "60");
  opus.SetParam("useinbandfec", "1");
  // This is not a default. We enable this to help reduce bandwidth because we
  // are using CBR.
  opus.SetParam("usedtx", "1");
  opus.SetParam("maxaveragebitrate", "32000");
  // This is not a default. We enable this for privacy.
  opus.SetParam("cbr", "1");
  opus.AddFeedbackParam({kRtcpFbParamTransportCc, kParamValueEmpty});
}

SessionDescriptionInterface* GroupCallSessionDescriptionBuilder::Build() const {
  // VP8 codec setup
  auto codec_vp8 = CreateVideoCodec(VP8_PT, kVp8CodecName);
  auto codec_vp8_rtx = CreateVideoRtxCodec(VP8_RTX_PT, VP8_PT);
  SetVideoCodecFeedbackParameters(codec_vp8);

  // VP9 codec setup
  auto codec_vp9 = CreateVideoCodec(VP9_PT, kVp9CodecName);
  auto vp9_profile = VP9ProfileToString(kVP9Profile);
  codec_vp9.SetParam(kVP9FmtpProfileId, vp9_profile);
  auto codec_vp9_rtx = CreateVideoRtxCodec(VP9_RTX_PT, VP9_PT);
  codec_vp9_rtx.SetParam(kVP9FmtpProfileId, vp9_profile);
  SetVideoCodecFeedbackParameters(codec_vp9);

  // Opus codec setup
  auto opus = CreateAudioCodec(OPUS_PT, kOpusCodecName, 48000, 2);
  SetOpusParameters(opus);

  auto local_audio = std::make_unique<AudioContentDescription>();
  SetRtpParameters(*local_audio);
  local_audio->set_direction(local_direction_);

  auto local_video = std::make_unique<VideoContentDescription>();
  SetRtpParameters(*local_video);
  local_video->set_direction(local_direction_);

  std::vector<std::unique_ptr<AudioContentDescription>> remote_audios;
  std::vector<std::unique_ptr<VideoContentDescription>> remote_videos;

  for (auto demux_id : remote_demux_ids_) {
    bool decode_svc = is_svc(demux_id);
    auto remote_audio = std::make_unique<AudioContentDescription>();
    auto remote_video = std::make_unique<VideoContentDescription>();
    SetRtpParameters(*remote_audio);
    SetRtpParameters(*remote_video);
    remote_audio->AddCodec(opus);
    remote_audio->AddRtpHeaderExtension(rtp_ext_audio_level_);
    // Codecs, extensions and rtcp_reduced_size are set for inactive sections
    // too, so the m-line shape stays stable across active<->inactive
    // transitions.
    if (decode_svc) {
      remote_video->AddCodec(codec_vp9);
      remote_video->AddCodec(codec_vp9_rtx);
    } else {
      remote_video->AddCodec(codec_vp8);
      remote_video->AddCodec(codec_vp8_rtx);
    }
    remote_video->set_rtcp_reduced_size(true);
    remote_video->AddRtpHeaderExtension(rtp_ext_transport_cc1_);
    remote_video->AddRtpHeaderExtension(rtp_ext_video_orientation_);
    remote_video->AddRtpHeaderExtension(rtp_ext_dependency_descriptor_);
    if (demux_id == DISABLED_DEMUX_ID) {
      remote_audio->set_direction(RtpTransceiverDirection::kInactive);
      remote_video->set_direction(RtpTransceiverDirection::kInactive);
    } else {
      remote_audio->set_direction(remote_direction_);
      remote_video->set_direction(remote_direction_);
      SetupAudioStream(*remote_audio, demux_id);
      SetupVideoStream(*remote_video, demux_id, decode_svc);
    }
    remote_audios.push_back(std::move(remote_audio));
    remote_videos.push_back(std::move(remote_video));
  }

  local_audio->AddCodec(opus);

  if (enable_svc_encode_) {
    local_video->AddCodec(codec_vp9);
    local_video->AddCodec(codec_vp9_rtx);
  } else {
    local_video->AddCodec(codec_vp8);
    local_video->AddCodec(codec_vp8_rtx);
  }

  // Note: Do not add transport-cc for audio.  Using transport-cc with audio is
  // still experimental in WebRTC. And don't add abs_send_time because it's only
  // used for video.
  local_video->set_rtcp_reduced_size(true);
  local_audio->AddRtpHeaderExtension(rtp_ext_audio_level_);
  local_video->AddRtpHeaderExtension(rtp_ext_transport_cc1_);
  local_video->AddRtpHeaderExtension(rtp_ext_video_orientation_);
  local_video->AddRtpHeaderExtension(rtp_ext_dependency_descriptor_);
  local_video->AddRtpHeaderExtension(rtp_ext_video_layers_allocation_);

  // Set up local_demux_id
  SetupAudioStream(*local_audio, local_demux_id_);
  SetupVideoStream(*local_video, local_demux_id_, enable_svc_encode_);

  // We don't set the crypto keys here.
  // We expect that will be done later by Rust_disableDtlsAndSetSrtpKey.

  // Keep the order as the WebRTC default: (audio, video).
  auto bundle = ContentGroup(GROUP_TYPE_BUNDLE);
  bundle.AddContentName(kLocalAudioContentName);
  bundle.AddContentName(kLocalVideoContentName);

  TransportDescription transport;
  transport.ice_mode = ICEMODE_FULL;
  transport.ice_ufrag = ice_ufrag_;
  transport.ice_pwd = ice_pwd_;
  transport.AddOption(ICE_OPTION_TRICKLE);
  // DTLS is disabled
  transport.connection_role = CONNECTIONROLE_NONE;
  transport.identity_fingerprint = nullptr;

  auto session = std::make_unique<SessionDescription>();
  session->AddTransportInfo({kLocalAudioContentName, transport});
  session->AddTransportInfo({kLocalVideoContentName, transport});
  session->AddContent(kLocalAudioContentName, MediaProtocolType::kRtp, false,
                      std::move(local_audio));
  session->AddContent(kLocalVideoContentName, MediaProtocolType::kRtp, false,
                      std::move(local_video));
  for (size_t i = 0, j = remote_demux_ids_.size(); i < j; ++i) {
    auto audio_name = absl::StrCat("remote-audio", i);
    auto video_name = absl::StrCat("remote-video", i);
    session->AddTransportInfo({audio_name, transport});
    session->AddContent(audio_name, MediaProtocolType::kRtp, false,
                        std::move(remote_audios[i]));
    bundle.AddContentName(audio_name);
    session->AddTransportInfo({video_name, transport});
    session->AddContent(video_name, MediaProtocolType::kRtp, false,
                        std::move(remote_videos[i]));
    bundle.AddContentName(video_name);
  }

  session->AddGroup(bundle);
  session->set_msid_signaling(kMsidSignalingMediaSection);

  auto typ = is_local() ? SdpType::kOffer : SdpType::kAnswer;
  // The session ID and session version (both "1" here) go into SDP, but are not
  // used at all.
  auto sdp =
      SessionDescriptionInterface::Create(typ, std::move(session), "1", "1")
          .release();
  return sdp;
}

}  // namespace rffi
}  // namespace webrtc