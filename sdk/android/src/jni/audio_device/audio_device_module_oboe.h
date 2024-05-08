/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_AUDIO_DEVICE_MODULE_OBOE_H_
#define SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_AUDIO_DEVICE_MODULE_OBOE_H_

#include <memory>

#include "absl/types/optional.h"
#include "modules/audio_device/include/audio_device.h"
#include "sdk/android/native_api/jni/scoped_java_ref.h"

namespace webrtc {

namespace jni {

// Extract an android.media.AudioManager from an android.content.Context.
//ScopedJavaLocalRef<jobject> GetAudioManagerOboe(JNIEnv* env,
//                                                const JavaRef<jobject>& j_context);

// Get default audio sample rate by querying an android.media.AudioManager.
//int GetDefaultSampleRateOboe(JNIEnv* env, const JavaRef<jobject>& j_audio_manager);

//bool IsLowLatencyInputSupported(JNIEnv* env, const JavaRef<jobject>& j_context);

//bool IsLowLatencyOutputSupported(JNIEnv* env, const JavaRef<jobject>& j_context);

// Get the Oboe-based AudioDeviceModule.
rtc::scoped_refptr<AudioDeviceModule> CreateAudioDeviceModuleOboe(
    AudioDeviceModule::AudioLayer audio_layer,
    bool is_stereo_playout_supported,
    bool is_stereo_record_supported,
    uint16_t playout_delay_ms);

}  // namespace jni

}  // namespace webrtc

#endif  // SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_AUDIO_DEVICE_MODULE_OBOE_H_
