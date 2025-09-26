/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_AUDIO_DEVICE_MODULE_OBOE_H_
#define SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_AUDIO_DEVICE_MODULE_OBOE_H_

#include "modules/audio_device/include/audio_device.h"

namespace webrtc {
namespace jni {

// Create the Oboe-based AudioDeviceModule.
scoped_refptr<AudioDeviceModule> CreateAudioDeviceModuleOboe(
    bool use_software_acoustic_echo_canceler,
    bool use_software_noise_suppressor,
    bool use_exclusive_sharing_mode,
    bool use_input_low_latency,
    bool use_input_voice_comm_preset);

}  // namespace jni
}  // namespace webrtc

#endif  // SDK_ANDROID_SRC_JNI_AUDIO_DEVICE_AUDIO_DEVICE_MODULE_OBOE_H_
