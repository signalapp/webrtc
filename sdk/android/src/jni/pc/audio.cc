/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/pc/audio.h"

#include "api/audio/echo_detector_creator.h"
#include "modules/audio_processing/include/audio_processing.h"

namespace webrtc {
namespace jni {

rtc::scoped_refptr<AudioProcessing> CreateAudioProcessing() {
  // RingRTC change to enable echo detection
  return AudioProcessingBuilder().SetEchoDetector(CreateEchoDetector()).Create();
}

}  // namespace jni
}  // namespace webrtc
