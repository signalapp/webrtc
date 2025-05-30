/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include <string>

#include "rffi/api/android/peer_connection_intf.h"

namespace webrtc {
namespace rffi {

// Returns a borrowed RC.
RUSTEXPORT PeerConnectionInterface*
Rust_borrowPeerConnectionFromJniOwnedPeerConnection(
    jlong owned_peer_connection) {
  return reinterpret_cast<jni::OwnedPeerConnection*>(owned_peer_connection)
      ->pc();
}

}  // namespace rffi
}  // namespace webrtc
