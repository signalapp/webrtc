/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#import <Foundation/Foundation.h>

#import "RTCCodecSpecificInfo.h"
#import "sdk/objc/base/RTCMacros.h"

/** Class for H265 specific config. */
RTC_OBJC_EXPORT
@interface RTC_OBJC_TYPE (RTCCodecSpecificInfoH265) : NSObject <RTC_OBJC_TYPE(RTCCodecSpecificInfo)>
@end
