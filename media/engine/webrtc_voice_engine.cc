/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/webrtc_voice_engine.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/algorithm.h"
#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include "api/audio/audio_mixer.h"
#include "api/audio/audio_processing.h"
#include "api/audio/audio_processing_statistics.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_options.h"
#include "api/call/audio_sink.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/audio_receive_stream.h"
#include "call/audio_send_stream.h"
#include "call/audio_state.h"
#include "call/call.h"
#include "call/packet_receiver.h"
#include "call/payload_type_picker.h"
#include "call/rtp_config.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "media/base/audio_source.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "media/base/media_channel_impl.h"
#include "media/base/media_config.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/stream_params.h"
#include "media/engine/adm_helpers.h"
#include "media/engine/webrtc_media_engine.h"
#include "modules/async_audio_processing/async_audio_processing.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/checks.h"
#include "rtc_base/dscp.h"
#include "rtc_base/experiments/struct_parameters_parser.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/strings/string_format.h"
#include "rtc_base/system/file_wrapper.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/metrics.h"

#if WEBRTC_ENABLE_PROTOBUF
#ifdef WEBRTC_ANDROID_PLATFORM_BUILD
#include "external/webrtc/webrtc/modules/audio_coding/audio_network_adaptor/config.pb.h"
#else
#include "modules/audio_coding/audio_network_adaptor/config.pb.h"
#endif

#endif

#if defined(WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE)
#include "api/audio/create_audio_device_module.h"
#endif

namespace webrtc {
namespace {

constexpr size_t kMaxUnsignaledRecvStreams = 4;

constexpr int kNackRtpHistoryMs = 5000;

const int kMinTelephoneEventCode = 0;  // RFC4733 (Section 2.3.1)
const int kMaxTelephoneEventCode = 255;

const int kMinPayloadType = 0;
const int kMaxPayloadType = 127;

class ProxySink : public AudioSinkInterface {
 public:
  explicit ProxySink(AudioSinkInterface* sink) : sink_(sink) {
    RTC_DCHECK(sink);
  }

  void OnData(const Data& audio) override { sink_->OnData(audio); }

 private:
  AudioSinkInterface* sink_;
};

bool ValidateStreamParams(const StreamParams& sp) {
  if (sp.ssrcs.empty()) {
    RTC_DLOG(LS_ERROR) << "No SSRCs in stream parameters: " << sp.ToString();
    return false;
  }
  if (sp.ssrcs.size() > 1) {
    RTC_DLOG(LS_ERROR) << "Multiple SSRCs in stream parameters: "
                       << sp.ToString();
    return false;
  }
  return true;
}

// Dumps an AudioCodec in RFC 2327-ish format.
std::string ToString(const Codec& codec) {
  StringBuilder ss;
  ss << codec.name << "/" << codec.clockrate << "/" << codec.channels;
  if (!codec.params.empty()) {
    ss << " {";
    for (const auto& param : codec.params) {
      ss << " " << param.first << "=" << param.second;
    }
    ss << " }";
  }
  ss << " (" << codec.id << ")";
  return ss.Release();
}

bool IsCodec(const Codec& codec, const char* ref_name) {
  return absl::EqualsIgnoreCase(codec.name, ref_name);
}

std::optional<Codec> FindCodec(const std::vector<Codec>& codecs,
                               const Codec& codec) {
  for (const webrtc::Codec& c : codecs) {
    if (c.Matches(codec)) {
      return c;
    }
  }
  return std::nullopt;
}

bool VerifyUniquePayloadTypes(const std::vector<Codec>& codecs) {
  if (codecs.empty()) {
    return true;
  }
  std::vector<int> payload_types;
  absl::c_transform(codecs, std::back_inserter(payload_types),
                    [](const webrtc::Codec& codec) { return codec.id; });
  absl::c_sort(payload_types);
  return absl::c_adjacent_find(payload_types) == payload_types.end();
}

std::optional<std::string> GetAudioNetworkAdaptorConfig(
    const AudioOptions& options) {
  if (options.audio_network_adaptor && *options.audio_network_adaptor &&
      options.audio_network_adaptor_config) {
    // Turn on audio network adaptor only when `options_.audio_network_adaptor`
    // equals true and `options_.audio_network_adaptor_config` has a value.
    return options.audio_network_adaptor_config;
  }
  return std::nullopt;
}

// Returns its smallest positive argument. If neither argument is positive,
// returns an arbitrary nonpositive value.
int MinPositive(int a, int b) {
  if (a <= 0) {
    return b;
  }
  if (b <= 0) {
    return a;
  }
  return std::min(a, b);
}

// `max_send_bitrate_bps` is the bitrate from "b=" in SDP.
// `rtp_max_bitrate_bps` is the bitrate from RtpSender::SetParameters.
std::optional<int> ComputeSendBitrate(int max_send_bitrate_bps,
                                      std::optional<int> rtp_max_bitrate_bps,
                                      const AudioCodecSpec& spec) {
  // If application-configured bitrate is set, take minimum of that and SDP
  // bitrate.
  const int bps = rtp_max_bitrate_bps
                      ? MinPositive(max_send_bitrate_bps, *rtp_max_bitrate_bps)
                      : max_send_bitrate_bps;
  if (bps <= 0) {
    return spec.info.default_bitrate_bps;
  }

  if (bps < spec.info.min_bitrate_bps) {
    // If codec is not multi-rate and `bps` is less than the fixed bitrate then
    // fail. If codec is not multi-rate and `bps` exceeds or equal the fixed
    // bitrate then ignore.
    RTC_LOG(LS_ERROR) << "Failed to set codec " << spec.format.name
                      << " to bitrate " << bps
                      << " bps"
                         ", requires at least "
                      << spec.info.min_bitrate_bps << " bps.";
    return std::nullopt;
  }

  if (spec.info.HasFixedBitrate()) {
    return spec.info.default_bitrate_bps;
  } else {
    // If codec is multi-rate then just set the bitrate.
    return std::min(bps, spec.info.max_bitrate_bps);
  }
}

struct AdaptivePtimeConfig {
  bool enabled = false;
  DataRate min_payload_bitrate = DataRate::KilobitsPerSec(16);
  // Value is chosen to ensure FEC can be encoded, see LBRR_WB_MIN_RATE_BPS in
  // libopus.
  DataRate min_encoder_bitrate = DataRate::KilobitsPerSec(16);
  bool use_slow_adaptation = true;

  std::optional<std::string> audio_network_adaptor_config;

  std::unique_ptr<StructParametersParser> Parser() {
    return StructParametersParser::Create(            //
        "enabled", &enabled,                          //
        "min_payload_bitrate", &min_payload_bitrate,  //
        "min_encoder_bitrate", &min_encoder_bitrate,  //
        "use_slow_adaptation", &use_slow_adaptation);
  }

  explicit AdaptivePtimeConfig(const FieldTrialsView& trials) {
    Parser()->Parse(trials.Lookup("WebRTC-Audio-AdaptivePtime"));
#if WEBRTC_ENABLE_PROTOBUF
    audio_network_adaptor::config::ControllerManager config;
    auto* frame_length_controller =
        config.add_controllers()->mutable_frame_length_controller_v2();
    frame_length_controller->set_min_payload_bitrate_bps(
        min_payload_bitrate.bps());
    frame_length_controller->set_use_slow_adaptation(use_slow_adaptation);
    config.add_controllers()->mutable_bitrate_controller();
    audio_network_adaptor_config = config.SerializeAsString();
#endif
  }
};

// TODO(tommi): Constructing a receive stream could be made simpler.
// Move some of this boiler plate code into the config structs themselves.
AudioReceiveStreamInterface::Config BuildReceiveStreamConfig(
    uint32_t remote_ssrc,
    uint32_t local_ssrc,
    bool use_nack,
    bool enable_non_sender_rtt,
    RtcpMode rtcp_mode,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpExtension>& /* extensions */,
    Transport* rtcp_send_transport,
    const scoped_refptr<AudioDecoderFactory>& decoder_factory,
    const std::map<int, SdpAudioFormat>& decoder_map,
    std::optional<AudioCodecPairId> codec_pair_id,
    size_t jitter_buffer_max_packets,
    bool jitter_buffer_fast_accelerate,
    int jitter_buffer_min_delay_ms,
    // RingRTC change to configure the jitter buffer's max target delay.
    int jitter_buffer_max_target_delay_ms,
    // RingRTC change to configure the RTCP report interval.
    int rtcp_report_interval_ms,
    scoped_refptr<FrameDecryptorInterface> frame_decryptor,
    const CryptoOptions& crypto_options,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  AudioReceiveStreamInterface::Config config;
  config.rtp.remote_ssrc = remote_ssrc;
  config.rtp.local_ssrc = local_ssrc;
  config.rtp.nack.rtp_history_ms = use_nack ? kNackRtpHistoryMs : 0;
  config.rtp.rtcp_mode = rtcp_mode;
  if (!stream_ids.empty()) {
    config.sync_group = stream_ids[0];
  }
  config.rtcp_send_transport = rtcp_send_transport;
  config.enable_non_sender_rtt = enable_non_sender_rtt;
  config.decoder_factory = decoder_factory;
  config.decoder_map = decoder_map;
  config.codec_pair_id = codec_pair_id;
  config.jitter_buffer_max_packets = jitter_buffer_max_packets;
  config.jitter_buffer_fast_accelerate = jitter_buffer_fast_accelerate;
  config.jitter_buffer_min_delay_ms = jitter_buffer_min_delay_ms;
  // RingRTC change to configure the jitter buffer's max target delay.
  config.jitter_buffer_max_target_delay_ms = jitter_buffer_max_target_delay_ms;
  // RingRTC change to configure the RTCP report interval.
  config.rtcp_report_interval_ms = rtcp_report_interval_ms;
  config.frame_decryptor = std::move(frame_decryptor);
  config.crypto_options = crypto_options;
  config.frame_transformer = std::move(frame_transformer);
  return config;
}

// Utility function to check if RED codec and its parameters match a codec spec.
bool CheckRedParameters(
    const Codec& red_codec,
    const AudioSendStream::Config::SendCodecSpec& send_codec_spec) {
  if (red_codec.clockrate != send_codec_spec.format.clockrate_hz ||
      red_codec.channels != send_codec_spec.format.num_channels) {
    return false;
  }

  // Check the FMTP line for the empty parameter which should match
  // <primary codec>/<primary codec>[/...]
  auto red_parameters = red_codec.params.find(kCodecParamNotInNameValueFormat);
  if (red_parameters == red_codec.params.end()) {
    RTC_LOG(LS_WARNING) << "audio/RED missing fmtp parameters.";
    return false;
  }
  std::vector<absl::string_view> redundant_payloads =
      webrtc::split(red_parameters->second, '/');
  // 32 is chosen as a maximum upper bound for consistency with the
  // red payload splitter.
  if (redundant_payloads.size() < 2 || redundant_payloads.size() > 32) {
    return false;
  }
  for (auto pt : redundant_payloads) {
    if (pt != absl::StrCat(send_codec_spec.payload_type)) {
      return false;
    }
  }
  return true;
}

SdpAudioFormat AudioCodecToSdpAudioFormat(const Codec& ac) {
  return SdpAudioFormat(ac.name, ac.clockrate, ac.channels, ac.params);
}

// Assign the payload types for the codecs of this voice engine.
// This is a "preliminary" pass, done to prime the
// payload type picker with a normal set of PTs.
// TODO: https://issues.webrtc.org/360058654 - remove.
std::vector<Codec> LegacyCollectCodecs(const std::vector<AudioCodecSpec>& specs,
                                       bool allocate_pt) {
  // Only used for the legacy "allocate_pt = true" case.
  PayloadTypePicker pt_mapper;
  std::vector<Codec> out;

  // Only generate CN payload types for these clockrates:
  std::map<int, bool, std::greater<int>> generate_cn = {{8000, false}};
  // Only generate telephone-event payload types for these clockrates:
  std::map<int, bool, std::greater<int>> generate_dtmf = {{8000, false},
                                                          {48000, false}};

  for (const auto& spec : specs) {
    Codec codec = webrtc::CreateAudioCodec(spec.format);
    if (allocate_pt) {
      auto pt_or_error = pt_mapper.SuggestMapping(codec, nullptr);
      // We need to do some extra stuff before adding the main codecs to out.
      if (!pt_or_error.ok()) {
        continue;
      }
      codec.id = pt_or_error.value();
    }
    if (spec.info.supports_network_adaption) {
      codec.AddFeedbackParam(
          FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty));
    }

    if (spec.info.allow_comfort_noise) {
      // Generate a CN entry if the decoder allows it and we support the
      // clockrate.
      auto cn = generate_cn.find(spec.format.clockrate_hz);
      if (cn != generate_cn.end()) {
        cn->second = true;
      }
    }

    // Generate a telephone-event entry if we support the clockrate.
    auto dtmf = generate_dtmf.find(spec.format.clockrate_hz);
    if (dtmf != generate_dtmf.end()) {
      dtmf->second = true;
    }

    out.push_back(codec);

    // TODO(hta):  Don't assign RED codecs until we know that the PT for Opus
    // is final
    if (codec.name == kOpusCodecName) {
      if (allocate_pt) {
        std::string red_fmtp =
            absl::StrCat(codec.id) + "/" + absl::StrCat(codec.id);
        Codec red_codec = webrtc::CreateAudioCodec(
            {kRedCodecName, codec.clockrate, codec.channels, {{"", red_fmtp}}});
        red_codec.id = pt_mapper.SuggestMapping(red_codec, nullptr).value();
        out.push_back(red_codec);
      } else {
        // We don't know the PT to put into the RED fmtp parameter yet.
        // Leave it out.
        Codec red_codec = webrtc::CreateAudioCodec({kRedCodecName, 48000, 2});
        out.push_back(red_codec);
      }
    }
  }

