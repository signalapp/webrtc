/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/pc/peer_connection_factory.h"

#include <jni.h>
#include <stdio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "api/audio/audio_device.h"
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/enable_media.h"
#include "api/environment/environment.h"
#include "api/fec_controller.h"
#include "api/media_stream_interface.h"
#include "api/neteq/neteq_factory.h"
#include "api/network_state_predictor.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/scoped_refptr.h"
#include "api/transport/network_control.h"
#include "modules/utility/include/jvm_android.h"
#include "rtc_base/checks.h"
#include "rtc_base/event_tracer.h"
#include "rtc_base/logging.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/rtc_certificate_generator.h"
#include "rtc_base/socket_factory.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/thread.h"
#include "sdk/android/generated_peerconnection_jni/PeerConnectionFactory_jni.h"
#include "sdk/android/native_api/jni/java_types.h"
#include "sdk/android/native_api/jni/scoped_java_ref.h"
#include "sdk/android/native_api/stacktrace/stacktrace.h"
#include "sdk/android/src/jni/android_network_monitor.h"
#include "sdk/android/src/jni/jni_helpers.h"
#include "sdk/android/src/jni/jvm.h"
#include "sdk/android/src/jni/logging/log_sink.h"
#include "sdk/android/src/jni/pc/android_network_monitor.h"
#include "sdk/android/src/jni/pc/media_constraints.h"
#include "sdk/android/src/jni/pc/media_stream_track.h"
#include "sdk/android/src/jni/pc/owned_factory_and_threads.h"
#include "sdk/android/src/jni/pc/peer_connection.h"
#include "sdk/android/src/jni/pc/rtp_capabilities.h"
#include "sdk/android/src/jni/pc/ssl_certificate_verifier_wrapper.h"
#include "sdk/android/src/jni/pc/video.h"
#include "sdk/media_constraints.h"
#include "system_wrappers/include/field_trial.h"
#include "third_party/jni_zero/jni_zero.h"

