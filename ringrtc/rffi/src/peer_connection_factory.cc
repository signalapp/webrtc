/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "pc/peer_connection_factory.h"

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/create_audio_device_module.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "media/engine/simulcast_encoder_adapter.h"
#include "modules/audio_device/dummy/file_audio_device_factory.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "rffi/api/injectable_network.h"
#include "rffi/api/media.h"
#include "rffi/api/peer_connection_factory.h"
#include "rffi/api/peer_connection_observer_intf.h"
#include "rffi/src/audio_device.h"
#include "rffi/src/peer_connection_observer.h"
#include "rffi/src/ptr.h"
#include "rtc_base/log_sinks.h"
#include "rtc_base/logging.h"
#include "rtc_base/message_digest.h"

namespace webrtc {
namespace rffi {

#if !defined(WEBRTC_IOS) && !defined(WEBRTC_ANDROID)
// This class adds simulcast support to the base factory and is modeled using
// the same business logic found in BuiltinVideoEncoderFactory and
// InternalEncoderFactory.
class RingRTCVideoEncoderFactory : public VideoEncoderFactory {
 public:
  std::vector<SdpVideoFormat> GetSupportedFormats() const override {
    return factory_.GetSupportedFormats();
  }

  std::unique_ptr<VideoEncoder> Create(const Environment& env,
                                       const SdpVideoFormat& format) override {
    if (format.IsCodecInList(factory_.GetSupportedFormats())) {
      if (std::optional<SdpVideoFormat> original_format =
              FuzzyMatchSdpVideoFormat(factory_.GetSupportedFormats(),
                                       format)) {
        // Create a simulcast enabled encoder
        // The adapter has a passthrough mode for the case that simulcast is not
        // used, so all responsibility can be delegated to it.
        return std::make_unique<SimulcastEncoderAdapter>(
            env, &factory_, nullptr, *original_format);
      }
    }
    return nullptr;
  }

  CodecSupport QueryCodecSupport(
      const SdpVideoFormat& format,
      std::optional<std::string> scalability_mode) const override {
    auto original_format =
        FuzzyMatchSdpVideoFormat(factory_.GetSupportedFormats(), format);
    return original_format
               ? factory_.QueryCodecSupport(*original_format, scalability_mode)
               : VideoEncoderFactory::CodecSupport{.is_supported = false};
  }

 private:
  VideoEncoderFactoryTemplate<LibvpxVp8EncoderTemplateAdapter,
                              LibvpxVp9EncoderTemplateAdapter>
      factory_;
};

// Cleanup class for ADM. Will drop a reference on the rust side, potentially
// invoking the destructor.
class AudioDeviceModuleCleanup {
 public:
  AudioDeviceModuleCleanup(void (*free_adm_cb)(const void*), void* rust_adm)
      : free_adm_cb_(free_adm_cb), adm_(rust_adm) {}

  ~AudioDeviceModuleCleanup() { free_adm_cb_(adm_); }

