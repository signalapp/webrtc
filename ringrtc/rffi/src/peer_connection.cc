/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "api/ice_gatherer_interface.h"
#include "api/ice_transport_interface.h"
#include "api/jsep_session_description.h"
#include "api/peer_connection_interface.h"
#include "api/video_codecs/vp9_profile.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "p2p/base/port.h"
#include "pc/media_session.h"
#include "pc/sdp_utils.h"
#include "pc/session_description.h"
#include "sdk/media_constraints.h"
#include "rffi/api/peer_connection_intf.h"
#include "rffi/src/ptr.h"
#include "rffi/src/sdp_observer.h"
#include "rffi/src/stats_observer.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/third_party/base64/base64.h"
#include "system_wrappers/include/field_trial.h"

#include <algorithm>
#include <string>

namespace webrtc {
namespace rffi {

int TRANSPORT_CC1_EXT_ID = 1;
int VIDEO_ORIENTATION_EXT_ID = 4;
int AUDIO_LEVEL_EXT_ID = 5;
int ABS_SEND_TIME_EXT_ID = 12;
// Old clients used this value, so don't use it until they are all gone.
int TX_TIME_OFFSET_EXT_ID = 13;

// Payload types must be over 96 and less than 128.
// 101 used by connection.rs
int DATA_PT = 101;
int OPUS_PT = 102;
int OPUS_RED_PT = 105;
int VP8_PT = 108;
int VP8_RTX_PT = 118;
int VP9_PT = 109;
int VP9_RTX_PT = 119;
int H264_CHP_PT = 104;
int H264_CHP_RTX_PT = 114;
int H264_CBP_PT = 103;
int H264_CBP_RTX_PT = 113;
int RED_PT = 120;
int RED_RTX_PT = 121;
int ULPFEC_PT = 122;

const uint32_t DISABLED_DEMUX_ID = 0;

RUSTEXPORT bool
Rust_updateTransceivers(webrtc::PeerConnectionInterface*      peer_connection_borrowed_rc,
                        uint32_t*                             remote_demux_ids_data_borrowed,
                        size_t                                length) {
  std::vector<uint32_t> remote_demux_ids;
  remote_demux_ids.assign(remote_demux_ids_data_borrowed, remote_demux_ids_data_borrowed + length);

  auto transceivers = peer_connection_borrowed_rc->GetTransceivers();
  // There should be at most 2 transceivers for each remote demux ID (there can
  // be fewer if new transceivers are about to be created), excluding the 2
  // transceivers for the local device's audio and video.
  if (remote_demux_ids.size() * 2 < transceivers.size() - 2) {
    RTC_LOG(LS_WARNING) << "Mismatched remote_demux_ids and transceivers count:"
      << " remote_demux_ids.size()=" << remote_demux_ids.size()
      << ", transceivers.size()=" << transceivers.size();
  }

  size_t remote_demux_ids_i = 0;
  for (auto transceiver : transceivers) {
    auto direction = transceiver->direction();
    if (direction != RtpTransceiverDirection::kInactive && direction != RtpTransceiverDirection::kRecvOnly) {
      // This is a transceiver used by the local device to send media.
      continue;
    }

    auto ids = transceiver->receiver()->stream_ids();

    if (remote_demux_ids_i < remote_demux_ids.size()) {
      auto desired_demux_id = remote_demux_ids[remote_demux_ids_i];
      if (desired_demux_id == DISABLED_DEMUX_ID) {
        transceiver->SetDirectionWithError(RtpTransceiverDirection::kInactive);
      } else if (ids.empty() || ids[0] != rtc::ToString(desired_demux_id)) {
        // This transceiver is being reused
        transceiver->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly);
      }
    }

    // The same demux ID is used for both the audio and video transceiver, and
    // audio is added first. So only advance to the next demux ID after seeing
    // a video transceiver.
    if (transceiver->media_type() == cricket::MEDIA_TYPE_VIDEO) {
      remote_demux_ids_i++;
    }
  }

  // Create transceivers for the remaining remote_demux_ids.
  for (auto i = remote_demux_ids_i; i < remote_demux_ids.size(); i++) {
    auto remote_demux_id = remote_demux_ids[i];

    RtpTransceiverInit init;
    init.direction = RtpTransceiverDirection::kRecvOnly;
    init.stream_ids = {rtc::ToString(remote_demux_id)};

    auto result = peer_connection_borrowed_rc->AddTransceiver(cricket::MEDIA_TYPE_AUDIO, init);
    if (!result.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to PeerConnection::AddTransceiver(audio)";
      return false;
    }

    result = peer_connection_borrowed_rc->AddTransceiver(cricket::MEDIA_TYPE_VIDEO, init);
    if (!result.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to PeerConnection::AddTransceiver(video)";
      return false;
    }
  }

