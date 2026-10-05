/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <jni.h>

#include <string>

#include "rtc_base/logging.h"
#include "sdk/android/generated_instrumentationtests_jni/LoggableTest_jni.h"
#include "sdk/android/native_api/jni/java_types.h"
#include "sdk/android/src/jni/jni_helpers.h"
#include "third_party/jni_zero/jni_zero.h"

namespace webrtc {
namespace jni {

static void JNI_LoggableTest_LogInfoTestMessage(
    JNIEnv* jni,
    const jni_zero::JavaRef<jstring>& j_message) {
  std::string message = JavaToNativeString(jni, j_message);
  RTC_LOG(LS_INFO) << message;
}

}  // namespace jni
}  // namespace webrtc

DEFINE_JNI(LoggableTest)
