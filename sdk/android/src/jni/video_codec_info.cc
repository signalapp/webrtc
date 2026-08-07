/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/video_codec_info.h"

#include <jni.h>

#include "api/video_codecs/sdp_video_format.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "sdk/android/generated_video_jni/VideoCodecInfo_jni.h"
#include "sdk/android/native_api/jni/java_types.h"
#include "sdk/android/native_api/jni/scoped_java_ref.h"

namespace webrtc {
namespace jni {

SdpVideoFormat VideoCodecInfoToSdpVideoFormat(JNIEnv* jni,
                                              const JavaRef<jobject>& j_info) {
  // RingRTC change to include scalability modes into java VideoCodecInfo
  // Construct the list of scalability modes to include in the native version
  // of SdpVideoFormat
  absl::InlinedVector<ScalabilityMode, kScalabilityModeCount> scalability_modes;
  for (auto j_mode_str :
       Iterable(jni, Java_VideoCodecInfo_getScalabilityModes(jni, j_info))) {
    auto mode_str =
        JavaToNativeString(jni, static_java_ref_cast<jstring>(jni, j_mode_str));
    auto mode = ScalabilityModeFromString(mode_str);
    if (mode.has_value()) {
      scalability_modes.push_back(*mode);
    }
  }
  return SdpVideoFormat(
      JavaToNativeString(jni, Java_VideoCodecInfo_getName(jni, j_info)),
      JavaToNativeStringMap(jni, Java_VideoCodecInfo_getParams(jni, j_info)),
      // RingRTC change to include scalability modes into java VideoCodecInfo
      scalability_modes);
}

ScopedJavaLocalRef<jobject> SdpVideoFormatToVideoCodecInfo(
    JNIEnv* jni,
    const SdpVideoFormat& format) {
  ScopedJavaLocalRef<jobject> j_params =
      NativeToJavaStringMap(jni, format.parameters);
  // RingRTC change to include scalability modes into java VideoCodecInfo
  // Store the scalability modes as strings so that instances of SdpVideoFormat
  // can be constructed with scalability info included.
  ScopedJavaLocalRef<jobject> j_scalability_modes = NativeToJavaList(
      jni, format.scalability_modes, [](JNIEnv* jni, ScalabilityMode mode) {
        return NativeToJavaString(jni, ScalabilityModeToString(mode));
      });
  return Java_VideoCodecInfo_Constructor(
      jni, NativeToJavaString(jni, format.name), j_params, j_scalability_modes);
}

}  // namespace jni
}  // namespace webrtc