  return true;
}

// Borrows the observer until the result is given to the observer,
// so the observer must stay alive until it's given a result.
RUSTEXPORT void
Rust_createOffer(PeerConnectionInterface*              peer_connection_borrowed_rc,
                 CreateSessionDescriptionObserverRffi* csd_observer_borrowed_rc) {

  // No constraints are set
  MediaConstraints constraints = MediaConstraints();
  PeerConnectionInterface::RTCOfferAnswerOptions options;

  CopyConstraintsIntoOfferAnswerOptions(&constraints, &options);
  peer_connection_borrowed_rc->CreateOffer(csd_observer_borrowed_rc, options);
}

// Borrows the observer until the result is given to the observer,
// so the observer must stay alive until it's given a result.
RUSTEXPORT void
Rust_setLocalDescription(PeerConnectionInterface*           peer_connection_borrowed_rc,
                         SetSessionDescriptionObserverRffi* ssd_observer_borrowed_rc,
                         SessionDescriptionInterface*       local_description_owned) {
  peer_connection_borrowed_rc->SetLocalDescription(ssd_observer_borrowed_rc, local_description_owned);
}

// Returns an owned pointer.
RUSTEXPORT const char*
Rust_toSdp(SessionDescriptionInterface* session_description_borrowed) {

  std::string sdp;
  if (session_description_borrowed->ToString(&sdp)) {
    return strdup(&sdp[0u]);
  }

  RTC_LOG(LS_ERROR) << "Unable to convert SessionDescription to SDP";
  return nullptr;
}

// Returns an owned pointer.
static SessionDescriptionInterface*
createSessionDescriptionInterface(SdpType type, const char* sdp_borrowed) {

  if (sdp_borrowed != nullptr) {
    return CreateSessionDescription(type, std::string(sdp_borrowed)).release();
  } else {
    return nullptr;
  }
}

// Returns an owned pointer.
RUSTEXPORT SessionDescriptionInterface*
Rust_answerFromSdp(const char* sdp_borrowed) {
  return createSessionDescriptionInterface(SdpType::kAnswer, sdp_borrowed);
}

RUSTEXPORT SessionDescriptionInterface*
Rust_offerFromSdp(const char* sdp_borrowed) {
  return createSessionDescriptionInterface(SdpType::kOffer, sdp_borrowed);
}

RUSTEXPORT bool
Rust_disableDtlsAndSetSrtpKey(webrtc::SessionDescriptionInterface* session_description_borrowed,
                              int                                  crypto_suite,
                              const char*                          key_borrowed,
                              size_t                               key_len,
                              const char*                          salt_borrowed,
                              size_t                               salt_len) {
  if (!session_description_borrowed) {
    return false;
  }

  cricket::SessionDescription* session = session_description_borrowed->description();
  if (!session) {
    return false;
  }

  cricket::CryptoParams crypto_params;
  crypto_params.crypto_suite = rtc::SrtpCryptoSuiteToName(crypto_suite);

  std::string key(key_borrowed, key_len);
  std::string salt(salt_borrowed, salt_len);
  crypto_params.key_params = "inline:" + rtc::Base64::Encode(key + salt);

  // Disable DTLS
  for (cricket::TransportInfo& transport : session->transport_infos()) {
    transport.description.connection_role = cricket::CONNECTIONROLE_NONE;
    transport.description.identity_fingerprint = nullptr;
  }

  // Set SRTP key
  for (cricket::ContentInfo& content : session->contents()) {
    cricket::MediaContentDescription* media = content.media_description();
    if (media) {
      media->set_protocol(cricket::kMediaProtocolSavpf);
      std::vector<cricket::CryptoParams> cryptos;
      cryptos.push_back(crypto_params);
      media->set_cryptos(cryptos);
    }
  }

  return true;
}

static int
codecPriority(const RffiVideoCodec c) {
  // Lower values are given higher priority
  switch (c.type) {
    case kRffiVideoCodecVp9: return 0;
    case kRffiVideoCodecVp8: return 1;
    default: return 100;
  }
}

RUSTEXPORT RffiConnectionParametersV4*
Rust_sessionDescriptionToV4(const webrtc::SessionDescriptionInterface* session_description_borrowed,
                            bool enable_vp9) {
  if (!session_description_borrowed) {
    return nullptr;
  }

  const cricket::SessionDescription* session = session_description_borrowed->description();
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
  auto* video = cricket::GetFirstVideoContentDescription(session);
  if (video) {
    for (const auto& codec : video->codecs()) {
      auto codec_type = webrtc::PayloadStringToCodecType(codec.name);

      if (codec_type == webrtc::kVideoCodecVP9) {
        if (enable_vp9) {
          auto profile = ParseSdpForVP9Profile(codec.params);
          std::string profile_id_string;
          codec.GetParam("profile-id", &profile_id_string);
          if (!profile) {
            RTC_LOG(LS_WARNING) << "Ignoring VP9 codec because profile-id = " << profile_id_string;
            continue;
          }

          if (profile != VP9Profile::kProfile0) {
            RTC_LOG(LS_WARNING) << "Ignoring VP9 codec with non-zero profile-id = " << profile_id_string;
            continue;
          }

          RffiVideoCodec vp9;
          vp9.type = kRffiVideoCodecVp9;
          v4->receive_video_codecs.push_back(vp9);
        }
      } else if (codec_type == webrtc::kVideoCodecVP8) {
        RffiVideoCodec vp8;
        vp8.type = kRffiVideoCodecVp8;
        v4->receive_video_codecs.push_back(vp8);
      }
    }
  }

  std::stable_sort(v4->receive_video_codecs.begin(), v4->receive_video_codecs.end(), [](const RffiVideoCodec lhs, const RffiVideoCodec rhs) {
      return codecPriority(lhs) < codecPriority(rhs);
  });

  auto* rffi_v4 = new RffiConnectionParametersV4();
  rffi_v4->ice_ufrag_borrowed = v4->ice_ufrag.c_str();
  rffi_v4->ice_pwd_borrowed = v4->ice_pwd.c_str();
  rffi_v4->receive_video_codecs_borrowed = v4->receive_video_codecs.data();
  rffi_v4->receive_video_codecs_size = v4->receive_video_codecs.size();
  rffi_v4->backing_owned = v4.release();
  return rffi_v4;
}

RUSTEXPORT void
Rust_deleteV4(RffiConnectionParametersV4* v4_owned) {
  if (!v4_owned) {
    return;
  }

  delete v4_owned->backing_owned;
  delete v4_owned;
}

// Returns an owned pointer.
RUSTEXPORT webrtc::SessionDescriptionInterface*
Rust_sessionDescriptionFromV4(bool offer,
                              const RffiConnectionParametersV4* v4_borrowed,
                              bool enable_tcc_audio,
                              bool enable_red_audio,
                              bool enable_vp9) {
  // Major changes from the default WebRTC behavior:
  // 1. We remove all codecs except Opus, VP8, and VP9
  // 2. We remove all header extensions except for transport-cc, video orientation,
  //    and abs send time.
  // 3. Opus CBR and DTX is enabled.
  // 4. RED is enabled for audio.

  // For some reason, WebRTC insists that the video SSRCs for one side don't 
  // overlap with SSRCs from the other side.  To avoid potential problems, we'll give the
  // caller side 1XXX and the callee side 2XXX;
  uint32_t BASE_SSRC = offer ? 1000 : 2000;
  // 1001 and 2001 used by connection.rs
  uint32_t AUDIO_SSRC = BASE_SSRC + 2;
  uint32_t VIDEO_SSRC = BASE_SSRC + 3;
  uint32_t VIDEO_RTX_SSRC = BASE_SSRC + 13;

  // This should stay in sync with PeerConnectionFactory.createAudioTrack
  std::string AUDIO_TRACK_ID = "audio1";
  // This must stay in sync with PeerConnectionFactory.createVideoTrack
  std::string VIDEO_TRACK_ID = "video1";

  auto transport = cricket::TransportDescription();
  transport.ice_mode = cricket::ICEMODE_FULL;
  transport.ice_ufrag = std::string(v4_borrowed->ice_ufrag_borrowed);
  transport.ice_pwd = std::string(v4_borrowed->ice_pwd_borrowed);
  transport.AddOption(cricket::ICE_OPTION_TRICKLE);
  transport.AddOption(cricket::ICE_OPTION_RENOMINATION);

  // DTLS is disabled
  transport.connection_role = cricket::CONNECTIONROLE_NONE;
  transport.identity_fingerprint = nullptr;

  auto set_rtp_params = [] (cricket::MediaContentDescription* media) {
    media->set_protocol(cricket::kMediaProtocolSavpf);
    media->set_rtcp_mux(true);
    media->set_direction(webrtc::RtpTransceiverDirection::kSendRecv);
  };

  auto audio = std::make_unique<cricket::AudioContentDescription>();
  set_rtp_params(audio.get());
  auto video = std::make_unique<cricket::VideoContentDescription>();
  set_rtp_params(video.get());

  // Turn on the RED "meta codec" for Opus redundancy.
  auto opus_red = cricket::CreateAudioCodec(OPUS_RED_PT, cricket::kRedCodecName, 48000, 2);
  opus_red.SetParam("", std::to_string(OPUS_PT) + "/" + std::to_string(OPUS_PT));

  // If the LBRED field trial is enabled, force RED.
  constexpr char kFieldTrialName[] = "RingRTC-Audio-LBRed-For-Opus";
  if (field_trial::IsEnabled(kFieldTrialName)) {
    enable_red_audio = true;
  }

  if (enable_red_audio) {
    // Add RED before Opus to use it by default when sending.
    audio->AddCodec(opus_red);
  }

  auto opus = cricket::CreateAudioCodec(OPUS_PT, cricket::kOpusCodecName, 48000, 2);
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
  opus.AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamTransportCc, cricket::kParamValueEmpty));
  audio->AddCodec(opus);

  if (!enable_red_audio) {
    // Add RED after Opus so that RED packets can at least be decoded properly if received.
    audio->AddCodec(opus_red);
  }

  auto add_video_feedback_params = [] (cricket::VideoCodec* video_codec) {
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamTransportCc, cricket::kParamValueEmpty));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamCcm, cricket::kRtcpFbCcmParamFir));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamNack, cricket::kParamValueEmpty));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamNack, cricket::kRtcpFbNackParamPli));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamRemb, cricket::kParamValueEmpty));
  };

  std::stable_sort(v4_borrowed->receive_video_codecs_borrowed, v4_borrowed->receive_video_codecs_borrowed + v4_borrowed->receive_video_codecs_size, [](const RffiVideoCodec lhs, const RffiVideoCodec rhs) {
      return codecPriority(lhs) < codecPriority(rhs);
  });

  for (size_t i = 0; i < v4_borrowed->receive_video_codecs_size; i++) {
    RffiVideoCodec rffi_codec = v4_borrowed->receive_video_codecs_borrowed[i];
    if (rffi_codec.type == kRffiVideoCodecVp9) {
      if (enable_vp9) {
        auto vp9 = cricket::CreateVideoCodec(VP9_PT, cricket::kVp9CodecName);
        vp9.params[kVP9FmtpProfileId] = VP9ProfileToString(VP9Profile::kProfile0);
        auto vp9_rtx = cricket::CreateVideoRtxCodec(VP9_RTX_PT, VP9_PT);
        vp9_rtx.params[kVP9FmtpProfileId] = VP9ProfileToString(VP9Profile::kProfile0);
        add_video_feedback_params(&vp9);

        video->AddCodec(vp9);
        video->AddCodec(vp9_rtx);
      }
    } else if (rffi_codec.type == kRffiVideoCodecVp8) {
      auto vp8 = cricket::CreateVideoCodec(VP8_PT, cricket::kVp8CodecName);
      auto vp8_rtx = cricket::CreateVideoRtxCodec(VP8_RTX_PT, VP8_PT);
      add_video_feedback_params(&vp8);

      video->AddCodec(vp8);
      video->AddCodec(vp8_rtx);
    }
  }

  // These are "meta codecs" for redundancy and FEC.
  // They are enabled by default currently with WebRTC.
  auto red = cricket::CreateVideoCodec(RED_PT, cricket::kRedCodecName);
  auto red_rtx = cricket::CreateVideoRtxCodec(RED_RTX_PT, RED_PT);
  auto ulpfec = cricket::CreateVideoCodec(ULPFEC_PT, cricket::kUlpfecCodecName);

  video->AddCodec(red);
  video->AddCodec(red_rtx);
  video->AddCodec(ulpfec);

  auto transport_cc1 = webrtc::RtpExtension(webrtc::TransportSequenceNumber::Uri(), TRANSPORT_CC1_EXT_ID);
  // TransportCC V2 is now enabled by default, but the difference is that V2 doesn't send periodic updates
  // and instead waits for feedback requests.  Since the existing clients don't send feedback
  // requests, we can't enable V2.  We'd have to add it to signaling to move from V1 to V2.
  // auto transport_cc2 = webrtc::RtpExtension(webrtc::TransportSequenceNumberV2::Uri(), TRANSPORT_CC2_EXT_ID);
  auto video_orientation = webrtc::RtpExtension(webrtc::VideoOrientation ::Uri(), VIDEO_ORIENTATION_EXT_ID);
  // abs_send_time and tx_time_offset are used for more accurate REMB messages from the receiver,
  // which are used by googcc in some small ways.  So, keep it enabled.
  // But it doesn't make sense to enable both abs_send_time and tx_time_offset, so only use abs_send_time.
  auto abs_send_time = webrtc::RtpExtension(webrtc::AbsoluteSendTime::Uri(), ABS_SEND_TIME_EXT_ID);
  // auto tx_time_offset = webrtc::RtpExtension(webrtc::TransmissionOffset::Uri(), TX_TIME_OFFSET_EXT_ID);

  // Note: Using transport-cc with audio is still experimental in WebRTC.
  // And don't add abs_send_time because it's only used for video.
  if (enable_tcc_audio) {
    audio->AddRtpHeaderExtension(transport_cc1);
  }

  video->AddRtpHeaderExtension(transport_cc1);
  video->AddRtpHeaderExtension(video_orientation);
  video->AddRtpHeaderExtension(abs_send_time);

  auto audio_stream = cricket::StreamParams();
  audio_stream.id = AUDIO_TRACK_ID;
  audio_stream.add_ssrc(AUDIO_SSRC);

  auto video_stream = cricket::StreamParams();
  video_stream.id = VIDEO_TRACK_ID;
  video_stream.add_ssrc(VIDEO_SSRC);
  video_stream.AddFidSsrc(VIDEO_SSRC, VIDEO_RTX_SSRC);  // AKA RTX

  // Things that are the same for all of them
  for (auto* stream : {&audio_stream, &video_stream}) {
    // WebRTC just generates a random 16-byte string for the entire PeerConnection.
    // It's used to send an SDES RTCP message.
    // The value doesn't seem to be used for anything else.
    // We'll set it around just in case.
    // But everything seems to work fine without it.
    stream->cname = "CNAMECNAMECNAME!";

    stream->set_stream_ids({"s"});
  }

  audio->AddStream(audio_stream);
  video->AddStream(video_stream);

  // TODO: Why is this only for video by default in WebRTC? Should we enable it for all of them?
  video->set_rtcp_reduced_size(true);

  // Keep the order as the WebRTC default: (audio, video, data).
  auto audio_content_name = "audio";
  auto video_content_name = "video";

  auto session = std::make_unique<cricket::SessionDescription>();
  session->AddTransportInfo(cricket::TransportInfo(audio_content_name, transport));
  session->AddTransportInfo(cricket::TransportInfo(video_content_name, transport));

  bool stopped = false;
  session->AddContent(audio_content_name, cricket::MediaProtocolType::kRtp, stopped, std::move(audio));
  session->AddContent(video_content_name, cricket::MediaProtocolType::kRtp, stopped, std::move(video));

  auto bundle = cricket::ContentGroup(cricket::GROUP_TYPE_BUNDLE);
  bundle.AddContentName(audio_content_name);
  bundle.AddContentName(video_content_name);
  session->AddGroup(bundle);

  session->set_msid_signaling(cricket::kMsidSignalingMediaSection);

  auto typ = offer ? SdpType::kOffer : SdpType::kAnswer;
  return new webrtc::JsepSessionDescription(typ, std::move(session), "1", "1");
}

