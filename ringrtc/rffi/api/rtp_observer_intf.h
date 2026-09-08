/*
 * Copyright 2025 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_RTP_OBSERVER_INTF_H__
#define RFFI_API_RTP_OBSERVER_INTF_H__

#include <cstddef>
#include <cstdint>

#include "rffi/api/rffi_defs.h"

/**
 * Rust friendly wrapper for creating objects that implement the
 * webrtc::RtpPacketSinkInterface interface for receiving RTP packets.
 *
 */

namespace webrtc {
namespace rffi {
class RtpObserverRffi;

/* RTP Observer callback function pointers */
typedef struct {
  // Warning: this runs on the WebRTC network thread, so doing anything that
  // would block is dangerous, especially taking a lock that is also taken
  // while calling something that blocks on the network thread.
  void (*onRtpReceived)(ptr::Borrowed<void> observer_borrowed,
                        uint8_t pt,
                        uint16_t seqnum,
                        uint32_t timestamp,
                        uint32_t ssrc,
                        ptr::Borrowed<const uint8_t> payload_data_borrowed,
                        size_t payload_size);
} RtpObserverCallbacks;

RUSTEXPORT ptr::Owned<webrtc::rffi::RtpObserverRffi> Rust_createRtpObserver(
    ptr::Borrowed<void> observer_borrowed,
    ptr::Borrowed<const RtpObserverCallbacks> callbacks_borrowed);

RUSTEXPORT void Rust_deleteRtpObserver(
    ptr::Owned<webrtc::rffi::RtpObserverRffi> observer_owned);

}  // namespace rffi
}  // namespace webrtc

#endif /* RFFI_API_RTP_OBSERVER_INTF_H__ */