  // Add CN codecs after "proper" audio codecs.
  // RingRTC change to disable comfort noise codecs.
#if 0
  for (const auto& cn : generate_cn) {
    if (cn.second) {
      Codec cn_codec = webrtc::CreateAudioCodec({kCnCodecName, cn.first, 1});
      if (allocate_pt) {
        cn_codec.id = pt_mapper.SuggestMapping(cn_codec, nullptr).value();
      }
      out.push_back(cn_codec);
    }
  }
#endif
  // Add telephone-event codecs last.
  // RingRTC change to disable telephone-event codecs.
#if 0
  for (const auto& dtmf : generate_dtmf) {
    if (dtmf.second) {
      Codec dtmf_codec =
          webrtc::CreateAudioCodec({kDtmfCodecName, dtmf.first, 1});
      if (allocate_pt) {
        dtmf_codec.id = pt_mapper.SuggestMapping(dtmf_codec, nullptr).value();
      }
      out.push_back(dtmf_codec);
    }
  }
#endif
  return out;
}

}  // namespace

WebRtcVoiceEngine::WebRtcVoiceEngine(
    const Environment& env,
    scoped_refptr<AudioDeviceModule> adm,
    scoped_refptr<AudioEncoderFactory> encoder_factory,
    scoped_refptr<AudioDecoderFactory> decoder_factory,
    scoped_refptr<AudioMixer> audio_mixer,
    scoped_refptr<AudioProcessing> audio_processing,
    std::unique_ptr<AudioFrameProcessor> audio_frame_processor)
    : env_(env),
      adm_(std::move(adm)),
      encoder_factory_(std::move(encoder_factory)),
      decoder_factory_(std::move(decoder_factory)),
      audio_mixer_(std::move(audio_mixer)),
      apm_(std::move(audio_processing)),
      audio_frame_processor_(std::move(audio_frame_processor)),
      minimized_remsampling_on_mobile_trial_enabled_(
          env_.field_trials().IsEnabled(
              "WebRTC-Audio-MinimizeResamplingOnMobile")),
      payload_types_in_transport_trial_enabled_(
          env_.field_trials().IsEnabled("WebRTC-PayloadTypesInTransport")) {
  RTC_LOG(LS_INFO) << "WebRtcVoiceEngine::WebRtcVoiceEngine";
  RTC_DCHECK(decoder_factory_);
  RTC_DCHECK(encoder_factory_);
  // The rest of our initialization will happen in Init.
}

WebRtcVoiceEngine::~WebRtcVoiceEngine() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  RTC_LOG(LS_INFO) << "WebRtcVoiceEngine::~WebRtcVoiceEngine";
  if (initialized_) {
    StopAecDump();

    // Stop AudioDevice.
    adm()->StopPlayout();
    adm()->StopRecording();
    adm()->RegisterAudioCallback(nullptr);
    adm()->Terminate();
  }
}

void WebRtcVoiceEngine::Init() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  RTC_LOG(LS_INFO) << "WebRtcVoiceEngine::Init";

  // TaskQueue expects to be created/destroyed on the same thread.
  RTC_DCHECK(!low_priority_worker_queue_);
  low_priority_worker_queue_ = env_.task_queue_factory().CreateTaskQueue(
      "rtc-low-prio", TaskQueueFactory::Priority::LOW);

  // Load our audio codec lists.
  RTC_LOG(LS_VERBOSE) << "Supported send codecs in order of preference:";
  send_codecs_ =
      LegacyCollectCodecs(encoder_factory_->GetSupportedEncoders(),
                          !payload_types_in_transport_trial_enabled_);
  for (const webrtc::Codec& codec : send_codecs_) {
    RTC_LOG(LS_VERBOSE) << ToString(codec);
  }

  RTC_LOG(LS_VERBOSE) << "Supported recv codecs in order of preference:";
  recv_codecs_ =
      LegacyCollectCodecs(decoder_factory_->GetSupportedDecoders(),
                          !payload_types_in_transport_trial_enabled_);
  for (const webrtc::Codec& codec : recv_codecs_) {
    RTC_LOG(LS_VERBOSE) << ToString(codec);
  }

#if defined(WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE)
  // No ADM supplied? Create a default one.
  if (!adm_) {
    adm_ =
        CreateAudioDeviceModule(env_, AudioDeviceModule::kPlatformDefaultAudio);
  }
#endif  // WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE
  RTC_CHECK(adm());
  webrtc::adm_helpers::Init(adm());

  // Set up AudioState.
  {
    AudioState::Config config;
    if (audio_mixer_) {
      config.audio_mixer = audio_mixer_;
    } else {
      config.audio_mixer = AudioMixerImpl::Create();
    }
    config.audio_processing = apm_;
    config.audio_device_module = adm_;
    if (audio_frame_processor_) {
      config.async_audio_processing_factory =
          make_ref_counted<AsyncAudioProcessing::Factory>(
              std::move(audio_frame_processor_), env_.task_queue_factory());
    }
    audio_state_ = AudioState::Create(config);
  }

  // Connect the ADM to our audio path.
  adm()->RegisterAudioCallback(audio_state()->audio_transport());

  // Set default engine options.
  {
    AudioOptions options;
    options.echo_cancellation = true;
    options.auto_gain_control = true;
#if defined(WEBRTC_IOS)
    // On iOS, VPIO provides built-in NS.
    options.noise_suppression = false;
#else
    options.noise_suppression = true;
#endif
    options.highpass_filter = true;
    options.stereo_swapping = false;
    options.audio_jitter_buffer_max_packets = 200;
    options.audio_jitter_buffer_fast_accelerate = false;
    options.audio_jitter_buffer_min_delay_ms = 0;

#if !defined(WEBRTC_IOS) && !defined(WEBRTC_ANDROID)
    // RingRTC changes to override audio options.
    auto config = apm_->GetConfig();
    options.echo_cancellation = config.echo_canceller.enabled;
    options.auto_gain_control = config.gain_controller1.enabled;
    options.noise_suppression = config.noise_suppression.enabled;
    options.highpass_filter = config.high_pass_filter.enabled;
#endif

    ApplyOptions(options);
  }
  initialized_ = true;
}

scoped_refptr<AudioState> WebRtcVoiceEngine::GetAudioState() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  return audio_state_;
}

std::unique_ptr<VoiceMediaSendChannelInterface>
WebRtcVoiceEngine::CreateSendChannel(Call* call,
                                     const MediaConfig& config,
                                     const AudioOptions& options,
                                     const CryptoOptions& crypto_options,
                                     AudioCodecPairId codec_pair_id) {
  return std::make_unique<WebRtcVoiceSendChannel>(
      this, config, options, crypto_options, call, codec_pair_id);
}

std::unique_ptr<VoiceMediaReceiveChannelInterface>
WebRtcVoiceEngine::CreateReceiveChannel(Call* call,
                                        const MediaConfig& config,
                                        const AudioOptions& options,
                                        const CryptoOptions& crypto_options,
                                        AudioCodecPairId codec_pair_id) {
  return std::make_unique<WebRtcVoiceReceiveChannel>(
      this, config, options, crypto_options, call, codec_pair_id);
}

void WebRtcVoiceEngine::ApplyOptions(const AudioOptions& options_in) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  RTC_LOG(LS_INFO) << "WebRtcVoiceEngine::ApplyOptions: "
                   << options_in.ToString();
  AudioOptions options = options_in;  // The options are modified below.

  // RingRTC changes to override audio options. (code removed)

#if defined(WEBRTC_IOS)
  if (options.ios_force_software_aec_HACK &&
      *options.ios_force_software_aec_HACK) {
    // EC may be forced on for a device known to have non-functioning platform
    // AEC.
    options.echo_cancellation = true;
    RTC_LOG(LS_WARNING)
        << "Force software AEC on iOS. May conflict with platform AEC.";
  } else {
    // On iOS, VPIO provides built-in EC.
    options.echo_cancellation = false;
    RTC_LOG(LS_INFO) << "Always disable AEC on iOS. Use built-in instead.";
  }
// RingRTC changes to override audio options. (code removed)
#endif

// Set and adjust gain control options.
#if defined(WEBRTC_IOS)
  // On iOS, VPIO provides built-in AGC.
  options.auto_gain_control = false;
  RTC_LOG(LS_INFO) << "Always disable AGC on iOS. Use built-in instead.";
#endif

#if defined(WEBRTC_IOS) || defined(WEBRTC_ANDROID)
  // Turn off the gain control if specified by the field trial.
  // The purpose of the field trial is to reduce the amount of resampling
  // performed inside the audio processing module on mobile platforms by
  // whenever possible turning off the fixed AGC mode and the high-pass filter.
  // (https://bugs.chromium.org/p/webrtc/issues/detail?id=6181).
  if (minimized_remsampling_on_mobile_trial_enabled_) {
    options.auto_gain_control = false;
    RTC_LOG(LS_INFO) << "Disable AGC according to field trial.";
    if (!(options.noise_suppression.value_or(false) ||
          options.echo_cancellation.value_or(false))) {
      // If possible, turn off the high-pass filter.
      RTC_LOG(LS_INFO)
          << "Disable high-pass filter in response to field trial.";
      options.highpass_filter = false;
    }
  }
#endif

  if (options.echo_cancellation) {
    // Check if platform supports built-in EC. Currently only supported on
    // Android and in combination with Java based audio layer.
    // TODO(henrika): investigate possibility to support built-in EC also
    // in combination with Open SL ES audio.
    const bool built_in_aec = adm()->BuiltInAECIsAvailable();
    if (built_in_aec) {
      // Built-in EC exists on this device. Enable/Disable it according to the
      // echo_cancellation audio option.
      const bool enable_built_in_aec = *options.echo_cancellation;
      if (adm()->EnableBuiltInAEC(enable_built_in_aec) == 0 &&
          enable_built_in_aec) {
        // Disable internal software EC if built-in EC is enabled,
        // i.e., replace the software EC with the built-in EC.
        options.echo_cancellation = false;
        RTC_LOG(LS_INFO)
            << "Disabling EC since built-in EC will be used instead";
      }
    }
  }

  if (options.auto_gain_control) {
    bool built_in_agc_avaliable = adm()->BuiltInAGCIsAvailable();
    if (built_in_agc_avaliable) {
      if (adm()->EnableBuiltInAGC(*options.auto_gain_control) == 0 &&
          *options.auto_gain_control) {
        // Disable internal software AGC if built-in AGC is enabled,
        // i.e., replace the software AGC with the built-in AGC.
        options.auto_gain_control = false;
        RTC_LOG(LS_INFO)
            << "Disabling AGC since built-in AGC will be used instead";
      }
    }
  }

  if (options.noise_suppression) {
    if (adm()->BuiltInNSIsAvailable()) {
      bool builtin_ns = *options.noise_suppression;
      if (adm()->EnableBuiltInNS(builtin_ns) == 0 && builtin_ns) {
        // Disable internal software NS if built-in NS is enabled,
        // i.e., replace the software NS with the built-in NS.
        options.noise_suppression = false;
        RTC_LOG(LS_INFO)
            << "Disabling NS since built-in NS will be used instead";
      }
    }
  }

  if (options.stereo_swapping) {
    audio_state()->SetStereoChannelSwapping(*options.stereo_swapping);
  }

  if (options.audio_jitter_buffer_max_packets) {
    audio_jitter_buffer_max_packets_ =
        std::max(20, *options.audio_jitter_buffer_max_packets);
  }
  if (options.audio_jitter_buffer_fast_accelerate) {
    audio_jitter_buffer_fast_accelerate_ =
        *options.audio_jitter_buffer_fast_accelerate;
  }
  if (options.audio_jitter_buffer_min_delay_ms) {
    audio_jitter_buffer_min_delay_ms_ =
        *options.audio_jitter_buffer_min_delay_ms;
  }

  AudioProcessing* ap = apm();
  if (!ap) {
    return;
  }

  AudioProcessing::Config apm_config = ap->GetConfig();

  if (options.echo_cancellation) {
    apm_config.echo_canceller.enabled = *options.echo_cancellation;
    // RingRTC change to disable AECM
    apm_config.echo_canceller.mobile_mode = false;
  }

  if (options.auto_gain_control) {
    const bool enabled = *options.auto_gain_control;
    apm_config.gain_controller1.enabled = enabled;
#if defined(WEBRTC_IOS) || defined(WEBRTC_ANDROID)
    apm_config.gain_controller1.mode =
        apm_config.gain_controller1.kFixedDigital;
#else
    apm_config.gain_controller1.mode =
        apm_config.gain_controller1.kAdaptiveAnalog;
#endif
  }

  if (options.highpass_filter) {
    apm_config.high_pass_filter.enabled = *options.highpass_filter;
  }

  if (options.noise_suppression) {
    const bool enabled = *options.noise_suppression;
    apm_config.noise_suppression.enabled = enabled;
    apm_config.noise_suppression.level =
        AudioProcessing::Config::NoiseSuppression::Level::kHigh;
  }

  ap->ApplyConfig(apm_config);
}

