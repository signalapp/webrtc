/*
 *  Copyright 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/video_frame_buffer.h"

#include <jni.h>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "rtc_base/checks.h"
#include "sdk/android/generated_instrumentationtests_jni/VideoFrameBufferTest_jni.h"
#include "sdk/android/src/jni/jni_helpers.h"
#include "sdk/android/src/jni/video_frame.h"
#include "sdk/android/src/jni/wrapped_native_i420_buffer.h"
#include "third_party/jni_zero/jni_zero.h"

namespace webrtc {
namespace jni {

static jint JNI_VideoFrameBufferTest_GetBufferType(
    JNIEnv* jni,
    const jni_zero::JavaRef<jobject>& video_frame_buffer) {
  webrtc::scoped_refptr<VideoFrameBuffer> buffer =
      JavaToNativeFrameBuffer(jni, video_frame_buffer);
  return static_cast<jint>(buffer->type());
}

static jni_zero::ScopedJavaLocalRef<jobject>
JNI_VideoFrameBufferTest_GetNativeI420Buffer(
    JNIEnv* jni,
    const jni_zero::JavaRef<jobject>& i420_buffer) {
  webrtc::scoped_refptr<VideoFrameBuffer> buffer =
      JavaToNativeFrameBuffer(jni, i420_buffer);
  const I420BufferInterface* inputBuffer = buffer->GetI420();
  RTC_DCHECK(inputBuffer != nullptr);
  webrtc::scoped_refptr<I420Buffer> outputBuffer =
      I420Buffer::Copy(*inputBuffer);
  return WrapI420Buffer(jni, outputBuffer);
}

}  // namespace jni
}  // namespace webrtc

DEFINE_JNI(VideoFrameBufferTest)
