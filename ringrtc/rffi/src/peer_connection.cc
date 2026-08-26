/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include <algorithm>
#include <array>
#include <string>

#include "api/ice_gatherer_interface.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/video_codecs/vp9_profile.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "p2p/base/port.h"
#include "pc/media_session.h"
#include "pc/rtp_media_utils.h"
#include "pc/sdp_utils.h"
#include "pc/session_description.h"
#include "rffi/api/peer_connection_intf.h"
#include "rffi/src/constants.h"
#include "rffi/src/group_call_sdp_builder.h"
#include "rffi/src/network.h"
#include "rffi/src/ptr.h"
#include "rffi/src/rtp_observer.h"
#include "rffi/src/sdp_observer.h"
#include "rffi/src/stats_observer.h"
#include "rtc_base/net_helper.h"
#include "sdk/media_constraints.h"

namespace webrtc {
namespace rffi {

class ConnectionParametersV4 {
 public:
  std::string ice_ufrag;
  std::string ice_pwd;
  std::vector<RffiVideoCodec> bidirectional_video_codecs;
  std::vector<RffiVideoCodec> encode_only_video_codecs;
  std::vector<RffiVideoCodec> decode_only_video_codecs;
};

RUSTEXPORT bool Rust_setScalabilityMode(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    const char* scalability_mode,
    int max_bitrate_bps) {
  if (scalability_mode == nullptr) {
    return false;
  }
  std::optional<int> max_bitrate =
      max_bitrate_bps > 0 ? std::make_optional(max_bitrate_bps) : std::nullopt;
  for (auto transceivers = peer_connection_borrowed_rc->GetTransceivers();
       auto transceiver : transceivers) {
    if (transceiver->media_type() == MediaType::VIDEO &&
        transceiver->direction() != RtpTransceiverDirection::kInactive &&
        transceiver->direction() != RtpTransceiverDirection::kRecvOnly) {
      auto sender = transceiver->sender();
      auto parameters = sender->GetParameters();
      // Ensure we have encodings for this sender. Not having encodings here
      // may indicate that SDP negotiation has not completed.
      if (!parameters.encodings.empty()) {
        bool supports_svc = std::any_of(
            parameters.codecs.begin(), parameters.codecs.end(),
            [](auto& codec) { return codec.name == kVp9CodecName; });
        if (supports_svc) {
          parameters.encodings[0].scalability_mode = scalability_mode;
          parameters.encodings[0].max_bitrate_bps = max_bitrate;
          parameters.encodings[0].scale_resolution_down_by = 1.0;
          sender->GenerateKeyFrame({});
          auto result = sender->SetParameters(parameters);
          if (!result.ok()) {
            RTC_LOG(LS_WARNING) << "Failed to enable SVC: " << result.message();
            return false;
          }
        }
      }
    }
  }
  return true;
}

RUSTEXPORT bool Rust_updateTransceivers(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    const uint32_t* remote_demux_ids_data_borrowed,
    size_t length) {
  const std::span remote_demux_ids(remote_demux_ids_data_borrowed,
                                   remote_demux_ids_data_borrowed + length);

  auto transceivers = peer_connection_borrowed_rc->GetTransceivers();
  // There should be at most 2 transceivers for each remote demux ID (there can
  // be fewer if new transceivers are about to be created), excluding the 2
  // transceivers for the local device's audio and video.
  if (remote_demux_ids.size() * 2 < transceivers.size() - 2) {
    RTC_LOG(LS_WARNING) << "Mismatched remote_demux_ids and transceivers count:"
                        << " remote_demux_ids.size()="
                        << remote_demux_ids.size()
                        << ", transceivers.size()=" << transceivers.size();
  }

  size_t remote_demux_ids_i = 0;
  for (auto transceiver : transceivers) {
    auto direction = transceiver->direction();
    if (direction != RtpTransceiverDirection::kInactive &&
        direction != RtpTransceiverDirection::kRecvOnly) {
      // This is a transceiver used by the local device to send media.
      continue;
    }

    auto ids = transceiver->receiver()->stream_ids();

    if (remote_demux_ids_i < remote_demux_ids.size()) {
      auto desired_demux_id = remote_demux_ids[remote_demux_ids_i];
      if (desired_demux_id == DISABLED_DEMUX_ID) {
        transceiver->SetDirectionWithError(RtpTransceiverDirection::kInactive);
      } else if (ids.empty() || ids[0] != absl::StrCat(desired_demux_id)) {
        // This transceiver is being reused
        transceiver->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly);
      }
    }

    // The same demux ID is used for both the audio and video transceivers, and
    // audio is added first. So only advance to the next demux ID after seeing
    // a video transceiver.
    if (transceiver->media_type() == MediaType::VIDEO) {
      remote_demux_ids_i++;
    }
  }

  // Create transceivers for the remaining remote_demux_ids.
  for (auto i = remote_demux_ids_i; i < remote_demux_ids.size(); i++) {
    auto remote_demux_id = remote_demux_ids[i];
    auto stream_id = std::to_string(remote_demux_id);
    RtpTransceiverInit init;
    init.direction = RtpTransceiverDirection::kRecvOnly;
    init.stream_ids = {stream_id};
    if (!peer_connection_borrowed_rc->AddTransceiver(MediaType::AUDIO, init)
             .ok()) {
      RTC_LOG(LS_ERROR) << "Failed to add audio transceiver";
      return false;
    }
    if (!peer_connection_borrowed_rc->AddTransceiver(MediaType::VIDEO, init)
             .ok()) {
      RTC_LOG(LS_ERROR) << "Failed to add video transceiver";
      return false;
    }
  }