webrtc::JsepSessionDescription*
CreateSessionDescriptionForGroupCall(bool local, 
                                     const std::string& ice_ufrag,
                                     const std::string& ice_pwd,
                                     RffiSrtpKey srtp_key,
                                     uint32_t local_demux_id,
                                     std::vector<uint32_t> remote_demux_ids) {
  // Major changes from the default WebRTC behavior:
  // 1. We remove all codecs except Opus and VP8.
  // 2. We remove all header extensions except for transport-cc, video orientation,
  //    abs send time, and audio level.
  // 3. Opus CBR and DTX is enabled.

  // This must stay in sync with PeerConnectionFactory.createAudioTrack
  std::string LOCAL_AUDIO_TRACK_ID = "audio1";
  // This must stay in sync with PeerConnectionFactory.createVideoTrack
  std::string LOCAL_VIDEO_TRACK_ID = "video1";

  auto transport = cricket::TransportDescription();
  transport.ice_mode = cricket::ICEMODE_FULL;
  transport.ice_ufrag = ice_ufrag;
  transport.ice_pwd = ice_pwd;
  transport.AddOption(cricket::ICE_OPTION_TRICKLE);

  // DTLS is disabled
  transport.connection_role = cricket::CONNECTIONROLE_NONE;
  transport.identity_fingerprint = nullptr;

  // Use SRTP master key material instead
  cricket::CryptoParams crypto_params;
  crypto_params.crypto_suite = rtc::SrtpCryptoSuiteToName(srtp_key.suite);
  std::string key(srtp_key.key_borrowed, srtp_key.key_len);
  std::string salt(srtp_key.salt_borrowed, srtp_key.salt_len);
  crypto_params.key_params = "inline:" + rtc::Base64::Encode(key + salt);

  auto set_rtp_params = [crypto_params] (cricket::MediaContentDescription* media) {
    media->set_protocol(cricket::kMediaProtocolSavpf);
    media->set_rtcp_mux(true);

    std::vector<cricket::CryptoParams> cryptos;
    cryptos.push_back(crypto_params);
    media->set_cryptos(cryptos);
  };

  auto local_direction = local ? RtpTransceiverDirection::kSendOnly : RtpTransceiverDirection::kRecvOnly;

  auto local_audio = std::make_unique<cricket::AudioContentDescription>();
  set_rtp_params(local_audio.get());
  local_audio.get()->set_direction(local_direction);

  auto local_video = std::make_unique<cricket::VideoContentDescription>();
  set_rtp_params(local_video.get());
  local_video.get()->set_direction(local_direction);

  auto remote_direction = local ? RtpTransceiverDirection::kRecvOnly : RtpTransceiverDirection::kSendOnly;

  std::vector<std::unique_ptr<cricket::AudioContentDescription>> remote_audios;
  for (auto demux_id : remote_demux_ids) {
    auto remote_audio = std::make_unique<cricket::AudioContentDescription>();
    set_rtp_params(remote_audio.get());
    if (demux_id == DISABLED_DEMUX_ID) {
      remote_audio.get()->set_direction(RtpTransceiverDirection::kInactive);
    } else {
      remote_audio.get()->set_direction(remote_direction);
    }

    remote_audios.push_back(std::move(remote_audio));
  }

  std::vector<std::unique_ptr<cricket::VideoContentDescription>> remote_videos;
  for (auto demux_id : remote_demux_ids) {
    auto remote_video = std::make_unique<cricket::VideoContentDescription>();
    set_rtp_params(remote_video.get());
    if (demux_id == DISABLED_DEMUX_ID) {
      remote_video.get()->set_direction(RtpTransceiverDirection::kInactive);
    } else {
      remote_video.get()->set_direction(remote_direction);
    }

    remote_videos.push_back(std::move(remote_video));
  }

  auto opus = cricket::CreateAudioCodec(OPUS_PT, cricket::kOpusCodecName, 48000, 2);
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
  opus.AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamTransportCc, cricket::kParamValueEmpty));

  // Turn on the RED "meta codec" for Opus redundancy.
  auto opus_red = cricket::CreateAudioCodec(OPUS_RED_PT, cricket::kRedCodecName, 48000, 2);
  opus_red.SetParam("", std::to_string(OPUS_PT) + "/" + std::to_string(OPUS_PT));

  // Add RED after Opus so that RED packets can at least be decoded properly if received.
  local_audio->AddCodec(opus);
  local_audio->AddCodec(opus_red);
  for (auto& remote_audio : remote_audios) {
    remote_audio->AddCodec(opus);
    remote_audio->AddCodec(opus_red);
  }

  auto add_video_feedback_params = [] (cricket::VideoCodec* video_codec) {
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamTransportCc, cricket::kParamValueEmpty));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamCcm, cricket::kRtcpFbCcmParamFir));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamNack, cricket::kParamValueEmpty));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamNack, cricket::kRtcpFbNackParamPli));
    video_codec->AddFeedbackParam(cricket::FeedbackParam(cricket::kRtcpFbParamRemb, cricket::kParamValueEmpty));
  };

  auto vp8 = cricket::CreateVideoCodec(VP8_PT, cricket::kVp8CodecName);
  auto vp8_rtx = cricket::CreateVideoRtxCodec(VP8_RTX_PT, VP8_PT);
  add_video_feedback_params(&vp8);

  // These are "meta codecs" for redundancy and FEC.
  // They are enabled by default currently with WebRTC.
  auto red = cricket::CreateVideoCodec(RED_PT, cricket::kRedCodecName);
  auto red_rtx = cricket::CreateVideoRtxCodec(RED_RTX_PT, RED_PT);

  local_video->AddCodec(vp8);
  local_video->AddCodec(vp8_rtx);

  local_video->AddCodec(red);
  local_video->AddCodec(red_rtx);

  for (auto& remote_video : remote_videos) {
    remote_video->AddCodec(vp8);
    remote_video->AddCodec(vp8_rtx);

    remote_video->AddCodec(red);
    remote_video->AddCodec(red_rtx);
  }

  auto audio_level = webrtc::RtpExtension(webrtc::AudioLevel::Uri(), AUDIO_LEVEL_EXT_ID);
  // Note: Do not add transport-cc for audio.  Using transport-cc with audio is still experimental in WebRTC.
  // And don't add abs_send_time because it's only used for video.
  local_audio->AddRtpHeaderExtension(audio_level);
  for (auto& remote_audio : remote_audios) {
    remote_audio->AddRtpHeaderExtension(audio_level);
  }

  auto transport_cc1 = webrtc::RtpExtension(webrtc::TransportSequenceNumber::Uri(), TRANSPORT_CC1_EXT_ID);
  // TransportCC V2 is now enabled by default, but the difference is that V2 doesn't send periodic updates
  // and instead waits for feedback requests.  Since the SFU doesn't currently send feedback requests,
  // we can't enable V2.  We'd have to add it to the SFU to move from V1 to V2.
  // auto transport_cc2 = webrtc::RtpExtension(webrtc::TransportSequenceNumberV2::Uri(), TRANSPORT_CC2_EXT_ID);
  auto video_orientation = webrtc::RtpExtension(webrtc::VideoOrientation::Uri(), VIDEO_ORIENTATION_EXT_ID);
  // abs_send_time and tx_time_offset are used for more accurate REMB messages from the receiver,
  // but the SFU doesn't process REMB messages anyway, nor does it send or receive these header extensions.
  // So, don't waste bytes on them.
  // auto abs_send_time = webrtc::RtpExtension(webrtc::AbsoluteSendTime::Uri(), ABS_SEND_TIME_EXT_ID);
  // auto tx_time_offset = webrtc::RtpExtension(webrtc::TransmissionOffset::Uri(), TX_TIME_OFFSET_EXT_ID);
  local_video->AddRtpHeaderExtension(transport_cc1);
  local_video->AddRtpHeaderExtension(video_orientation);
  for (auto& remote_video : remote_videos) {
    remote_video->AddRtpHeaderExtension(transport_cc1);
    remote_video->AddRtpHeaderExtension(video_orientation);
  }

  auto setup_streams = [local, &LOCAL_AUDIO_TRACK_ID, &LOCAL_VIDEO_TRACK_ID] (cricket::MediaContentDescription* audio,
                                                                              cricket::MediaContentDescription* video,
                                                                              uint32_t demux_id) {
    uint32_t audio_ssrc = demux_id + 0;
    // Leave room for audio RTX
    uint32_t video1_ssrc = demux_id + 2;
    uint32_t video1_rtx_ssrc = demux_id + 3;
    uint32_t video2_ssrc = demux_id + 4;
    uint32_t video2_rtx_ssrc = demux_id + 5;
    uint32_t video3_ssrc = demux_id + 6;
    uint32_t video3_rtx_ssrc = demux_id + 7;
    // Leave room for some more video layers or FEC
    // uint32_t data_ssrc = demux_id + 0xD;  Used by group_call.rs

    auto audio_stream = cricket::StreamParams();

    // We will use the string version of the demux ID to know which
    // transceiver is for which remote device.
    std::string demux_id_str = rtc::ToString(demux_id);

    // For local, this should stay in sync with PeerConnectionFactory.createAudioTrack
    audio_stream.id = local ? LOCAL_AUDIO_TRACK_ID : demux_id_str;
    audio_stream.add_ssrc(audio_ssrc);

    auto video_stream = cricket::StreamParams();
    // For local, this should stay in sync with PeerConnectionFactory.createVideoSource
    video_stream.id = local ? LOCAL_VIDEO_TRACK_ID : demux_id_str;
    video_stream.add_ssrc(video1_ssrc);
    if (local) {
      // Don't add simulcast for remote descriptions
      video_stream.add_ssrc(video2_ssrc);
      video_stream.add_ssrc(video3_ssrc);
      video_stream.ssrc_groups.push_back(cricket::SsrcGroup(cricket::kSimSsrcGroupSemantics, video_stream.ssrcs));
    }
    video_stream.AddFidSsrc(video1_ssrc, video1_rtx_ssrc);  // AKA RTX
    if (local) {
      // Don't add simulcast for remote descriptions
      video_stream.AddFidSsrc(video2_ssrc, video2_rtx_ssrc);  // AKA RTX
      video_stream.AddFidSsrc(video3_ssrc, video3_rtx_ssrc);  // AKA RTX
    }
    // This makes screen share use 2 layers of the highest resolution
    // (but different quality/framerate) rather than 3 layers of
    // differing resolution.
    video->set_conference_mode(true);

    // Things that are the same for all of them
    for (auto* stream : {&audio_stream, &video_stream}) {
      // WebRTC just generates a random 16-byte string for the entire PeerConnection.
      // It's used to send an SDES RTCP message.
      // The value doesn't seem to be used for anything else.
      // We'll set it around just in case.
      // But everything seems to work fine without it.
      stream->cname = demux_id_str;

      stream->set_stream_ids({demux_id_str});
    }

    audio->AddStream(audio_stream);
    video->AddStream(video_stream);
  };

  // Set up local_demux_id
  setup_streams(local_audio.get(), local_video.get(), local_demux_id);

  // Set up remote_demux_ids
  for (size_t i = 0; i < remote_demux_ids.size(); i++) {
    auto remote_audio = &remote_audios[i];
    auto remote_video = &remote_videos[i];
    uint32_t rtp_demux_id = remote_demux_ids[i];

    if (rtp_demux_id == DISABLED_DEMUX_ID) {
      continue;
    }

    setup_streams(remote_audio->get(), remote_video->get(), rtp_demux_id);
  }

  // TODO: Why is this only for video by default in WebRTC? Should we enable it for all of them?
  local_video->set_rtcp_reduced_size(true);
  for (auto& remote_video : remote_videos) {
    remote_video->set_rtcp_reduced_size(true);
  }

  // We don't set the crypto keys here.
  // We expect that will be done later by Rust_disableDtlsAndSetSrtpKey.

  // Keep the order as the WebRTC default: (audio, video).
  auto local_audio_content_name = "local-audio0";
  auto local_video_content_name = "local-video0";

  auto remote_audio_content_name = "remote-audio";
  auto remote_video_content_name = "remote-video";

  auto bundle = cricket::ContentGroup(cricket::GROUP_TYPE_BUNDLE);
  bundle.AddContentName(local_audio_content_name);
  bundle.AddContentName(local_video_content_name);

  auto session = std::make_unique<cricket::SessionDescription>();
  session->AddTransportInfo(cricket::TransportInfo(local_audio_content_name, transport));
  session->AddTransportInfo(cricket::TransportInfo(local_video_content_name, transport));

  bool stopped = false;
  session->AddContent(local_audio_content_name, cricket::MediaProtocolType::kRtp, stopped, std::move(local_audio));
  session->AddContent(local_video_content_name, cricket::MediaProtocolType::kRtp, stopped, std::move(local_video));

  auto audio_it = remote_audios.begin();
  auto video_it = remote_videos.begin();
  for (auto i = 0; audio_it != remote_audios.end() && video_it != remote_videos.end(); i++) {
    auto remote_audio = std::move(*audio_it);
    audio_it = remote_audios.erase(audio_it);

    std::string audio_name = remote_audio_content_name;
    audio_name += std::to_string(i);
    session->AddTransportInfo(cricket::TransportInfo(audio_name, transport));
    session->AddContent(audio_name, cricket::MediaProtocolType::kRtp, stopped, std::move(remote_audio));
    bundle.AddContentName(audio_name);

    auto remote_video = std::move(*video_it);
    video_it = remote_videos.erase(video_it);

    std::string video_name = remote_video_content_name;
    video_name += std::to_string(i);
    session->AddTransportInfo(cricket::TransportInfo(video_name, transport));
    session->AddContent(video_name, cricket::MediaProtocolType::kRtp, stopped, std::move(remote_video));
    bundle.AddContentName(video_name);
  }

  session->AddGroup(bundle);

  session->set_msid_signaling(cricket::kMsidSignalingMediaSection);

  auto typ = local ? SdpType::kOffer : SdpType::kAnswer;
  // The session ID and session version (both "1" here) go into SDP, but are not used at all.
  return new webrtc::JsepSessionDescription(typ, std::move(session), "1", "1");
}