const std::vector<Codec>& WebRtcVoiceEngine::LegacySendCodecs() const {
  RTC_DCHECK(signal_thread_checker_.IsCurrent());
  return send_codecs_;
}

const std::vector<Codec>& WebRtcVoiceEngine::LegacyRecvCodecs() const {
  RTC_DCHECK(signal_thread_checker_.IsCurrent());
  return recv_codecs_;
}

std::vector<RtpHeaderExtensionCapability>
WebRtcVoiceEngine::GetRtpHeaderExtensions() const {
  RTC_DCHECK(signal_thread_checker_.IsCurrent());
  std::vector<RtpHeaderExtensionCapability> result;
  // id is *not* incremented for non-default extensions, UsedIds needs to
  // resolve conflicts.
  int id = 1;
  // RingRTC change to disable unused header extensions
  for (const auto& uri : {// webrtc::RtpExtension::kAudioLevelUri,
                          webrtc::RtpExtension::kAbsSendTimeUri,
                          webrtc::RtpExtension::kTransportSequenceNumberUri,
                          webrtc::RtpExtension::kMidUri}) {
    result.emplace_back(uri, id++, RtpTransceiverDirection::kSendRecv);
  }
  for (const auto& uri : {webrtc::RtpExtension::kAbsoluteCaptureTimeUri}) {
    result.emplace_back(uri, id, RtpTransceiverDirection::kStopped);
  }
  return result;
}

bool WebRtcVoiceEngine::StartAecDump(FileWrapper file, int64_t max_size_bytes) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);

  AudioProcessing* ap = apm();
  if (!ap) {
    RTC_LOG(LS_WARNING)
        << "Attempting to start aecdump when no audio processing module is "
           "present, hence no aecdump is started.";
    return false;
  }

  return ap->CreateAndAttachAecDump(file.Release(), max_size_bytes,
                                    low_priority_worker_queue_.get());
}

void WebRtcVoiceEngine::StopAecDump() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  AudioProcessing* ap = apm();
  if (ap) {
    ap->DetachAecDump();
  } else {
    RTC_LOG(LS_WARNING) << "Attempting to stop aecdump when no audio "
                           "processing module is present";
  }
}

std::optional<AudioDeviceModule::Stats>
WebRtcVoiceEngine::GetAudioDeviceStats() {
  return adm()->GetStats();
}

AudioDeviceModule* WebRtcVoiceEngine::adm() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  RTC_DCHECK(adm_);
  return adm_.get();
}

AudioProcessing* WebRtcVoiceEngine::apm() const {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  return apm_.get();
}

AudioState* WebRtcVoiceEngine::audio_state() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  RTC_DCHECK(audio_state_);
  return audio_state_.get();
}

// --------------------------------- WebRtcVoiceSendChannel ------------------

class WebRtcVoiceSendChannel::WebRtcAudioSendStream : public AudioSource::Sink {
 public:
  WebRtcAudioSendStream(
      uint32_t ssrc,
      const std::string& mid,
      const std::string& c_name,
      const std::string track_id,
      const std::optional<AudioSendStream::Config::SendCodecSpec>&
          send_codec_spec,
      bool extmap_allow_mixed,
      const std::vector<RtpExtension>& extensions,
      int max_send_bitrate_bps,
      int rtcp_report_interval_ms,
      const std::optional<std::string>& audio_network_adaptor_config,
      Call* call,
      Transport* send_transport,
      const scoped_refptr<AudioEncoderFactory>& encoder_factory,
      const std::optional<AudioCodecPairId> codec_pair_id,
      scoped_refptr<FrameEncryptorInterface> frame_encryptor,
      const CryptoOptions& crypto_options)
      : adaptive_ptime_config_(call->trials()),
        call_(call),
        config_(send_transport),
        max_send_bitrate_bps_(max_send_bitrate_bps),
        rtp_parameters_(CreateRtpParametersWithOneEncoding()) {
    RTC_DCHECK(call);
    RTC_DCHECK(encoder_factory);
    config_.rtp.ssrc = ssrc;
    config_.rtp.mid = mid;
    config_.rtp.c_name = c_name;
    config_.rtp.extmap_allow_mixed = extmap_allow_mixed;
    config_.rtp.extensions = extensions;
    config_.has_dscp =
        rtp_parameters_.encodings[0].network_priority != Priority::kLow;
    config_.encoder_factory = encoder_factory;
    config_.codec_pair_id = codec_pair_id;
    config_.track_id = track_id;
    config_.frame_encryptor = frame_encryptor;
    config_.crypto_options = crypto_options;
    config_.rtcp_report_interval_ms = rtcp_report_interval_ms;
    rtp_parameters_.encodings[0].ssrc = ssrc;
    rtp_parameters_.rtcp.cname = c_name;
    rtp_parameters_.header_extensions = extensions;

    audio_network_adaptor_config_from_options_ = audio_network_adaptor_config;
    UpdateAudioNetworkAdaptorConfig();

    if (send_codec_spec) {
      UpdateSendCodecSpec(*send_codec_spec);
    }

    stream_ = call_->CreateAudioSendStream(config_);
  }

  WebRtcAudioSendStream() = delete;
  WebRtcAudioSendStream(const WebRtcAudioSendStream&) = delete;
  WebRtcAudioSendStream& operator=(const WebRtcAudioSendStream&) = delete;

  ~WebRtcAudioSendStream() override {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    ClearSource();
    call_->DestroyAudioSendStream(stream_);
  }

  void SetSendCodecSpec(
      const AudioSendStream::Config::SendCodecSpec& send_codec_spec) {
    UpdateSendCodecSpec(send_codec_spec);
    ReconfigureAudioSendStream(nullptr);
  }

  void SetRtpExtensions(const std::vector<RtpExtension>& extensions) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    config_.rtp.extensions = extensions;
    rtp_parameters_.header_extensions = extensions;
    ReconfigureAudioSendStream(nullptr);
  }

  void SetExtmapAllowMixed(bool extmap_allow_mixed) {
    config_.rtp.extmap_allow_mixed = extmap_allow_mixed;
    ReconfigureAudioSendStream(nullptr);
  }

  void SetMid(const std::string& mid) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    if (config_.rtp.mid == mid) {
      return;
    }
    config_.rtp.mid = mid;
    ReconfigureAudioSendStream(nullptr);
  }

  void SetRtcpMode(RtcpMode mode) {
    bool reduced_size = mode == RtcpMode::kReducedSize;
    if (rtp_parameters_.rtcp.reduced_size == reduced_size) {
      return;
    }
    rtp_parameters_.rtcp.reduced_size = reduced_size;
    // Note: this is not wired up beyond this point. For all audio
    // RTCP packets sent by a sender there is no difference.
    ReconfigureAudioSendStream(nullptr);
  }

  void SetFrameEncryptor(
      scoped_refptr<FrameEncryptorInterface> frame_encryptor) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    config_.frame_encryptor = frame_encryptor;
    ReconfigureAudioSendStream(nullptr);
  }

  void SetAudioNetworkAdaptorConfig(
      const std::optional<std::string>& audio_network_adaptor_config) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    if (audio_network_adaptor_config_from_options_ ==
        audio_network_adaptor_config) {
      return;
    }
    audio_network_adaptor_config_from_options_ = audio_network_adaptor_config;
    UpdateAudioNetworkAdaptorConfig();
    UpdateAllowedBitrateRange();
    ReconfigureAudioSendStream(nullptr);
  }

  bool SetMaxSendBitrate(int bps) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(config_.send_codec_spec);
    RTC_DCHECK(audio_codec_spec_);
    auto send_rate = ComputeSendBitrate(
        bps, rtp_parameters_.encodings[0].max_bitrate_bps, *audio_codec_spec_);

    if (!send_rate) {
      return false;
    }

    max_send_bitrate_bps_ = bps;

    if (send_rate != config_.send_codec_spec->target_bitrate_bps) {
      config_.send_codec_spec->target_bitrate_bps = send_rate;
      ReconfigureAudioSendStream(nullptr);
    }
    return true;
  }

  bool SendTelephoneEvent(int payload_type,
                          int payload_freq,
                          int event,
                          int duration_ms) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(stream_);
    return stream_->SendTelephoneEvent(payload_type, payload_freq, event,
                                       duration_ms);
  }

  void SetSend(bool send) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    send_ = send;
    UpdateSendState();
  }

  void SetMuted(bool muted) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(stream_);
    stream_->SetMuted(muted);
    muted_ = muted;
  }

  bool muted() const {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    return muted_;
  }

  AudioSendStream::Stats GetStats(bool has_remote_tracks) const {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(stream_);
    return stream_->GetStats(has_remote_tracks);
  }

  // Starts the sending by setting ourselves as a sink to the AudioSource to
  // get data callbacks.
  // This method is called on the libjingle worker thread.
  // TODO(xians): Make sure Start() is called only once.
  void SetSource(AudioSource* source) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(source);
    if (source_) {
      RTC_DCHECK(source_ == source);
      return;
    }
    source->SetSink(this);
    source_ = source;
    UpdateSendState();
  }

  // Stops sending by setting the sink of the AudioSource to nullptr. No data
  // callback will be received after this method.
  // This method is called on the libjingle worker thread.
  void ClearSource() {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    if (source_) {
      source_->SetSink(nullptr);
      source_ = nullptr;
    }
    UpdateSendState();
  }

  // AudioSource::Sink implementation.
  // This method is called on the audio thread.
  void OnData(const void* audio_data,
              int bits_per_sample,
              int sample_rate,
              size_t number_of_channels,
              size_t number_of_frames,
              std::optional<int64_t> absolute_capture_timestamp_ms) override {
    TRACE_EVENT_BEGIN2("webrtc", "WebRtcAudioSendStream::OnData", "sample_rate",
                       sample_rate, "number_of_frames", number_of_frames);
    RTC_DCHECK_EQ(16, bits_per_sample);
    RTC_CHECK_RUNS_SERIALIZED(&audio_capture_race_checker_);
    RTC_DCHECK(stream_);
    std::unique_ptr<AudioFrame> audio_frame(new AudioFrame());
    audio_frame->UpdateFrame(
        audio_frame->timestamp_, static_cast<const int16_t*>(audio_data),
        number_of_frames, sample_rate, audio_frame->speech_type_,
        audio_frame->vad_activity_, number_of_channels);
    // TODO(bugs.webrtc.org/10739): add dcheck that
    // `absolute_capture_timestamp_ms` always receives a value.
    if (absolute_capture_timestamp_ms) {
      audio_frame->set_absolute_capture_timestamp_ms(
          *absolute_capture_timestamp_ms);
    }
    stream_->SendAudioData(std::move(audio_frame));
    TRACE_EVENT_END1("webrtc", "WebRtcAudioSendStream::OnData",
                     "number_of_channels", number_of_channels);
  }

  // Callback from the `source_` when it is going away. In case Start() has
  // never been called, this callback won't be triggered.
  void OnClose() override {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    // Set `source_` to nullptr to make sure no more callback will get into
    // the source.
    source_ = nullptr;
    UpdateSendState();
  }

  const RtpParameters& rtp_parameters() const { return rtp_parameters_; }

  RTCError SetRtpParameters(const RtpParameters& parameters,
                            SetParametersCallback callback) {
    RTCError error = CheckRtpParametersInvalidModificationAndValues(
        rtp_parameters_, parameters, call_->trials());
    if (!error.ok()) {
      return webrtc::InvokeSetParametersCallback(callback, error);
    }

    std::optional<int> send_rate;
    if (audio_codec_spec_) {
      send_rate = ComputeSendBitrate(max_send_bitrate_bps_,
                                     parameters.encodings[0].max_bitrate_bps,
                                     *audio_codec_spec_);
      if (!send_rate) {
        return webrtc::InvokeSetParametersCallback(
            callback, RTCError(RTCErrorType::INTERNAL_ERROR));
      }
    }

    const std::optional<int> old_rtp_max_bitrate =
        rtp_parameters_.encodings[0].max_bitrate_bps;
    double old_priority = rtp_parameters_.encodings[0].bitrate_priority;
    Priority old_dscp = rtp_parameters_.encodings[0].network_priority;
    bool old_adaptive_ptime = rtp_parameters_.encodings[0].adaptive_ptime;
    rtp_parameters_ = parameters;
    config_.bitrate_priority = rtp_parameters_.encodings[0].bitrate_priority;
    config_.has_dscp =
        (rtp_parameters_.encodings[0].network_priority != Priority::kLow);

    bool reconfigure_send_stream =
        (rtp_parameters_.encodings[0].max_bitrate_bps != old_rtp_max_bitrate) ||
        (rtp_parameters_.encodings[0].bitrate_priority != old_priority) ||
        (rtp_parameters_.encodings[0].network_priority != old_dscp) ||
        (rtp_parameters_.encodings[0].adaptive_ptime != old_adaptive_ptime);
    if (rtp_parameters_.encodings[0].max_bitrate_bps != old_rtp_max_bitrate) {
      // Update the bitrate range.
      if (send_rate) {
        config_.send_codec_spec->target_bitrate_bps = send_rate;
      }
    }
    if (reconfigure_send_stream) {
      // Changing adaptive_ptime may update the audio network adaptor config
      // used.
      UpdateAudioNetworkAdaptorConfig();
      UpdateAllowedBitrateRange();
      ReconfigureAudioSendStream(std::move(callback));
    } else {
      webrtc::InvokeSetParametersCallback(callback, RTCError::OK());
    }

    rtp_parameters_.rtcp.cname = config_.rtp.c_name;
    rtp_parameters_.rtcp.reduced_size =
        config_.rtp.rtcp_mode == RtcpMode::kReducedSize;

    // parameters.encodings[0].active could have changed.
    UpdateSendState();
    return RTCError::OK();
  }

  void SetEncoderToPacketizerFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    config_.frame_transformer = std::move(frame_transformer);
    ReconfigureAudioSendStream(nullptr);
  }

  // RingRTC change to configure opus
  void ConfigureEncoder(const AudioEncoder::Config& config) {
    stream_->ConfigureEncoder(config);
  }

  // RingRTC change to get audio levels
  uint16_t GetAudioLevel() {
    return stream_->GetAudioLevel();
  }

 private:
  void UpdateSendState() {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(stream_);
    RTC_DCHECK_EQ(1UL, rtp_parameters_.encodings.size());
    // Stream can be started without |source_| being set.
    if (send_ && rtp_parameters_.encodings[0].active) {
      stream_->Start();
    } else {
      stream_->Stop();
    }
  }

  void UpdateAllowedBitrateRange() {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    // The order of precedence, from lowest to highest is:
    // - a reasonable default of 32kbps min/max
    // - fixed target bitrate from codec spec
    // - lower min bitrate if adaptive ptime is enabled
    const int kDefaultBitrateBps = 32000;
    config_.min_bitrate_bps = kDefaultBitrateBps;
    config_.max_bitrate_bps = kDefaultBitrateBps;

    if (config_.send_codec_spec &&
        config_.send_codec_spec->target_bitrate_bps) {
      config_.min_bitrate_bps = *config_.send_codec_spec->target_bitrate_bps;
      config_.max_bitrate_bps = *config_.send_codec_spec->target_bitrate_bps;
    }

    if (rtp_parameters_.encodings[0].adaptive_ptime) {
      config_.min_bitrate_bps = std::min(
          config_.min_bitrate_bps,
          static_cast<int>(adaptive_ptime_config_.min_encoder_bitrate.bps()));
    }
  }

  void UpdateSendCodecSpec(
      const AudioSendStream::Config::SendCodecSpec& send_codec_spec) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    config_.send_codec_spec = send_codec_spec;
    auto info =
        config_.encoder_factory->QueryAudioEncoder(send_codec_spec.format);
    RTC_DCHECK(info);
    // If a specific target bitrate has been set for the stream, use that as
    // the new default bitrate when computing send bitrate.
    if (send_codec_spec.target_bitrate_bps) {
      info->default_bitrate_bps = std::max(
          info->min_bitrate_bps,
          std::min(info->max_bitrate_bps, *send_codec_spec.target_bitrate_bps));
    }

    audio_codec_spec_.emplace(AudioCodecSpec{send_codec_spec.format, *info});

    config_.send_codec_spec->target_bitrate_bps = ComputeSendBitrate(
        max_send_bitrate_bps_, rtp_parameters_.encodings[0].max_bitrate_bps,
        *audio_codec_spec_);

    UpdateAllowedBitrateRange();

    // Encoder will only use two channels if the stereo parameter is set.
    const auto& it = send_codec_spec.format.parameters.find("stereo");
    if (it != send_codec_spec.format.parameters.end() && it->second == "1") {
      num_encoded_channels_ = 2;
    } else {
      num_encoded_channels_ = 1;
    }
  }

  void UpdateAudioNetworkAdaptorConfig() {
    if (adaptive_ptime_config_.enabled ||
        rtp_parameters_.encodings[0].adaptive_ptime) {
      config_.audio_network_adaptor_config =
          adaptive_ptime_config_.audio_network_adaptor_config;
      return;
    }
    config_.audio_network_adaptor_config =
        audio_network_adaptor_config_from_options_;
  }

  void ReconfigureAudioSendStream(SetParametersCallback callback) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    RTC_DCHECK(stream_);
    stream_->Reconfigure(config_, std::move(callback));
  }

  int NumPreferredChannels() const override { return num_encoded_channels_; }

  const AdaptivePtimeConfig adaptive_ptime_config_;
  SequenceChecker worker_thread_checker_;
  RaceChecker audio_capture_race_checker_;
  Call* call_ = nullptr;
  AudioSendStream::Config config_;
  // The stream is owned by WebRtcAudioSendStream and may be reallocated if
  // configuration changes.
  AudioSendStream* stream_ = nullptr;

  // Raw pointer to AudioSource owned by LocalAudioTrackHandler.
  // PeerConnection will make sure invalidating the pointer before the object
  // goes away.
  AudioSource* source_ = nullptr;
  bool send_ = false;
  bool muted_ = false;
  int max_send_bitrate_bps_;
  RtpParameters rtp_parameters_;
  std::optional<AudioCodecSpec> audio_codec_spec_;
  // TODO(webrtc:11717): Remove this once audio_network_adaptor in AudioOptions
  // has been removed.
  std::optional<std::string> audio_network_adaptor_config_from_options_;
  std::atomic<int> num_encoded_channels_{-1};
};