  return true;
}

// Borrows the observer until the result is given to the observer,
// so the observer must stay alive until it's given a result.
RUSTEXPORT void Rust_createOffer(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    CreateSessionDescriptionObserverRffi* csd_observer_borrowed_rc) {
  // No constraints are set
  MediaConstraints constraints = MediaConstraints();
  PeerConnectionInterface::RTCOfferAnswerOptions options;

  CopyConstraintsIntoOfferAnswerOptions(&constraints, &options);
  peer_connection_borrowed_rc->CreateOffer(csd_observer_borrowed_rc, options);
}

RUSTEXPORT bool Rust_createSendOnlyTransceiver(
    webrtc::PeerConnectionInterface* peer_connection_borrowed_rc) {
  RtpTransceiverInit init;
  init.direction = RtpTransceiverDirection::kSendOnly;
  init.stream_ids = {kVideoTrackId};
  auto add_transceiver_result =
      peer_connection_borrowed_rc->AddTransceiver(MediaType::VIDEO, init);
  bool success = add_transceiver_result.ok();
  if (success) {
    // We do not actually need to do anything here! peer_connection_factory.cc
    // does, but only for group calls; this function is limited to 1:1.
  } else {
    RTC_LOG(LS_ERROR) << "Couldn't create send transceiver: "
                      << add_transceiver_result.error().message();
  }
  return success;
}

// Borrows the observer until the result is given to the observer,
// so the observer must stay alive until it's given a result.
RUSTEXPORT void Rust_setLocalDescription(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    SetSessionDescriptionObserverRffi* ssd_observer_borrowed_rc,
    SessionDescriptionInterface* local_description_owned) {
  peer_connection_borrowed_rc->SetLocalDescription(ssd_observer_borrowed_rc,
                                                   local_description_owned);
}

// Returns an owned pointer.
RUSTEXPORT const char* Rust_toSdp(
    SessionDescriptionInterface* session_description_borrowed) {
  std::string sdp;
  if (session_description_borrowed->ToString(&sdp)) {
    return strdup(&sdp[0u]);
  }

  RTC_LOG(LS_ERROR) << "Unable to convert SessionDescription to SDP";
  return nullptr;
}

RUSTEXPORT bool Rust_disableDtlsAndSetSrtpKey(
    SessionDescriptionInterface* session_description_borrowed,
    int crypto_suite,
    const char* key_borrowed,
    size_t key_len,
    const char* salt_borrowed,
    size_t salt_len) {
  if (!session_description_borrowed) {
    return false;
  }

  SessionDescription* session = session_description_borrowed->description();
  if (!session) {
    return false;
  }

  CryptoParams crypto_params;
  crypto_params.crypto_suite = crypto_suite;

  crypto_params.key_params.SetData(key_borrowed, key_len);
  crypto_params.key_params.AppendData(salt_borrowed, salt_len);

  // Disable DTLS
  for (TransportInfo& transport : session->transport_infos()) {
    transport.description.connection_role = CONNECTIONROLE_NONE;
    transport.description.identity_fingerprint = nullptr;
  }

  // Set SRTP key
  for (ContentInfo& content : session->contents()) {
    MediaContentDescription* media = content.media_description();
    if (media) {
      media->set_protocol(kMediaProtocolSavpf);
      media->set_crypto(crypto_params);
    }
  }

  return true;
}

static int codecPriority(const RffiVideoCodec c) {
  // Lower values are given higher priority
  switch (c.type) {
    case kRffiVideoCodecVp9:
      return 0;
    case kRffiVideoCodecVp8:
      return 1;
    default:
      return 100;
  }
}

static void sort_codecs(std::vector<RffiVideoCodec>& list) {
  std::stable_sort(list.begin(), list.end(),
                   [](const RffiVideoCodec lhs, const RffiVideoCodec rhs) {
                     return codecPriority(lhs) < codecPriority(rhs);
                   });
}

static void sort_codecs(RffiVideoCodec* first, size_t size) {
  std::stable_sort(first, first + size,
                   [](const RffiVideoCodec lhs, const RffiVideoCodec rhs) {
                     return codecPriority(lhs) < codecPriority(rhs);
                   });
}

// Legacy version - only does symmetric codecs.
RUSTEXPORT RffiConnectionParametersV4* Rust_sessionDescriptionToV4Legacy(
    const SessionDescriptionInterface* session_description_borrowed,
    bool enable_vp9) {
  if (!session_description_borrowed) {
    return nullptr;
  }

  const SessionDescription* session =
      session_description_borrowed->description();
  if (!session) {
    return nullptr;
  }

  // Get ICE ufrag + pwd
  if (session->transport_infos().empty()) {
    return nullptr;
  }

  auto v4 = std::make_unique<ConnectionParametersV4>();

  auto* transport = &session->transport_infos()[0].description;
  v4->ice_ufrag = transport->ice_ufrag;
  v4->ice_pwd = transport->ice_pwd;

  // Get video codecs
  auto* video = GetFirstVideoContentDescription(session);
  if (video) {
    for (const auto& codec : video->codecs()) {
      auto codec_type = PayloadStringToCodecType(codec.name);

      if (codec_type == kVideoCodecVP9) {
        if (enable_vp9) {
          auto profile = ParseSdpForVP9Profile(codec.params);
          std::string profile_id_string;
          codec.GetParam("profile-id", &profile_id_string);
          if (!profile) {
            RTC_LOG(LS_WARNING) << "Ignoring VP9 codec because profile-id = "
                                << profile_id_string;
            continue;
          }

          if (profile != VP9Profile::kProfile0) {
            RTC_LOG(LS_WARNING)
                << "Ignoring VP9 codec with non-zero profile-id = "
                << profile_id_string;
            continue;
          }

          RffiVideoCodec vp9;
          vp9.type = kRffiVideoCodecVp9;
          v4->bidirectional_video_codecs.push_back(vp9);
        }
      } else if (codec_type == kVideoCodecVP8) {
        RffiVideoCodec vp8;
        vp8.type = kRffiVideoCodecVp8;
        v4->bidirectional_video_codecs.push_back(vp8);
      }
    }
  }

  std::stable_sort(v4->bidirectional_video_codecs.begin(),
                   v4->bidirectional_video_codecs.end(),
                   [](const RffiVideoCodec lhs, const RffiVideoCodec rhs) {
                     return codecPriority(lhs) < codecPriority(rhs);
                   });

  auto* rffi_v4 = new RffiConnectionParametersV4();
  rffi_v4->ice_ufrag_borrowed = v4->ice_ufrag.c_str();
  rffi_v4->ice_pwd_borrowed = v4->ice_pwd.c_str();
  rffi_v4->bidirectional_video_codecs_borrowed =
      v4->bidirectional_video_codecs.data();
  rffi_v4->bidirectional_video_codecs_size =
      v4->bidirectional_video_codecs.size();
  rffi_v4->encode_only_video_codecs_borrowed = nullptr;
  rffi_v4->encode_only_video_codecs_size = 0;
  rffi_v4->decode_only_video_codecs_borrowed = nullptr;
  rffi_v4->decode_only_video_codecs_size = 0;
  rffi_v4->backing_owned = v4.release();
  return rffi_v4;
}

// Only used for case where remote advertises support for asymmetric codecs
RUSTEXPORT RffiConnectionParametersV4* Rust_sessionDescriptionToV4(
    const SessionDescriptionInterface* session_description_borrowed,
    bool enable_vp9_encode,
    bool enable_vp9_decode) {
  if (!session_description_borrowed) {
    return nullptr;
  }
  const SessionDescription* session =
      session_description_borrowed->description();
  if (!session) {
    return nullptr;
  }

  // Get ICE ufrag + pwd
  if (session->transport_infos().empty()) {
    return nullptr;
  }

  auto v4 = std::make_unique<ConnectionParametersV4>();

  auto* transport = &session->transport_infos()[0].description;
  v4->ice_ufrag = transport->ice_ufrag;
  v4->ice_pwd = transport->ice_pwd;

  // Get video codecs
  auto contains = [](const std::vector<RffiVideoCodec>& list,
                     RffiVideoCodecType codec_type) {
    return std::find_if(list.begin(), list.end(),
                        [codec_type](RffiVideoCodec codec) {
                          return codec.type == codec_type;
                        }) != list.end();
  };
  for (const auto& content : session->contents()) {
    if (!content.media_description() ||
        content.media_description()->type() != MediaType::VIDEO) {
      continue;
    }
    auto* video = content.media_description()->as_video();
    if (!video) {
      RTC_LOG(LS_ERROR)
          << "unexpected null pointer for video content; type was "
          << content.media_description()->type();
      continue;
    }
    auto direction = video->direction();

    for (const auto& codec : video->codecs()) {
      auto codec_type = PayloadStringToCodecType(codec.name);

      if (codec_type == kVideoCodecVP9) {
        if (enable_vp9_encode || enable_vp9_decode) {
          auto profile = ParseSdpForVP9Profile(codec.params);
          std::string profile_id_string;
          codec.GetParam("profile-id", &profile_id_string);
          if (!profile) {
            RTC_LOG(LS_WARNING) << "Ignoring VP9 codec because profile-id = "
                                << profile_id_string;
            continue;
          }

          if (profile != VP9Profile::kProfile0) {
            RTC_LOG(LS_WARNING)
                << "Ignoring VP9 codec with non-zero profile-id = "
                << profile_id_string;
            continue;
          }
          RffiVideoCodec vp9;
          vp9.type = kRffiVideoCodecVp9;

          if (enable_vp9_decode && enable_vp9_encode &&
              !contains(v4->bidirectional_video_codecs, kRffiVideoCodecVp9)) {
            v4->bidirectional_video_codecs.push_back(vp9);
          }

          if (enable_vp9_decode && RtpTransceiverDirectionHasRecv(direction) &&
              !contains(v4->decode_only_video_codecs, kRffiVideoCodecVp9)) {
            v4->decode_only_video_codecs.push_back(vp9);
          }
          if (enable_vp9_encode && RtpTransceiverDirectionHasSend(direction) &&
              !contains(v4->encode_only_video_codecs, kRffiVideoCodecVp9)) {
            v4->encode_only_video_codecs.push_back(vp9);
          }
        } else {
          RTC_LOG(LS_WARNING) << "Ignoring VP9 codec due to lack of support";
        }
      } else if (codec_type == kVideoCodecVP8) {
        RffiVideoCodec vp8;
        vp8.type = kRffiVideoCodecVp8;

        if (!contains(v4->bidirectional_video_codecs, kRffiVideoCodecVp8)) {
          v4->bidirectional_video_codecs.push_back(vp8);
        }

        if (RtpTransceiverDirectionHasRecv(direction) &&
            !contains(v4->decode_only_video_codecs, kRffiVideoCodecVp8)) {
          v4->decode_only_video_codecs.push_back(vp8);
        }
        if (RtpTransceiverDirectionHasSend(direction) &&
            !contains(v4->encode_only_video_codecs, kRffiVideoCodecVp8)) {
          v4->encode_only_video_codecs.push_back(vp8);
        }
      }
    }
  }

  sort_codecs(v4->bidirectional_video_codecs);
  sort_codecs(v4->encode_only_video_codecs);
  sort_codecs(v4->decode_only_video_codecs);

  auto* rffi_v4 = new RffiConnectionParametersV4();
  rffi_v4->ice_ufrag_borrowed = v4->ice_ufrag.c_str();
  rffi_v4->ice_pwd_borrowed = v4->ice_pwd.c_str();
  rffi_v4->bidirectional_video_codecs_borrowed =
      v4->bidirectional_video_codecs.data();
  rffi_v4->bidirectional_video_codecs_size =
      v4->bidirectional_video_codecs.size();
  rffi_v4->encode_only_video_codecs_borrowed =
      v4->encode_only_video_codecs.data();
  rffi_v4->encode_only_video_codecs_size = v4->encode_only_video_codecs.size();
  rffi_v4->decode_only_video_codecs_borrowed =
      v4->decode_only_video_codecs.data();
  rffi_v4->decode_only_video_codecs_size = v4->decode_only_video_codecs.size();
  rffi_v4->backing_owned = v4.release();
  return rffi_v4;
}

RUSTEXPORT void Rust_deleteV4(RffiConnectionParametersV4* v4_owned) {
  if (!v4_owned) {
    return;
  }

  delete v4_owned->backing_owned;
  delete v4_owned;
}

// Returns an owned pointer.
// Used to negotiate with clients that do not advertise support for asymmetric
// codecs.
RUSTEXPORT SessionDescriptionInterface* Rust_sessionDescriptionFromV4Legacy(
    bool offer,
    const RffiConnectionParametersV4* v4_borrowed,
    bool enable_tcc_audio,
    bool enable_vp9) {
  if (v4_borrowed->bidirectional_video_codecs_borrowed == nullptr) {
    RTC_LOG(LS_ERROR)
        << "Rust_sessionDescriptionFromV4Legacy: bidirectional codecs null";
    return nullptr;
  }
  // Major changes from the default WebRTC behavior:
  // 1. We remove all codecs except Opus, VP8, and VP9
  // 2. We remove all header extensions except for transport-cc, video
  //    orientation, and abs send time.
  // 3. Opus CBR and DTX is enabled.

  // For some reason, WebRTC insists that the video SSRCs for one side don't
  // overlap with SSRCs from the other side. To avoid potential problems, we'll
  // give the caller side 1XXX and the callee side 2XXX;
  uint32_t BASE_SSRC = offer ? 1000 : 2000;
  // 1001 and 2001 used by connection.rs
  uint32_t AUDIO_SSRC = BASE_SSRC + 2;
  uint32_t VIDEO_SSRC = BASE_SSRC + 3;
  uint32_t VIDEO_RTX_SSRC = BASE_SSRC + 13;

  auto transport = TransportDescription();
  transport.ice_mode = ICEMODE_FULL;
  transport.ice_ufrag = std::string(v4_borrowed->ice_ufrag_borrowed);
  transport.ice_pwd = std::string(v4_borrowed->ice_pwd_borrowed);
  transport.AddOption(ICE_OPTION_TRICKLE);
  transport.AddOption(ICE_OPTION_RENOMINATION);

  // DTLS is disabled
  transport.connection_role = CONNECTIONROLE_NONE;
  transport.identity_fingerprint = nullptr;

  auto set_rtp_params = [](MediaContentDescription* media) {
    media->set_protocol(kMediaProtocolSavpf);
    media->set_manually_specify_keys(true);
    media->set_rtcp_mux(true);
    media->set_direction(RtpTransceiverDirection::kSendRecv);
  };

  auto audio = std::make_unique<AudioContentDescription>();
  set_rtp_params(audio.get());
  auto video = std::make_unique<VideoContentDescription>();
  set_rtp_params(video.get());

  auto opus = CreateAudioCodec(OPUS_PT, kOpusCodecName, 48000, 2);
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
  opus.AddFeedbackParam(
      FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty));
  audio->AddCodec(opus);