// Returns an owned pointer.
RUSTEXPORT webrtc::SessionDescriptionInterface*
Rust_localDescriptionForGroupCall(const char* ice_ufrag_borrowed,
                                  const char* ice_pwd_borrowed,
                                  RffiSrtpKey client_srtp_key,
                                  uint32_t local_demux_id,
                                  uint32_t* remote_demux_ids_borrowed,
                                  size_t remote_demux_ids_len) {
  std::vector<uint32_t> remote_demux_ids;
  remote_demux_ids.assign(remote_demux_ids_borrowed, remote_demux_ids_borrowed + remote_demux_ids_len);
  return CreateSessionDescriptionForGroupCall(
    true /* local */, std::string(ice_ufrag_borrowed), std::string(ice_pwd_borrowed), client_srtp_key, local_demux_id, remote_demux_ids);
}

// Returns an owned pointer.
RUSTEXPORT webrtc::SessionDescriptionInterface*
Rust_remoteDescriptionForGroupCall(const char* ice_ufrag_borrowed,
                                   const char* ice_pwd_borrowed,
                                   RffiSrtpKey server_srtp_key,
                                   uint32_t local_demux_id,
                                   uint32_t* remote_demux_ids_borrowed,
                                   size_t remote_demux_ids_len) {
  std::vector<uint32_t> remote_demux_ids;
  remote_demux_ids.assign(remote_demux_ids_borrowed, remote_demux_ids_borrowed + remote_demux_ids_len);
  return CreateSessionDescriptionForGroupCall(
    false /* local */, std::string(ice_ufrag_borrowed), std::string(ice_pwd_borrowed), server_srtp_key, local_demux_id, remote_demux_ids);
}

