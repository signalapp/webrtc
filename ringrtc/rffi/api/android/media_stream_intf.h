/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

/**
 * Rust friendly wrapper around webrtc::jni::JavaMediaStream object
 */

#ifndef ANDROID_MEDIA_STREAM_INTF_H__
#define ANDROID_MEDIA_STREAM_INTF_H__

#include <jni.h>

#include "rffi/api/rffi_defs.h"

namespace webrtc {
class MediaStreamInterface;
namespace jni {
class JavaMediaStream;
}

namespace rffi {
// Create a JavaMediaStream C++ object from a
// webrtc::MediaStreamInterface* object.
RUSTEXPORT ptr::Owned<webrtc::jni::JavaMediaStream> Rust_createJavaMediaStream(
    // Note that the name doesn't match the type as used in RingRTC
    ptr::OwnedRc<webrtc::MediaStreamInterface> media_stream_borrowed_rc);

// Delete a JavaMediaStream C++ object.
RUSTEXPORT void Rust_deleteJavaMediaStream(
    ptr::Owned<webrtc::jni::JavaMediaStream> java_media_stream_owned);

// Return the Java JNI object contained within the JavaMediaStream C++
// object.
RUSTEXPORT jobject Rust_getJavaMediaStreamObject(
    ptr::Borrowed<webrtc::jni::JavaMediaStream> java_media_stream_borrowed);

}  // namespace rffi
}  // namespace webrtc

#endif /* ANDROID_MEDIA_STREAM_INTF_H__ */