WebRtcVoiceSendChannel::WebRtcVoiceSendChannel(
    WebRtcVoiceEngine* engine,
    const MediaConfig& config,
    const AudioOptions& options,
    const CryptoOptions& crypto_options,
    Call* call,
    AudioCodecPairId codec_pair_id)
    : MediaChannelUtil(call->network_thread(), config.enable_dscp),
      worker_thread_(call->worker_thread()),
      engine_(engine),
      call_(call),
      audio_config_(config.audio),
      codec_pair_id_(codec_pair_id),
      crypto_options_(crypto_options) {
  RTC_LOG(LS_VERBOSE) << "WebRtcVoiceSendChannel::WebRtcVoiceSendChannel";
  RTC_DCHECK(call);
  SetOptions(options);
}

WebRtcVoiceSendChannel::~WebRtcVoiceSendChannel() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DLOG(LS_VERBOSE) << "WebRtcVoiceSendChannel::~WebRtcVoiceSendChannel";
  // TODO(solenberg): Should be able to delete the streams directly, without
  //                  going through RemoveNnStream(), once stream objects handle
  //                  all (de)configuration.
  while (!send_streams_.empty()) {
    RemoveSendStream(send_streams_.begin()->first);
  }
}

bool WebRtcVoiceSendChannel::SetOptions(const AudioOptions& options) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "Setting voice channel options: " << options.ToString();

  // We retain all of the existing options, and apply the given ones
  // on top.  This means there is no way to "clear" options such that
  // they go back to the engine default.
  options_.SetAll(options);
  engine()->ApplyOptions(options_);

  std::optional<std::string> audio_network_adaptor_config =
      GetAudioNetworkAdaptorConfig(options_);
  for (auto& it : send_streams_) {
    it.second->SetAudioNetworkAdaptorConfig(audio_network_adaptor_config);
  }

  RTC_LOG(LS_INFO) << "Set voice send channel options. Current options: "
                   << options_.ToString();
  return true;
}

bool WebRtcVoiceSendChannel::SetSenderParameters(
    const AudioSenderParameter& params) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::SetSenderParameters");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "WebRtcVoiceMediaChannel::SetSenderParameters: "
                   << params.ToString();
  // TODO(pthatcher): Refactor this to be more clean now that we have
  // all the information at once.

  // Finding if the RtpParameters force a specific codec
  std::optional<Codec> force_codec;
  if (send_streams_.size() == 1) {
    // Since audio simulcast is not supported, currently, only PlanB
    // has multiple tracks and we don't care about getting the
    // functionality working there properly.
    auto rtp_parameters = send_streams_.begin()->second->rtp_parameters();
    if (rtp_parameters.encodings[0].codec) {
      auto matched_codec =
          absl::c_find_if(params.codecs, [&](auto negotiated_codec) {
            return negotiated_codec.MatchesRtpCodec(
                *rtp_parameters.encodings[0].codec);
          });
      if (matched_codec != params.codecs.end()) {
        force_codec = *matched_codec;
      } else {
        // The requested codec has been negotiated away, we clear it from the
        // parameters.
        for (auto& encoding : rtp_parameters.encodings) {
          encoding.codec.reset();
        }
        send_streams_.begin()->second->SetRtpParameters(rtp_parameters,
                                                        nullptr);
      }
    }
  }

  if (!SetSendCodecs(params.codecs, force_codec)) {
    return false;
  }

  if (!ValidateRtpExtensions(params.extensions, send_rtp_extensions_)) {
    return false;
  }

  if (ExtmapAllowMixed() != params.extmap_allow_mixed) {
    SetExtmapAllowMixed(params.extmap_allow_mixed);
    for (auto& it : send_streams_) {
      it.second->SetExtmapAllowMixed(params.extmap_allow_mixed);
    }
  }

  std::vector<RtpExtension> filtered_extensions =
      FilterRtpExtensions(params.extensions, RtpExtension::IsSupportedForAudio,
                          true, call_->trials());
  if (send_rtp_extensions_ != filtered_extensions) {
    send_rtp_extensions_.swap(filtered_extensions);
    for (auto& it : send_streams_) {
      it.second->SetRtpExtensions(send_rtp_extensions_);
    }
  }
  if (!params.mid.empty()) {
    mid_ = params.mid;
    for (auto& it : send_streams_) {
      it.second->SetMid(params.mid);
    }
  }

  if (send_codec_spec_ && !SetMaxSendBitrate(params.max_bandwidth_bps)) {
    return false;
  }
  rtcp_mode_ =
      params.rtcp.reduced_size ? RtcpMode::kReducedSize : RtcpMode::kCompound;
  for (auto& it : send_streams_) {
    it.second->SetRtcpMode(rtcp_mode_);
  }
  return SetOptions(params.options);
}

std::optional<Codec> WebRtcVoiceSendChannel::GetSendCodec() const {
  if (send_codec_spec_) {
    return webrtc::CreateAudioCodec(send_codec_spec_->format);
  }
  return std::nullopt;
}

// Utility function called from SetSenderParameters() to extract current send
// codec settings from the given list of codecs (originally from SDP). Both send
// and receive streams may be reconfigured based on the new settings.
bool WebRtcVoiceSendChannel::SetSendCodecs(
    const std::vector<Codec>& codecs,
    std::optional<Codec> preferred_codec) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  dtmf_payload_type_ = std::nullopt;
  dtmf_payload_freq_ = -1;

  // Validate supplied codecs list.
  for (const webrtc::Codec& codec : codecs) {
    // TODO(solenberg): Validate more aspects of input - that payload types
    //                  don't overlap, remove redundant/unsupported codecs etc -
    //                  the same way it is done for RtpHeaderExtensions.
    if (codec.id < kMinPayloadType || codec.id > kMaxPayloadType) {
      RTC_LOG(LS_WARNING) << "Codec payload type out of range: "
                          << ToString(codec);
      return false;
    }
  }

  // Find PT of telephone-event codec with lowest clockrate, as a fallback, in
  // case we don't have a DTMF codec with a rate matching the send codec's, or
  // if this function returns early.
  std::vector<Codec> dtmf_codecs;
  for (const webrtc::Codec& codec : codecs) {
    if (IsCodec(codec, kDtmfCodecName)) {
      dtmf_codecs.push_back(codec);
      if (!dtmf_payload_type_ || codec.clockrate < dtmf_payload_freq_) {
        dtmf_payload_type_ = codec.id;
        dtmf_payload_freq_ = codec.clockrate;
      }
    }
  }

  // Scan through the list to figure out the codec to use for sending.
  std::optional<AudioSendStream::Config::SendCodecSpec> send_codec_spec;
  BitrateConstraints bitrate_config;
  std::optional<AudioCodecInfo> voice_codec_info;
  size_t send_codec_position = 0;
  for (const webrtc::Codec& voice_codec : codecs) {
    if (!(IsCodec(voice_codec, kCnCodecName) ||
          IsCodec(voice_codec, kDtmfCodecName) ||
          IsCodec(voice_codec, kRedCodecName)) &&
        (!preferred_codec || preferred_codec->Matches(voice_codec))) {
      SdpAudioFormat format(voice_codec.name, voice_codec.clockrate,
                            voice_codec.channels, voice_codec.params);

      voice_codec_info = engine()->encoder_factory_->QueryAudioEncoder(format);
      if (!voice_codec_info) {
        RTC_LOG(LS_WARNING) << "Unknown codec " << ToString(voice_codec);
        send_codec_position++;
        continue;
      }

      send_codec_spec =
          AudioSendStream::Config::SendCodecSpec(voice_codec.id, format);
      if (voice_codec.bitrate > 0) {
        send_codec_spec->target_bitrate_bps = voice_codec.bitrate;
      }
      send_codec_spec->nack_enabled = webrtc::HasNack(voice_codec);
      send_codec_spec->enable_non_sender_rtt = webrtc::HasRrtr(voice_codec);
      bitrate_config = GetBitrateConfigForCodec(voice_codec);
      break;
    }
    send_codec_position++;
  }

  if (!send_codec_spec) {
    // No codecs in common, bail out early.
    return true;
  }

  RTC_DCHECK(voice_codec_info);
  if (voice_codec_info->allow_comfort_noise) {
    // Loop through the codecs list again to find the CN codec.
    // TODO(solenberg): Break out into a separate function?
    for (const webrtc::Codec& cn_codec : codecs) {
      if (IsCodec(cn_codec, kCnCodecName) &&
          cn_codec.clockrate == send_codec_spec->format.clockrate_hz &&
          cn_codec.channels == voice_codec_info->num_channels) {
        if (cn_codec.channels != 1) {
          RTC_LOG(LS_WARNING)
              << "CN #channels " << cn_codec.channels << " not supported.";
        } else if (cn_codec.clockrate != 8000) {
          RTC_LOG(LS_WARNING)
              << "CN frequency " << cn_codec.clockrate << " not supported.";
        } else {
          send_codec_spec->cng_payload_type = cn_codec.id;
        }
        break;
      }
    }

    // Find the telephone-event PT exactly matching the preferred send codec.
    for (const webrtc::Codec& dtmf_codec : dtmf_codecs) {
      if (dtmf_codec.clockrate == send_codec_spec->format.clockrate_hz) {
        dtmf_payload_type_ = dtmf_codec.id;
        dtmf_payload_freq_ = dtmf_codec.clockrate;
        break;
      }
    }
  }

  // Loop through the codecs to find the RED codec that matches opus
  // with respect to clockrate and number of channels.
  // RED codec needs to be negotiated before the actual codec they
  // reference.
  for (size_t i = 0; i < send_codec_position; ++i) {
    const Codec& red_codec = codecs[i];
    if (IsCodec(red_codec, kRedCodecName) &&
        CheckRedParameters(red_codec, *send_codec_spec)) {
      send_codec_spec->red_payload_type = red_codec.id;
      break;
    }
  }

  if (send_codec_spec_ != send_codec_spec) {
    send_codec_spec_ = std::move(send_codec_spec);
    // Apply new settings to all streams.
    for (const auto& kv : send_streams_) {
      kv.second->SetSendCodecSpec(*send_codec_spec_);
    }
  } else {
    // If the codec isn't changing, set the start bitrate to -1 which means
    // "unchanged" so that BWE isn't affected.
    bitrate_config.start_bitrate_bps = -1;
  }
  call_->GetTransportControllerSend()->SetSdpBitrateParameters(bitrate_config);

  send_codecs_ = codecs;

  if (send_codec_changed_callback_) {
    send_codec_changed_callback_();
  }

  return true;
}

