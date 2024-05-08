/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include <memory>

#include "sdk/android/generated_java_audio_jni/OboeAudioDeviceModule_jni.h"

#include "api/sequence_checker.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/include/audio_device_defines.h"
#include "sdk/android/src/jni/audio_device/audio_common.h"

#include "sdk/android/src/jni/audio_device/audio_device_module_oboe.h"

#include "sdk/android/src/jni/jni_helpers.h"

#include "rtc_base/logging.h"

namespace webrtc {
namespace jni {

static jlong JNI_OboeAudioDeviceModule_CreateAudioDeviceModule(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_context,
    const JavaParamRef<jobject>& j_audio_manager,
    int input_sample_rate,
    int output_sample_rate,
    jboolean j_use_stereo_input,
    jboolean j_use_stereo_output) {
  RTC_LOG(LS_WARNING) << "JimX: JNI_OboeAudioDeviceModule_CreateAudioDeviceModule";

  return jlongFromPointer(CreateAudioDeviceModuleOboe(
          AudioDeviceModule::kAndroidJavaAudio,
          j_use_stereo_input, j_use_stereo_output,
          kHighLatencyModeDelayEstimateInMilliseconds)
      .release());
}

}  // namespace jni
}  // namespace webrtc