  auto add_video_feedback_params = [](Codec* video_codec) {
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamCcm, kRtcpFbCcmParamFir));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kRtcpFbNackParamPli));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamRemb, kParamValueEmpty));
  };

  std::stable_sort(v4_borrowed->bidirectional_video_codecs_borrowed,
                   v4_borrowed->bidirectional_video_codecs_borrowed +
                       v4_borrowed->bidirectional_video_codecs_size,
                   [](const RffiVideoCodec lhs, const RffiVideoCodec rhs) {
                     return codecPriority(lhs) < codecPriority(rhs);
                   });

  for (size_t i = 0; i < v4_borrowed->bidirectional_video_codecs_size; i++) {
    RffiVideoCodec rffi_codec =
        v4_borrowed->bidirectional_video_codecs_borrowed[i];
    if (rffi_codec.type == kRffiVideoCodecVp9) {
      if (enable_vp9) {
        auto vp9 = CreateVideoCodec(VP9_PT, kVp9CodecName);
        vp9.params[kVP9FmtpProfileId] =
            VP9ProfileToString(VP9Profile::kProfile0);
        auto vp9_rtx = CreateVideoRtxCodec(VP9_RTX_PT, VP9_PT);
        vp9_rtx.params[kVP9FmtpProfileId] =
            VP9ProfileToString(VP9Profile::kProfile0);
        add_video_feedback_params(&vp9);

        video->AddCodec(vp9);
        video->AddCodec(vp9_rtx);
      }
    } else if (rffi_codec.type == kRffiVideoCodecVp8) {
      auto vp8 = CreateVideoCodec(VP8_PT, kVp8CodecName);
      auto vp8_rtx = CreateVideoRtxCodec(VP8_RTX_PT, VP8_PT);
      add_video_feedback_params(&vp8);

      video->AddCodec(vp8);
      video->AddCodec(vp8_rtx);
    }
  }

  // These are "meta codecs" for redundancy and FEC.
  // They are enabled by default currently with WebRTC.
  auto red = CreateVideoCodec(RED_PT, kRedCodecName);
  auto red_rtx = CreateVideoRtxCodec(RED_RTX_PT, RED_PT);
  auto ulpfec = CreateVideoCodec(ULPFEC_PT, kUlpfecCodecName);

  video->AddCodec(red);
  video->AddCodec(red_rtx);
  video->AddCodec(ulpfec);

  auto transport_cc1 =
      RtpExtension(TransportSequenceNumber::Uri(), TRANSPORT_CC1_EXT_ID);
  // TransportCC V2 is now enabled by default, but the difference is that V2
  // doesn't send periodic updates and instead waits for feedback requests.
  // Since the existing clients don't send feedback requests, we can't enable
  // V2. We'd have to add it to signaling to move from V1 to V2.
  auto video_orientation =
      RtpExtension(VideoOrientation ::Uri(), VIDEO_ORIENTATION_EXT_ID);
  // abs_send_time and tx_time_offset are used for more accurate REMB messages
  // from the receiver, which are used by googcc in some small ways. So, keep
  // it enabled. But it doesn't make sense to enable both abs_send_time and
  // tx_time_offset, so only use abs_send_time.
  auto abs_send_time =
      RtpExtension(AbsoluteSendTime::Uri(), ABS_SEND_TIME_EXT_ID);

  // Note: Using transport-cc with audio is still experimental in WebRTC.
  // And don't add abs_send_time because it's only used for video.
  if (enable_tcc_audio) {
    audio->AddRtpHeaderExtension(transport_cc1);
  }

  video->AddRtpHeaderExtension(transport_cc1);
  video->AddRtpHeaderExtension(video_orientation);
  video->AddRtpHeaderExtension(abs_send_time);

  auto audio_stream = StreamParams();
  audio_stream.id = kAudioTrackId;
  audio_stream.add_ssrc(AUDIO_SSRC);

  auto video_stream = StreamParams();
  video_stream.id = kVideoTrackId;
  video_stream.add_ssrc(VIDEO_SSRC);
  video_stream.AddFidSsrc(VIDEO_SSRC, VIDEO_RTX_SSRC);  // AKA RTX

  // Things that are the same for all of them
  for (auto* stream : {&audio_stream, &video_stream}) {
    // WebRTC just generates a random 16-byte string for the entire
    // PeerConnection. It's used to send an SDES RTCP message. The value doesn't
    // seem to be used for anything else. We'll set it around just in case. But
    // everything seems to work fine without it.
    stream->cname = "CNAMECNAMECNAME!";

    stream->set_stream_ids({"s"});
  }

  audio->AddStream(audio_stream);
  video->AddStream(video_stream);
  video->set_rtcp_reduced_size(true);

  // Keep the order as the WebRTC default: (audio, video, data).
  auto audio_content_name = "audio";
  auto video_content_name = "video";

  auto session = std::make_unique<SessionDescription>();
  session->AddTransportInfo({audio_content_name, transport});
  session->AddTransportInfo({video_content_name, transport});

  bool stopped = false;
  session->AddContent(audio_content_name, MediaProtocolType::kRtp, stopped,
                      std::move(audio));
  session->AddContent(video_content_name, MediaProtocolType::kRtp, stopped,
                      std::move(video));

  auto bundle = ContentGroup(GROUP_TYPE_BUNDLE);
  bundle.AddContentName(audio_content_name);
  bundle.AddContentName(video_content_name);
  session->AddGroup(bundle);

  session->set_msid_signaling(kMsidSignalingMediaSection);

  auto typ = offer ? SdpType::kOffer : SdpType::kAnswer;
  return SessionDescriptionInterface::Create(typ, std::move(session), "1", "1")
      .release();
}