void WebRtcVoiceSendChannel::SetSend(bool send) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::SetSend");
  if (send_ == send) {
    return;
  }

  // Apply channel specific options.
  if (send) {
    engine()->ApplyOptions(options_);

    // RingRTC change to not do early InitRecording()
#if false
    // Initialize the ADM for recording (this may take time on some platforms,
    // e.g. Android).
    if (options_.init_recording_on_send.value_or(true) &&
        // InitRecording() may return an error if the ADM is already recording.
        !engine()->adm()->RecordingIsInitialized() &&
        !engine()->adm()->Recording()) {
      if (engine()->adm()->InitRecording() != 0) {
        RTC_LOG(LS_WARNING) << "Failed to initialize recording";
      }
    }
#endif
  }

  // Change the settings on each send channel.
  for (auto& kv : send_streams_) {
    kv.second->SetSend(send);
  }

  send_ = send;
}

bool WebRtcVoiceSendChannel::SetAudioSend(uint32_t ssrc,
                                          bool enable,
                                          const AudioOptions* options,
                                          AudioSource* source) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  // TODO(solenberg): The state change should be fully rolled back if any one of
  //                  these calls fail.
  if (!SetLocalSource(ssrc, source)) {
    return false;
  }
  if (!MuteStream(ssrc, !enable)) {
    return false;
  }
  if (enable && options) {
    return SetOptions(*options);
  }
  return true;
}

bool WebRtcVoiceSendChannel::AddSendStream(const StreamParams& sp) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::AddSendStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "AddSendStream: " << sp.ToString();

  uint32_t ssrc = sp.first_ssrc();
  RTC_DCHECK(0 != ssrc);

  if (send_streams_.find(ssrc) != send_streams_.end()) {
    RTC_LOG(LS_ERROR) << "Stream already exists with ssrc " << ssrc;
    return false;
  }

  std::optional<std::string> audio_network_adaptor_config =
      GetAudioNetworkAdaptorConfig(options_);
  WebRtcAudioSendStream* stream = new WebRtcAudioSendStream(
      ssrc, mid_, sp.cname, sp.id, send_codec_spec_, ExtmapAllowMixed(),
      send_rtp_extensions_, max_send_bitrate_bps_,
      audio_config_.rtcp_report_interval_ms, audio_network_adaptor_config,
      call_, transport(), engine()->encoder_factory_, codec_pair_id_, nullptr,
      crypto_options_);
  send_streams_.insert(std::make_pair(ssrc, stream));
  if (ssrc_list_changed_callback_) {
    std::set<uint32_t> ssrcs_in_use;
    for (auto it : send_streams_) {
      ssrcs_in_use.insert(it.first);
    }
    ssrc_list_changed_callback_(ssrcs_in_use);
  }

  send_streams_[ssrc]->SetSend(send_);
  return true;
}

bool WebRtcVoiceSendChannel::RemoveSendStream(uint32_t ssrc) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::RemoveSendStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "RemoveSendStream: " << ssrc;

  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    RTC_LOG(LS_WARNING) << "Try to remove stream with ssrc " << ssrc
                        << " which doesn't exist.";
    return false;
  }

  it->second->SetSend(false);

  // TODO(solenberg): If we're removing the receiver_reports_ssrc_ stream, find
  // the first active send stream and use that instead, reassociating receive
  // streams.

  delete it->second;
  send_streams_.erase(it);
  if (send_streams_.empty()) {
    SetSend(false);
  }
  return true;
}

void WebRtcVoiceSendChannel::SetSsrcListChangedCallback(
    absl::AnyInvocable<void(const std::set<uint32_t>&)> callback) {
  ssrc_list_changed_callback_ = std::move(callback);
}

bool WebRtcVoiceSendChannel::SetLocalSource(uint32_t ssrc,
                                            AudioSource* source) {
  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    if (source) {
      // Return an error if trying to set a valid source with an invalid ssrc.
      RTC_LOG(LS_ERROR) << "SetLocalSource failed with ssrc " << ssrc;
      return false;
    }

    // The channel likely has gone away, do nothing.
    return true;
  }

  if (source) {
    it->second->SetSource(source);
  } else {
    it->second->ClearSource();
  }

  return true;
}

bool WebRtcVoiceSendChannel::CanInsertDtmf() {
  return dtmf_payload_type_.has_value() && send_;
}

void WebRtcVoiceSendChannel::SetFrameEncryptor(
    uint32_t ssrc,
    scoped_refptr<FrameEncryptorInterface> frame_encryptor) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream != send_streams_.end()) {
    matching_stream->second->SetFrameEncryptor(frame_encryptor);
  }
}

bool WebRtcVoiceSendChannel::InsertDtmf(uint32_t ssrc,
                                        int event,
                                        int duration) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "WebRtcVoiceMediaChannel::InsertDtmf";
  if (!CanInsertDtmf()) {
    return false;
  }

  // Figure out which WebRtcAudioSendStream to send the event on.
  auto it = ssrc != 0 ? send_streams_.find(ssrc) : send_streams_.begin();
  if (it == send_streams_.end()) {
    RTC_LOG(LS_WARNING) << "The specified ssrc " << ssrc << " is not in use.";
    return false;
  }
  if (event < kMinTelephoneEventCode || event > kMaxTelephoneEventCode) {
    RTC_LOG(LS_WARNING) << "DTMF event code " << event << " out of range.";
    return false;
  }
  RTC_DCHECK_NE(-1, dtmf_payload_freq_);
  return it->second->SendTelephoneEvent(*dtmf_payload_type_, dtmf_payload_freq_,
                                        event, duration);
}

void WebRtcVoiceSendChannel::OnPacketSent(const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  // TODO(tommi): We shouldn't need to go through call_ to deliver this
  // notification. We should already have direct access to
  // video_send_delay_stats_ and transport_send_ptr_ via `stream_`.
  // So we should be able to remove OnSentPacket from Call and handle this per
  // channel instead. At the moment Call::OnSentPacket calls OnSentPacket for
  // the video stats, which we should be able to skip.
  call_->OnSentPacket(sent_packet);
}

void WebRtcVoiceSendChannel::OnNetworkRouteChanged(
    absl::string_view transport_name,
    const NetworkRoute& network_route) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);

  call_->OnAudioTransportOverheadChanged(network_route.packet_overhead);

  worker_thread_->PostTask(SafeTask(
      task_safety_.flag(),
      [this, name = std::string(transport_name), route = network_route] {
        RTC_DCHECK_RUN_ON(worker_thread_);
        call_->GetTransportControllerSend()->OnNetworkRouteChanged(name, route);
      }));
}

bool WebRtcVoiceSendChannel::MuteStream(uint32_t ssrc, bool muted) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  const auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    RTC_LOG(LS_WARNING) << "The specified ssrc " << ssrc << " is not in use.";
    return false;
  }
  it->second->SetMuted(muted);

  // TODO(solenberg):
  // We set the AGC to mute state only when all the channels are muted.
  // This implementation is not ideal, instead we should signal the AGC when
  // the mic channel is muted/unmuted. We can't do it today because there
  // is no good way to know which stream is mapping to the mic channel.
  // RingRTC change to make it possible to share an APM.
  // See set_capture_output_used in audio_processing.h.
  bool capture_output_used = false;
  for (const auto& kv : send_streams_) {
    capture_output_used = capture_output_used || !kv.second->muted();
  }
  AudioProcessing* ap = engine()->apm();
  if (ap) {
    ap->set_capture_output_used(this, capture_output_used);
  }

  return true;
}

bool WebRtcVoiceSendChannel::SetMaxSendBitrate(int bps) {
  RTC_LOG(LS_INFO) << "WebRtcVoiceMediaChannel::SetMaxSendBitrate.";
  max_send_bitrate_bps_ = bps;
  bool success = true;
  for (const auto& kv : send_streams_) {
    if (!kv.second->SetMaxSendBitrate(max_send_bitrate_bps_)) {
      success = false;
    }
  }
  return success;
}

void WebRtcVoiceSendChannel::OnReadyToSend(bool ready) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  RTC_LOG(LS_VERBOSE) << "OnReadyToSend: " << (ready ? "Ready." : "Not ready.");
  call_->SignalChannelNetworkState(
      MediaType::AUDIO, ready ? webrtc::kNetworkUp : webrtc::kNetworkDown);
}

bool WebRtcVoiceSendChannel::GetStats(VoiceMediaSendInfo* info) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::GetSendStats");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(info);

  // Get SSRC and stats for each sender.
  // With separate send and receive channels, we expect GetStats to be called on
  // both, and accumulate info, but only one channel (the send one) should have
  // senders.
  RTC_DCHECK(info->senders.size() == 0U || send_streams_.size() == 0);
  for (const auto& stream : send_streams_) {
    AudioSendStream::Stats stats = stream.second->GetStats(false);
    VoiceSenderInfo sinfo;
    sinfo.add_ssrc(stats.local_ssrc);
    sinfo.payload_bytes_sent = stats.payload_bytes_sent;
    sinfo.header_and_padding_bytes_sent = stats.header_and_padding_bytes_sent;
    sinfo.retransmitted_bytes_sent = stats.retransmitted_bytes_sent;
    sinfo.packets_sent = stats.packets_sent;
    sinfo.total_packet_send_delay = stats.total_packet_send_delay;
    sinfo.retransmitted_packets_sent = stats.retransmitted_packets_sent;
    sinfo.packets_lost = stats.packets_lost;
    sinfo.fraction_lost = stats.fraction_lost;
    sinfo.nacks_received = stats.nacks_received;
    sinfo.target_bitrate =
        stats.target_bitrate_bps > 0
            ? std::optional(DataRate::BitsPerSec(stats.target_bitrate_bps))
            : std::nullopt;
    sinfo.codec_name = stats.codec_name;
    sinfo.codec_payload_type = stats.codec_payload_type;
    sinfo.jitter_ms = stats.jitter_ms;
    sinfo.rtt_ms = stats.rtt_ms;
    sinfo.audio_level = stats.audio_level;
    sinfo.total_input_energy = stats.total_input_energy;
    sinfo.total_input_duration = stats.total_input_duration;
    sinfo.ana_statistics = stats.ana_statistics;
    sinfo.apm_statistics = stats.apm_statistics;
    sinfo.report_block_datas = std::move(stats.report_block_datas);

    auto encodings = stream.second->rtp_parameters().encodings;
    if (!encodings.empty()) {
      sinfo.active = encodings[0].active;
    }

    info->senders.push_back(sinfo);
  }

  FillSendCodecStats(info);

  return true;
}

void WebRtcVoiceSendChannel::FillSendCodecStats(
    VoiceMediaSendInfo* voice_media_info) {
  for (const auto& sender : voice_media_info->senders) {
    auto codec =
        absl::c_find_if(send_codecs_, [&sender](const webrtc::Codec& c) {
          return sender.codec_payload_type &&
                 *sender.codec_payload_type == c.id;
        });
    if (codec != send_codecs_.end()) {
      voice_media_info->send_codecs.insert(
          std::make_pair(codec->id, codec->ToCodecParameters()));
    }
  }
}

void WebRtcVoiceSendChannel::SetEncoderToPacketizerFrameTransformer(
    uint32_t ssrc,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream == send_streams_.end()) {
    RTC_LOG(LS_INFO) << "Attempting to set frame transformer for SSRC:" << ssrc
                     << " which doesn't exist.";
    return;
  }
  matching_stream->second->SetEncoderToPacketizerFrameTransformer(
      std::move(frame_transformer));
}