RUSTEXPORT void
Rust_createAnswer(PeerConnectionInterface*              peer_connection_borrowed_rc,
                  CreateSessionDescriptionObserverRffi* csd_observer_borrowed_rc) {

  // No constraints are set
  MediaConstraints constraints = MediaConstraints();
  PeerConnectionInterface::RTCOfferAnswerOptions options;

  CopyConstraintsIntoOfferAnswerOptions(&constraints, &options);
  peer_connection_borrowed_rc->CreateAnswer(csd_observer_borrowed_rc, options);
}

RUSTEXPORT void
Rust_setRemoteDescription(PeerConnectionInterface*           peer_connection_borrowed_rc,
                          SetSessionDescriptionObserverRffi* ssd_observer_borrowed_rc,
                          SessionDescriptionInterface*       description_owned) {
  peer_connection_borrowed_rc->SetRemoteDescription(ssd_observer_borrowed_rc, description_owned);
}

RUSTEXPORT void
Rust_deleteSessionDescription(SessionDescriptionInterface* description_owned) {
  delete description_owned;
}

RUSTEXPORT void
Rust_setOutgoingMediaEnabled(PeerConnectionInterface* peer_connection_borrowed_rc,
                             bool                     enabled) {
  RTC_LOG(LS_INFO) << "Rust_setOutgoingMediaEnabled(" << enabled << ")";
  int encodings_changed = 0;
  for (auto& sender : peer_connection_borrowed_rc->GetSenders()) {
    RtpParameters parameters = sender->GetParameters();
    for (auto& encoding: parameters.encodings) {
      RTC_LOG(LS_INFO) << "Rust_setOutgoingMediaEnabled() encoding.active was: " << encoding.active;
      encoding.active = enabled;
      encodings_changed++;
    }
    sender->SetParameters(parameters);
  }
  RTC_LOG(LS_INFO) << "Rust_setOutgoingMediaEnabled(" << enabled << ") for " << encodings_changed << " encodings.";
}

