/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#import "RTCCodecSpecificInfoH265+Private.h"

@implementation RTC_OBJC_TYPE (RTCCodecSpecificInfoH265)

- (webrtc::CodecSpecificInfo)nativeCodecSpecificInfo {
  webrtc::CodecSpecificInfo codecSpecificInfo;
  codecSpecificInfo.codecType = webrtc::kVideoCodecH265;
  return codecSpecificInfo;
}

@end