RtpParameters WebRtcVoiceSendChannel::GetRtpSendParameters(
    uint32_t ssrc) const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    RTC_LOG(LS_WARNING) << "Attempting to get RTP send parameters for stream "
                           "with ssrc "
                        << ssrc << " which doesn't exist.";
    return RtpParameters();
  }

  RtpParameters rtp_params = it->second->rtp_parameters();
  // Need to add the common list of codecs to the send stream-specific
  // RTP parameters.
  for (const webrtc::Codec& codec : send_codecs_) {
    rtp_params.codecs.push_back(codec.ToCodecParameters());
  }
  return rtp_params;
}

RTCError WebRtcVoiceSendChannel::SetRtpSendParameters(
    uint32_t ssrc,
    const RtpParameters& parameters,
    SetParametersCallback callback) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    RTC_LOG(LS_WARNING) << "Attempting to set RTP send parameters for stream "
                           "with ssrc "
                        << ssrc << " which doesn't exist.";
    return webrtc::InvokeSetParametersCallback(
        callback, RTCError(RTCErrorType::INTERNAL_ERROR));
  }

  // TODO(deadbeef): Handle setting parameters with a list of codecs in a
  // different order (which should change the send codec).
  RtpParameters current_parameters = GetRtpSendParameters(ssrc);
  if (current_parameters.codecs != parameters.codecs) {
    RTC_DLOG(LS_ERROR) << "Using SetParameters to change the set of codecs "
                          "is not currently supported.";
    return webrtc::InvokeSetParametersCallback(
        callback, RTCError(RTCErrorType::INTERNAL_ERROR));
  }

  if (!parameters.encodings.empty()) {
    // Note that these values come from:
    // https://tools.ietf.org/html/draft-ietf-tsvwg-rtcweb-qos-16#section-5
    DiffServCodePoint new_dscp = webrtc::DSCP_DEFAULT;
    switch (parameters.encodings[0].network_priority) {
      case Priority::kVeryLow:
        new_dscp = webrtc::DSCP_CS1;
        break;
      case Priority::kLow:
        new_dscp = webrtc::DSCP_DEFAULT;
        break;
      case Priority::kMedium:
        new_dscp = webrtc::DSCP_EF;
        break;
      case Priority::kHigh:
        new_dscp = webrtc::DSCP_EF;
        break;
    }
    SetPreferredDscp(new_dscp);

    std::optional<Codec> send_codec = GetSendCodec();
    // Since we validate that all layers have the same value, we can just check
    // the first layer.
    // TODO: https://issues.webrtc.org/362277533 - Support mixed-codec simulcast
    if (parameters.encodings[0].codec && send_codec &&
        !send_codec->MatchesRtpCodec(*parameters.encodings[0].codec)) {
      RTC_LOG(LS_VERBOSE) << "Trying to change codec to "
                          << parameters.encodings[0].codec->name;
      auto matched_codec =
          absl::c_find_if(send_codecs_, [&](auto negotiated_codec) {
            return negotiated_codec.MatchesRtpCodec(
                *parameters.encodings[0].codec);
          });

      if (matched_codec == send_codecs_.end()) {
        return webrtc::InvokeSetParametersCallback(
            callback,
            RTCError(RTCErrorType::INVALID_MODIFICATION,
                     "Attempted to use an unsupported codec for layer 0"));
      }

      SetSendCodecs(send_codecs_, *matched_codec);
    }
  }

  // TODO(minyue): The following legacy actions go into
  // `WebRtcAudioSendStream::SetRtpParameters()` which is called at the end,
  // though there are two difference:
  // 1. `WebRtcVoiceMediaChannel::SetChannelSendParameters()` only calls
  // `SetSendCodec` while `WebRtcAudioSendStream::SetRtpParameters()` calls
  // `SetSendCodecs`. The outcome should be the same.
  // 2. AudioSendStream can be recreated.

  // Codecs are handled at the WebRtcVoiceMediaChannel level.
  RtpParameters reduced_params = parameters;
  reduced_params.codecs.clear();
  return it->second->SetRtpParameters(reduced_params, std::move(callback));
}

void WebRtcVoiceSendChannel::ConfigureEncoders(const AudioEncoder::Config& config) {
  int count = 0;
  for (auto& it : send_streams_) {
    it.second->ConfigureEncoder(config);
    count++;
  }

  if (count == 0) {
    RTC_LOG(LS_WARNING) << "WebRtcVoiceMediaChannel::ConfigureEncoders(...) changed no send streams!";
  } else {
    RTC_LOG(LS_INFO) << "WebRtcVoiceMediaChannel::ConfigureEncoders(...) changed " << count << " transceivers.";
  }
}

// RingRTC change to get audio levels
void WebRtcVoiceSendChannel::GetCapturedAudioLevel(uint16_t* captured_out) {
  uint16_t captured = 0;
  for (const auto& kv : send_streams_) {
    captured = kv.second->GetAudioLevel();
  }

  *captured_out = captured;
}

// -------------------------- WebRtcVoiceReceiveChannel ----------------------

class WebRtcVoiceReceiveChannel::WebRtcAudioReceiveStream {
 public:
  WebRtcAudioReceiveStream(AudioReceiveStreamInterface::Config config,
                           Call* call)
      : call_(call), stream_(call_->CreateAudioReceiveStream(config)) {
    RTC_DCHECK(call);
    RTC_DCHECK(stream_);
  }

  WebRtcAudioReceiveStream() = delete;
  WebRtcAudioReceiveStream(const WebRtcAudioReceiveStream&) = delete;
  WebRtcAudioReceiveStream& operator=(const WebRtcAudioReceiveStream&) = delete;

  ~WebRtcAudioReceiveStream() {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    call_->DestroyAudioReceiveStream(stream_);
  }

  AudioReceiveStreamInterface& stream() {
    RTC_DCHECK(stream_);
    return *stream_;
  }

  void SetFrameDecryptor(
      scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetFrameDecryptor(std::move(frame_decryptor));
  }

  void SetUseNack(bool use_nack) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetNackHistory(use_nack ? kNackRtpHistoryMs : 0);
  }

  void SetRtcpMode(::webrtc::RtcpMode mode) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetRtcpMode(mode);
  }

  void SetNonSenderRttMeasurement(bool enabled) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetNonSenderRttMeasurement(enabled);
  }

  // Set a new payload type -> decoder map.
  void SetDecoderMap(const std::map<int, SdpAudioFormat>& decoder_map) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetDecoderMap(decoder_map);
  }

  AudioReceiveStreamInterface::Stats GetStats(
      bool get_and_clear_legacy_stats) const {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    return stream_->GetStats(get_and_clear_legacy_stats);
  }

  void SetRawAudioSink(std::unique_ptr<AudioSinkInterface> sink) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    // Need to update the stream's sink first; once raw_audio_sink_ is
    // reassigned, whatever was in there before is destroyed.
    stream_->SetSink(sink.get());
    raw_audio_sink_ = std::move(sink);
  }

  void SetOutputVolume(double volume) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetGain(volume);
  }

  void SetPlayout(bool playout) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    if (playout) {
      stream_->Start();
    } else {
      stream_->Stop();
    }
  }

  bool SetBaseMinimumPlayoutDelayMs(int delay_ms) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    if (stream_->SetBaseMinimumPlayoutDelayMs(delay_ms))
      return true;

    RTC_LOG(LS_ERROR) << "Failed to SetBaseMinimumPlayoutDelayMs"
                         " on AudioReceiveStreamInterface on SSRC="
                      << stream_->remote_ssrc()
                      << " with delay_ms=" << delay_ms;
    return false;
  }

  int GetBaseMinimumPlayoutDelayMs() const {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    return stream_->GetBaseMinimumPlayoutDelayMs();
  }

  std::vector<RtpSource> GetSources() {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    return stream_->GetSources();
  }

  void SetDepacketizerToDecoderFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) {
    RTC_DCHECK_RUN_ON(&worker_thread_checker_);
    stream_->SetDepacketizerToDecoderFrameTransformer(frame_transformer);
  }

  // RingRTC change to get audio levels
  uint16_t GetAudioLevel() {
    return stream_->GetAudioLevel();
  }

 private:
  SequenceChecker worker_thread_checker_;
  Call* call_ = nullptr;
  AudioReceiveStreamInterface* const stream_ = nullptr;
  std::unique_ptr<AudioSinkInterface> raw_audio_sink_
      RTC_GUARDED_BY(worker_thread_checker_);
};

WebRtcVoiceReceiveChannel::WebRtcVoiceReceiveChannel(
    WebRtcVoiceEngine* engine,
    const MediaConfig& config,
    const AudioOptions& options,
    const CryptoOptions& crypto_options,
    Call* call,
    AudioCodecPairId codec_pair_id)
    : MediaChannelUtil(call->network_thread(), config.enable_dscp),
      worker_thread_(call->worker_thread()),
      engine_(engine),
      call_(call),
      audio_config_(config.audio),
      codec_pair_id_(codec_pair_id),
      crypto_options_(crypto_options) {
  RTC_LOG(LS_VERBOSE) << "WebRtcVoiceReceiveChannel::WebRtcVoiceReceiveChannel";
  RTC_DCHECK(call);
  SetOptions(options);
}

WebRtcVoiceReceiveChannel::~WebRtcVoiceReceiveChannel() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DLOG(LS_VERBOSE)
      << "WebRtcVoiceReceiveChannel::~WebRtcVoiceReceiveChannel";
  // TODO(solenberg): Should be able to delete the streams directly, without
  //                  going through RemoveNnStream(), once stream objects handle
  //                  all (de)configuration.
  while (!recv_streams_.empty()) {
    RemoveRecvStream(recv_streams_.begin()->first);
  }
}

bool WebRtcVoiceReceiveChannel::SetReceiverParameters(
    const AudioReceiverParameters& params) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::SetReceiverParameters");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "WebRtcVoiceMediaChannel::SetReceiverParameters: "
                   << params.ToString();
  // TODO(pthatcher): Refactor this to be more clean now that we have
  // all the information at once.
  mid_ = params.mid;

  if (!SetRecvCodecs(params.codecs)) {
    return false;
  }

  if (!ValidateRtpExtensions(params.extensions, recv_rtp_extensions_)) {
    return false;
  }
  std::vector<RtpExtension> filtered_extensions =
      FilterRtpExtensions(params.extensions, RtpExtension::IsSupportedForAudio,
                          false, call_->trials());
  if (recv_rtp_extensions_ != filtered_extensions) {
    recv_rtp_extensions_.swap(filtered_extensions);
    recv_rtp_extension_map_ = RtpHeaderExtensionMap(recv_rtp_extensions_);
  }
  // RTCP mode, NACK, and receive-side RTT are not configured here because they
  // enable send functionality in the receive channels. This functionality is
  // instead configured using the SetReceiveRtcpMode, SetReceiveNackEnabled, and
  // SetReceiveNonSenderRttEnabled methods.
  return true;
}

RtpParameters WebRtcVoiceReceiveChannel::GetRtpReceiverParameters(
    uint32_t ssrc) const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RtpParameters rtp_params;
  auto it = recv_streams_.find(ssrc);
  if (it == recv_streams_.end()) {
    RTC_LOG(LS_WARNING)
        << "Attempting to get RTP receive parameters for stream "
           "with ssrc "
        << ssrc << " which doesn't exist.";
    return RtpParameters();
  }
  rtp_params.encodings.emplace_back();
  rtp_params.encodings.back().ssrc = it->second->stream().remote_ssrc();
  rtp_params.header_extensions = recv_rtp_extensions_;

  for (const webrtc::Codec& codec : recv_codecs_) {
    rtp_params.codecs.push_back(codec.ToCodecParameters());
  }
  rtp_params.rtcp.reduced_size = recv_rtcp_mode_ == RtcpMode::kReducedSize;
  return rtp_params;
}

RtpParameters WebRtcVoiceReceiveChannel::GetDefaultRtpReceiveParameters()
    const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RtpParameters rtp_params;
  if (!default_sink_) {
    // Getting parameters on a default, unsignaled audio receive stream but
    // because we've not configured to receive such a stream, `encodings` is
    // empty.
    return rtp_params;
  }
  rtp_params.encodings.emplace_back();

  for (const webrtc::Codec& codec : recv_codecs_) {
    rtp_params.codecs.push_back(codec.ToCodecParameters());
  }
  return rtp_params;
}

bool WebRtcVoiceReceiveChannel::SetOptions(const AudioOptions& options) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "Setting voice channel options: " << options.ToString();

  // We retain all of the existing options, and apply the given ones
  // on top.  This means there is no way to "clear" options such that
  // they go back to the engine default.
  options_.SetAll(options);
  engine()->ApplyOptions(options_);

  RTC_LOG(LS_INFO) << "Set voice receive channel options. Current options: "
                   << options_.ToString();
  return true;
}