RUSTEXPORT bool
Rust_setIncomingMediaEnabled(PeerConnectionInterface* peer_connection_borrowed_rc,
                             bool                     enabled) {
  RTC_LOG(LS_INFO) << "Rust_setIncomingMediaEnabled(" << enabled << ")";
  return peer_connection_borrowed_rc->SetIncomingRtpEnabled(enabled);
}

RUSTEXPORT void
Rust_setAudioPlayoutEnabled(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc,
                            bool                             enabled) {
  RTC_LOG(LS_INFO) << "Rust_setAudioPlayoutEnabled(" << enabled << ")";
  peer_connection_borrowed_rc->SetAudioPlayout(enabled);
}

RUSTEXPORT void
Rust_setAudioRecordingEnabled(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc,
                              bool                             enabled) {
  RTC_LOG(LS_INFO) << "Rust_setAudioRecordingEnabled(" << enabled << ")";
  peer_connection_borrowed_rc->SetAudioRecording(enabled);
}

RUSTEXPORT void
Rust_setIncomingAudioMuted(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc,
                           uint32_t                         ssrc,
                           bool                             muted) {
  RTC_LOG(LS_INFO) << "Rust_setIncomingAudioMuted(" << ssrc << ", " << muted << ")";
  peer_connection_borrowed_rc->SetIncomingAudioMuted(ssrc, muted);
}