// Returns an owned pointer.
// Used only for clients that advertise support for asymmetric codecs.
RUSTEXPORT SessionDescriptionInterface* Rust_sessionDescriptionFromV4(
    bool offer,
    const RffiConnectionParametersV4* v4_borrowed,
    bool enable_tcc_audio,
    bool enable_vp9_encode,
    bool enable_vp9_decode,
    bool v4_is_local) {
  if (v4_borrowed->decode_only_video_codecs_borrowed == nullptr ||
      v4_borrowed->encode_only_video_codecs_borrowed == nullptr) {
    RTC_LOG(LS_ERROR)
        << "Rust_sessionDescriptionFromV4: a required codec field was null";
    return nullptr;
  }
  if (v4_borrowed->decode_only_video_codecs_size == 0 ||
      v4_borrowed->encode_only_video_codecs_size == 0) {
    RTC_LOG(LS_ERROR) << "Rust_sessionDescriptionFromV4: required codec field "
                         "was empty: decode: "
                      << v4_borrowed->decode_only_video_codecs_size
                      << "; encode: "
                      << v4_borrowed->encode_only_video_codecs_size;
    return nullptr;
  }
  // Major changes from the default WebRTC behavior:
  // 1. We remove all codecs except Opus, VP8, and VP9
  // 2. We remove all header extensions except for transport-cc, video
  //    orientation, and abs send time.
  // 3. Opus CBR and DTX is enabled.
  // 4. We allow different video codecs for send vs receive

  // The SSRCs for one side cannot overlap with SSRCs from the other side,
  // because otherwise we couldn't distinguish duplex communication with
  // deliberate reuse of the same SSRC for simplex communication (e.g. a
  // response to a request).
  // So, we give the caller side 1XXX and the callee side 2XXX.
  uint32_t BASE_SSRC = offer ? 1000 : 2000;
  // 1001 and 2001 used by connection.rs
  uint32_t AUDIO_SSRC = BASE_SSRC + 2;
  uint32_t VIDEO_SSRC = BASE_SSRC + 3;
  uint32_t VIDEO_RTX_SSRC = BASE_SSRC + 13;

  auto transport = TransportDescription();
  transport.ice_mode = ICEMODE_FULL;
  transport.ice_ufrag = std::string(v4_borrowed->ice_ufrag_borrowed);
  transport.ice_pwd = std::string(v4_borrowed->ice_pwd_borrowed);
  transport.AddOption(ICE_OPTION_TRICKLE);
  transport.AddOption(ICE_OPTION_RENOMINATION);

  // DTLS is disabled
  transport.connection_role = CONNECTIONROLE_NONE;
  transport.identity_fingerprint = nullptr;

  auto set_rtp_params = [](MediaContentDescription* media) {
    media->set_protocol(kMediaProtocolSavpf);
    media->set_manually_specify_keys(true);
    media->set_rtcp_mux(true);
  };

  auto audio = std::make_unique<AudioContentDescription>();
  set_rtp_params(audio.get());
  audio->set_direction(RtpTransceiverDirection::kSendRecv);

  auto video_send = std::make_unique<VideoContentDescription>();
  set_rtp_params(video_send.get());
  video_send->set_direction(RtpTransceiverDirection::kSendOnly);

  auto video_recv = std::make_unique<VideoContentDescription>();
  set_rtp_params(video_recv.get());
  video_recv->set_direction(RtpTransceiverDirection::kRecvOnly);

  auto opus = CreateAudioCodec(OPUS_PT, kOpusCodecName, 48000, 2);
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
  opus.AddFeedbackParam(
      FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty));
  audio->AddCodec(opus);

  auto add_video_feedback_params = [](Codec* video_codec) {
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamCcm, kRtcpFbCcmParamFir));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamNack, kRtcpFbNackParamPli));
    video_codec->AddFeedbackParam(
        FeedbackParam(kRtcpFbParamRemb, kParamValueEmpty));
  };

  sort_codecs(v4_borrowed->encode_only_video_codecs_borrowed,
              v4_borrowed->encode_only_video_codecs_size);
  sort_codecs(v4_borrowed->decode_only_video_codecs_borrowed,
              v4_borrowed->decode_only_video_codecs_size);

  // OK to reuse this for send and receive; it's copied.
  auto vp9 = CreateVideoCodec(VP9_PT, kVp9CodecName);
  vp9.params[kVP9FmtpProfileId] = VP9ProfileToString(VP9Profile::kProfile0);
  auto vp9_rtx = CreateVideoRtxCodec(VP9_RTX_PT, VP9_PT);
  vp9_rtx.params[kVP9FmtpProfileId] = VP9ProfileToString(VP9Profile::kProfile0);
  add_video_feedback_params(&vp9);

  auto vp8 = CreateVideoCodec(VP8_PT, kVp8CodecName);
  auto vp8_rtx = CreateVideoRtxCodec(VP8_RTX_PT, VP8_PT);
  add_video_feedback_params(&vp8);

  for (size_t i = 0; i < v4_borrowed->decode_only_video_codecs_size; i++) {
    RffiVideoCodec rffi_codec =
        v4_borrowed->decode_only_video_codecs_borrowed[i];
    if (rffi_codec.type == kRffiVideoCodecVp9) {
      // If this v4 is from the local, only add vp9 to our video_recv if we
      // can decode it. If this v4 is from the remote, and it indicates
      // vp9 decode support, only add vp9 to video_recv if we can send
      // it to them.
      // In practice, since Rust_sessionDescriptionToV4 also checks the vp9
      // flags before generating V4, if we have a locally-generated V4 that
      // contains vp9, enable_vp9_decode will always be true. So, we could
      // condense this condition a bit, but we keep it explicit for readability.
      if ((v4_is_local && enable_vp9_decode) ||
          (!v4_is_local && enable_vp9_encode)) {
        video_recv->AddCodec(vp9);
        video_recv->AddCodec(vp9_rtx);
      }
    } else if (rffi_codec.type == kRffiVideoCodecVp8) {
      video_recv->AddCodec(vp8);
      video_recv->AddCodec(vp8_rtx);
    }
  }

  for (size_t i = 0; i < v4_borrowed->encode_only_video_codecs_size; i++) {
    RffiVideoCodec rffi_codec =
        v4_borrowed->encode_only_video_codecs_borrowed[i];
    if (rffi_codec.type == kRffiVideoCodecVp9) {
      // Similar logic as the receive case
      if ((v4_is_local && enable_vp9_encode) ||
          (!v4_is_local && enable_vp9_decode)) {
        video_send->AddCodec(vp9);
        video_send->AddCodec(vp9_rtx);
      }
    } else if (rffi_codec.type == kRffiVideoCodecVp8) {
      video_send->AddCodec(vp8);
      video_send->AddCodec(vp8_rtx);
    }
  }

  // These are "meta codecs" for redundancy and FEC.
  // They are enabled by default currently with WebRTC.
  auto red = CreateVideoCodec(RED_PT, kRedCodecName);
  auto red_rtx = CreateVideoRtxCodec(RED_RTX_PT, RED_PT);
  auto ulpfec = CreateVideoCodec(ULPFEC_PT, kUlpfecCodecName);

  auto transport_cc1 =
      RtpExtension(TransportSequenceNumber::Uri(), TRANSPORT_CC1_EXT_ID);
  // TransportCC V2 is now enabled by default, but the difference is that V2
  // doesn't send periodic updates and instead waits for feedback requests.
  // Since the existing clients don't send feedback requests, we can't enable
  // V2. We'd have to add it to signaling to move from V1 to V2.
  auto video_orientation =
      RtpExtension(VideoOrientation ::Uri(), VIDEO_ORIENTATION_EXT_ID);
  // abs_send_time and tx_time_offset are used for more accurate REMB messages
  // from the receiver, which are used by googcc in some small ways. So, keep
  // it enabled. But it doesn't make sense to enable both abs_send_time and
  // tx_time_offset, so only use abs_send_time.
  auto abs_send_time =
      RtpExtension(AbsoluteSendTime::Uri(), ABS_SEND_TIME_EXT_ID);

  for (auto* video : {video_send.get(), video_recv.get()}) {
    video->AddCodec(red);
    video->AddCodec(red_rtx);
    video->AddCodec(ulpfec);

    video->AddRtpHeaderExtension(transport_cc1);
    video->AddRtpHeaderExtension(video_orientation);
    video->AddRtpHeaderExtension(abs_send_time);

    video->set_rtcp_reduced_size(true);
  }

  // Note: Using transport-cc with audio is still experimental in WebRTC.
  // And don't add abs_send_time because it's only used for video.
  if (enable_tcc_audio) {
    audio->AddRtpHeaderExtension(transport_cc1);
  }

  auto audio_stream = StreamParams();
  audio_stream.id = kAudioTrackId;
  audio_stream.add_ssrc(AUDIO_SSRC);

  // Note that we only need one video stream here: the stream refers to media
  // that is being sent, so video_recv doesn't need an associated stream.
  auto video_stream = StreamParams();
  video_stream.id = kVideoTrackId;
  video_stream.add_ssrc(VIDEO_SSRC);
  video_stream.AddFidSsrc(VIDEO_SSRC, VIDEO_RTX_SSRC);  // AKA RTX

  // Things that are the same for all of them
  for (auto* stream : {&audio_stream, &video_stream}) {
    // WebRTC just generates a random 16-byte string for the entire
    // PeerConnection. It's used to send an SDES RTCP message. The value doesn't
    // seem to be used for anything else. We'll set it around just in case. But
    // everything seems to work fine without it.
    stream->cname = "CNAMECNAMECNAME!";

    stream->set_stream_ids({"s"});
  }

  audio->AddStream(audio_stream);
  video_send->AddStream(video_stream);

  // Keep the order as the WebRTC default: (audio, video, data).

  // Order needs to match between offer and answer (if the
  // stream from A to B is first in offer, it must be first in answer).
  // This means that if video-mid-0 is first in the offer, it must be first in
  // the answer. Since we arbitrarily choose to have the offer side's send
  // stream referred to as video-mid-0:
  // (a) the answer side must ensure that video-mid-0 is their receive stream
  // (b) the answer side must also put video-mid-0 first in their answer
  //     (accomplished by changing the order of `add_stream` calls, below)

  auto audio_content_name = "audio";
  auto video_send_content_name = offer ? "video-mid-0" : "video-mid-1";
  auto video_recv_content_name = offer ? "video-mid-1" : "video-mid-0";

  auto session = std::make_unique<SessionDescription>();
  auto bundle = ContentGroup(GROUP_TYPE_BUNDLE);

  auto add_stream = [&session, &transport, &bundle](
                        const char* stream_name,
                        std::unique_ptr<MediaContentDescription> stream) {
    bool stopped = false;
    session->AddTransportInfo(TransportInfo(stream_name, transport));
    session->AddContent(stream_name, MediaProtocolType::kRtp, stopped,
                        std::move(stream));
    bundle.AddContentName(stream_name);
  };

  add_stream(audio_content_name, std::move(audio));

  if (offer) {
    add_stream(video_send_content_name, std::move(video_send));
    add_stream(video_recv_content_name, std::move(video_recv));
  } else {
    add_stream(video_recv_content_name, std::move(video_recv));
    add_stream(video_send_content_name, std::move(video_send));
  }

  session->AddGroup(bundle);

  session->set_msid_signaling(kMsidSignalingMediaSection);

  auto typ = offer ? SdpType::kOffer : SdpType::kAnswer;
  return SessionDescriptionInterface::Create(typ, std::move(session), "1", "1")
      .release();
}

