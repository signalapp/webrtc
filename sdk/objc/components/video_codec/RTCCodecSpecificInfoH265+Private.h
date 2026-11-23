/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#import "RTCCodecSpecificInfoH265.h"

#include "modules/video_coding/include/video_codec_interface.h"

NS_ASSUME_NONNULL_BEGIN

/* Interfaces for converting to/from internal C++ formats. */
@interface RTC_OBJC_TYPE (RTCCodecSpecificInfoH265)
()

    - (webrtc::CodecSpecificInfo)nativeCodecSpecificInfo;

@end

NS_ASSUME_NONNULL_END