namespace webrtc {
namespace jni {

namespace {

// Take ownership of the jlong reference and cast it into an
// webrtc::scoped_refptr.
template <typename T>
scoped_refptr<T> TakeOwnershipOfRefPtr(jlong j_pointer) {
  T* ptr = reinterpret_cast<T*>(j_pointer);
  scoped_refptr<T> refptr;
  refptr.swap(&ptr);
  return refptr;
}

// Take ownership of the jlong reference and cast it into a std::unique_ptr.
template <typename T>
std::unique_ptr<T> TakeOwnershipOfUniquePtr(jlong native_pointer) {
  return std::unique_ptr<T>(reinterpret_cast<T*>(native_pointer));
}

typedef void (*JavaMethodPointer)(JNIEnv*, const jni_zero::JavaRef<jobject>&);

// Post a message on the given thread that will call the Java method on the
// given Java object.
void PostJavaCallback(JNIEnv* env,
                      Thread* queue,
                      const jni_zero::JavaRef<jobject>& j_object,
                      JavaMethodPointer java_method_pointer) {
  jni_zero::ScopedJavaGlobalRef<jobject> object(env, j_object);
  queue->PostTask([object = std::move(object), java_method_pointer] {
    java_method_pointer(AttachCurrentThreadIfNeeded(), object);
  });
}

std::optional<PeerConnectionFactoryInterface::Options>
JavaToNativePeerConnectionFactoryOptions(
    JNIEnv* jni,
    const jni_zero::JavaRef<jobject>& j_options) {
  if (j_options.is_null())
    return std::nullopt;

  PeerConnectionFactoryInterface::Options native_options;

  // This doesn't necessarily match the c++ version of this struct; feel free
  // to add more parameters as necessary.
  native_options.network_ignore_mask =
      Java_Options_getNetworkIgnoreMask(jni, j_options);
  native_options.disable_encryption =
      Java_Options_getDisableEncryption(jni, j_options);
  native_options.disable_network_monitor =
      Java_Options_getDisableNetworkMonitor(jni, j_options);

  return native_options;
}

// Place static objects into a container that gets leaked so we avoid
// non-trivial destructor.
struct StaticObjectContainer {
  // Field trials initialization string
  std::unique_ptr<std::string> field_trials_init_string;
  // Set in PeerConnectionFactory_InjectLoggable().
  std::unique_ptr<JNILogSink> jni_log_sink;
};

StaticObjectContainer& GetStaticObjects() {
  static StaticObjectContainer* static_objects = new StaticObjectContainer();
  return *static_objects;
}

ScopedJavaLocalRef<jobject> NativeToScopedJavaPeerConnectionFactory(
    JNIEnv* env,
    scoped_refptr<PeerConnectionFactoryInterface> pcf,
    std::unique_ptr<SocketFactory> socket_factory,
    std::unique_ptr<Thread> network_thread,
    std::unique_ptr<Thread> worker_thread,
    std::unique_ptr<Thread> signaling_thread) {
  OwnedFactoryAndThreads* owned_factory = new OwnedFactoryAndThreads(
      std::move(socket_factory), std::move(network_thread),
      std::move(worker_thread), std::move(signaling_thread), pcf);

  jni_zero::ScopedJavaLocalRef<jobject> j_pcf =
      Java_PeerConnectionFactory_Constructor(
          env, NativeToJavaPointer(owned_factory));

  PostJavaCallback(env, owned_factory->network_thread(), j_pcf,
                   &Java_PeerConnectionFactory_onNetworkThreadReady);
  PostJavaCallback(env, owned_factory->worker_thread(), j_pcf,
                   &Java_PeerConnectionFactory_onWorkerThreadReady);
  PostJavaCallback(env, owned_factory->signaling_thread(), j_pcf,
                   &Java_PeerConnectionFactory_onSignalingThreadReady);

  return j_pcf;
}

PeerConnectionFactoryInterface* PeerConnectionFactoryFromJava(jlong j_p) {
  return reinterpret_cast<OwnedFactoryAndThreads*>(j_p)->factory();
}

}  // namespace

// Note: Some of the video-specific PeerConnectionFactory methods are
// implemented in "video.cc". This is done so that if an application
// doesn't need video support, it can just link with "null_video.cc"
// instead of "video.cc", which doesn't bring in the video-specific
// dependencies.

// Set in PeerConnectionFactory_initializeAndroidGlobals().
static bool factory_static_initialized = false;

jobject NativeToJavaPeerConnectionFactory(
    JNIEnv* jni,
    scoped_refptr<PeerConnectionFactoryInterface> pcf,
    std::unique_ptr<SocketFactory> socket_factory,
    std::unique_ptr<Thread> network_thread,
    std::unique_ptr<Thread> worker_thread,
    std::unique_ptr<Thread> signaling_thread) {
  return NativeToScopedJavaPeerConnectionFactory(
             jni, pcf, std::move(socket_factory), std::move(network_thread),
             std::move(worker_thread), std::move(signaling_thread))
      .Release();
}

static void JNI_PeerConnectionFactory_InitializeAndroidGlobals(JNIEnv* jni) {
  if (!factory_static_initialized) {
    JVM::Initialize(GetJVM());
    factory_static_initialized = true;
  }
}

static void JNI_PeerConnectionFactory_InitializeFieldTrials(
    JNIEnv* jni,
    const jni_zero::JavaParamRef<jstring>& j_trials_init_string) {
  std::unique_ptr<std::string>& field_trials_init_string =
      GetStaticObjects().field_trials_init_string;

  if (j_trials_init_string.is_null()) {
    field_trials_init_string = nullptr;
    field_trial::InitFieldTrialsFromString(nullptr);
    return;
  }
  field_trials_init_string = std::make_unique<std::string>(
      JavaToNativeString(jni, j_trials_init_string));
  RTC_LOG(LS_INFO) << "initializeFieldTrials: " << *field_trials_init_string;
  field_trial::InitFieldTrialsFromString(field_trials_init_string->c_str());
}

static void JNI_PeerConnectionFactory_InitializeInternalTracer(JNIEnv* jni) {
  tracing::SetupInternalTracer();
}

static jboolean JNI_PeerConnectionFactory_StartInternalTracingCapture(
    JNIEnv* jni,
    const jni_zero::JavaParamRef<jstring>& j_event_tracing_filename) {
  if (j_event_tracing_filename.is_null())
    return false;

  const char* init_string =
      jni->GetStringUTFChars(j_event_tracing_filename.obj(), NULL);
  RTC_LOG(LS_INFO) << "Starting internal tracing to: " << init_string;
  bool ret = tracing::StartInternalCapture(init_string);
  jni->ReleaseStringUTFChars(j_event_tracing_filename.obj(), init_string);
  return ret;
}

static void JNI_PeerConnectionFactory_StopInternalTracingCapture(JNIEnv* jni) {
  tracing::StopInternalCapture();
}

static void JNI_PeerConnectionFactory_ShutdownInternalTracer(JNIEnv* jni) {
  tracing::ShutdownInternalTracer();
}

// Following parameters are optional:
// `audio_device_module`, `jencoder_factory`, `jdecoder_factory`,
// `audio_processor`, `fec_controller_factory`,
// `network_state_predictor_factory`, `neteq_factory`.
ScopedJavaLocalRef<jobject> CreatePeerConnectionFactoryForJava(
    JNIEnv* jni,
    const jni_zero::JavaParamRef<jobject>& jcontext,
    const jni_zero::JavaParamRef<jobject>& joptions,
    const Environment& env,
    scoped_refptr<AudioDeviceModule> audio_device_module,
    scoped_refptr<AudioEncoderFactory> audio_encoder_factory,
    scoped_refptr<AudioDecoderFactory> audio_decoder_factory,
    const jni_zero::JavaParamRef<jobject>& jencoder_factory,
    const jni_zero::JavaParamRef<jobject>& jdecoder_factory,
    scoped_refptr<AudioProcessing> audio_processor,
    std::unique_ptr<FecControllerFactoryInterface> fec_controller_factory,
    std::unique_ptr<NetworkControllerFactoryInterface>
        network_controller_factory,
    std::unique_ptr<NetworkStatePredictorFactoryInterface>
        network_state_predictor_factory,
    std::unique_ptr<NetEqFactory> neteq_factory) {
  // talk/ assumes pretty widely that the current Thread is ThreadManager'd, but
  // ThreadManager only WrapCurrentThread()s the thread where it is first
  // created.  Since the semantics around when auto-wrapping happens in
  // webrtc/rtc_base/ are convoluted, we simply wrap here to avoid having to
  // think about ramifications of auto-wrapping there.
  ThreadManager::Instance()->WrapCurrentThread();

  auto socket_server = std::make_unique<PhysicalSocketServer>();
  auto network_thread = std::make_unique<Thread>(socket_server.get());
  network_thread->SetName("network_thread", nullptr);
  RTC_CHECK(network_thread->Start()) << "Failed to start thread";

  std::unique_ptr<Thread> worker_thread = Thread::Create();
  worker_thread->SetName("worker_thread", nullptr);
  RTC_CHECK(worker_thread->Start()) << "Failed to start thread";

  std::unique_ptr<Thread> signaling_thread = Thread::Create();
  signaling_thread->SetName("signaling_thread", NULL);
  RTC_CHECK(signaling_thread->Start()) << "Failed to start thread";

  const std::optional<PeerConnectionFactoryInterface::Options> options =
      JavaToNativePeerConnectionFactoryOptions(jni, joptions);

  PeerConnectionFactoryDependencies dependencies;
  dependencies.env = env;
  dependencies.socket_factory = socket_server.get();
  dependencies.network_thread = network_thread.get();
  dependencies.worker_thread = worker_thread.get();
  dependencies.signaling_thread = signaling_thread.get();
  dependencies.event_log_factory = std::make_unique<RtcEventLogFactory>();
  dependencies.fec_controller_factory = std::move(fec_controller_factory);
  dependencies.network_controller_factory =
      std::move(network_controller_factory);
  dependencies.network_state_predictor_factory =
      std::move(network_state_predictor_factory);
  dependencies.neteq_factory = std::move(neteq_factory);
  if (!(options && options->disable_network_monitor)) {
    dependencies.network_monitor_factory =
        std::make_unique<AndroidNetworkMonitorFactory>();
  }

  dependencies.adm = std::move(audio_device_module);
  dependencies.audio_encoder_factory = std::move(audio_encoder_factory);
  dependencies.audio_decoder_factory = std::move(audio_decoder_factory);
  if (audio_processor != nullptr) {
    dependencies.audio_processing_builder =
        CustomAudioProcessing(std::move(audio_processor));
#ifndef WEBRTC_EXCLUDE_AUDIO_PROCESSING_MODULE
  } else {
    dependencies.audio_processing_builder =
        std::make_unique<BuiltinAudioProcessingBuilder>();
#endif
  }
  dependencies.video_encoder_factory =
      absl::WrapUnique(CreateVideoEncoderFactory(jni, jencoder_factory));
  dependencies.video_decoder_factory =
      absl::WrapUnique(CreateVideoDecoderFactory(jni, jdecoder_factory));
  EnableMedia(dependencies);

  scoped_refptr<PeerConnectionFactoryInterface> factory =
      CreateModularPeerConnectionFactory(std::move(dependencies));

  RTC_CHECK(factory) << "Failed to create the peer connection factory; "
                        "WebRTC/libjingle init likely failed on this device";
  // TODO(honghaiz): Maybe put the options as the argument of
  // CreatePeerConnectionFactory.
  if (options)
    factory->SetOptions(*options);

  return NativeToScopedJavaPeerConnectionFactory(
      jni, factory, std::move(socket_server), std::move(network_thread),
      std::move(worker_thread), std::move(signaling_thread));
}

static jni_zero::ScopedJavaLocalRef<jobject>
JNI_PeerConnectionFactory_CreatePeerConnectionFactory(
    JNIEnv* jni,
    const jni_zero::JavaParamRef<jobject>& jcontext,
    const jni_zero::JavaParamRef<jobject>& joptions,
    jlong webrtc_env_ref,
    jlong native_audio_device_module,
    jlong native_audio_encoder_factory,
    jlong native_audio_decoder_factory,
    const jni_zero::JavaParamRef<jobject>& jencoder_factory,
    const jni_zero::JavaParamRef<jobject>& jdecoder_factory,
    jlong native_audio_processor,
    jlong native_fec_controller_factory,
    jlong native_network_controller_factory,
    jlong native_network_state_predictor_factory,
    jlong native_neteq_factory) {
  const Environment* env = reinterpret_cast<Environment*>(webrtc_env_ref);
  RTC_CHECK(env != nullptr);
  scoped_refptr<AudioProcessing> audio_processor(
      reinterpret_cast<AudioProcessing*>(native_audio_processor));
  return CreatePeerConnectionFactoryForJava(
      jni, jcontext, joptions, *env,
      scoped_refptr<AudioDeviceModule>(
          reinterpret_cast<AudioDeviceModule*>(native_audio_device_module)),
      TakeOwnershipOfRefPtr<AudioEncoderFactory>(native_audio_encoder_factory),
      TakeOwnershipOfRefPtr<AudioDecoderFactory>(native_audio_decoder_factory),
      jencoder_factory, jdecoder_factory, std::move(audio_processor),
      TakeOwnershipOfUniquePtr<FecControllerFactoryInterface>(
          native_fec_controller_factory),
      TakeOwnershipOfUniquePtr<NetworkControllerFactoryInterface>(
          native_network_controller_factory),
      TakeOwnershipOfUniquePtr<NetworkStatePredictorFactoryInterface>(
          native_network_state_predictor_factory),
      TakeOwnershipOfUniquePtr<NetEqFactory>(native_neteq_factory));
}

static void JNI_PeerConnectionFactory_FreeFactory(JNIEnv*, jlong j_p) {
  delete reinterpret_cast<OwnedFactoryAndThreads*>(j_p);
  // RingRTC change to allow field trials to be initialized once.
  // field_trial::InitFieldTrialsFromString(nullptr);
  // GetStaticObjects().field_trials_init_string = nullptr;
}

static jlong JNI_PeerConnectionFactory_CreateLocalMediaStream(
    JNIEnv* jni,
    jlong native_factory,
    const jni_zero::JavaParamRef<jstring>& label) {
  scoped_refptr<MediaStreamInterface> stream(
      PeerConnectionFactoryFromJava(native_factory)
          ->CreateLocalMediaStream(JavaToStdString(jni, label)));
  return jlongFromPointer(stream.release());
}

static jlong JNI_PeerConnectionFactory_CreateAudioSource(
    JNIEnv* jni,
    jlong native_factory,
    const jni_zero::JavaParamRef<jobject>& j_constraints) {
  std::unique_ptr<MediaConstraints> constraints =
      JavaToNativeMediaConstraints(jni, j_constraints);
  AudioOptions options;
  CopyConstraintsIntoAudioOptions(constraints.get(), &options);
  scoped_refptr<AudioSourceInterface> source(
      PeerConnectionFactoryFromJava(native_factory)
          ->CreateAudioSource(options));
  return jlongFromPointer(source.release());
}

jlong JNI_PeerConnectionFactory_CreateAudioTrack(
    JNIEnv* jni,
    jlong native_factory,
    const jni_zero::JavaParamRef<jstring>& id,
    jlong native_source) {
  scoped_refptr<AudioTrackInterface> track(
      PeerConnectionFactoryFromJava(native_factory)
          ->CreateAudioTrack(
              JavaToStdString(jni, id),
              reinterpret_cast<AudioSourceInterface*>(native_source)));
  return jlongFromPointer(track.release());
}

ScopedJavaLocalRef<jobject> JNI_PeerConnectionFactory_GetRtpSenderCapabilities(
    JNIEnv* jni,
    jlong native_factory,
    const jni_zero::JavaParamRef<jobject>& media_type) {
  auto factory = PeerConnectionFactoryFromJava(native_factory);
  return NativeToJavaRtpCapabilities(
      jni, factory->GetRtpSenderCapabilities(
               JavaToNativeMediaType(jni, media_type)));
}

ScopedJavaLocalRef<jobject>
JNI_PeerConnectionFactory_GetRtpReceiverCapabilities(
    JNIEnv* jni,
    jlong native_factory,
    const jni_zero::JavaParamRef<jobject>& media_type) {
  auto factory = PeerConnectionFactoryFromJava(native_factory);
  return NativeToJavaRtpCapabilities(
      jni, factory->GetRtpReceiverCapabilities(
               JavaToNativeMediaType(jni, media_type)));
}

static jboolean JNI_PeerConnectionFactory_StartAecDump(
    JNIEnv* jni,
    jlong native_factory,
    jint file_descriptor,
    jint filesize_limit_bytes) {
  FILE* f = fdopen(file_descriptor, "wb");
  if (!f) {
    close(file_descriptor);
    return false;
  }

  return PeerConnectionFactoryFromJava(native_factory)
      ->StartAecDump(f, filesize_limit_bytes);
}

static void JNI_PeerConnectionFactory_StopAecDump(JNIEnv* jni,
                                                  jlong native_factory) {
  PeerConnectionFactoryFromJava(native_factory)->StopAecDump();
}

static jlong JNI_PeerConnectionFactory_CreatePeerConnection(
    JNIEnv* jni,
    jlong factory,
    const jni_zero::JavaParamRef<jobject>& j_rtc_config,
    const jni_zero::JavaParamRef<jobject>& j_constraints,
    jlong observer_p,
    const jni_zero::JavaParamRef<jobject>& j_sslCertificateVerifier) {
  std::unique_ptr<PeerConnectionObserver> observer(
      reinterpret_cast<PeerConnectionObserver*>(observer_p));

  PeerConnectionInterface::RTCConfiguration rtc_config(
      PeerConnectionInterface::RTCConfigurationType::kAggressive);
  JavaToNativeRTCConfiguration(jni, j_rtc_config, &rtc_config);

  if (rtc_config.certificates.empty()) {
    // Generate non-default certificate.
    KeyType key_type = GetRtcConfigKeyType(jni, j_rtc_config);
    if (key_type != KT_DEFAULT) {
      scoped_refptr<RTCCertificate> certificate =
          RTCCertificateGenerator::GenerateCertificate(KeyParams(key_type),
                                                       std::nullopt);
      if (!certificate) {
        RTC_LOG(LS_ERROR) << "Failed to generate certificate. KeyType: "
                          << key_type;
        return 0;
      }
      rtc_config.certificates.push_back(certificate);
    }
  }

  std::unique_ptr<MediaConstraints> constraints;
  if (!j_constraints.is_null()) {
    constraints = JavaToNativeMediaConstraints(jni, j_constraints);
    CopyConstraintsIntoRtcConfiguration(constraints.get(), &rtc_config);
  }

  PeerConnectionDependencies peer_connection_dependencies(observer.get());
  if (!j_sslCertificateVerifier.is_null()) {
    peer_connection_dependencies.tls_cert_verifier =
        std::make_unique<SSLCertificateVerifierWrapper>(
            jni, j_sslCertificateVerifier);
  }

  auto result =
      PeerConnectionFactoryFromJava(factory)->CreatePeerConnectionOrError(
          rtc_config, std::move(peer_connection_dependencies));
  if (!result.ok())
    return 0;

  return jlongFromPointer(new OwnedPeerConnection(
      result.MoveValue(), std::move(observer), std::move(constraints)));
}

static jlong JNI_PeerConnectionFactory_CreateVideoSource(
    JNIEnv* jni,
    jlong native_factory,
    jboolean is_screencast,
    jboolean align_timestamps) {
  OwnedFactoryAndThreads* factory =
      reinterpret_cast<OwnedFactoryAndThreads*>(native_factory);
  return jlongFromPointer(CreateVideoSource(jni, factory->signaling_thread(),
                                            factory->worker_thread(),
                                            is_screencast, align_timestamps));
}

static jlong JNI_PeerConnectionFactory_CreateVideoTrack(
    JNIEnv* jni,
    jlong native_factory,
    const jni_zero::JavaParamRef<jstring>& id,
    jlong native_source) {
  scoped_refptr<VideoTrackInterface> track =
      PeerConnectionFactoryFromJava(native_factory)
          ->CreateVideoTrack(
              scoped_refptr<VideoTrackSourceInterface>(
                  reinterpret_cast<VideoTrackSourceInterface*>(native_source)),
              JavaToStdString(jni, id));
  return jlongFromPointer(track.release());
}

static jlong JNI_PeerConnectionFactory_GetNativePeerConnectionFactory(
    JNIEnv* jni,
    jlong native_factory) {
  return jlongFromPointer(PeerConnectionFactoryFromJava(native_factory));
}

static void JNI_PeerConnectionFactory_InjectLoggable(
    JNIEnv* jni,
    const jni_zero::JavaParamRef<jobject>& j_logging,
    jint nativeSeverity) {
  std::unique_ptr<JNILogSink>& jni_log_sink = GetStaticObjects().jni_log_sink;

  // If there is already a LogSink, remove it from LogMessage.
  if (jni_log_sink) {
    LogMessage::RemoveLogToStream(jni_log_sink.get());
  }
  jni_log_sink = std::make_unique<JNILogSink>(jni, j_logging);
  LogMessage::AddLogToStream(jni_log_sink.get(),
                             static_cast<LoggingSeverity>(nativeSeverity));
  LogMessage::LogToDebug(LS_NONE);
}

static void JNI_PeerConnectionFactory_DeleteLoggable(JNIEnv* jni) {
  std::unique_ptr<JNILogSink>& jni_log_sink = GetStaticObjects().jni_log_sink;

  if (jni_log_sink) {
    LogMessage::RemoveLogToStream(jni_log_sink.get());
    jni_log_sink.reset();
  }
}

static void JNI_PeerConnectionFactory_PrintStackTrace(JNIEnv* env, jint tid) {
  RTC_LOG(LS_WARNING) << StackTraceToString(GetStackTrace(tid));
}

}  // namespace jni
}  // namespace webrtc
