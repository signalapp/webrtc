/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#import "RTCPeerConnectionFactory+Native.h"
#import "RTCPeerConnectionFactory+Private.h"
#import "RTCPeerConnectionFactoryOptions+Private.h"
#import "RTCRtpCapabilities+Private.h"

#import "RTCAudioSource+Private.h"
#import "RTCAudioTrack+Private.h"
#import "RTCMediaConstraints+Private.h"
#import "RTCMediaStream+Private.h"
#import "RTCPeerConnection+Private.h"
#import "RTCVideoSource+Private.h"
#import "RTCVideoTrack+Private.h"
#import "base/RTCLogging.h"
#import "base/RTCVideoDecoderFactory.h"
#import "base/RTCVideoEncoderFactory.h"
#import "helpers/NSString+StdString.h"
#include "rtc_base/checks.h"
#include "sdk/objc/native/api/network_monitor_factory.h"
#include "sdk/objc/native/api/ssl_certificate_verifier.h"

#include "api/audio/audio_device.h"
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#import "components/video_codec/RTCVideoDecoderFactoryH264.h"
#import "components/video_codec/RTCVideoEncoderFactoryH264.h"
#include "media/base/media_constants.h"

#include "sdk/objc/native/api/objc_audio_device_module.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#include "sdk/objc/native/api/video_encoder_factory.h"
#include "sdk/objc/native/src/objc_video_decoder_factory.h"
#include "sdk/objc/native/src/objc_video_encoder_factory.h"

#if defined(WEBRTC_IOS)
#import "sdk/objc/native/api/audio_device_module.h"
#endif

// RingRTC changes for low-level FFI
#import "RTCMediaStreamTrack+Private.h"

@implementation RTC_OBJC_TYPE (RTCPeerConnectionFactory) {
  std::unique_ptr<webrtc::Thread> _networkThread;
  std::unique_ptr<webrtc::Thread> _workerThread;
  std::unique_ptr<webrtc::Thread> _signalingThread;
  BOOL _hasStartedAecDump;
}

@synthesize nativeFactory = _nativeFactory;

- (instancetype)init {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();
  dependencies.video_encoder_factory = webrtc::ObjCToNativeVideoEncoderFactory(
      [[RTC_OBJC_TYPE(RTCVideoEncoderFactoryH264) alloc] init]);
  dependencies.video_decoder_factory = webrtc::ObjCToNativeVideoDecoderFactory(
      [[RTC_OBJC_TYPE(RTCVideoDecoderFactoryH264) alloc] init]);
  dependencies.env = webrtc::CreateEnvironment();
#ifdef WEBRTC_IOS
  dependencies.adm = webrtc::CreateAudioDeviceModule(*dependencies.env);
#endif
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)
    initWithEncoderFactory:
        (nullable id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)>)encoderFactory
            decoderFactory:(nullable id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)>)
                               decoderFactory {
  return [self initWithEncoderFactory:encoderFactory
                       decoderFactory:decoderFactory
                          audioDevice:nil];
}

- (instancetype)
    initWithEncoderFactory:
        (nullable id<RTC_OBJC_TYPE(RTCVideoEncoderFactory)>)encoderFactory
            decoderFactory:(nullable id<RTC_OBJC_TYPE(RTCVideoDecoderFactory)>)
                               decoderFactory
               audioDevice:
                   (nullable id<RTC_OBJC_TYPE(RTCAudioDevice)>)audioDevice {
#ifdef HAVE_NO_MEDIA
  return [self initWithNoMedia];
#else
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.env = webrtc::CreateEnvironment();
  dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();
  if (encoderFactory) {
    dependencies.video_encoder_factory =
        webrtc::ObjCToNativeVideoEncoderFactory(encoderFactory);
  }
  if (decoderFactory) {
    dependencies.video_decoder_factory =
        webrtc::ObjCToNativeVideoDecoderFactory(decoderFactory);
  }
  if (audioDevice) {
    dependencies.adm =
        webrtc::CreateAudioDeviceModule(*dependencies.env, audioDevice);
#ifdef WEBRTC_IOS
  } else {
    dependencies.adm = webrtc::CreateAudioDeviceModule(*dependencies.env);
#endif
  }
  return [self initWithMediaAndDependencies:dependencies];
#endif
}