 private:
  void (*free_adm_cb_)(const void*);
  void* adm_;
};

class PeerConnectionFactoryWithOwnedThreads
    : public PeerConnectionFactoryOwner {
 public:
  static scoped_refptr<PeerConnectionFactoryWithOwnedThreads> Create(
      const RffiAudioConfig* audio_config_borrowed,
      bool use_injectable_network) {
    // Creating a PeerConnectionFactory is a little complex.  To make sure we're
    // doing it right, we read several examples: Android SDK:
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/sdk/android/src/jni/pc/peer_connection_factory.cc
    // iOS SDK:
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/sdk/objc/api/peerconnection/RTCPeerConnectionFactory.mm
    // Chromium:
    //  https://cs.chromium.org/chromium/src/third_party/blink/renderer/modules/peerconnection/peer_connection_dependency_factory.cc
    // Default:
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/api/create_peerconnection_factory.cc?q=CreateModularPeerConnectionFactory%5C(&dr=C&l=40
    // Others:
    //  https://cs.chromium.org/chromium/src/remoting/protocol/webrtc_transport.cc?l=246
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/examples/peerconnection/client/conductor.cc?q=CreatePeerConnectionFactory%5C(&l=133&dr=C
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/examples/unityplugin/simple_peer_connection.cc?q=CreatePeerConnectionFactory%5C(&dr=C&l=131
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/examples/objcnativeapi/objc/objc_call_client.mm?q=CreateModularPeerConnectionFactory%5C(&dr=C&l=104
    //  https://cs.chromium.org/chromium/src/third_party/webrtc/examples/androidnativeapi/jni/android_call_client.cc?q=CreatePeerConnectionFactory%5C(&dr=C&l=141

    auto network_thread = CreateAndStartNetworkThread("Network-Thread");
    auto worker_thread = CreateAndStartNonNetworkThread("Worker-Thread");
    auto signaling_thread = CreateAndStartNonNetworkThread("Signaling-Thread");
    auto env = CreateEnvironment();
    std::unique_ptr<InjectableNetwork> injectable_network;
    if (use_injectable_network) {
      injectable_network = CreateInjectableNetwork(env, network_thread.get());
    }

    PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = network_thread.get();
    dependencies.worker_thread = worker_thread.get();
    dependencies.signaling_thread = signaling_thread.get();
    dependencies.event_log_factory = std::make_unique<RtcEventLogFactory>();
    dependencies.env = env;

    // The audio device module must be created (and destroyed) on the _worker_
    // thread. It is safe to release the reference on this thread, however,
    // because the PeerConnectionFactory keeps its own reference.
    auto adm =
        worker_thread->BlockingCall([&]() -> scoped_refptr<AudioDeviceModule> {
          switch (audio_config_borrowed->audio_device_module_type) {
            case kRffiAudioDeviceModuleFile:
              FileAudioDeviceFactory::SetFilenamesToUse(
                  audio_config_borrowed->input_file_borrowed,
                  audio_config_borrowed->output_file_borrowed);
              return CreateAudioDeviceModule(env,
                                             AudioDeviceModule::kDummyAudio);
            case kRffiAudioDeviceModuleRingRtc:
              return RingRTCAudioDeviceModule::Create(
                  audio_config_borrowed->rust_adm_borrowed,
                  audio_config_borrowed->rust_audio_device_callbacks);
          }
        });

    dependencies.adm = adm;
    dependencies.audio_encoder_factory = CreateBuiltinAudioEncoderFactory();
    dependencies.audio_decoder_factory = CreateBuiltinAudioDecoderFactory();

    AudioProcessing::Config config;
    config.high_pass_filter.enabled =
        audio_config_borrowed->high_pass_filter_enabled;
    config.echo_canceller.enabled = audio_config_borrowed->aec_enabled;
    config.noise_suppression.enabled = audio_config_borrowed->ns_enabled;
    config.gain_controller1.enabled = audio_config_borrowed->agc_enabled;

    auto builder = std::make_unique<BuiltinAudioProcessingBuilder>();
    builder->SetConfig(config);
    dependencies.audio_processing_builder = std::move(builder);
    dependencies.audio_mixer = AudioMixerImpl::Create();

    dependencies.video_encoder_factory =
        std::make_unique<RingRTCVideoEncoderFactory>();
    dependencies
        .video_decoder_factory = std::make_unique<VideoDecoderFactoryTemplate<
        LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter>>();

    EnableMedia(dependencies);

    auto factory = CreateModularPeerConnectionFactory(std::move(dependencies));
    return make_ref_counted<PeerConnectionFactoryWithOwnedThreads>(
        std::move(factory), std::move(network_thread), std::move(worker_thread),
        std::move(signaling_thread), std::move(injectable_network), adm.get(),
        audio_config_borrowed->free_adm_cb,
        audio_config_borrowed->rust_adm_borrowed);
  }

  ~PeerConnectionFactoryWithOwnedThreads() override {
    RTC_LOG(LS_INFO) << "~PeerConnectionFactoryWithOwnedThreads()";
  }

  PeerConnectionFactoryInterface* peer_connection_factory() override {
    return factory_.get();
  }

  rffi::InjectableNetwork* injectable_network() override {
    return injectable_network_.get();
  }

  int16_t AudioPlayoutDevices() override {
    return owned_worker_thread_->BlockingCall(
        [&]() { return audio_device_module_->PlayoutDevices(); });
  }

  int32_t AudioPlayoutDeviceName(uint16_t index,
                                 char* name_out,
                                 char* uuid_out) override {
    return owned_worker_thread_->BlockingCall([&]() {
      return audio_device_module_->PlayoutDeviceName(index, name_out, uuid_out);
    });
  }

  bool SetAudioPlayoutDevice(uint16_t index) override {
    return owned_worker_thread_->BlockingCall([&]() {
      // We need to stop and restart playout if it's already in progress.
      bool was_initialized = audio_device_module_->PlayoutIsInitialized();
      bool was_playing = audio_device_module_->Playing();
      if (was_initialized) {
        if (audio_device_module_->StopPlayout() != 0) {
          return false;
        }
      }
      if (audio_device_module_->SetPlayoutDevice(index) != 0) {
        return false;
      }
      if (was_initialized) {
        if (audio_device_module_->InitPlayout() != 0) {
          return false;
        }
      }
      if (was_playing) {
        if (audio_device_module_->StartPlayout() != 0) {
          return false;
        }
      }
      return true;
    });
  }

  int16_t AudioRecordingDevices() override {
    return owned_worker_thread_->BlockingCall(
        [&]() { return audio_device_module_->RecordingDevices(); });
  }

  int32_t AudioRecordingDeviceName(uint16_t index,
                                   char* name_out,
                                   char* uuid_out) override {
    return owned_worker_thread_->BlockingCall([&]() {
      return audio_device_module_->RecordingDeviceName(index, name_out,
                                                       uuid_out);
    });
  }

  bool SetAudioRecordingDevice(uint16_t index) override {
    return owned_worker_thread_->BlockingCall([&]() {
      // We need to stop and restart recording if it is already in progress.
      bool was_initialized = audio_device_module_->RecordingIsInitialized();
      bool was_recording = audio_device_module_->Recording();
      if (was_initialized) {
        if (audio_device_module_->StopRecording() != 0) {
          return false;
        }
      }
      if (audio_device_module_->SetRecordingDevice(index) != 0) {
        return false;
      }
      if (was_initialized) {
        if (audio_device_module_->InitRecording() != 0) {
          return false;
        }
      }
      if (was_recording) {
        if (audio_device_module_->StartRecording() != 0) {
          return false;
        }
      }
      return true;
    });
  }

 protected:
  PeerConnectionFactoryWithOwnedThreads(
      scoped_refptr<PeerConnectionFactoryInterface> factory,
      std::unique_ptr<Thread> owned_network_thread,
      std::unique_ptr<Thread> owned_worker_thread,
      std::unique_ptr<Thread> owned_signaling_thread,
      std::unique_ptr<rffi::InjectableNetwork> injectable_network,
      AudioDeviceModule* audio_device_module,
      void (*free_adm_cb)(const void*),
      void* rust_adm_borrowed)
      : adm_cleanup_(free_adm_cb, rust_adm_borrowed),
        owned_network_thread_(std::move(owned_network_thread)),
        owned_worker_thread_(std::move(owned_worker_thread)),
        owned_signaling_thread_(std::move(owned_signaling_thread)),
        injectable_network_(std::move(injectable_network)),
        audio_device_module_(audio_device_module),
        factory_(std::move(factory)) {}

 private:
  static std::unique_ptr<Thread> CreateAndStartNetworkThread(std::string name) {
    std::unique_ptr<Thread> thread = Thread::CreateWithSocketServer();
    thread->SetName(name, nullptr);
    thread->Start();
    return thread;
  }

  static std::unique_ptr<Thread> CreateAndStartNonNetworkThread(
      std::string name) {
    std::unique_ptr<Thread> thread = Thread::Create();
    thread->SetName(name, nullptr);
    thread->Start();
    return thread;
  }

  // This must be destroyed last.
  AudioDeviceModuleCleanup adm_cleanup_;
  const std::unique_ptr<Thread> owned_network_thread_;
  const std::unique_ptr<Thread> owned_worker_thread_;
  const std::unique_ptr<Thread> owned_signaling_thread_;
  std::unique_ptr<rffi::InjectableNetwork> injectable_network_;
  AudioDeviceModule* audio_device_module_;
  const scoped_refptr<PeerConnectionFactoryInterface> factory_;
};
#endif  // !defined(WEBRTC_IOS) && !defined(WEBRTC_ANDROID)

// Returns an owned RC.
RUSTEXPORT PeerConnectionFactoryOwner* Rust_createPeerConnectionFactory(
    const RffiAudioConfig* audio_config_borrowed,
    bool use_injectable_network) {
#if !defined(WEBRTC_IOS) && !defined(WEBRTC_ANDROID)
  auto factory_owner = PeerConnectionFactoryWithOwnedThreads::Create(
      audio_config_borrowed, use_injectable_network);
  return take_rc(std::move(factory_owner));
#else
  return nullptr;
#endif
}

// Returns an owned RC.
RUSTEXPORT PeerConnectionFactoryOwner* Rust_createPeerConnectionFactoryWrapper(
    PeerConnectionFactoryInterface* factory_borrowed_rc) {
  class PeerConnectionFactoryWrapper : public PeerConnectionFactoryOwner {
   public:
    PeerConnectionFactoryInterface* peer_connection_factory() override {
      return factory_.get();
    }

    PeerConnectionFactoryWrapper(
        scoped_refptr<PeerConnectionFactoryInterface> factory)
        : factory_(std::move(factory)) {}

   private:
    const scoped_refptr<PeerConnectionFactoryInterface> factory_;
  };

  return take_rc(make_ref_counted<PeerConnectionFactoryWrapper>(
      inc_rc(factory_borrowed_rc)));
}

// Returns an owned RC.
RUSTEXPORT PeerConnectionInterface* Rust_createPeerConnection(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
    PeerConnectionObserverRffi* observer_borrowed,
    RffiPeerConnectionKind kind,
    const RffiAudioJitterBufferConfig* audio_jitter_buffer_config_borrowed,
    int32_t audio_rtcp_report_interval_ms,
    const RffiIceServers* ice_servers_borrowed,
    AudioTrackInterface* outgoing_audio_track_borrowed_rc,
    VideoTrackInterface* outgoing_video_track_borrowed_rc) {
  auto factory = factory_owner_borrowed_rc->peer_connection_factory();

  PeerConnectionInterface::RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  config.tcp_candidate_policy =
      PeerConnectionInterface::kTcpCandidatePolicyDisabled;
  if (kind == RffiPeerConnectionKind::kRelayed) {
    config.type = PeerConnectionInterface::kRelay;
  } else if (kind == RffiPeerConnectionKind::kGroupCall) {
    config.tcp_candidate_policy =
        PeerConnectionInterface::kTcpCandidatePolicyEnabled;
  }
  config.audio_jitter_buffer_max_packets =
      audio_jitter_buffer_config_borrowed->max_packets;
  config.audio_jitter_buffer_fast_accelerate =
      audio_jitter_buffer_config_borrowed->fast_accelerate;
  config.audio_jitter_buffer_min_delay_ms =
      audio_jitter_buffer_config_borrowed->min_delay_ms;
  config.set_audio_jitter_buffer_max_target_delay_ms(
      audio_jitter_buffer_config_borrowed->max_target_delay_ms);
  config.set_audio_rtcp_report_interval_ms(audio_rtcp_report_interval_ms);
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  for (size_t i = 0; i < ice_servers_borrowed->servers_size; i++) {
    RffiIceServer ice_server = ice_servers_borrowed->servers[i];
    if (ice_server.urls_size > 0) {
      PeerConnectionInterface::IceServer rtc_ice_server;
      rtc_ice_server.username = std::string(ice_server.username_borrowed);
      rtc_ice_server.password = std::string(ice_server.password_borrowed);
      rtc_ice_server.hostname = std::string(ice_server.hostname_borrowed);
      for (size_t j = 0; j < ice_server.urls_size; j++) {
        rtc_ice_server.urls.push_back(std::string(ice_server.urls_borrowed[j]));
      }
      config.servers.push_back(rtc_ice_server);
    }
  }

  config.crypto_options = CryptoOptions{};
  if (observer_borrowed->enable_frame_encryption()) {
    config.crypto_options->sframe.require_frame_encryption = true;
  }
  config.crypto_options->srtp.enable_gcm_crypto_suites = true;
  config.continual_gathering_policy =
      PeerConnectionInterface::ContinualGatheringPolicy::GATHER_CONTINUALLY;

  // PeerConnectionDependencies.observer is copied to PeerConnection.observer_.
  // It must live as long as the PeerConnection.
  PeerConnectionDependencies deps(observer_borrowed);
  if (factory_owner_borrowed_rc->injectable_network()) {
    deps.allocator =
        factory_owner_borrowed_rc->injectable_network()->CreatePortAllocator();
  }
  auto result = factory->CreatePeerConnectionOrError(config, std::move(deps));
  if (!result.ok()) {
    RTC_LOG(LS_INFO) << "Failed to CreatePeerConnection: "
                     << result.error().message();
    return nullptr;
  }
  scoped_refptr<PeerConnectionInterface> pc = result.MoveValue();

  // We use an arbitrary stream_id because existing apps want a MediaStream to
  // pop out.
  auto stream_id = "s";
  std::vector<std::string> stream_ids;
  stream_ids.push_back(stream_id);

  if (outgoing_audio_track_borrowed_rc) {
    if (kind == RffiPeerConnectionKind::kGroupCall) {
      RtpTransceiverInit init;
      init.direction = RtpTransceiverDirection::kSendOnly;
      init.stream_ids = stream_ids;

      auto add_transceiver_result =
          pc->AddTransceiver(inc_rc(outgoing_audio_track_borrowed_rc), init);
      if (add_transceiver_result.ok()) {
        if (observer_borrowed->enable_frame_encryption()) {
          auto rtp_sender = add_transceiver_result.MoveValue()->sender();
          rtp_sender->SetFrameEncryptor(observer_borrowed->CreateEncryptor());
        }
      } else {
        RTC_LOG(LS_ERROR) << "Failed to PeerConnection::AddTransceiver(audio)";
      }

    } else {
      auto add_track_result =
          pc->AddTrack(inc_rc(outgoing_audio_track_borrowed_rc), stream_ids);
      if (add_track_result.ok()) {
        if (observer_borrowed->enable_frame_encryption()) {
          auto rtp_sender = add_track_result.MoveValue();
          rtp_sender->SetFrameEncryptor(observer_borrowed->CreateEncryptor());
        }
      } else {
        RTC_LOG(LS_ERROR) << "Failed to PeerConnection::AddTrack(audio)";
      }
    }
  }

  if (outgoing_video_track_borrowed_rc) {
    std::vector<RtpEncodingParameters> rtp_parameters = {{}};
    if (kind == RffiPeerConnectionKind::kGroupCall) {
      rtp_parameters[0].max_bitrate_bps = 100000;
    }

    if (kind == RffiPeerConnectionKind::kGroupCall) {
      RtpTransceiverInit init;
      init.direction = RtpTransceiverDirection::kSendOnly;
      init.stream_ids = stream_ids;
      init.send_encodings = rtp_parameters;

      auto add_transceiver_result =
          pc->AddTransceiver(inc_rc(outgoing_video_track_borrowed_rc), init);
      if (add_transceiver_result.ok()) {
        if (observer_borrowed->enable_frame_encryption()) {
          auto rtp_sender = add_transceiver_result.MoveValue()->sender();
          rtp_sender->SetFrameEncryptor(observer_borrowed->CreateEncryptor());
        }
      } else {
        RTC_LOG(LS_ERROR) << "Failed to PeerConnection::AddTransceiver(video)";
      }

    } else {
      auto add_track_result = pc->AddTrack(
          inc_rc(outgoing_video_track_borrowed_rc), stream_ids, rtp_parameters);
      if (add_track_result.ok()) {
        if (observer_borrowed->enable_frame_encryption()) {
          auto rtp_sender = add_track_result.MoveValue();
          rtp_sender->SetFrameEncryptor(observer_borrowed->CreateEncryptor());
        }
      } else {
        RTC_LOG(LS_ERROR) << "Failed to PeerConnection::AddTrack(video)";
      }
    }
  }

  return take_rc(pc);
}

// Returns a borrowed pointer.
RUSTEXPORT rffi::InjectableNetwork* Rust_getInjectableNetwork(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc) {
  return factory_owner_borrowed_rc->injectable_network();
}

// Returns an owned RC.
RUSTEXPORT AudioTrackInterface* Rust_createAudioTrack(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc) {
  auto factory = factory_owner_borrowed_rc->peer_connection_factory();

  AudioOptions options;
  auto source = factory->CreateAudioSource(options);
  // Note: This must stay "audio1" to stay in sync with V4 signaling.
  return take_rc(factory->CreateAudioTrack("audio1", source.get()));
}

// Returns an owned RC.
RUSTEXPORT rffi::VideoSource* Rust_createVideoSource() {
  return take_rc(make_ref_counted<rffi::VideoSource>());
}

// Returns an owned RC.
RUSTEXPORT VideoTrackInterface* Rust_createVideoTrack(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
    VideoTrackSourceInterface* source_borrowed_rc) {
  auto factory = factory_owner_borrowed_rc->peer_connection_factory();

  // Note: This must stay "video1" to stay in sync with V4 signaling.
  return take_rc(factory->CreateVideoTrack(
      scoped_refptr<VideoTrackSourceInterface>(source_borrowed_rc), "video1"));
}

RUSTEXPORT int16_t Rust_getAudioPlayoutDevices(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc) {
  return factory_owner_borrowed_rc->AudioPlayoutDevices();
}

RUSTEXPORT int32_t Rust_getAudioPlayoutDeviceName(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
    uint16_t index,
    char* name_out,
    char* uuid_out) {
  return factory_owner_borrowed_rc->AudioPlayoutDeviceName(index, name_out,
                                                           uuid_out);
}

RUSTEXPORT bool Rust_setAudioPlayoutDevice(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
    uint16_t index) {
  return factory_owner_borrowed_rc->SetAudioPlayoutDevice(index);
}

RUSTEXPORT int16_t Rust_getAudioRecordingDevices(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc) {
  return factory_owner_borrowed_rc->AudioRecordingDevices();
}

RUSTEXPORT int32_t Rust_getAudioRecordingDeviceName(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
    uint16_t index,
    char* name_out,
    char* uuid_out) {
  return factory_owner_borrowed_rc->AudioRecordingDeviceName(index, name_out,
                                                             uuid_out);
}

RUSTEXPORT bool Rust_setAudioRecordingDevice(
    PeerConnectionFactoryOwner* factory_owner_borrowed_rc,
    uint16_t index) {
  return factory_owner_borrowed_rc->SetAudioRecordingDevice(index);
}

}  // namespace rffi
}  // namespace webrtc
