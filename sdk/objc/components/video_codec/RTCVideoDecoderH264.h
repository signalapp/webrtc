/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <Foundation/Foundation.h>

#import "RTCVideoCodecInfo.h"
#import "RTCVideoDecoder.h"
#import "sdk/objc/base/RTCMacros.h"

NS_ASSUME_NONNULL_BEGIN

RTC_OBJC_EXPORT
@interface RTC_OBJC_TYPE (RTCVideoDecoderH264) : NSObject <RTC_OBJC_TYPE(RTCVideoDecoder)>

/** Returns the list of supported codec formats (profiles) for the H264 decoder. */
+ (NSArray<RTC_OBJC_TYPE(RTCVideoCodecInfo) *> *)supportedCodecs;

// RingRTC change to add support for H265
/** Creates an H264 decoder. Enables H265  mode if `codecInfo` specifies H265. */
- (instancetype)initWithCodecInfo:(RTC_OBJC_TYPE(RTCVideoCodecInfo) *)codecInfo;

/** Returns the H265 formats this class can decode. Empty unless built with
 * rtc_use_h265 and the device has a hardware HEVC decoder.
 */
+ (NSArray<RTC_OBJC_TYPE(RTCVideoCodecInfo) *> *)supportedH265Codecs;

/** Whether hardware H265 decoding is available. */
+ (BOOL)isH265Supported;
// end RingRTC change to add support for H265

@end

NS_ASSUME_NONNULL_END
