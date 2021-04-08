/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCAudioTrack+Private.h"

#import "RTCAudioSource+Private.h"
#import "RTCMediaStreamTrack+Private.h"
#import "RTCPeerConnectionFactory+Private.h"
#import "helpers/NSString+StdString.h"

#include "rtc_base/checks.h"

// RingRTC change to allow access to the native track
@implementation RTC_OBJC_TYPE (RTCAudioTrack) {
  rtc::scoped_refptr<webrtc::AudioTrackInterface> _nativeTrack;
}

@synthesize source = _source;

- (instancetype)initWithFactory:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
                         source:(RTC_OBJC_TYPE(RTCAudioSource) *)source
                        trackId:(NSString *)trackId {
  RTC_DCHECK(factory);
  RTC_DCHECK(source);
  RTC_DCHECK(trackId.length);

  std::string nativeId = [NSString stdStringForString:trackId];
  rtc::scoped_refptr<webrtc::AudioTrackInterface> track =
      factory.nativeFactory->CreateAudioTrack(nativeId, source.nativeAudioSource);
  if (self = [self initWithFactory:factory nativeTrack:track type:RTCMediaStreamTrackTypeAudio]) {
    _source = source;
    // RingRTC change to allow access to the native track
    _nativeTrack = track;
  }
  return self;
}

- (instancetype)initWithFactory:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
                    nativeTrack:(rtc::scoped_refptr<webrtc::MediaStreamTrackInterface>)nativeTrack
                           type:(RTCMediaStreamTrackType)type {
  NSParameterAssert(factory);
  NSParameterAssert(nativeTrack);
  NSParameterAssert(type == RTCMediaStreamTrackTypeAudio);
  return [super initWithFactory:factory nativeTrack:nativeTrack type:type];
}

- (RTC_OBJC_TYPE(RTCAudioSource) *)source {
  if (!_source) {
    rtc::scoped_refptr<webrtc::AudioSourceInterface> source =
        self.nativeAudioTrack->GetSource();
    if (source) {
      _source = [[RTC_OBJC_TYPE(RTCAudioSource) alloc] initWithFactory:self.factory
                                                     nativeAudioSource:source.get()];
    }
  }
  return _source;
}

// RingRTC change to allow access to the native track
- (void *)getNativeAudioTrack {
  return _nativeTrack.release();
}

#pragma mark - Private

- (rtc::scoped_refptr<webrtc::AudioTrackInterface>)nativeAudioTrack {
  return static_cast<webrtc::AudioTrackInterface *>(self.nativeTrack.get());
}

@end