- (instancetype)initWithNativeDependencies:
    (webrtc::PeerConnectionFactoryDependencies &)dependencies {
  self = [super init];
  if (self) {
    _networkThread = webrtc::Thread::CreateWithSocketServer();
    _networkThread->SetName("network_thread", _networkThread.get());
    BOOL result = _networkThread->Start();
    RTC_DCHECK(result) << "Failed to start network thread.";

    _workerThread = webrtc::Thread::Create();
    _workerThread->SetName("worker_thread", _workerThread.get());
    result = _workerThread->Start();
    RTC_DCHECK(result) << "Failed to start worker thread.";

    _signalingThread = webrtc::Thread::Create();
    _signalingThread->SetName("signaling_thread", _signalingThread.get());
    result = _signalingThread->Start();
    RTC_DCHECK(result) << "Failed to start signaling thread.";

    // Set fields that are relevant both to 'no media' and 'with media'
    // scenarios.
    dependencies.network_thread = _networkThread.get();
    dependencies.worker_thread = _workerThread.get();
    dependencies.signaling_thread = _signalingThread.get();
    if (!dependencies.env.has_value()) {
      dependencies.env = webrtc::CreateEnvironment();
    }
    if (dependencies.network_monitor_factory == nullptr &&
        dependencies.env->field_trials().IsEnabled(
            "WebRTC-Network-UseNWPathMonitor")) {
      dependencies.network_monitor_factory =
          webrtc::CreateNetworkMonitorFactory();
    }

    _nativeFactory =
        webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));
    NSAssert(_nativeFactory, @"Failed to initialize PeerConnectionFactory!");
  }
  return self;
}

- (instancetype)initWithNoMedia {
  webrtc::PeerConnectionFactoryDependencies default_deps;
  return [self initWithNativeDependencies:default_deps];
}

- (instancetype)
    initWithNativeAudioEncoderFactory:
        (webrtc::scoped_refptr<webrtc::AudioEncoderFactory>)audioEncoderFactory
            nativeAudioDecoderFactory:
                (webrtc::scoped_refptr<webrtc::AudioDecoderFactory>)
                    audioDecoderFactory
            nativeVideoEncoderFactory:
                (std::unique_ptr<webrtc::VideoEncoderFactory>)
                    videoEncoderFactory
            nativeVideoDecoderFactory:
                (std::unique_ptr<webrtc::VideoDecoderFactory>)
                    videoDecoderFactory
                    audioDeviceModule:
                        (webrtc::AudioDeviceModule *)audioDeviceModule
                audioProcessingModule:
                    (webrtc::scoped_refptr<webrtc::AudioProcessing>)
                        audioProcessingModule {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.audio_encoder_factory = std::move(audioEncoderFactory);
  dependencies.audio_decoder_factory = std::move(audioDecoderFactory);
  dependencies.video_encoder_factory = std::move(videoEncoderFactory);
  dependencies.video_decoder_factory = std::move(videoDecoderFactory);
  dependencies.adm = std::move(audioDeviceModule);
  if (audioProcessingModule != nullptr) {
    dependencies.audio_processing_builder =
        CustomAudioProcessing(std::move(audioProcessingModule));
  }
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)
    initWithNativeAudioEncoderFactory:
        (webrtc::scoped_refptr<webrtc::AudioEncoderFactory>)audioEncoderFactory
            nativeAudioDecoderFactory:
                (webrtc::scoped_refptr<webrtc::AudioDecoderFactory>)
                    audioDecoderFactory
            nativeVideoEncoderFactory:
                (std::unique_ptr<webrtc::VideoEncoderFactory>)
                    videoEncoderFactory
            nativeVideoDecoderFactory:
                (std::unique_ptr<webrtc::VideoDecoderFactory>)
                    videoDecoderFactory
                    audioDeviceModule:
                        (webrtc::AudioDeviceModule *)audioDeviceModule
                audioProcessingModule:
                    (webrtc::scoped_refptr<webrtc::AudioProcessing>)
                        audioProcessingModule
             networkControllerFactory:
                 (std::unique_ptr<webrtc::NetworkControllerFactoryInterface>)
                     networkControllerFactory {
  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.adm = std::move(audioDeviceModule);
  dependencies.audio_encoder_factory = std::move(audioEncoderFactory);
  dependencies.audio_decoder_factory = std::move(audioDecoderFactory);
  dependencies.video_encoder_factory = std::move(videoEncoderFactory);
  dependencies.video_decoder_factory = std::move(videoDecoderFactory);
  if (audioProcessingModule != nullptr) {
    dependencies.audio_processing_builder =
        CustomAudioProcessing(std::move(audioProcessingModule));
  }
  dependencies.network_controller_factory = std::move(networkControllerFactory);
  return [self initWithMediaAndDependencies:dependencies];
}

