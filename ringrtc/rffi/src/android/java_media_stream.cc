/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

/*
 * Rust friendly wrapper around JavaMediaStream object
 */

#include <string>

#include "rffi/api/android/media_stream_intf.h"
#include "rffi/src/ptr.h"
#include "sdk/android/native_api/jni/jvm.h"
#include "sdk/android/src/jni/pc/peer_connection.h"

namespace webrtc {
namespace rffi {

// Returns an owned pointer.
RUSTEXPORT webrtc::jni::JavaMediaStream* Rust_createJavaMediaStream(
    MediaStreamInterface* stream_borrowed_rc) {
  JNIEnv* env = AttachCurrentThreadIfNeeded();
  // jni::JavaMediaStream takes an owned RC.
  return new jni::JavaMediaStream(env, inc_rc(stream_borrowed_rc));
}

RUSTEXPORT void Rust_deleteJavaMediaStream(
    webrtc::jni::JavaMediaStream* stream_owned) {
  delete stream_owned;
}

RUSTEXPORT jobject
Rust_getJavaMediaStreamObject(webrtc::jni::JavaMediaStream* stream_borrowed) {
  return stream_borrowed->j_media_stream().obj();
}

}  // namespace rffi
}  // namespace webrtc