// Returns an owned pointer.
RUSTEXPORT SessionDescriptionInterface* Rust_localDescriptionForGroupCall(
    const char* ice_ufrag_borrowed,
    const char* ice_pwd_borrowed,
    RffiSrtpKey client_srtp_key,
    uint32_t local_demux_id,
    const uint32_t* remote_demux_ids_borrowed,
    size_t remote_demux_ids_len,
    const uint32_t* remote_demux_ids_require_svc_borrowed,
    size_t remote_demux_ids_needs_svc_len,
    bool enable_svc_encode) {
  const std::span remote_demux_ids(
      remote_demux_ids_borrowed,
      remote_demux_ids_borrowed + remote_demux_ids_len);
  const std::span remote_demux_ids_needs_svc(
      remote_demux_ids_require_svc_borrowed,
      remote_demux_ids_require_svc_borrowed + remote_demux_ids_needs_svc_len);
  return LocalGroupCallSessionDescriptionBuilder(local_demux_id,
                                                 client_srtp_key)
      .enable_svc_encode(enable_svc_encode)
      .with_ice_credentials(ice_ufrag_borrowed, ice_pwd_borrowed)
      .with_remote_demux_ids(remote_demux_ids)
      .with_remote_demux_ids_require_svc(remote_demux_ids_needs_svc)
      .Build();
}