bool WebRtcVoiceReceiveChannel::SetRecvCodecs(
    const std::vector<Codec>& codecs_in) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  auto codecs = codecs_in;
  // Record the payload types used in the payload type suggester.
  RTC_LOG(LS_INFO) << "Setting receive voice codecs. Mid is " << mid_;
  for (auto& codec : codecs) {
    auto error = call_->GetPayloadTypeSuggester()->AddLocalMapping(
        mid_, codec.id, codec);
    if (!error.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to register PT for " << codec.ToString();
      return false;
    }
  }

  if (!VerifyUniquePayloadTypes(codecs)) {
    RTC_LOG(LS_ERROR) << "Codec payload types overlap.";
    return false;
  }

  // Create a payload type -> SdpAudioFormat map with all the decoders. Fail
  // unless the factory claims to support all decoders.
  std::map<int, SdpAudioFormat> decoder_map;
  for (const webrtc::Codec& codec : codecs) {
    // Log a warning if a codec's payload type is changing. This used to be
    // treated as an error. It's abnormal, but not really illegal.
    std::optional<Codec> old_codec = FindCodec(recv_codecs_, codec);
    if (old_codec && old_codec->id != codec.id) {
      RTC_LOG(LS_WARNING) << codec.name << " mapped to a second payload type ("
                          << codec.id << ", was already mapped to "
                          << old_codec->id << ")";
    }
    auto format = AudioCodecToSdpAudioFormat(codec);
    if (!IsCodec(codec, kCnCodecName) && !IsCodec(codec, kDtmfCodecName) &&
        !IsCodec(codec, kRedCodecName) &&
        !engine()->decoder_factory_->IsSupportedDecoder(format)) {
      RTC_LOG(LS_ERROR) << "Unsupported codec: " << absl::StrCat(format);
      return false;
    }
    // We allow adding new codecs but don't allow changing the payload type of
    // codecs that are already configured since we might already be receiving
    // packets with that payload type. See RFC3264, Section 8.3.2.
    // TODO(deadbeef): Also need to check for clashes with previously mapped
    // payload types, and not just currently mapped ones. For example, this
    // should be illegal:
    // 1. {100: opus/48000/2, 101: ISAC/16000}
    // 2. {100: opus/48000/2}
    // 3. {100: opus/48000/2, 101: ISAC/32000}
    // Though this check really should happen at a higher level, since this
    // conflict could happen between audio and video codecs.
    auto existing = decoder_map_.find(codec.id);
    if (existing != decoder_map_.end() && !existing->second.Matches(format)) {
      RTC_LOG(LS_ERROR) << "Attempting to use payload type " << codec.id
                        << " for " << codec.name
                        << ", but it is already used for "
                        << existing->second.name;
      return false;
    }
    decoder_map.insert({codec.id, std::move(format)});
  }

  if (decoder_map == decoder_map_) {
    // There's nothing new to configure.
    return true;
  }

  bool playout_enabled = playout_;
  // Receive codecs can not be changed while playing. So we temporarily
  // pause playout.
  SetPlayout(false);
  RTC_DCHECK(!playout_);

  decoder_map_ = std::move(decoder_map);
  for (auto& kv : recv_streams_) {
    kv.second->SetDecoderMap(decoder_map_);
  }

  recv_codecs_ = codecs;

  SetPlayout(playout_enabled);
  RTC_DCHECK_EQ(playout_, playout_enabled);

  return true;
}

void WebRtcVoiceReceiveChannel::SetRtcpMode(::webrtc::RtcpMode mode) {
  // Check if the reduced size RTCP status changed on the
  // preferred send codec, and in that case reconfigure all receive streams.
  if (recv_rtcp_mode_ != mode) {
    RTC_LOG(LS_INFO) << "Changing RTCP mode on receive streams.";
    recv_rtcp_mode_ = mode;
    for (auto& kv : recv_streams_) {
      kv.second->SetRtcpMode(recv_rtcp_mode_);
    }
  }
}

void WebRtcVoiceReceiveChannel::SetReceiveNackEnabled(bool enabled) {
  // Check if the NACK status has changed on the
  // preferred send codec, and in that case reconfigure all receive streams.
  if (recv_nack_enabled_ != enabled) {
    RTC_LOG(LS_INFO) << "Changing NACK status on receive streams.";
    recv_nack_enabled_ = enabled;
    for (auto& kv : recv_streams_) {
      kv.second->SetUseNack(recv_nack_enabled_);
    }
  }
}

void WebRtcVoiceReceiveChannel::SetReceiveNonSenderRttEnabled(bool enabled) {
  // Check if the receive-side RTT status has changed on the preferred send
  // codec, in that case reconfigure all receive streams.
  if (enable_non_sender_rtt_ != enabled) {
    RTC_LOG(LS_INFO) << "Changing receive-side RTT status on receive streams.";
    enable_non_sender_rtt_ = enabled;
    for (auto& kv : recv_streams_) {
      kv.second->SetNonSenderRttMeasurement(enable_non_sender_rtt_);
    }
  }
}

void WebRtcVoiceReceiveChannel::SetPlayout(bool playout) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::SetPlayout");
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (playout_ == playout) {
    return;
  }

  for (const auto& kv : recv_streams_) {
    kv.second->SetPlayout(playout);
  }
  playout_ = playout;
}

bool WebRtcVoiceReceiveChannel::AddRecvStream(const StreamParams& sp) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::AddRecvStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "AddRecvStream: " << sp.ToString();

  if (!sp.has_ssrcs()) {
    // This is a StreamParam with unsignaled SSRCs. Store it, so it can be used
    // later when we know the SSRCs on the first packet arrival.
    unsignaled_stream_params_ = sp;
    return true;
  }

  if (!ValidateStreamParams(sp)) {
    return false;
  }

  const uint32_t ssrc = sp.first_ssrc();

  // If this stream was previously received unsignaled, we promote it, possibly
  // updating the sync group if stream ids have changed.
  if (MaybeDeregisterUnsignaledRecvStream(ssrc)) {
    auto stream_ids = sp.stream_ids();
    std::string sync_group = stream_ids.empty() ? std::string() : stream_ids[0];
    call_->OnUpdateSyncGroup(recv_streams_[ssrc]->stream(),
                             std::move(sync_group));
    return true;
  }

  if (recv_streams_.find(ssrc) != recv_streams_.end()) {
    RTC_LOG(LS_ERROR) << "Stream already exists with ssrc " << ssrc;
    return false;
  }

  // Create a new channel for receiving audio data.
  auto config = BuildReceiveStreamConfig(
      ssrc, receiver_reports_ssrc_, recv_nack_enabled_, enable_non_sender_rtt_,
      recv_rtcp_mode_, sp.stream_ids(), recv_rtp_extensions_, transport(),
      engine()->decoder_factory_, decoder_map_, codec_pair_id_,
      engine()->audio_jitter_buffer_max_packets_,
      engine()->audio_jitter_buffer_fast_accelerate_,
      engine()->audio_jitter_buffer_min_delay_ms_,
      // RingRTC change to configure the jitter buffer's max target delay.
      audio_config_.jitter_buffer_max_target_delay_ms,
      // RingRTC change to configure the RTCP report interval.
      audio_config_.rtcp_report_interval_ms, unsignaled_frame_decryptor_,
      crypto_options_, unsignaled_frame_transformer_);

  recv_streams_.insert(std::make_pair(
      ssrc, new WebRtcAudioReceiveStream(std::move(config), call_)));
  recv_streams_[ssrc]->SetPlayout(playout_);

  return true;
}

bool WebRtcVoiceReceiveChannel::RemoveRecvStream(uint32_t ssrc) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::RemoveRecvStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "RemoveRecvStream: " << ssrc;

  const auto it = recv_streams_.find(ssrc);
  if (it == recv_streams_.end()) {
    RTC_LOG(LS_WARNING) << "Try to remove stream with ssrc " << ssrc
                        << " which doesn't exist.";
    return false;
  }

  MaybeDeregisterUnsignaledRecvStream(ssrc);

  it->second->SetRawAudioSink(nullptr);
  delete it->second;
  recv_streams_.erase(it);
  return true;
}

void WebRtcVoiceReceiveChannel::ResetUnsignaledRecvStream() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "ResetUnsignaledRecvStream.";
  unsignaled_stream_params_ = StreamParams();
  // Create a copy since RemoveRecvStream will modify `unsignaled_recv_ssrcs_`.
  std::vector<uint32_t> to_remove = unsignaled_recv_ssrcs_;
  for (uint32_t ssrc : to_remove) {
    RemoveRecvStream(ssrc);
  }
}

std::optional<uint32_t> WebRtcVoiceReceiveChannel::GetUnsignaledSsrc() const {
  if (unsignaled_recv_ssrcs_.empty()) {
    return std::nullopt;
  }
  // In the event of multiple unsignaled ssrcs, the last in the vector will be
  // the most recent one (the one forwarded to the MediaStreamTrack).
  return unsignaled_recv_ssrcs_.back();
}

void WebRtcVoiceReceiveChannel::ChooseReceiverReportSsrc(
    const std::set<uint32_t>& choices) {
  // Don't change SSRC if set is empty. Note that this differs from
  // the behavior of video.
  if (choices.empty()) {
    return;
  }
  if (choices.find(receiver_reports_ssrc_) != choices.end()) {
    return;
  }
  uint32_t ssrc = *(choices.begin());
  receiver_reports_ssrc_ = ssrc;
  for (auto& kv : recv_streams_) {
    call_->OnLocalSsrcUpdated(kv.second->stream(), ssrc);
  }
}

// Not implemented.
// TODO(https://crbug.com/webrtc/12676): Implement a fix for the unsignalled
// SSRC race that can happen when an m= section goes from receiving to not
// receiving.
void WebRtcVoiceReceiveChannel::OnDemuxerCriteriaUpdatePending() {}
void WebRtcVoiceReceiveChannel::OnDemuxerCriteriaUpdateComplete() {}

bool WebRtcVoiceReceiveChannel::SetOutputVolume(uint32_t ssrc, double volume) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << webrtc::StringFormat(
      "WRVMC::%s({ssrc=%u}, {volume=%.2f})", __func__, ssrc, volume);
  const auto it = recv_streams_.find(ssrc);
  if (it == recv_streams_.end()) {
    // RingRTC change to reduce log noise.
    RTC_LOG(LS_INFO) << webrtc::StringFormat(
        "WRVMC::%s => (WARNING: no receive stream for SSRC %u)", __func__,
        ssrc);
    return false;
  }
  it->second->SetOutputVolume(volume);
  RTC_LOG(LS_INFO) << webrtc::StringFormat(
      "WRVMC::%s => (stream with SSRC %u now uses volume %.2f)", __func__, ssrc,
      volume);
  return true;
}

bool WebRtcVoiceReceiveChannel::SetDefaultOutputVolume(double volume) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  default_recv_volume_ = volume;
  for (uint32_t ssrc : unsignaled_recv_ssrcs_) {
    const auto it = recv_streams_.find(ssrc);
    if (it == recv_streams_.end()) {
      RTC_LOG(LS_WARNING) << "SetDefaultOutputVolume: no recv stream " << ssrc;
      return false;
    }
    it->second->SetOutputVolume(volume);
    RTC_LOG(LS_INFO) << "SetDefaultOutputVolume() to " << volume
                     << " for recv stream with ssrc " << ssrc;
  }
  return true;
}

bool WebRtcVoiceReceiveChannel::SetBaseMinimumPlayoutDelayMs(uint32_t ssrc,
                                                             int delay_ms) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  std::vector<uint32_t> ssrcs(1, ssrc);
  // SSRC of 0 represents the default receive stream.
  if (ssrc == 0) {
    default_recv_base_minimum_delay_ms_ = delay_ms;
    ssrcs = unsignaled_recv_ssrcs_;
  }
  for (uint32_t recv_ssrc : ssrcs) {
    const auto it = recv_streams_.find(recv_ssrc);
    if (it == recv_streams_.end()) {
      RTC_LOG(LS_WARNING) << "SetBaseMinimumPlayoutDelayMs: no recv stream "
                          << recv_ssrc;
      return false;
    }
    it->second->SetBaseMinimumPlayoutDelayMs(delay_ms);
    RTC_LOG(LS_INFO) << "SetBaseMinimumPlayoutDelayMs() to " << delay_ms
                     << " for recv stream with ssrc " << recv_ssrc;
  }
  return true;
}

std::optional<int> WebRtcVoiceReceiveChannel::GetBaseMinimumPlayoutDelayMs(
    uint32_t ssrc) const {
  // SSRC of 0 represents the default receive stream.
  if (ssrc == 0) {
    return default_recv_base_minimum_delay_ms_;
  }

  const auto it = recv_streams_.find(ssrc);

  if (it != recv_streams_.end()) {
    return it->second->GetBaseMinimumPlayoutDelayMs();
  }
  return std::nullopt;
}

void WebRtcVoiceReceiveChannel::SetFrameDecryptor(
    uint32_t ssrc,
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = recv_streams_.find(ssrc);
  if (matching_stream != recv_streams_.end()) {
    matching_stream->second->SetFrameDecryptor(frame_decryptor);
  }
  // Handle unsignaled frame decryptors.
  if (ssrc == 0) {
    unsignaled_frame_decryptor_ = frame_decryptor;
  }
}

void WebRtcVoiceReceiveChannel::OnPacketReceived(
    const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);

  // TODO(bugs.webrtc.org/11993): This code is very similar to what
  // WebRtcVideoChannel::OnPacketReceived does. For maintainability and
  // consistency it would be good to move the interaction with
  // call_->Receiver() to a common implementation and provide a callback on
  // the worker thread for the exception case (DELIVERY_UNKNOWN_SSRC) and
  // how retry is attempted.
  worker_thread_->PostTask(
      SafeTask(task_safety_.flag(), [this, packet = packet]() mutable {
        RTC_DCHECK_RUN_ON(worker_thread_);

        // TODO(bugs.webrtc.org/7135): extensions in `packet` is currently set
        // in RtpTransport and does not necessarily include extensions specific
        // to this channel/MID. Also see comment in
        // BaseChannel::MaybeUpdateDemuxerAndRtpExtensions_w.
        // It would likely be good if extensions where merged per BUNDLE and
        // applied directly in RtpTransport::DemuxPacket;
        packet.IdentifyExtensions(recv_rtp_extension_map_);
        if (!packet.arrival_time().IsFinite()) {
          packet.set_arrival_time(Timestamp::Micros(webrtc::TimeMicros()));
        }

        call_->Receiver()->DeliverRtpPacket(
            MediaType::AUDIO, std::move(packet),
            absl::bind_front(
                &WebRtcVoiceReceiveChannel::MaybeCreateDefaultReceiveStream,
                this));
      }));
}

