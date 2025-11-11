/*
 * Copyright 2025 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/src/rtp_observer.h"

#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace rffi {

RtpObserverRffi::RtpObserverRffi(void* observer,
                                 const RtpObserverCallbacks* callbacks)
    : observer_(observer), callbacks_(*callbacks) {
  RTC_LOG(LS_WARNING) << "RtpObserverRffi:ctor(): " << this->observer_;
}

RtpObserverRffi::~RtpObserverRffi() {
  RTC_LOG(LS_WARNING) << "RtpObserverRffi:dtor(): " << this->observer_;
}

void RtpObserverRffi::OnRtpPacket(const RtpPacketReceived& rtp_packet) {
  if (callbacks_.onRtpReceived != nullptr) {
    uint8_t pt = rtp_packet.PayloadType();
    uint16_t seqnum = rtp_packet.SequenceNumber();
    uint32_t timestamp = rtp_packet.Timestamp();
    uint32_t ssrc = rtp_packet.Ssrc();
    const uint8_t* payload_data = rtp_packet.payload().data();
    size_t payload_size = rtp_packet.payload().size();
    RTC_LOG(LS_VERBOSE) << "OnRtpPacket() pt: " << pt << " seqnum: " << seqnum
                        << " timestamp: " << timestamp << " ssrc: " << ssrc
                        << " payload_size: " << payload_size;
    callbacks_.onRtpReceived(observer_, pt, seqnum, timestamp, ssrc,
                             payload_data, payload_size);
  }
}

RUSTEXPORT RtpObserverRffi* Rust_createRtpObserver(
    void* observer_borrowed,
    const RtpObserverCallbacks* callbacks_borrowed) {
  return new RtpObserverRffi(observer_borrowed, callbacks_borrowed);
}

RUSTEXPORT void Rust_deleteRtpObserver(RtpObserverRffi* observer_owned) {
  delete observer_owned;
}

}  // namespace rffi
}  // namespace webrtc