RUSTEXPORT bool
Rust_addIceCandidateFromSdp(PeerConnectionInterface* peer_connection_borrowed_rc,
                            const char*              sdp_borrowed) {
  // Since we always use bundle, we can always use index 0 and ignore the mid
  std::unique_ptr<IceCandidateInterface> ice_candidate(
      CreateIceCandidate("", 0, std::string(sdp_borrowed), nullptr));

  return peer_connection_borrowed_rc->AddIceCandidate(ice_candidate.get());
}

RUSTEXPORT bool
Rust_removeIceCandidates(PeerConnectionInterface* pc_borrowed_rc,
                         IpPort* removed_addresses_data_borrowed,
                         size_t removed_addresses_len) {
  std::vector<IpPort> removed_addresses;
  removed_addresses.assign(removed_addresses_data_borrowed, removed_addresses_data_borrowed + removed_addresses_len);

  std::vector<cricket::Candidate> candidates_removed;
  for (const auto& address_removed : removed_addresses) {
    // This only needs to contain the correct transport_name, component, protocol, and address.
    // SeeCandidate::MatchesForRemoval and JsepTransportController::RemoveRemoteCandidates
    // and JsepTransportController::RemoveRemoteCandidates.
    // But we know (because we bundle/rtcp-mux everything) that the transport name is "audio",
    // and the component is 1.  We also know (because we don't use TCP candidates) that the
    // protocol is UDP.  So we only need to know the address.
    cricket::Candidate candidate_removed;
    candidate_removed.set_transport_name("audio");
    candidate_removed.set_component(cricket::ICE_CANDIDATE_COMPONENT_RTP);
    candidate_removed.set_protocol(cricket::UDP_PROTOCOL_NAME);
    candidate_removed.set_address(IpPortToRtcSocketAddress(address_removed));

    candidates_removed.push_back(candidate_removed);
  }

  return pc_borrowed_rc->RemoveIceCandidates(candidates_removed);
}