// Returns an owned pointer.
RUSTEXPORT SessionDescriptionInterface* Rust_remoteDescriptionForGroupCall(
    const char* ice_ufrag_borrowed,
    const char* ice_pwd_borrowed,
    RffiSrtpKey server_srtp_key,
    uint32_t local_demux_id,
    const uint32_t* remote_demux_ids_borrowed,
    size_t remote_demux_ids_len,
    const uint32_t* remote_demux_ids_require_svc_borrowed,
    size_t remote_demux_ids_needs_svc_len,
    bool enable_svc_encode) {
  const std::span remote_demux_ids(
      remote_demux_ids_borrowed,
      remote_demux_ids_borrowed + remote_demux_ids_len);
  const std::span remote_demux_ids_needs_svc(
      remote_demux_ids_require_svc_borrowed,
      remote_demux_ids_require_svc_borrowed + remote_demux_ids_needs_svc_len);
  return RemoteGroupCallSessionDescriptionBuilder(local_demux_id,
                                                  server_srtp_key)
      .enable_svc_encode(enable_svc_encode)
      .with_ice_credentials(ice_ufrag_borrowed, ice_pwd_borrowed)
      .with_remote_demux_ids(remote_demux_ids)
      .with_remote_demux_ids_require_svc(remote_demux_ids_needs_svc)
      .Build();
}

