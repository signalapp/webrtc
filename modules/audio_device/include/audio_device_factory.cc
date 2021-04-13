/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_device/include/audio_device_factory.h"

#include <memory>

#if defined(WEBRTC_WIN)
#include "modules/audio_device/win/audio_device_module_win.h"
#include "modules/audio_device/win/core_audio_input_win.h"
#include "modules/audio_device/win/core_audio_output_win.h"
#include "modules/audio_device/win/core_audio_utility_win.h"
#endif

#include "api/task_queue/task_queue_factory.h"
#include "rtc_base/logging.h"

namespace webrtc {

rtc::scoped_refptr<AudioDeviceModule> CreateWindowsCoreAudioAudioDeviceModule(
    TaskQueueFactory* task_queue_factory,
    bool automatic_restart) {
  RTC_DLOG(INFO) << __FUNCTION__;
  return CreateWindowsCoreAudioAudioDeviceModuleForTest(task_queue_factory,
                                                        automatic_restart);
}

rtc::scoped_refptr<AudioDeviceModuleForTest>
CreateWindowsCoreAudioAudioDeviceModuleForTest(
    TaskQueueFactory* task_queue_factory,
    bool automatic_restart) {
  RTC_DLOG(INFO) << __FUNCTION__;
  return CreateWindowsCoreAudioAudioDeviceModuleFromInputAndOutput(
      std::make_unique<webrtc_win::CoreAudioInput>(automatic_restart),
      std::make_unique<webrtc_win::CoreAudioOutput>(automatic_restart),
      task_queue_factory);
}

}  // namespace webrtc