RUSTEXPORT bool
Rust_addIceCandidateFromServer(PeerConnectionInterface* pc_borrowed_rc,
                               Ip ip,
                               uint16_t port,
                               bool tcp) {
  cricket::Candidate candidate;
  // The default foundation is "", which is fine because we bundle.
  // The default generation is 0,  which is fine because we don't do ICE restarts.
  // The default username and password are "", which is fine because
  //   P2PTransportChannel::AddRemoteCandidate looks up the ICE ufrag and pwd
  //   from the remote description when the candidate's copy is empty.
  // Unset network ID, network cost, and network type are fine because they are for p2p use.
  // An unset relay protocol is fine because we aren't doing relay.
  // An unset related address is fine because we aren't doing relay or STUN.
  //
  // The critical values are component, type, protocol, and address, so we set those.
  //
  // The component doesn't really matter because we use RTCP-mux, so there is only one component.
  // However, WebRTC expects it to be set to ICE_CANDIDATE_COMPONENT_RTP(1), so we do that.
  //
  // The priority is also important for controlling whether we prefer IPv4 or IPv6 when both are available.
  // WebRTC generally prefers IPv6 over IPv4 for local candidates (see rtc_base::IPAddressPrecedence).
  // So we leave the priority unset to allow the local candidate preference to break the tie.
  candidate.set_component(cricket::ICE_CANDIDATE_COMPONENT_RTP);
  candidate.set_type(cricket::LOCAL_PORT_TYPE);  // AKA "host"
  candidate.set_address(rtc::SocketAddress(IpToRtcIp(ip), port));
  candidate.set_protocol(tcp ? cricket::TCP_PROTOCOL_NAME : cricket::UDP_PROTOCOL_NAME);

  // Since we always use bundle, we can always use index 0 and ignore the mid
  std::unique_ptr<IceCandidateInterface> ice_candidate(
      CreateIceCandidate("", 0, candidate));

  return pc_borrowed_rc->AddIceCandidate(ice_candidate.get());
}

RUSTEXPORT IceGathererInterface*
Rust_createSharedIceGatherer(PeerConnectionInterface* peer_connection_borrowed_rc) {
  return take_rc(peer_connection_borrowed_rc->CreateSharedIceGatherer());
}

RUSTEXPORT bool
Rust_useSharedIceGatherer(PeerConnectionInterface* peer_connection_borrowed_rc,
                          IceGathererInterface* ice_gatherer_borrowed_rc) {
  return peer_connection_borrowed_rc->UseSharedIceGatherer(inc_rc(ice_gatherer_borrowed_rc));
}

RUSTEXPORT void
Rust_getStats(PeerConnectionInterface* peer_connection_borrowed_rc,
              StatsObserverRffi* stats_observer_borrowed_rc) {
  peer_connection_borrowed_rc->GetStats(stats_observer_borrowed_rc);
}

// This is fairly complex in WebRTC, but I think it's something like this:
// Must be that 0 <= min <= start <= max.
// But any value can be unset (-1).  If so, here is what happens:
// If min isn't set, either use 30kbps (from PeerConnectionFactory::CreateCall_w) or no min (0 from WebRtcVideoChannel::ApplyChangedParams)
// If start isn't set, use the previous start; initially 100kbps (from PeerConnectionFactory::CreateCall_w)
// If max isn't set, either use 2mbps (from PeerConnectionFactory::CreateCall_w) or no max (-1 from WebRtcVideoChannel::ApplyChangedParams
// If min and max are set but haven't changed since last the last unset value, nothing happens.
// There is only an action if either min or max has changed or start is set.
RUSTEXPORT void
Rust_setSendBitrates(PeerConnectionInterface* peer_connection_borrowed_rc,
                     int32_t                  min_bitrate_bps,
                     int32_t                  start_bitrate_bps,
                     int32_t                  max_bitrate_bps) {
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
RUSTEXPORT bool
Rust_sendRtp(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc,
             uint8_t pt,
             uint16_t seqnum,
             uint32_t timestamp,
             uint32_t ssrc,
             const uint8_t* payload_data_borrowed,
             size_t payload_size) {
  size_t packet_size = 12 /* RTP header */ + payload_size + 16 /* SRTP footer */;
  std::unique_ptr<RtpPacket> packet(
    new RtpPacket(nullptr /* header extension map */, packet_size));
  packet->SetPayloadType(pt);
  packet->SetSequenceNumber(seqnum);
  packet->SetTimestamp(timestamp);
  packet->SetSsrc(ssrc);
  memcpy(packet->AllocatePayload(payload_size), payload_data_borrowed, payload_size);
  return peer_connection_borrowed_rc->SendRtp(std::move(packet));
}

// Warning: this blocks on the WebRTC network thread, so avoid calling it
// while holding a lock, especially a lock also taken in a callback
// from the network thread.
RUSTEXPORT bool
Rust_receiveRtp(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc, uint8_t pt, bool enable_incoming) {
  return peer_connection_borrowed_rc->ReceiveRtp(pt, enable_incoming);
}

RUSTEXPORT void
Rust_configureAudioEncoders(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc, const webrtc::AudioEncoder::Config* config_borrowed) {
  RTC_LOG(LS_INFO) << "Rust_configureAudioEncoders(...)";
  peer_connection_borrowed_rc->ConfigureAudioEncoders(*config_borrowed);
}

RUSTEXPORT void
Rust_getAudioLevels(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc,
                    cricket::AudioLevel* captured_out,
                    cricket::ReceivedAudioLevel* received_out, 
                    size_t received_out_size,
                    size_t* received_size_out) {
  RTC_LOG(LS_VERBOSE) << "Rust_getAudioLevels(...)";
  peer_connection_borrowed_rc->GetAudioLevels(captured_out, received_out, received_out_size, received_size_out);
}

RUSTEXPORT uint32_t
Rust_getLastBandwidthEstimateBps(webrtc::PeerConnectionInterface* peer_connection_borrowed_rc) {
  RTC_LOG(LS_VERBOSE) << "Rust_getLastBandwidthEstimateBps(...)";
  return peer_connection_borrowed_rc->GetLastBandwidthEstimateBps();
}

RUSTEXPORT void
Rust_closePeerConnection(PeerConnectionInterface* peer_connection_borrowed_rc) {
    peer_connection_borrowed_rc->Close();
}

} // namespace rffi
} // namespace webrtc