RUSTEXPORT void Rust_createAnswer(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    CreateSessionDescriptionObserverRffi* csd_observer_borrowed_rc) {
  // No constraints are set
  MediaConstraints constraints = MediaConstraints();
  PeerConnectionInterface::RTCOfferAnswerOptions options;

  CopyConstraintsIntoOfferAnswerOptions(&constraints, &options);
  peer_connection_borrowed_rc->CreateAnswer(csd_observer_borrowed_rc, options);
}

RUSTEXPORT void Rust_setRemoteDescription(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    SetSessionDescriptionObserverRffi* ssd_observer_borrowed_rc,
    SessionDescriptionInterface* description_owned) {
  peer_connection_borrowed_rc->SetRemoteDescription(ssd_observer_borrowed_rc,
                                                    description_owned);
}

RUSTEXPORT void Rust_deleteSessionDescription(
    SessionDescriptionInterface* description_owned) {
  delete description_owned;
}

RUSTEXPORT void Rust_setOutgoingMediaEnabled(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    bool enabled) {
  RTC_LOG(LS_INFO) << "Rust_setOutgoingMediaEnabled(" << enabled << ")";
  int encodings_changed = 0;
  for (auto& sender : peer_connection_borrowed_rc->GetSenders()) {
    RtpParameters parameters = sender->GetParameters();
    for (auto& encoding : parameters.encodings) {
      RTC_LOG(LS_INFO) << "Rust_setOutgoingMediaEnabled() encoding.active was: "
                       << encoding.active;
      encoding.active = enabled;
      encodings_changed++;
    }
    sender->SetParameters(parameters);
  }
  RTC_LOG(LS_INFO) << "Rust_setOutgoingMediaEnabled(" << enabled << ") for "
                   << encodings_changed << " encodings.";
}

RUSTEXPORT bool Rust_setIncomingMediaEnabled(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    bool enabled) {
  RTC_LOG(LS_INFO) << "Rust_setIncomingMediaEnabled(" << enabled << ")";
  return peer_connection_borrowed_rc->SetIncomingRtpEnabled(enabled);
}

RUSTEXPORT void Rust_setAudioPlayoutEnabled(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    bool enabled) {
  RTC_LOG(LS_INFO) << "Rust_setAudioPlayoutEnabled(" << enabled << ")";
  peer_connection_borrowed_rc->SetAudioPlayout(enabled);
}

RUSTEXPORT void Rust_setAudioRecordingEnabled(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    bool enabled) {
  RTC_LOG(LS_INFO) << "Rust_setAudioRecordingEnabled(" << enabled << ")";
  peer_connection_borrowed_rc->SetAudioRecording(enabled);
}

RUSTEXPORT bool Rust_addIceCandidateFromSdp(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    const char* sdp_borrowed) {
  // Since we always use bundle, we can always use index 0 and ignore the mid
  std::unique_ptr<IceCandidate> ice_candidate(
      CreateIceCandidate("", 0, std::string(sdp_borrowed), nullptr));

  return peer_connection_borrowed_rc->AddIceCandidate(ice_candidate.get());
}

RUSTEXPORT bool Rust_removeIceCandidates(
    PeerConnectionInterface* pc_borrowed_rc,
    IpPort* removed_addresses_data_borrowed,
    size_t removed_addresses_len) {
  if (removed_addresses_len == 0) {
    RTC_LOG(LS_ERROR) << "Rust_removeIceCandidates: no candidates to remove";
    return false;
  } else {
    std::vector<IpPort> removed_addresses;
    removed_addresses.assign(
        removed_addresses_data_borrowed,
        removed_addresses_data_borrowed + removed_addresses_len);

    size_t number_removed = 0;
    for (const auto& address_removed : removed_addresses) {
      // This only needs to contain the correct transport_name, component,
      // protocol, and address. SeeCandidate::MatchesForRemoval and
      // JsepTransportController::RemoveRemoteCandidates and
      // JsepTransportController::RemoveRemoteCandidates. But we know (because
      // we bundle/rtcp-mux everything) that the transport name is "audio", and
      // the component is 1. We also know (because we don't use TCP candidates)
      // that the protocol is UDP. So we only need to know the address.
      Candidate candidate_removed;
      candidate_removed.set_component(ICE_CANDIDATE_COMPONENT_RTP);
      candidate_removed.set_protocol(UDP_PROTOCOL_NAME);
      candidate_removed.set_address(IpPortToRtcSocketAddress(address_removed));

      IceCandidate candidate("audio", /*sdp_mline_index=*/-1,
                             candidate_removed);
      if (pc_borrowed_rc->RemoveIceCandidate(&candidate)) {
        number_removed++;
      }
    }

    if (number_removed != removed_addresses.size()) {
      RTC_LOG(LS_ERROR)
          << "Rust_removeIceCandidates: Failed to remove candidates: Requested "
          << removed_addresses.size() << " but only " << number_removed
          << " are removed.";
    }

    return true;
  }
}

RUSTEXPORT bool Rust_addIceCandidateFromServer(
    PeerConnectionInterface* pc_borrowed_rc,
    Ip ip,
    uint16_t port,
    bool tcp,
    const char* hostname) {
  Candidate candidate;
  // The default foundation is "", which is fine because we bundle.
  // The default generation is 0, which is fine because we don't do ICE
  // restarts. The default username and password are "", which is fine because
  // P2PTransportChannel::AddRemoteCandidate looks up the ICE ufrag and pwd
  // from the remote description when the candidate's copy is empty.
  // Unset network ID, network cost, and network type are fine because they are
  // for p2p use. An unset relay protocol is fine because we aren't doing relay.
  // An unset related address is fine because we aren't doing relay or STUN.
  //
  // The critical values are component, type, protocol, and address, so we set
  // those.
  //
  // The component doesn't really matter because we use RTCP-mux, so there is
  // only one component. However, WebRTC expects it to be set to
  // ICE_CANDIDATE_COMPONENT_RTP(1), so we do that.
  //
  // The priority is also important for controlling whether we prefer IPv4 or
  // IPv6 when both are available. WebRTC generally prefers IPv6 over IPv4 for
  // local candidates (see rtc_base::IPAddressPrecedence). So we leave the
  // priority unset to allow the local candidate preference to break the tie.
  candidate.set_component(ICE_CANDIDATE_COMPONENT_RTP);
  candidate.set_type(IceCandidateType::kHost);

  if (tcp && hostname != NULL) {
    SocketAddress addr = SocketAddress(std::string(hostname), port);
    addr.SetResolvedIP(IpToRtcIp(ip));
    candidate.set_address(addr);
    candidate.set_protocol(TLS_PROTOCOL_NAME);
  } else {
    candidate.set_address(SocketAddress(IpToRtcIp(ip), port));
    candidate.set_protocol(tcp ? TCP_PROTOCOL_NAME : UDP_PROTOCOL_NAME);
  }

  // Since we always use bundle, we can always use index 0 and ignore the mid
  std::unique_ptr<IceCandidate> ice_candidate(
      CreateIceCandidate("", 0, candidate));

  return pc_borrowed_rc->AddIceCandidate(ice_candidate.get());
}