- (instancetype)initWithMediaAndDependencies:
    (webrtc::PeerConnectionFactoryDependencies &)dependencies {
#ifndef WEBRTC_EXCLUDE_AUDIO_PROCESSING_MODULE
  if (dependencies.audio_processing_builder == nullptr) {
    dependencies.audio_processing_builder =
        std::make_unique<webrtc::BuiltinAudioProcessingBuilder>();
  }
#endif
  if (dependencies.event_log_factory == nullptr) {
    dependencies.event_log_factory =
        std::make_unique<webrtc::RtcEventLogFactory>();
  }
  webrtc::EnableMedia(dependencies);
  return [self initWithNativeDependencies:dependencies];
}

- (RTC_OBJC_TYPE(RTCRtpCapabilities) *)rtpSenderCapabilitiesForKind:
    (NSString *)kind {
  webrtc::MediaType mediaType = [[self class] mediaTypeForKind:kind];

  webrtc::RtpCapabilities rtpCapabilities =
      _nativeFactory->GetRtpSenderCapabilities(mediaType);
  return [[RTC_OBJC_TYPE(RTCRtpCapabilities) alloc]
      initWithNativeRtpCapabilities:rtpCapabilities];
}

- (RTC_OBJC_TYPE(RTCRtpCapabilities) *)rtpReceiverCapabilitiesForKind:
    (NSString *)kind {
  webrtc::MediaType mediaType = [[self class] mediaTypeForKind:kind];

  webrtc::RtpCapabilities rtpCapabilities =
      _nativeFactory->GetRtpReceiverCapabilities(mediaType);
  return [[RTC_OBJC_TYPE(RTCRtpCapabilities) alloc]
      initWithNativeRtpCapabilities:rtpCapabilities];
}

