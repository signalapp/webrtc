/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/logging.h"

#include <jni.h>

#include <string>

#include "rtc_base/base_java_jni/Logging_jni.h"
#include "sdk/android/native_api/jni/java_types.h"
#include "sdk/android/src/jni/jni_helpers.h"
#include "third_party/jni_zero/jni_zero.h"

namespace webrtc {
namespace jni {

static void JNI_Logging_EnableLogToDebugOutput(JNIEnv* jni,
                                               jint nativeSeverity) {
  if (nativeSeverity >= LS_VERBOSE && nativeSeverity <= LS_NONE) {
    LogMessage::LogToDebug(static_cast<LoggingSeverity>(nativeSeverity));
  }
}

static void JNI_Logging_EnableLogThreads(JNIEnv* jni) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  LogMessage::LogThreads(true);
#pragma clang diagnostic pop
}

static void JNI_Logging_EnableLogTimeStamps(JNIEnv* jni) {
  LogMessage::LogTimestamps(true);
}

static void JNI_Logging_Log(JNIEnv* jni,
                            jint j_severity,
                            const jni_zero::JavaRef<jstring>& j_tag,
                            const jni_zero::JavaRef<jstring>& j_message) {
  std::string message = JavaToNativeString(jni, j_message);
  std::string tag = JavaToNativeString(jni, j_tag);
  RTC_LOG_TAG(static_cast<LoggingSeverity>(j_severity), tag.c_str()) << message;
}

}  // namespace jni
}  // namespace webrtc

DEFINE_JNI(Logging)