bool WebRtcVoiceReceiveChannel::MaybeCreateDefaultReceiveStream(
    const RtpPacketReceived& packet) {
  // Create an unsignaled receive stream for this previously not received
  // ssrc. If there already is N unsignaled receive streams, delete the
  // oldest. See: https://bugs.chromium.org/p/webrtc/issues/detail?id=5208
  uint32_t ssrc = packet.Ssrc();
  RTC_DCHECK(!absl::c_linear_search(unsignaled_recv_ssrcs_, ssrc));

  // Add new stream.
  StreamParams sp = unsignaled_stream_params_;
  sp.ssrcs.push_back(ssrc);
  RTC_LOG(LS_INFO) << "Creating unsignaled receive stream for SSRC=" << ssrc;
  if (!AddRecvStream(sp)) {
    RTC_LOG(LS_WARNING) << "Could not create unsignaled receive stream.";
    return false;
  }
  unsignaled_recv_ssrcs_.push_back(ssrc);
  RTC_HISTOGRAM_COUNTS_LINEAR("WebRTC.Audio.NumOfUnsignaledStreams",
                              unsignaled_recv_ssrcs_.size(), 1, 100, 101);

  // Remove oldest unsignaled stream, if we have too many.
  if (unsignaled_recv_ssrcs_.size() > kMaxUnsignaledRecvStreams) {
    uint32_t remove_ssrc = unsignaled_recv_ssrcs_.front();
    RTC_DLOG(LS_INFO) << "Removing unsignaled receive stream with SSRC="
                      << remove_ssrc;
    RemoveRecvStream(remove_ssrc);
  }
  RTC_DCHECK_GE(kMaxUnsignaledRecvStreams, unsignaled_recv_ssrcs_.size());

  SetOutputVolume(ssrc, default_recv_volume_);
  SetBaseMinimumPlayoutDelayMs(ssrc, default_recv_base_minimum_delay_ms_);

  // The default sink can only be attached to one stream at a time, so we hook
  // it up to the *latest* unsignaled stream we've seen, in order to support
  // the case where the SSRC of one unsignaled stream changes.
  if (default_sink_) {
    for (uint32_t drop_ssrc : unsignaled_recv_ssrcs_) {
      auto it = recv_streams_.find(drop_ssrc);
      it->second->SetRawAudioSink(nullptr);
    }
    std::unique_ptr<AudioSinkInterface> proxy_sink(
        new ProxySink(default_sink_.get()));
    SetRawAudioSink(ssrc, std::move(proxy_sink));
  }
  return true;
}

bool WebRtcVoiceReceiveChannel::GetStats(VoiceMediaReceiveInfo* info,
                                         bool get_and_clear_legacy_stats) {
  TRACE_EVENT0("webrtc", "WebRtcVoiceMediaChannel::GetReceiveStats");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(info);

  // Get SSRC and stats for each receiver.
  RTC_DCHECK_EQ(info->receivers.size(), 0U);
  for (const auto& stream : recv_streams_) {
    uint32_t ssrc = stream.first;
    // When SSRCs are unsignaled, there's only one audio MediaStreamTrack, but
    // multiple RTP streams can be received over time (if the SSRC changes for
    // whatever reason). We only want the RTCMediaStreamTrackStats to represent
    // the stats for the most recent stream (the one whose audio is actually
    // routed to the MediaStreamTrack), so here we ignore any unsignaled SSRCs
    // except for the most recent one (last in the vector). This is somewhat of
    // a hack, and means you don't get *any* stats for these inactive streams,
    // but it's slightly better than the previous behavior, which was "highest
    // SSRC wins".
    // See: https://bugs.chromium.org/p/webrtc/issues/detail?id=8158
    if (!unsignaled_recv_ssrcs_.empty()) {
      auto end_it = --unsignaled_recv_ssrcs_.end();
      if (absl::linear_search(unsignaled_recv_ssrcs_.begin(), end_it, ssrc)) {
        continue;
      }
    }
    AudioReceiveStreamInterface::Stats stats =
        stream.second->GetStats(get_and_clear_legacy_stats);
    VoiceReceiverInfo rinfo;
    rinfo.add_ssrc(stats.remote_ssrc);
    rinfo.payload_bytes_received = stats.payload_bytes_received;
    rinfo.header_and_padding_bytes_received =
        stats.header_and_padding_bytes_received;
    rinfo.packets_received = stats.packets_received;
    rinfo.fec_packets_received = stats.fec_packets_received;
    rinfo.fec_packets_discarded = stats.fec_packets_discarded;
    rinfo.packets_lost = stats.packets_lost;
    rinfo.packets_discarded = stats.packets_discarded;
    rinfo.codec_name = stats.codec_name;
    rinfo.codec_payload_type = stats.codec_payload_type;
    rinfo.jitter_ms = stats.jitter_ms;
    rinfo.jitter_buffer_ms = stats.jitter_buffer_ms;
    rinfo.jitter_buffer_preferred_ms = stats.jitter_buffer_preferred_ms;
    rinfo.delay_estimate_ms = stats.delay_estimate_ms;
    rinfo.audio_level = stats.audio_level;
    rinfo.total_output_energy = stats.total_output_energy;
    rinfo.total_samples_received = stats.total_samples_received;
    rinfo.total_output_duration = stats.total_output_duration;
    rinfo.concealed_samples = stats.concealed_samples;
    rinfo.silent_concealed_samples = stats.silent_concealed_samples;
    rinfo.concealment_events = stats.concealment_events;
    rinfo.jitter_buffer_delay_seconds = stats.jitter_buffer_delay_seconds;
    rinfo.jitter_buffer_emitted_count = stats.jitter_buffer_emitted_count;
    rinfo.jitter_buffer_target_delay_seconds =
        stats.jitter_buffer_target_delay_seconds;
    rinfo.jitter_buffer_minimum_delay_seconds =
        stats.jitter_buffer_minimum_delay_seconds;
    rinfo.inserted_samples_for_deceleration =
        stats.inserted_samples_for_deceleration;
    rinfo.removed_samples_for_acceleration =
        stats.removed_samples_for_acceleration;
    rinfo.expand_rate = stats.expand_rate;
    rinfo.speech_expand_rate = stats.speech_expand_rate;
    rinfo.secondary_decoded_rate = stats.secondary_decoded_rate;
    rinfo.secondary_discarded_rate = stats.secondary_discarded_rate;
    rinfo.accelerate_rate = stats.accelerate_rate;
    rinfo.preemptive_expand_rate = stats.preemptive_expand_rate;
    rinfo.delayed_packet_outage_samples = stats.delayed_packet_outage_samples;
    rinfo.decoding_calls_to_silence_generator =
        stats.decoding_calls_to_silence_generator;
    rinfo.decoding_calls_to_neteq = stats.decoding_calls_to_neteq;
    rinfo.decoding_normal = stats.decoding_normal;
    rinfo.decoding_plc = stats.decoding_plc;
    rinfo.decoding_codec_plc = stats.decoding_codec_plc;
    rinfo.decoding_cng = stats.decoding_cng;
    rinfo.decoding_plc_cng = stats.decoding_plc_cng;
    rinfo.decoding_muted_output = stats.decoding_muted_output;
    rinfo.capture_start_ntp_time_ms = stats.capture_start_ntp_time_ms;
    rinfo.last_packet_received = stats.last_packet_received;
    rinfo.estimated_playout_ntp_timestamp_ms =
        stats.estimated_playout_ntp_timestamp_ms;
    rinfo.jitter_buffer_flushes = stats.jitter_buffer_flushes;
    rinfo.relative_packet_arrival_delay_seconds =
        stats.relative_packet_arrival_delay_seconds;
    rinfo.interruption_count = stats.interruption_count;
    rinfo.total_interruption_duration_ms = stats.total_interruption_duration_ms;
    rinfo.last_sender_report_timestamp = stats.last_sender_report_timestamp;
    rinfo.last_sender_report_utc_timestamp =
        stats.last_sender_report_utc_timestamp;
    rinfo.last_sender_report_remote_utc_timestamp =
        stats.last_sender_report_remote_utc_timestamp;
    rinfo.sender_reports_packets_sent = stats.sender_reports_packets_sent;
    rinfo.sender_reports_bytes_sent = stats.sender_reports_bytes_sent;
    rinfo.sender_reports_reports_count = stats.sender_reports_reports_count;
    rinfo.round_trip_time = stats.round_trip_time;
    rinfo.round_trip_time_measurements = stats.round_trip_time_measurements;
    rinfo.total_round_trip_time = stats.total_round_trip_time;
    rinfo.total_processing_delay_seconds = stats.total_processing_delay_seconds;
    if (recv_nack_enabled_) {
      rinfo.nacks_sent = stats.nacks_sent;
    }

    info->receivers.push_back(rinfo);
  }

  FillReceiveCodecStats(info);

  info->device_underrun_count = engine_->adm()->GetPlayoutUnderrunCount();

  return true;
}

void WebRtcVoiceReceiveChannel::FillReceiveCodecStats(
    VoiceMediaReceiveInfo* voice_media_info) {
  for (const auto& receiver : voice_media_info->receivers) {
    auto codec =
        absl::c_find_if(recv_codecs_, [&receiver](const webrtc::Codec& c) {
          return receiver.codec_payload_type &&
                 *receiver.codec_payload_type == c.id;
        });
    if (codec != recv_codecs_.end()) {
      voice_media_info->receive_codecs.insert(
          std::make_pair(codec->id, codec->ToCodecParameters()));
    }
  }
}

void WebRtcVoiceReceiveChannel::SetRawAudioSink(
    uint32_t ssrc,
    std::unique_ptr<AudioSinkInterface> sink) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_VERBOSE) << "WebRtcVoiceMediaChannel::SetRawAudioSink: ssrc:"
                      << ssrc << " " << (sink ? "(ptr)" : "NULL");
  const auto it = recv_streams_.find(ssrc);
  if (it == recv_streams_.end()) {
    RTC_LOG(LS_WARNING) << "SetRawAudioSink: no recv stream " << ssrc;
    return;
  }
  it->second->SetRawAudioSink(std::move(sink));
}

void WebRtcVoiceReceiveChannel::SetDefaultRawAudioSink(
    std::unique_ptr<AudioSinkInterface> sink) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_VERBOSE) << "WebRtcVoiceMediaChannel::SetDefaultRawAudioSink:";
  if (!unsignaled_recv_ssrcs_.empty()) {
    std::unique_ptr<AudioSinkInterface> proxy_sink(
        sink ? new ProxySink(sink.get()) : nullptr);
    SetRawAudioSink(unsignaled_recv_ssrcs_.back(), std::move(proxy_sink));
  }
  default_sink_ = std::move(sink);
}

std::vector<RtpSource> WebRtcVoiceReceiveChannel::GetSources(
    uint32_t ssrc) const {
  auto it = recv_streams_.find(ssrc);
  if (it == recv_streams_.end()) {
    RTC_LOG(LS_ERROR) << "Attempting to get contributing sources for SSRC:"
                      << ssrc << " which doesn't exist.";
    return std::vector<RtpSource>();
  }
  return it->second->GetSources();
}

void WebRtcVoiceReceiveChannel::SetDepacketizerToDecoderFrameTransformer(
    uint32_t ssrc,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (ssrc == 0) {
    // If the receiver is unsignaled, save the frame transformer and set it when
    // the stream is associated with an ssrc.
    unsignaled_frame_transformer_ = std::move(frame_transformer);
    return;
  }

  auto matching_stream = recv_streams_.find(ssrc);
  if (matching_stream == recv_streams_.end()) {
    RTC_LOG(LS_INFO) << "Attempting to set frame transformer for SSRC:" << ssrc
                     << " which doesn't exist.";
    return;
  }
  matching_stream->second->SetDepacketizerToDecoderFrameTransformer(
      std::move(frame_transformer));
}

bool WebRtcVoiceReceiveChannel::MaybeDeregisterUnsignaledRecvStream(
    uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto it = absl::c_find(unsignaled_recv_ssrcs_, ssrc);
  if (it != unsignaled_recv_ssrcs_.end()) {
    unsignaled_recv_ssrcs_.erase(it);
    return true;
  }
  return false;
}

// RingRTC change to get audio levels
std::optional<ReceivedAudioLevel> WebRtcVoiceReceiveChannel::GetReceivedAudioLevel() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (recv_streams_.empty()) {
    RTC_LOG(LS_WARNING)
        << "Attempting to GetReceivedAudioLevel for channel with no receiving streams."
        << " mid_=" << mid_;
    return std::nullopt;
  }

  auto kv = recv_streams_.begin();
  return ReceivedAudioLevel {
      kv->first,
      kv->second->GetAudioLevel()
  };
}

}  // namespace webrtc