- (RTC_OBJC_TYPE(RTCAudioSource) *)audioSourceWithConstraints:
    (nullable RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints {
  std::unique_ptr<webrtc::MediaConstraints> nativeConstraints;
  if (constraints) {
    nativeConstraints = constraints.nativeConstraints;
  }
  webrtc::AudioOptions options;
  CopyConstraintsIntoAudioOptions(nativeConstraints.get(), &options);

  webrtc::scoped_refptr<webrtc::AudioSourceInterface> source =
      _nativeFactory->CreateAudioSource(options);
  return [[RTC_OBJC_TYPE(RTCAudioSource) alloc] initWithFactory:self
                                              nativeAudioSource:source];
}

- (RTC_OBJC_TYPE(RTCAudioTrack) *)audioTrackWithTrackId:(NSString *)trackId {
  RTC_OBJC_TYPE(RTCAudioSource) *audioSource =
      [self audioSourceWithConstraints:nil];
  return [self audioTrackWithSource:audioSource trackId:trackId];
}

- (RTC_OBJC_TYPE(RTCAudioTrack) *)audioTrackWithSource:
                                      (RTC_OBJC_TYPE(RTCAudioSource) *)source
                                               trackId:(NSString *)trackId {
  return [[RTC_OBJC_TYPE(RTCAudioTrack) alloc] initWithFactory:self
                                                        source:source
                                                       trackId:trackId];
}

- (RTC_OBJC_TYPE(RTCVideoSource) *)videoSource {
  return [[RTC_OBJC_TYPE(RTCVideoSource) alloc]
      initWithFactory:self
      signalingThread:_signalingThread.get()
         workerThread:_workerThread.get()];
}

- (RTC_OBJC_TYPE(RTCVideoSource) *)videoSourceForScreenCast:
    (BOOL)forScreenCast {
  return [[RTC_OBJC_TYPE(RTCVideoSource) alloc]
      initWithFactory:self
      signalingThread:_signalingThread.get()
         workerThread:_workerThread.get()
         isScreenCast:forScreenCast];
}

- (RTC_OBJC_TYPE(RTCVideoTrack) *)videoTrackWithSource:
                                      (RTC_OBJC_TYPE(RTCVideoSource) *)source
                                               trackId:(NSString *)trackId {
  return [[RTC_OBJC_TYPE(RTCVideoTrack) alloc] initWithFactory:self
                                                        source:source
                                                       trackId:trackId];
}

- (RTC_OBJC_TYPE(RTCMediaStream) *)mediaStreamWithStreamId:
    (NSString *)streamId {
  return [[RTC_OBJC_TYPE(RTCMediaStream) alloc] initWithFactory:self
                                                       streamId:streamId];
}

- (nullable RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithConfiguration:
        (RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                        constraints:
                            (RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                           delegate:(nullable id<RTC_OBJC_TYPE(
                                         RTCPeerConnectionDelegate)>)delegate {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc] initWithFactory:self
                                                     configuration:configuration
                                                       constraints:constraints
                                               certificateVerifier:nil
                                                          delegate:delegate];
}

- (nullable RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithConfiguration:
        (RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                        constraints:
                            (RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                certificateVerifier:
                    (id<RTC_OBJC_TYPE(RTCSSLCertificateVerifier)>)
                        certificateVerifier
                           delegate:(nullable id<RTC_OBJC_TYPE(
                                         RTCPeerConnectionDelegate)>)delegate {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc]
          initWithFactory:self
            configuration:configuration
              constraints:constraints
      certificateVerifier:certificateVerifier
                 delegate:delegate];
}

- (nullable RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithDependencies:
        (RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                       constraints:
                           (RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                      dependencies:
                          (std::unique_ptr<webrtc::PeerConnectionDependencies>)
                              dependencies
                          delegate:
                              (id<RTC_OBJC_TYPE(RTCPeerConnectionDelegate)>)
                                  delegate {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc]
      initWithDependencies:self
             configuration:configuration
               constraints:constraints
              dependencies:std::move(dependencies)
                  delegate:delegate];
}

// RingRTC changes for low-level FFI
- (RTC_OBJC_TYPE(RTCVideoTrack) *)videoTrackFromNativeTrack:(void *)nativeTrackBorrowedRc {
  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track = webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface>((webrtc::MediaStreamTrackInterface*)nativeTrackBorrowedRc);

  return [[RTC_OBJC_TYPE(RTCVideoTrack) alloc] initWithFactory:self
                                                   nativeTrack:track
                                                          type:RTCMediaStreamTrackTypeVideo];
}

// RingRTC changes for low-level FFI
- (RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithConfiguration:(RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                        constraints:(RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                           observer:(void *)observer {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc] initWithFactory:self
                                                     configuration:configuration
                                                       constraints:constraints
                                                          observer:observer];
}

// RingRTC changes for low-level FFI
- (RTC_OBJC_TYPE(RTCPeerConnection) *)
    peerConnectionWithDependencies:(RTC_OBJC_TYPE(RTCConfiguration) *)configuration
                       constraints:(RTC_OBJC_TYPE(RTCMediaConstraints) *)constraints
                      dependencies:(std::unique_ptr<webrtc::PeerConnectionDependencies>)dependencies
                          observer:(void *)observer {
  return [[RTC_OBJC_TYPE(RTCPeerConnection) alloc] initWithDependencies:self
                                                          configuration:configuration
                                                            constraints:constraints
                                                           dependencies:std::move(dependencies)
                                                               observer:observer];
}

- (void)setOptions:
    (nonnull RTC_OBJC_TYPE(RTCPeerConnectionFactoryOptions) *)options {
  RTC_DCHECK(options != nil);
  _nativeFactory->SetOptions(options.nativeOptions);
}

- (BOOL)startAecDumpWithFilePath:(NSString *)filePath
                  maxSizeInBytes:(int64_t)maxSizeInBytes {
  RTC_DCHECK(filePath.length);
  RTC_DCHECK_GT(maxSizeInBytes, 0);

  if (_hasStartedAecDump) {
    RTCLogError(@"Aec dump already started.");
    return NO;
  }
  FILE *f = fopen(filePath.UTF8String, "wb");
  if (!f) {
    RTCLogError(
        @"Error opening file: %@. Error: %s", filePath, strerror(errno));
    return NO;
  }
  _hasStartedAecDump = _nativeFactory->StartAecDump(f, maxSizeInBytes);
  return _hasStartedAecDump;
}

- (void)stopAecDump {
  _nativeFactory->StopAecDump();
  _hasStartedAecDump = NO;
}

// RingRTC changes for low-level FFI
- (void *)getOwnedNativeFactory {
  return self.nativeFactory.release();
}

- (webrtc::Thread *)signalingThread {
  return _signalingThread.get();
}

- (webrtc::Thread *)workerThread {
  return _workerThread.get();
}

- (webrtc::Thread *)networkThread {
  return _networkThread.get();
}

#pragma mark - Private

+ (webrtc::MediaType)mediaTypeForKind:(NSString *)kind {
  if (kind == kRTCMediaStreamTrackKindAudio) {
    return webrtc::MediaType::AUDIO;
  } else if (kind == kRTCMediaStreamTrackKindVideo) {
    return webrtc::MediaType::VIDEO;
  } else {
    RTC_DCHECK_NOTREACHED();
    return webrtc::MediaType::UNSUPPORTED;
  }
}

@end