RUSTEXPORT IceGathererInterface* Rust_createSharedIceGatherer(
    PeerConnectionInterface* peer_connection_borrowed_rc) {
  return take_rc(peer_connection_borrowed_rc->CreateSharedIceGatherer());
}

RUSTEXPORT bool Rust_useSharedIceGatherer(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    IceGathererInterface* ice_gatherer_borrowed_rc) {
  return peer_connection_borrowed_rc->UseSharedIceGatherer(
      inc_rc(ice_gatherer_borrowed_rc));
}

RUSTEXPORT void Rust_getStats(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    StatsObserverRffi* stats_observer_borrowed_rc) {
  peer_connection_borrowed_rc->GetStats(stats_observer_borrowed_rc);
}

// This is fairly complex in WebRTC, but I think it's something like this:
// Must be that 0 <= min <= start <= max.
// But any value can be unset (-1). If so, here is what happens:
// If min isn't set, either use 30kbps (from
// PeerConnectionFactory::CreateCall_w) or no min (0 from
// WebRtcVideoChannel::ApplyChangedParams)
// If start isn't set, use the previous start; initially 100kbps (from
// PeerConnectionFactory::CreateCall_w)
// If max isn't set, either use 2mbps (from
// PeerConnectionFactory::CreateCall_w) or no max (-1 from
// WebRtcVideoChannel::ApplyChangedParams
// If min and max are set but haven't changed since last the last unset value,
// nothing happens. There is only an action if either min or max has changed
// or start is set.
RUSTEXPORT void Rust_setSendBitrates(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    int32_t min_bitrate_bps,
    int32_t start_bitrate_bps,
    int32_t max_bitrate_bps) {
  struct BitrateSettings bitrate_settings;
  if (min_bitrate_bps >= 0) {
    bitrate_settings.min_bitrate_bps = min_bitrate_bps;
  }
  if (start_bitrate_bps >= 0) {
    bitrate_settings.start_bitrate_bps = start_bitrate_bps;
  }
  if (max_bitrate_bps >= 0) {
    bitrate_settings.max_bitrate_bps = max_bitrate_bps;
  }
  peer_connection_borrowed_rc->SetBitrate(bitrate_settings);
}

// Warning: this blocks on the WebRTC network thread, so avoid calling it
// while holding a lock, especially a lock also taken in a callback
// from the network thread.
RUSTEXPORT bool Rust_sendRtp(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    uint8_t pt,
    uint16_t seqnum,
    uint32_t timestamp,
    uint32_t ssrc,
    const uint8_t* payload_data_borrowed,
    size_t payload_size) {
  size_t packet_size =
      12 /* RTP header */ + payload_size + 16 /* SRTP footer */;
  std::unique_ptr<RtpPacket> packet(
      new RtpPacket(nullptr /* header extension map */, packet_size));
  packet->SetPayloadType(pt);
  packet->SetSequenceNumber(seqnum);
  packet->SetTimestamp(timestamp);
  packet->SetSsrc(ssrc);
  memcpy(packet->AllocatePayload(payload_size), payload_data_borrowed,
         payload_size);
  return peer_connection_borrowed_rc->SendRtp(std::move(packet));
}

// Warning: this blocks on the WebRTC network thread, so avoid calling it
// while holding a lock, especially a lock also taken in a callback
// from the network thread.
RUSTEXPORT bool Rust_receiveRtp(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    uint8_t pt,
    bool enable_incoming) {
  return peer_connection_borrowed_rc->ReceiveRtp(pt, enable_incoming);
}

RUSTEXPORT void Rust_configureAudioEncoders(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    const AudioEncoder::Config* config_borrowed) {
  RTC_LOG(LS_INFO) << "Rust_configureAudioEncoders(...)";
  peer_connection_borrowed_rc->ConfigureAudioEncoders(*config_borrowed);
}

RUSTEXPORT void Rust_configureAudioDecoders(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    const AudioDecoder::Config* config_borrowed) {
  RTC_LOG(LS_INFO) << "Rust_configureAudioDecoders(...)";
  peer_connection_borrowed_rc->ConfigureAudioDecoders(*config_borrowed);
}

RUSTEXPORT void Rust_getAudioLevels(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    uint16_t* captured_out,
    ReceivedAudioLevel* received_out,
    size_t received_out_size,
    size_t* received_size_out) {
  RTC_LOG(LS_VERBOSE) << "Rust_getAudioLevels(...)";
  peer_connection_borrowed_rc->GetAudioLevels(
      captured_out, received_out, received_out_size, received_size_out);
}

RUSTEXPORT uint32_t Rust_getLastBandwidthEstimateBps(
    PeerConnectionInterface* peer_connection_borrowed_rc) {
  RTC_LOG(LS_VERBOSE) << "Rust_getLastBandwidthEstimateBps(...)";
  return peer_connection_borrowed_rc->GetLastBandwidthEstimateBps();
}

RUSTEXPORT void Rust_setRtpPacketObserver(
    PeerConnectionInterface* peer_connection_borrowed_rc,
    RtpObserverRffi* rtp_observer_borrowed) {
  RTC_LOG(LS_INFO) << "Rust_setRtpPacketObserver";
  peer_connection_borrowed_rc->SetRtpPacketObserver(rtp_observer_borrowed);
}

RUSTEXPORT void Rust_closePeerConnection(
    PeerConnectionInterface* peer_connection_borrowed_rc) {
  peer_connection_borrowed_rc->Close();
}

RUSTEXPORT void Rust_regatherOnAllNetworks(
    PeerConnectionInterface* peer_connection_borrowed_rc) {
  peer_connection_borrowed_rc->RegatherOnAllNetworks();
}

}  // namespace rffi
}  // namespace webrtc
