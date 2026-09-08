/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef ANDROID_PEER_CONNECTION_H__
#define ANDROID_PEER_CONNECTION_H__

#include <jni.h>

#include "rffi/api/rffi_defs.h"

namespace webrtc {
class PeerConnectionInterface;

namespace rffi {

// Return a borrowed RC to the native PeerConnection inside of the Java wrapper.
RUSTEXPORT ptr::BorrowedRc<webrtc::PeerConnectionInterface>
Rust_borrowPeerConnectionFromJniOwnedPeerConnection(
    jlong owned_peer_connection);

}  // namespace rffi
}  // namespace webrtc
#endif /* ANDROID_PEER_CONNECTION_H__ */
