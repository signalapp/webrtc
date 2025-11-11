/*
 * Copyright 2025 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_RTP_OBSERVER_H__
#define RFFI_RTP_OBSERVER_H__

#include "call/rtp_packet_sink_interface.h"
#include "rffi/api/rtp_observer_intf.h"

namespace webrtc {
namespace rffi {

/**
 * Adapter between the C++ RtpPacketSinkInterface interface
 * and Rust. Wraps an instance of the Rust interface and dispatches
 * C++ callbacks to Rust.
 */

class RtpObserverRffi : public RtpPacketSinkInterface {
 public:
  // Passed-in observer must live as long as the RtpObserverRffi.
  RtpObserverRffi(void* observer, const RtpObserverCallbacks* callbacks);
  ~RtpObserverRffi() override;

  void OnRtpPacket(const RtpPacketReceived& rtp_packet) override;

 private:
  void* observer_;
  RtpObserverCallbacks callbacks_;
};

}  // namespace rffi
}  // namespace webrtc

#endif /* RFFI_RTP_OBSERVER_H__ */
