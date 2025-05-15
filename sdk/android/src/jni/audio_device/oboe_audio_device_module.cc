/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rtc_base/logging.h"
#include "sdk/android/generated_java_audio_jni/OboeAudioDeviceModule_jni.h"
#include "sdk/android/src/jni/audio_device/audio_device_module_oboe.h"
#include "sdk/android/src/jni/jni_helpers.h"

namespace webrtc {
namespace jni {

static jlong JNI_OboeAudioDeviceModule_CreateAudioDeviceModule(
    JNIEnv* env,
    jboolean j_use_software_acoustic_echo_canceler,
    jboolean j_use_software_noise_suppressor,
    jboolean j_use_exclusive_sharing_mode,
    jboolean j_use_input_low_latency,
    jboolean j_use_input_voice_comm_preset) {
  RTC_LOG(LS_WARNING) << "JNI_OboeAudioDeviceModule_CreateAudioDeviceModule";
  return jlongFromPointer(
      CreateAudioDeviceModuleOboe(
          j_use_software_acoustic_echo_canceler,
          j_use_software_noise_suppressor, j_use_exclusive_sharing_mode,
          j_use_input_low_latency, j_use_input_voice_comm_preset)
          .release());
}

}  // namespace jni
}  // namespace webrtc
